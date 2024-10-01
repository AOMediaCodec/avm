use anyhow::anyhow;
use async_process::{Command, Stdio};
use avm_analyzer_common::{
    AvmStreamInfo, AvmStreamList, DecodeProgress, DecodeState, ProgressRequest, ProgressResponse, StartDecodeResponse,
    DEFAULT_PROTO_PATH_FRAME_SUFFIX_TEMPLATE,
};
use avm_stats::{Frame, PixelPlane, PixelType, Plane};
use axum::{
    extract::{DefaultBodyLimit, Multipart, Query, State},
    http::StatusCode,
    response::{IntoResponse, Response},
    routing::{get, post},
    Json, Router,
};
use clap::Parser;
use futures_lite::{io::BufReader, prelude::*};
use image::{imageops::FilterType, DynamicImage, Rgb, RgbImage};
use prost::Message;

use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::time::Duration;
use std::{
    collections::HashMap,
    io::Error,
    path::{Path, PathBuf},
    sync::{Arc, Mutex},
};
use tower::ServiceBuilder;
use tower_http::cors::CorsLayer;
use tower_http::limit::RequestBodyLimitLayer;
use tower_http::services::ServeDir;
use tower_http::timeout::TimeoutLayer;

use tracing_subscriber::{layer::SubscriberExt, util::SubscriberInitExt};
const PROTO_DIR_SUFFIX: &str = "_protos";
const MAX_UPLOAD_SIZE: usize = 100 * 1024 * 1024; // 100MiB
#[derive(Parser, Debug)]
#[command(version)]
struct Args {
    // TODO(comc): Combine with dump_obu into a single avm_build dir?
    // TODO(comc): Allow multiple different build versions, stored by git hash.
    /// Path to extract_proto binary.
    #[arg(long)]
    extract_proto: String,

    /// Path to dump_obu binary. Used to check the number of frames in a stream.
    #[arg(long)]
    dump_obu: String,

    /// Path to store decoded streams.
    #[arg(long)]
    working_dir: String,

    /// Path to frontend app root.
    #[arg(long)]
    frontend_root: String,

    /// Port.
    #[arg(short, long, default_value_t = 8080)]
    port: u16,

    /// IP address to bind to. Running on a workstation directly, we typically want this to be 127.0.0.1. Running within docker, 0.0.0.0 is necessary.
    #[arg(short, long, default_value = "127.0.0.1")]
    ip: String,

    /// Upload requests will timeout in this many seconds.
    #[arg(short, long, default_value_t = 5)]
    timeout_seconds: u32,
}

#[derive(Clone)]
struct DecodeInfo {
    state: DecodeState,
    paths: Vec<String>,
}
impl DecodeInfo {
    fn new(total_frames: usize) -> Self {
        Self {
            state: DecodeState::Pending(DecodeProgress {
                total_frames,
                decoded_frames: 0,
            }),
            paths: Vec::new(),
        }
    }
    // TODO(comc): Check frame_path matches template.
    fn add_frame(&mut self, frame_path: &str) {
        match &mut self.state {
            DecodeState::Pending(progress) => progress.decoded_frames += 1,
            _ => panic!("Can't add frame to finished decode."),
        }
        self.paths.push(frame_path.into());
    }
}

struct PendingStreams {
    streams: HashMap<String, DecodeInfo>,
}

impl PendingStreams {
    fn new() -> Self {
        Self {
            streams: HashMap::new(),
        }
    }
}

fn find_existing_streams(root: &Path) -> anyhow::Result<Vec<AvmStreamInfo>> {
    tracing::info!("Looking for existing streams in {root:?}");
    let mut streams = Vec::new();
    for entry in fs::read_dir(root)? {
        let mut proto_count = 0;
        let entry = entry?;
        let path = entry.path();
        let path_str = path.to_string_lossy().to_string();
        if path.is_file() && path_str.ends_with("_thumbnail.png") {
            let thumbnail_bytes = std::fs::read(entry.path())?;
            let path = entry.path();
            let file_name = path.file_name().unwrap().to_string_lossy();
            let stream_name = file_name.strip_suffix("_thumbnail.png").unwrap();
            let proto_dir = root.join(format!("{stream_name}{PROTO_DIR_SUFFIX}"));
            for maybe_proto in fs::read_dir(proto_dir)? {
                let maybe_proto = maybe_proto?;
                let maybe_proto_path = maybe_proto.path();
                let maybe_proto_path_name = maybe_proto_path.to_string_lossy().to_string();
                if maybe_proto_path.is_file() && maybe_proto_path_name.ends_with(".pb") {
                    proto_count += 1;
                }
            }
            let proto_path_template =
                format!("{stream_name}{PROTO_DIR_SUFFIX}/{stream_name}{DEFAULT_PROTO_PATH_FRAME_SUFFIX_TEMPLATE}");
            let stream_info = AvmStreamInfo {
                num_frames: proto_count,
                stream_name: stream_name.into(),
                proto_path_template,
                thumbnail_png: Some(thumbnail_bytes),
            };
            tracing::info!("Found existing stream with {proto_count} frames: {stream_name}");
            streams.push(stream_info);
        }
    }
    Ok(streams)
}

#[derive(Clone)]
struct ServerConfig {
    working_dir_path: PathBuf,
    extract_proto_path: PathBuf,
    dump_obu_path: PathBuf,
}

#[derive(Clone)]
struct ServerState {
    config: ServerConfig,
    pending_streams: Arc<Mutex<PendingStreams>>,
    finished_streams: Arc<Mutex<Vec<AvmStreamInfo>>>,
}

#[tokio::main]
async fn main() {
    let args = Args::parse();

    let timeout_service =
        ServiceBuilder::new().layer(TimeoutLayer::new(Duration::from_secs(args.timeout_seconds as u64)));
    tracing_subscriber::registry()
        .with(tracing_subscriber::EnvFilter::try_from_default_env().unwrap_or_else(|_| "debug".into()))
        .with(tracing_subscriber::fmt::layer())
        .init();

    let frontend_path = Path::new(&args.frontend_root);
    let working_dir_path = Path::new(&args.working_dir);
    let existing_streams = match find_existing_streams(working_dir_path) {
        Err(err) => {
            tracing::warn!("Error finding existing streams: {err:?}");
            Vec::new()
        }
        Ok(streams) => streams,
    };
    let state = ServerState {
        config: ServerConfig {
            working_dir_path: working_dir_path.into(),
            extract_proto_path: Path::new(&args.extract_proto).into(),
            dump_obu_path: Path::new(&args.dump_obu).into(),
        },
        pending_streams: Arc::new(Mutex::new(PendingStreams::new())),
        finished_streams: Arc::new(Mutex::new(existing_streams)),
    };
    // build our application with some routes
    let app = Router::new()
        .route("/upload", post(upload_stream))
        .route("/progress", get(check_progress))
        .route("/stream_list", get(get_stream_list))
        .with_state(state)
        .nest_service("/streams", ServeDir::new(working_dir_path))
        .nest_service("/", ServeDir::new(frontend_path))
        .layer(DefaultBodyLimit::disable())
        .layer(RequestBodyLimitLayer::new(MAX_UPLOAD_SIZE))
        .layer(CorsLayer::permissive())
        .layer(timeout_service)
        .layer(tower_http::trace::TraceLayer::new_for_http());

    let listener = tokio::net::TcpListener::bind(format!("{}:{}", args.ip, args.port))
        .await
        .unwrap();
    tracing::debug!("listening on {}", listener.local_addr().unwrap());
    axum::serve(listener, app).await.unwrap();
}

async fn check_progress(
    State(state): State<ServerState>,
    request: Query<ProgressRequest>,
) -> Result<impl IntoResponse, ServerError> {
    let pending_streams = state.pending_streams.lock().unwrap();
    tracing::info!("check_progress {:?}", pending_streams.streams.keys());
    let Some(stream_info) = pending_streams.streams.get(&request.stream_name) else {
        return Err(anyhow!("Unknown stream.").into());
    };

    Ok(Json(ProgressResponse {
        stream_name: request.stream_name.to_owned(),
        state: stream_info.state.clone(),
    }))
}

async fn get_stream_list(State(state): State<ServerState>) -> Result<impl IntoResponse, ServerError> {
    let streams = state.finished_streams.lock().unwrap();
    Ok(Json(AvmStreamList {
        streams: streams.clone(),
    }))
}

async fn upload_stream(
    State(state): State<ServerState>,
    mut multipart: Multipart,
) -> Result<impl IntoResponse, ServerError> {
    // tracing::info!("upload_stream: {multipart:?}");
    // TODO(comc): ok_or instead of unwrap.
    if let Some(field) = multipart.next_field().await.expect("Multipart upload failure.") {
        // Name field is unused. Filename is used instead.
        let _name = field.name().unwrap().to_string();
        let file_name = field.file_name().unwrap().to_string();
        let _content_type = field.content_type().unwrap().to_string();
        let data = field.bytes().await.unwrap();
        tracing::debug!("Decoding {file_name}: {} bytes", data.len());

        let stream_path = std::path::Path::new(&file_name);
        let stream_path_local = state.config.working_dir_path.join(stream_path);
        // stream_name should always be the filename of the stream without the file extension.
        let stream_name = stream_path.file_stem().unwrap().to_string_lossy().to_string();
        async_fs::write(stream_path_local.as_path(), data).await?;

        let num_frames = check_num_frames(state.config.dump_obu_path.as_path(), stream_path_local.as_path()).await?;
        tracing::debug!("Frames: {num_frames}");
        spawn_extract_proto(
            state.config.working_dir_path.as_path(),
            state.config.extract_proto_path.as_path(),
            stream_path_local.as_path(),
            num_frames,
            state.pending_streams.clone(),
            state.finished_streams.clone(),
        )?;

        let proto_path_template =
            format!("{stream_name}{PROTO_DIR_SUFFIX}/{stream_name}{DEFAULT_PROTO_PATH_FRAME_SUFFIX_TEMPLATE}");
        let stream_info = AvmStreamInfo {
            stream_name,
            proto_path_template,
            num_frames,
            thumbnail_png: None,
        };
        return Ok(Json(StartDecodeResponse { stream_info }));
    }
    Err(anyhow!("No file received.").into())
}

async fn check_num_frames(dump_obu_path: &Path, stream: &Path) -> Result<usize, Error> {
    let mut child = Command::new(dump_obu_path)
        .arg(stream)
        .stdout(Stdio::piped())
        .spawn()
        .unwrap();
    let mut lines = BufReader::new(child.stdout.take().unwrap()).lines();
    let mut count = 0;
    while let Some(line) = lines.next().await {
        if line?.contains("OBU_FRAME") {
            count += 1;
        }
    }
    Ok(count)
}

// TODO(comc): Refactor this common code out of avm_analyzer_app (probably into avm_stats).
async fn create_thumbnail(first_frame: &Path, thumbnail_out: &Path) {
    let first_frame = first_frame.to_owned();
    let thumbnail_out = thumbnail_out.to_owned();
    match tokio::task::spawn_blocking(move || {
        tracing::info!("Creating thumbnail: {first_frame:?} --> {thumbnail_out:?}");
        let frame = std::fs::read(first_frame).unwrap();
        let frame = Frame::decode(frame.as_slice()).unwrap();
        let mut planes = Vec::new();
        for i in 0..3 {
            planes.push(PixelPlane::create_from_frame(&frame, Plane::from_i32(i), PixelType::Reconstruction).unwrap());
        }

        let width = planes[0].width as usize;
        let height = planes[0].height as usize;
        let uv_width = planes[1].width as usize;
        let uv_height = planes[1].height as usize;
        let uv_width_scale = (width + 1) / uv_width;
        let uv_height_scale = (height + 1) / uv_height;
        let mut img = RgbImage::new(width as u32, height as u32);
        let raw_y = planes[0].pixels.as_slice();
        let raw_u = planes[1].pixels.as_slice();
        let raw_v = planes[2].pixels.as_slice();

        for i in 0..height {
            for j in 0..width {
                let y = raw_y[i * width + j] as f32;
                let u = raw_u[(i / uv_height_scale) * (width / uv_width_scale) + (j / uv_width_scale)] as f32;
                let v = raw_v[(i / uv_height_scale) * (width / uv_width_scale) + (j / uv_width_scale)] as f32;

                let is_8_bit = planes[0].bit_depth == 8;
                let y = if is_8_bit { y } else { y / 4.0 };
                let u = if is_8_bit { u - 128.0 } else { u / 4.0 - 128.0 };
                let v = if is_8_bit { v - 128.0 } else { v / 4.0 - 128.0 };
                let r = (y + 1.13983 * v) as u8;
                let g = (y - 0.39465 * u - 0.58060 * v) as u8;
                let b = (y + 2.03211 * u) as u8;
                img.put_pixel(j as u32, i as u32, Rgb([r, g, b]));
            }
        }
        let img = DynamicImage::ImageRgb8(img);
        let resized = img.resize(64, 64, FilterType::CatmullRom);
        match resized.save(thumbnail_out.clone()) {
            Ok(_) => {}
            Err(err) => {
                tracing::warn!("Error resizing thumbnail: {err:?}");
            }
        }
        // Make thumbnail accessible to all, evne if docker container creates it as root.
        let metadata = fs::metadata(thumbnail_out.clone()).unwrap();
        let mut current_permissions = metadata.permissions();
        current_permissions.set_mode(0o644);
        fs::set_permissions(thumbnail_out, current_permissions)
    })
    .await
    {
        Ok(_) => {}
        Err(err) => {
            tracing::warn!("Error creating thumbnail: {err:?}");
        }
    }
}

async fn load_thumbnail(thumbnail_path: &Path) -> anyhow::Result<Vec<u8>> {
    let thumbnail_path = thumbnail_path.to_owned();
    let bytes = tokio::task::spawn_blocking(move || std::fs::read(thumbnail_path)).await;
    match bytes {
        Err(err) => Err(err.into()),
        Ok(Err(err)) => Err(err.into()),
        Ok(Ok(bytes)) => Ok(bytes),
    }
}

// TODO(comc): Check for existing finished and pending decodes before spawning new jobs.
fn spawn_extract_proto(
    working_dir_path: &Path,
    extract_proto_path: &Path,
    stream_path: &Path,
    total_frames: usize,
    pending_streams: Arc<Mutex<PendingStreams>>,
    finished_streams: Arc<Mutex<Vec<AvmStreamInfo>>>,
) -> Result<impl IntoResponse, ServerError> {
    let extract_proto_path = extract_proto_path.to_owned();
    let stream_name = stream_path.file_stem().unwrap().to_string_lossy().to_string();
    let output_path = working_dir_path.join(format!("{stream_name}{PROTO_DIR_SUFFIX}"));
    tracing::info!("Creating proto output path: {output_path:?}");
    std::fs::create_dir_all(&output_path)?;
    // TODO(comc): Frontend option to force new encode, or use existing.
    pending_streams
        .lock()
        .unwrap()
        .streams
        .insert(stream_name.clone(), DecodeInfo::new(total_frames));
    let stream_path = stream_path.to_owned();
    let working_dir_path = working_dir_path.to_owned();
    tokio::spawn(async move {
        let mut child = Command::new(extract_proto_path)
            .arg("--stream")
            .arg(stream_path)
            .arg("--output_folder")
            .arg(output_path)
            .stdout(Stdio::piped())
            .spawn()
            .unwrap();
        let mut lines = BufReader::new(child.stdout.take().unwrap()).lines();
        while let Some(Ok(line)) = lines.next().await {
            if line.starts_with("Wrote:") {
                let parts: Vec<_> = line.split(' ').filter(|s| !s.is_empty()).collect();
                let mut pending_streams = pending_streams.lock().unwrap();
                let stream_info = pending_streams.streams.get_mut(&stream_name).unwrap();
                let frame_path = parts.last().unwrap();
                tracing::debug!("Frame: {}", frame_path);
                stream_info.add_frame(frame_path);
            }
        }
        let status = child.status().await;
        tracing::debug!("Status: {:?}", status);

        let decode_info = {
            let pending_streams = pending_streams.lock().unwrap();
            pending_streams.streams[&stream_name].clone()
        };
        let success = if let Ok(status) = status {
            status.success()
        } else {
            false
        };
        if success {
            let mut stream_info = {
                let num_frames = decode_info.paths.len();
                let mut pending_streams = pending_streams.lock().unwrap();
                let decode_info = pending_streams.streams.get_mut(&stream_name).unwrap();
                // TODO(comc): Update client with actual number of frames, which may be different because of TIP / non-showable frames.
                decode_info.state = DecodeState::Complete(num_frames);

                let proto_path_template =
                    format!("{stream_name}{PROTO_DIR_SUFFIX}/{stream_name}{DEFAULT_PROTO_PATH_FRAME_SUFFIX_TEMPLATE}");
                AvmStreamInfo {
                    stream_name: stream_name.clone(),
                    proto_path_template,
                    num_frames,
                    thumbnail_png: None,
                }
            };
            let thumbnail_path = working_dir_path.join(format!("{stream_name}_thumbnail.png"));
            let proto_path = working_dir_path.join(stream_info.get_proto_path(0));
            create_thumbnail(&proto_path, &thumbnail_path).await;
            match load_thumbnail(&thumbnail_path).await {
                Ok(thumbnail_bytes) => stream_info.thumbnail_png = Some(thumbnail_bytes),
                Err(err) => {
                    tracing::warn!("Unable to load thumbnail: {thumbnail_path:?} {err:?}");
                }
            }

            // TODO(comc): Overwrite existing stream_info if name already exists. Currently duplicate streams are sent in streams_list.
            let mut finished_streams = finished_streams.lock().unwrap();
            finished_streams.push(stream_info.clone());
        } else {
            let mut pending_streams = pending_streams.lock().unwrap();
            let decode_info = pending_streams.streams.get_mut(&stream_name).unwrap();
            decode_info.state = DecodeState::Failed;
        }
    });
    Ok(())
}

struct ServerError(anyhow::Error);

impl IntoResponse for ServerError {
    fn into_response(self) -> Response {
        (
            StatusCode::INTERNAL_SERVER_ERROR,
            format!("Something went wrong: {}", self.0),
        )
            .into_response()
    }
}

impl<E> From<E> for ServerError
where
    E: Into<anyhow::Error>,
{
    fn from(err: E) -> Self {
        Self(err.into())
    }
}

use avm_analyzer_common::AvmStreamInfo;
use avm_stats::{FrameStatistic, HeatmapSettings, PlaneType, Sample, StatsSettings};
use base64::{engine::general_purpose::URL_SAFE, Engine as _};
use egui::{pos2, vec2, Color32, Pos2, Rect, Stroke};
use log::info;
use serde::{Deserialize, Serialize};
use web_time::Instant;

use crate::{
    stream::{Stream, StreamSource},
    views::{SelectedObject, SelectedObjectKind, ViewMode},
};

const GITHUB_ROOT: &str = "https://github.com/AOMediaCodec/avm";
const DEFAULT_STREAMS_URL: &str = "/streams";
const DEFAULT_WORLD_BOUNDS_WIDTH: f32 = 1280.0;
const DEFAULT_WORLD_BOUNDS_HEIGHT: f32 = 720.0;

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct OverlayStyle {
    pub transform_unit_stroke: Stroke,
    pub coding_unit_stroke: Stroke,
    pub superblock_stroke: Stroke,
    pub highlighted_object_stroke: Stroke,
    pub selected_object_stroke: Stroke,
    pub mode_name_color: Color32,
    pub pixel_viewer_text_color: Color32,
    pub enable_text_shadows: bool,
}

impl Default for OverlayStyle {
    fn default() -> Self {
        Self {
            transform_unit_stroke: Stroke::new(1.0, Color32::DARK_RED),
            coding_unit_stroke: Stroke::new(1.0, Color32::RED),
            superblock_stroke: Stroke::new(1.0, Color32::BLUE),
            highlighted_object_stroke: Stroke::new(4.0, Color32::LIGHT_YELLOW),
            selected_object_stroke: Stroke::new(3.0, Color32::YELLOW),
            mode_name_color: Color32::LIGHT_GREEN,
            pixel_viewer_text_color: Color32::LIGHT_GREEN,
            enable_text_shadows: true,
        }
    }
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct StyleSettings {
    pub overlay: OverlayStyle,
}
#[allow(clippy::derivable_impls)]
impl Default for StyleSettings {
    fn default() -> Self {
        Self {
            overlay: OverlayStyle::default(),
        }
    }
}

#[derive(Debug, Serialize, Deserialize, Clone, Copy, PartialEq)]
pub enum FrameSortOrder {
    Decode,
    Display,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct PersistentSettings {
    pub avm_source_url: String,
    pub style: StyleSettings,
    pub apply_cache_strategy: bool,
    pub cache_strategy_limit: usize,
    pub update_sharable_url: bool,
    pub frame_sort_order: FrameSortOrder,
}

impl Default for PersistentSettings {
    fn default() -> Self {
        Self {
            avm_source_url: GITHUB_ROOT.into(),
            style: StyleSettings::default(),
            apply_cache_strategy: false,
            cache_strategy_limit: 10,
            update_sharable_url: false,
            frame_sort_order: FrameSortOrder::Decode,
        }
    }
}

#[derive(Debug, Serialize, Deserialize, Clone, Copy, PartialEq)]
pub enum CoeffViewSelect {
    Quantized,
    Dequantized,
    DequantValue,
}

impl CoeffViewSelect {
    pub fn name(&self) -> &str {
        match self {
            Self::Quantized => "Quantized Coefficients",
            Self::Dequantized => "Dequantized Coefficients",
            Self::DequantValue => "Inverse Quantization Matrix",
        }
    }
}

#[derive(Debug, Serialize, Deserialize, Clone, Copy, PartialEq)]
pub enum MotionFieldColoring {
    RefFrames,
    PastFuture,
    Monochrome,
}

impl MotionFieldColoring {
    pub fn name(&self) -> &str {
        match self {
            Self::RefFrames => "Reference frames",
            Self::PastFuture => "Past vs Future",
            Self::Monochrome => "Monochrome",
        }
    }
}

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq)]
pub struct MotionFieldSettings {
    pub show: bool,
    pub show_origin: bool,
    pub normalize: bool,
    /// In 4x4 units.
    pub granularity: usize,
    pub auto_granularity: bool,
    pub coloring: MotionFieldColoring,
    pub scale: f32,
}

impl Default for MotionFieldSettings {
    fn default() -> Self {
        Self {
            show: false,
            show_origin: true,
            normalize: true,
            granularity: 1,
            auto_granularity: true,
            coloring: MotionFieldColoring::RefFrames,
            scale: 1.0,
        }
    }
}

#[derive(Debug, Serialize, Deserialize, Clone, Copy, PartialEq, Eq)]
pub enum DistortionView {
    Original,
    Reconstruction,
    Distortion,
}

impl DistortionView {
    pub fn name(&self) -> &str {
        match self {
            Self::Original => "Source (pre-encode)",
            Self::Reconstruction => "Reconstruction",
            Self::Distortion => "Distortion",
        }
    }
}

#[derive(Debug, Serialize, Deserialize, Clone, PartialEq)]
pub struct SharableSettings {
    pub view_mode: ViewMode,
    pub current_plane: PlaneType,
    pub show_relative_delta: bool,
    pub show_overlay: bool,
    pub show_yuv: bool,
    pub motion_field: MotionFieldSettings,
    pub selected_stat: FrameStatistic,
    pub stats_settings: StatsSettings,
    pub symbol_info_filter: String,
    pub symbol_info_show_tags: bool,
    pub world_bounds: Rect,
    pub show_pixel_viewer: bool,
    pub show_coeffs_viewer: bool,
    pub pixel_viewer_bounds: Rect,
    pub coeffs_viewer_bounds: Rect,
    pub coeff_view_select: CoeffViewSelect,
    pub show_heatmap_legend: bool,
    pub heatmap_settings: HeatmapSettings,
    pub heatmap_histogram_log_scale: bool,
    pub show_remote_streams: bool,
    pub streams_url: String,
    // The following fields are used only during serialization. Otherwise their settings or state equivalent is used.
    pub selected_object_kind: Option<SelectedObjectKind>,
    pub selected_stream: Option<AvmStreamInfo>,
    pub selected_frame: usize,
}

impl Default for SharableSettings {
    fn default() -> Self {
        Self {
            current_plane: PlaneType::Rgb,
            show_relative_delta: false,
            show_overlay: true,
            show_yuv: true,
            motion_field: MotionFieldSettings::default(),
            view_mode: ViewMode::CodingFlow,
            stats_settings: StatsSettings::default(),
            selected_stat: FrameStatistic::BlockSizes,
            symbol_info_filter: "".into(),
            symbol_info_show_tags: true,
            world_bounds: Rect::from_min_size(
                Pos2::ZERO,
                vec2(DEFAULT_WORLD_BOUNDS_WIDTH, DEFAULT_WORLD_BOUNDS_HEIGHT),
            ),
            show_pixel_viewer: false,
            show_coeffs_viewer: false,
            pixel_viewer_bounds: Rect::from_min_max(pos2(0.0, 0.0), pos2(4.0, 4.0)),
            coeffs_viewer_bounds: Rect::from_min_max(pos2(0.0, 0.0), pos2(4.0, 4.0)),
            coeff_view_select: CoeffViewSelect::Dequantized,
            show_heatmap_legend: true,
            heatmap_settings: HeatmapSettings::default(),
            heatmap_histogram_log_scale: false,
            show_remote_streams: true,
            streams_url: DEFAULT_STREAMS_URL.into(),
            selected_object_kind: None,
            selected_stream: None,
            selected_frame: 0,
        }
    }
}

impl SharableSettings {
    pub fn encode(&self) -> String {
        let bytes = bincode::serialize(self).unwrap();
        let compressed = weezl::encode::Encoder::new(weezl::BitOrder::Lsb, 8)
            .encode(&bytes)
            .unwrap();
        URL_SAFE.encode(compressed)
    }

    pub fn decode(settings_str: Option<&str>) -> Option<Self> {
        if let Some(settings_str) = settings_str {
            if let Ok(bytes) = URL_SAFE.decode(settings_str) {
                if let Ok(uncompressed) = weezl::decode::Decoder::new(weezl::BitOrder::Lsb, 8).decode(&bytes) {
                    if let Ok(sharable) = bincode::deserialize::<SharableSettings>(&uncompressed) {
                        return Some(sharable);
                    }
                }
            }
        }
        None
    }
}

pub struct PlaybackSettings {
    pub playback_fps: f32,
    pub show_loaded_frames_only: bool,
    pub playback_loop: bool,
    pub playback_running: bool,
    pub current_frame_display_instant: Instant,
}

impl Default for PlaybackSettings {
    fn default() -> Self {
        Self {
            playback_fps: 30.0,
            show_loaded_frames_only: true,
            playback_loop: true,
            playback_running: false,
            current_frame_display_instant: Instant::now(),
        }
    }
}

pub struct Settings {
    pub show_stream_select: bool,
    pub show_decode_progress: bool,
    pub scroll_to_index: Option<usize>,
    pub cached_stat_data: Option<Vec<Sample>>,
    pub show_performance_window: bool,
    pub show_settings_window: bool,
    pub persistent: PersistentSettings,
    pub sharable: SharableSettings,
    pub selected_object: Option<SelectedObject>,
    /// The first object that was selected when double clicked to navigate up the hierarchy.
    pub selected_object_leaf: Option<SelectedObjectKind>,
    pub have_world_bounds_from_shared_settings: bool,
    pub playback: PlaybackSettings,
}

#[allow(clippy::derivable_impls)]
impl Default for Settings {
    fn default() -> Self {
        Self {
            show_stream_select: false,
            show_decode_progress: false,
            scroll_to_index: None,
            cached_stat_data: None,
            show_performance_window: false,
            show_settings_window: false,
            persistent: PersistentSettings::default(),
            sharable: SharableSettings::default(),
            selected_object: None,
            selected_object_leaf: None,
            have_world_bounds_from_shared_settings: false,
            playback: PlaybackSettings::default(),
        }
    }
}

impl Settings {
    pub fn apply_saved_settings_string(&mut self, settings: Option<String>) {
        if let Some(settings) = settings {
            if let Ok(persistent) = serde_json::from_str::<PersistentSettings>(&settings) {
                info!("Restoring saved settings: {persistent:?}");
                self.persistent = persistent;
            }
        }
    }

    pub fn from_shared_settings_string(settings_str: Option<&str>) -> Self {
        if let Some(sharable) = SharableSettings::decode(settings_str) {
            info!("Loaded shared settings: {sharable:?}");
            return Self {
                sharable,
                have_world_bounds_from_shared_settings: true,
                ..Default::default()
            };
        }
        Self::default()
    }

    pub fn create_shared_settings(&self, stream: &Option<Stream>) -> SharableSettings {
        let mut shared = self.sharable.clone();
        shared.selected_frame = 0;
        shared.selected_stream = None;
        shared.selected_object_kind = None;
        if let Some(stream) = stream {
            // Only save state for HTTP streams.
            if matches!(stream.source, StreamSource::Http(_)) {
                shared.selected_frame = stream.current_frame_index;
                // TODO(comc): Can be simplified with serde skip.
                shared.selected_stream = Some(AvmStreamInfo {
                    thumbnail_png: None,
                    ..stream.stream_info.clone()
                });
                if let Some(selected_object) = &self.selected_object {
                    shared.selected_object_kind = Some(selected_object.kind.clone());
                }
            }
        }
        shared
    }
}

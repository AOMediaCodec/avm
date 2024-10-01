mod color_maps;

use std::collections::HashMap;
// TODO(comc): Consider using egui mutex instead of std::sync.
use std::sync::{Arc, Mutex};

use avm_stats::{
    calculate_heatmap, Frame, FrameError, Heatmap, HeatmapSettings, PixelPlane, PixelType, Plane, PlaneType,
};
pub use color_maps::JET_COLORMAP;
use egui::{ColorImage, TextureHandle, TextureOptions};
use itertools::{Itertools, MinMaxResult};

#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
struct PixelPlaneKey {
    frame_decode_index: usize,
    plane: Plane,
    pixel_type: PixelType,
}

impl PixelPlaneKey {
    pub fn new(frame_decode_index: usize, plane: Plane, pixel_type: PixelType) -> Self {
        Self {
            frame_decode_index,
            plane,
            pixel_type,
        }
    }
}

/// Manages a collection of of different `PixelPlane` buffers.
#[derive(Default)]
pub struct PixelDataManager {
    // Lazily populated with actual pixel data.
    pixel_data: Mutex<HashMap<PixelPlaneKey, Result<Arc<PixelPlane>, FrameError>>>,
}

impl PixelDataManager {
    pub fn get_or_create_pixels(
        &self,
        frame: &Frame,
        plane: Plane,
        pixel_type: PixelType,
    ) -> Result<Arc<PixelPlane>, FrameError> {
        let key = PixelPlaneKey::new(frame.decode_index(), plane, pixel_type);
        self.pixel_data
            .lock()
            .unwrap()
            .entry(key)
            .or_insert_with(|| PixelPlane::create_from_frame(frame, key.plane, key.pixel_type).map(Arc::new))
            .clone()
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct ImageType {
    pub plane_type: PlaneType,
    pub pixel_type: PixelType,
    pub show_relative_delta: bool,
    pub is_heatmap: bool,
}

impl ImageType {
    pub fn new(plane_type: PlaneType, pixel_type: PixelType, show_relative_delta: bool, is_heatmap: bool) -> Self {
        Self {
            plane_type,
            pixel_type,
            show_relative_delta,
            is_heatmap,
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
struct ImageKey {
    frame_decode_index: usize,
    image_type: ImageType,
}

impl ImageKey {
    fn new(frame_decode_index: usize, image_type: ImageType) -> Self {
        Self {
            frame_decode_index,
            image_type,
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
struct HeatmapKey {
    frame_decode_index: usize,
}

impl HeatmapKey {
    fn new(frame_decode_index: usize) -> Self {
        Self { frame_decode_index }
    }
}

#[derive(Clone)]
struct StoredHeatmap {
    heatmap: Heatmap,
    texture_handle: TextureHandle,
}

// TODO(comc): Wipe existing images when a frame is unloaded.
#[derive(Default)]
pub struct ImageManager {
    // Lazily populated with actual images.
    images: Mutex<HashMap<ImageKey, Result<TextureHandle, FrameError>>>,
    heatmaps: Mutex<HashMap<HeatmapKey, Result<StoredHeatmap, FrameError>>>,
}

#[allow(clippy::identity_op)]
impl ImageManager {
    fn image_from_heatmap(ctx: &egui::Context, heatmap: &mut Heatmap) -> TextureHandle {
        let width = heatmap.width;
        let height = heatmap.height;
        let raw_rgb = heatmap
            .data
            .drain(..)
            .flat_map(|x| JET_COLORMAP[x as usize])
            .collect_vec();
        let color_image = ColorImage::from_rgb([width, height], raw_rgb.as_slice());
        ctx.load_texture("heatmap", color_image, TextureOptions::NEAREST)
    }

    fn image_from_yuv_planes(ctx: &egui::Context, planes: &[&PixelPlane]) -> TextureHandle {
        let width = planes[0].width as usize;
        let height = planes[0].height as usize;
        let uv_width = planes[1].width as usize;
        let uv_height = planes[1].height as usize;
        let uv_width_scale = (width + 1) / uv_width;
        let uv_height_scale = (height + 1) / uv_height;

        let mut raw_rgb = vec![0; width * height * 3];
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

                raw_rgb[i * width * 3 + j * 3 + 0] = r;
                raw_rgb[i * width * 3 + j * 3 + 1] = g;
                raw_rgb[i * width * 3 + j * 3 + 2] = b;
            }
        }
        let color_image = ColorImage::from_rgb([width, height], raw_rgb.as_slice());
        ctx.load_texture("yuv", color_image, TextureOptions::NEAREST)
    }

    fn image_from_single_plane(ctx: &egui::Context, plane: &PixelPlane, show_relative_delta: bool) -> TextureHandle {
        let width = plane.width as usize;
        let height = plane.height as usize;
        let mut raw_rgb = vec![0; width * height * 3];
        let mut min = -255;
        let mut max = 255;
        if show_relative_delta {
            match plane.pixels.iter().minmax() {
                MinMaxResult::NoElements | MinMaxResult::OneElement(_) => {}
                MinMaxResult::MinMax(&min_v, &max_v) => {
                    min = min_v;
                    max = max_v;
                }
            }
        }

        let pixel_max = 1 << plane.bit_depth;
        for i in 0..height {
            for j in 0..width {
                let mut sample = plane.pixels[i * width + j];
                if plane.pixel_type.is_delta() {
                    if show_relative_delta {
                        let rel = (sample - min) as f32 / (max - min) as f32;
                        sample = (rel * 255.0) as i16;
                    } else {
                        sample = (sample + pixel_max - 1) / 2;
                    }
                }
                let sample = if plane.bit_depth == 8 || show_relative_delta {
                    sample
                } else {
                    sample / 4
                } as u8;
                raw_rgb[i * width * 3 + j * 3 + 0] = sample;
                raw_rgb[i * width * 3 + j * 3 + 1] = sample;
                raw_rgb[i * width * 3 + j * 3 + 2] = sample;
            }
        }
        let color_image = ColorImage::from_rgb([width, height], raw_rgb.as_slice());
        ctx.load_texture("yuv", color_image, TextureOptions::NEAREST)
    }

    fn create_image(
        ctx: &egui::Context,
        pixel_manager: &PixelDataManager,
        frame: &Frame,
        image_type: ImageType,
    ) -> Result<TextureHandle, FrameError> {
        match image_type.plane_type {
            PlaneType::Rgb => {
                let planes: Vec<_> = (0..3)
                    .map(|i| pixel_manager.get_or_create_pixels(frame, Plane::from_i32(i), image_type.pixel_type))
                    .collect::<Result<_, _>>()?;

                Ok(Self::image_from_yuv_planes(
                    ctx,
                    planes.iter().map(|p| p.as_ref()).collect::<Vec<_>>().as_slice(),
                ))
            }
            PlaneType::Planar(plane) => {
                let pixels = pixel_manager.get_or_create_pixels(frame, plane, image_type.pixel_type)?;
                Ok(Self::image_from_single_plane(
                    ctx,
                    pixels.as_ref(),
                    image_type.show_relative_delta,
                ))
            }
        }
    }

    fn create_heatmap(
        ctx: &egui::Context,
        frame: &Frame,
        _image_type: ImageType,
        heatmap_settings: &HeatmapSettings,
    ) -> Result<StoredHeatmap, FrameError> {
        let mut heatmap = calculate_heatmap(frame, heatmap_settings)?;
        let texture_handle = Self::image_from_heatmap(ctx, &mut heatmap);
        Ok(StoredHeatmap {
            heatmap,
            texture_handle,
        })
    }

    pub fn get_or_create_image(
        &self,
        ctx: &egui::Context,
        pixel_manager: &PixelDataManager,
        frame: &Frame,
        image_type: ImageType,
        heatmap_settings: &HeatmapSettings,
    ) -> Result<TextureHandle, FrameError> {
        if image_type.is_heatmap {
            let key = HeatmapKey::new(frame.decode_index());
            let mut heatmaps = self.heatmaps.lock().unwrap();
            heatmaps
                .entry(key)
                .or_insert_with(move || Self::create_heatmap(ctx, frame, image_type, heatmap_settings))
                .as_ref()
                .map(|h| h.texture_handle.clone())
                .map_err(|err| err.clone())
        } else {
            let key = ImageKey::new(frame.decode_index(), image_type);
            let mut images = self.images.lock().unwrap();
            images
                .entry(key)
                .or_insert_with(move || Self::create_image(ctx, pixel_manager, frame, image_type))
                .clone()
        }
    }

    // TODO(comc): Refactor to not need clone.
    pub fn get_or_create_heatmap(
        &self,
        ctx: &egui::Context,
        frame: &Frame,
        image_type: ImageType,
        heatmap_settings: &HeatmapSettings,
    ) -> Result<Heatmap, FrameError> {
        let mut heatmaps = self.heatmaps.lock().unwrap();
        let key = HeatmapKey::new(frame.decode_index());
        heatmaps
            .entry(key)
            .or_insert_with(move || Self::create_heatmap(ctx, frame, image_type, heatmap_settings))
            .as_ref()
            .map(|h| h.heatmap.clone())
            .map_err(|err| err.clone())
    }

    pub fn clear_heatmaps(&self) {
        self.heatmaps.lock().unwrap().clear();
    }
}

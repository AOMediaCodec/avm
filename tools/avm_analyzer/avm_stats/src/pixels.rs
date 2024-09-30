use crate::FrameError;
use crate::Plane;
use crate::{Frame, PixelBuffer, Spatial, Superblock};
use std::fmt;

/// Where in the codec pipeline a pixel buffer was sampled from.
#[derive(Debug, PartialEq, Eq, Clone, Copy, Hash)]
pub enum PixelType {
    /// Pre-encode pixels (may not always be available).
    Original,
    /// Intra or inter predicted pixels.
    Prediction,
    /// Reconstructed pixels BEFORE filtering.
    PreFiltered,
    /// Final reconstructed pixels AFTER filtering.
    Reconstruction,
    /// Residual, i.e. (PreFiltered - Prediction).
    Residual,
    /// The effect of the in-loop filtering, i.e. (Reconstruction - PreFiltered).
    FilterDelta,
    /// (Original - Reconstruction) - depends on Original pixels being available.
    Distortion,
}

impl PixelType {
    /// Whether this pixel type represents a difference between two other pixel types.
    pub fn is_delta(&self) -> bool {
        match self {
            Self::Original | Self::Prediction | Self::PreFiltered | Self::Reconstruction => false,
            Self::Residual | Self::FilterDelta | Self::Distortion => true,
        }
    }
}

impl PixelBuffer {
    /// Retrieves a pixel from the buffer, and compensates for bit_depth adjustment if necessary.
    /// `desired_bit_depth` will typically be the bit_depth of the stream itself. The underlying
    /// buffer may have a different bit_depth in the case of original YUV pixels.
    pub fn get_pixel(&self, x: i32, y: i32, desired_bit_depth: u8) -> Result<i16, FrameError> {
        use FrameError::*;
        let stride = self.width;
        let index = (y * stride + x) as usize;
        let mut pixel = *self.pixels.get(index).ok_or_else(|| {
            BadPixelBuffer(format!(
                "Out of bounds access (x={x}, y={y}) on pixel buffer (width={}, height={}).",
                self.width, self.height
            ))
        })?;
        if (self.bit_depth as u8) < desired_bit_depth {
            let left_shift = desired_bit_depth - self.bit_depth as u8;
            pixel <<= left_shift;
        }
        else if (self.bit_depth as u8) > desired_bit_depth {
            let right_shift = self.bit_depth as u8 - desired_bit_depth;
            pixel >>= right_shift;
        }
        Ok(pixel as i16)
    }
}

/// Reference to a pixel buffer, or two pixel buffers in the case of a delta pixel type.
#[derive(Debug, Clone)]
pub enum PixelBufferRef<'a> {
    Single(&'a PixelBuffer),
    Delta(&'a PixelBuffer, &'a PixelBuffer),
}

impl<'a> PixelBufferRef<'a> {
    pub fn new_single(buf: &'a PixelBuffer) -> Self {
        Self::Single(buf)
    }
    pub fn new_delta(buf_1: &'a PixelBuffer, buf_2: &'a PixelBuffer) -> Self {
        Self::Delta(buf_1, buf_2)
    }

    /// Assumes both underlying buffers have the same width.
    pub fn width(&self) -> i32 {
        match self {
            Self::Single(buf) => buf.width,
            Self::Delta(buf_1, _) => buf_1.width,
        }
    }

    /// Assumes both underlying buffers have the same height.
    pub fn height(&self) -> i32 {
        match self {
            Self::Single(buf) => buf.height,
            Self::Delta(buf_1, _) => buf_1.height,
        }
    }

    /// Get a pixel from the underlying buffer(s), or a `FrameError` if OoB access occurs.
    pub fn get_pixel(&self, x: i32, y: i32, desired_bit_depth: u8) -> Result<i16, FrameError> {
        match self {
            Self::Single(buf) => {
                buf.get_pixel(x, y, desired_bit_depth)
            }
            Self::Delta(buf_1, buf_2) => {
                let pixel_1 = buf_1.get_pixel(x, y, desired_bit_depth)?;
                let pixel_2 = buf_2.get_pixel(x, y, desired_bit_depth)?;
                Ok(pixel_1 - pixel_2)
            }
        }
    }
}

impl fmt::Display for PixelType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let text = match self {
            PixelType::Original => "Original YUV",
            PixelType::Prediction => "Prediction",
            PixelType::PreFiltered => "Prefiltered",
            PixelType::Reconstruction => "Reconstruction",
            PixelType::Residual => "Residual",
            PixelType::FilterDelta => "Filter Delta",
            PixelType::Distortion => "Distortion",
        };
        write!(f, "{text}")
    }
}

/// Pixel data for a single plane (Y, U or V) and single pixel type.
pub struct PixelPlane {
    pub bit_depth: u8,
    pub width: i32,
    pub height: i32,
    pub pixels: Vec<i16>,
    pub plane: Plane,
    pub pixel_type: PixelType,
}

impl PixelPlane {
    fn create_from_tip_frame(frame: &Frame, plane: Plane, pixel_type: PixelType) -> Result<Self, FrameError> {
        use FrameError::*;
        let tip_params = frame.tip_frame_params.as_ref().unwrap();
        let width = plane.subsampled(frame.width(), frame.subsampling_x());
        let height = plane.subsampled(frame.height(), frame.subsampling_y());
        let bit_depth = frame.bit_depth();
        let mut pixels = vec![0; (width * height) as usize];

        let pixel_data = tip_params
            .pixel_data
            .get(plane.to_i32() as usize)
            .ok_or(BadFrame("Missing pixel data in tip frame.".into()))?;
        let pixel_buffer = match pixel_type {
            PixelType::Original => {
                PixelBufferRef::new_single(pixel_data.original.as_ref().ok_or(MissingPixelBuffer(format!(
                    "Original pixel data for plane {} not present",
                    plane.to_usize()
                )))?)
            }

            PixelType::Reconstruction => {
                PixelBufferRef::new_single(pixel_data.reconstruction.as_ref().ok_or(MissingPixelBuffer(format!(
                    "Reconstruction pixel data for plane {} not present",
                    plane.to_usize()
                )))?)
            }

            PixelType::Distortion => {
                let original = pixel_data.original.as_ref().ok_or(MissingPixelBuffer(format!(
                    "Original pixel data for plane {} not present",
                    plane.to_usize()
                )))?;
                let reconstruction = pixel_data.reconstruction.as_ref().ok_or(MissingPixelBuffer(format!(
                    "Reconstruction pixel data for plane {} not present",
                    plane.to_usize()
                )))?;
                PixelBufferRef::new_delta(original, reconstruction)
            }

            _ => {
                return Err(FrameError::Internal(format!(
                    "Tried to retrieve invalid single pixel buffer type ({pixel_type:?}) from TIP params."
                )))
            }
        };

        for y in 0..height {
            for x in 0..width {
                let index = (y * width + x) as usize;
                pixels[index] = pixel_buffer.get_pixel(x, y, bit_depth)?;
            }
        }
        Ok(Self {
            bit_depth,
            width,
            height,
            pixels,
            plane,
            pixel_type,
        })
    }

    fn create_from_superblocks(frame: &Frame, plane: Plane, pixel_type: PixelType) -> Result<Self, FrameError> {
        use FrameError::*;
        let width = plane.subsampled(frame.width(), frame.subsampling_x());
        let height = plane.subsampled(frame.height(), frame.subsampling_y());
        let bit_depth = frame.bit_depth();
        let mut pixels = vec![0; (width * height) as usize];
        let frame_width=frame.width();
        let frame_height=frame.height();

        for sb_ctx in frame.iter_superblocks() {
            let sb = sb_ctx.superblock;
            let sb_width = plane.subsampled(sb.width(), frame.subsampling_x());
            let sb_height = plane.subsampled(sb.height(), frame.subsampling_y());

            if sb_width <= 0 || sb_height <= 0 {
                return Err(BadSuperblock(format!("Invalid dimensions: {sb_width}x{sb_height}")));
            }

            let sb_x = plane.subsampled(sb.x(), frame.subsampling_x());
            let sb_y = plane.subsampled(sb.y(), frame.subsampling_y());
            if sb_x < 0 || sb_x >= width || sb_y < 0 || sb_y >= height {
                return Err(BadSuperblock(format!("Outside frame bounds: x={sb_x}, y={sb_y}")));
            }

            let remaining_width = width - sb_x;
            let remaining_height = height - sb_y;
            let cropped_sb_width = sb_width.min(remaining_width);
            let cropped_sb_height = sb_height.min(remaining_height);

            let pixel_buffer = sb.get_pixels(plane, pixel_type)?;

            if cropped_sb_width > pixel_buffer.width() || cropped_sb_height > pixel_buffer.height() {
                return Err(BadPixelBuffer(format!(
                    "Expected pixel buffer shape: ({}x{}), Actual: ({}x{})",
                    cropped_sb_width,
                    cropped_sb_height,
                    pixel_buffer.width(),
                    pixel_buffer.height(),
                )));
            }

            for rel_y in 0..sb_height {
                let abs_y = sb_y + rel_y;
                // Clip on frame bottom edge if frame height isn't a multiple of superblock size.
                if abs_y >= height {
                    break;
                }
                for rel_x in 0..sb_width {
                    let abs_x = sb_x + rel_x;
                    // Clip on frame right edge if frame width isn't a multiple of superblock size.
                    if abs_x >= width {
                        break;
                    }
                    let dest_index = (abs_y * width + abs_x) as usize;
                    pixels[dest_index] = pixel_buffer.get_pixel(rel_x, rel_y, bit_depth)?;
                }
            }
        }
        Ok(Self {
            bit_depth,
            width,
            height,
            pixels,
            plane,
            pixel_type,
        })
    }

    pub fn create_from_frame(frame: &Frame, plane: Plane, pixel_type: PixelType) -> Result<Self, FrameError> {
        if let Some(tip_params) = &frame.tip_frame_params {
            // TODO(comc): Const for this 2.
            if tip_params.tip_mode == 2 {
                return Self::create_from_tip_frame(frame, plane, pixel_type);
            }
        }
        Self::create_from_superblocks(frame, plane, pixel_type)
    }
}

impl Superblock {
    /// Retrieves a single `PixelBuffer` from this superblock.
    pub fn get_single_pixel_buffer(&self, plane: Plane, pixel_type: PixelType) -> Result<&PixelBuffer, FrameError> {
        use FrameError::*;
        let pixel_data = self.pixel_data.get(plane.to_usize()).ok_or(MissingPixelBuffer(format!(
            "Pixel data for plane {} not present ({} total)",
            plane.to_usize(),
            self.pixel_data.len()
        )))?;

        let pixels = match pixel_type {
            PixelType::Original => pixel_data.original.as_ref().ok_or(MissingPixelBuffer(format!(
                "Original pixel data for plane {} not present",
                plane.to_usize()
            )))?,

            PixelType::Prediction => pixel_data.prediction.as_ref().ok_or(MissingPixelBuffer(format!(
                "Prediction pixel data for plane {} not present",
                plane.to_usize()
            )))?,

            PixelType::PreFiltered => pixel_data.pre_filtered.as_ref().ok_or(MissingPixelBuffer(format!(
                "Pre-filtered pixel data for plane {} not present",
                plane.to_usize()
            )))?,

            PixelType::Reconstruction => pixel_data.reconstruction.as_ref().ok_or(MissingPixelBuffer(format!(
                "Reconstruction pixel data for plane {} not present",
                plane.to_usize()
            )))?,

            _ => {
                return Err(FrameError::Internal(format!(
                    "Tried to retrieve invalid single pixel buffer type ({pixel_type:?}) from protobuf superblock."
                )))
            }
        };
        let width = pixels.width;
        let height = pixels.height;
        let num_pixels = width * height;
        let actual_pixels = pixels.pixels.len() as i32;
        if num_pixels != actual_pixels {
            return Err(FrameError::BadPixelBuffer(format!(
                "Pixel buffer contains {actual_pixels} pixels, but dimensions require {num_pixels} pixels ({}x{})",
                width, height
            )));
        }
        Ok(pixels)
    }

    pub fn get_pixels(&self, plane: Plane, pixel_type: PixelType) -> Result<PixelBufferRef, FrameError> {
        if pixel_type.is_delta() {
            let (buf_1, buf_2) = match pixel_type {
                PixelType::Residual => {
                    let pre_filtered = self.get_single_pixel_buffer(plane, PixelType::PreFiltered)?;
                    let prediction = self.get_single_pixel_buffer(plane, PixelType::Prediction)?;
                    (pre_filtered, prediction)
                }
                PixelType::FilterDelta => {
                    let reconstruction = self.get_single_pixel_buffer(plane, PixelType::Reconstruction)?;
                    let pre_filtered = self.get_single_pixel_buffer(plane, PixelType::PreFiltered)?;
                    (reconstruction, pre_filtered)
                }
                PixelType::Distortion => {
                    let original = self.get_single_pixel_buffer(plane, PixelType::Original)?;
                    let reconstruction = self.get_single_pixel_buffer(plane, PixelType::Reconstruction)?;
                    (original, reconstruction)
                }
                _ => {
                    return Err(FrameError::Internal(format!(
                        "Tried to retrieve invalid pixel delta type: {pixel_type:?}"
                    )));
                }
            };

            if buf_1.width != buf_2.width || buf_1.height != buf_2.height {
                return Err(FrameError::BadPixelBuffer(format!(
                    "Mismatched dimensions: {}x{} vs {}x{}",
                    buf_1.width, buf_1.height, buf_2.width, buf_2.height
                )));
            }
            Ok(PixelBufferRef::new_delta(buf_1, buf_2))
        } else {
            let buf = self.get_single_pixel_buffer(plane, pixel_type)?;
            Ok(PixelBufferRef::new_single(buf))
        }
    }
}

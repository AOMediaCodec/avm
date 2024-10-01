use anyhow::anyhow;
use egui::{pos2, vec2, Align2, Color32, PointerButton, Rect, Rounding, Shape, Stroke, TextStyle, Ui, Vec2};

use crate::app_state::AppState;
use crate::views::RenderView;
use crate::views::ScreenBounds;
use crate::views::{MIN_BLOCK_HEIGHT_FOR_TEXT, MIN_BLOCK_WIDTH_FOR_TEXT};

pub const MIN_BLOCK_SIZE_FOR_GRID: f32 = 4.0;
pub struct DetailedPixelViewer;

impl RenderView for DetailedPixelViewer {
    fn title(&self) -> String {
        "Pixel View".into()
    }

    fn render(&self, ui: &mut Ui, state: &mut AppState) -> anyhow::Result<()> {
        let Some(stream) = &state.stream else {
            return Ok(());
        };

        let Some(frame) = stream.current_frame() else {
            return Ok(());
        };

        let Some(selected_object) = &state.settings.selected_object else {
            return Ok(());
        };

        let plane = state.settings.sharable.current_plane.to_plane();
        let Ok(pixel_data) = stream.pixel_data.get_or_create_pixels(
            frame,
            plane,
            state.settings.sharable.view_mode.view_settings().pixel_type,
        ) else {
            return Ok(());
        };
        let mut object_rect = selected_object.rect(frame).ok_or(anyhow!("Invalid selected object"))?;
        if plane.is_chroma() {
            if frame.subsampling_x() != 0 {
                *object_rect.right_mut() /= 2.0;
                *object_rect.left_mut() /= 2.0;
            }
            if frame.subsampling_y() != 0 {
                *object_rect.bottom_mut() /= 2.0;
                *object_rect.top_mut() /= 2.0;
            }
        }

        let size = ui.available_size_before_wrap();
        let (screen_bounds, response) = ui.allocate_exact_size(size, egui::Sense::drag());
        let mut world_bounds = state.settings.sharable.pixel_viewer_bounds;
        let scale = world_bounds.calc_scale(screen_bounds);
        let mut hover_pos_world = None;

        if response.dragged_by(PointerButton::Primary) {
            let delta = response.drag_delta() / -scale;
            world_bounds = world_bounds.translate(delta);
        } else if response.hover_pos().is_some() && ui.input(|i| i.scroll_delta) != Vec2::ZERO {
            let delta = ui.input(|i| i.scroll_delta);

            if let Some(mouse_pos) = ui.input(|i| i.pointer.hover_pos()) {
                if screen_bounds.contains(mouse_pos) {
                    let zoom = if delta.y < 0.0 {
                        f32::powf(1.001, -delta.y)
                    } else {
                        1.0 / (f32::powf(1.001, delta.y))
                    };
                    let zoom_center = world_bounds.screen_pos_to_world(mouse_pos, screen_bounds);
                    world_bounds.zoom_point(zoom_center, zoom);
                }
            }
        } else if response.hover_pos().is_some() {
            if let Some(mouse_pos) = ui.input(|i| i.pointer.hover_pos()) {
                hover_pos_world = Some(world_bounds.screen_pos_to_world(mouse_pos, screen_bounds));
            }
        }
        state.settings.sharable.pixel_viewer_bounds = world_bounds;

        let painter = ui.painter().with_clip_rect(response.rect);
        let clip_rect = painter.clip_rect();
        let mut shapes = Vec::new();
        let ui_style = ui.ctx().style();
        let mut hover_text = None;
        for y in 0..object_rect.height() as usize {
            for x in 0..object_rect.width() as usize {
                let stride = pixel_data.width as usize;
                let offset_x = object_rect.left_top().x as usize;
                let offset_y = object_rect.left_top().y as usize;
                let abs_x = x + offset_x;
                let abs_y = y + offset_y;
                let in_bounds = abs_x < pixel_data.width as usize && abs_y < pixel_data.height as usize;
                let index = (y + offset_y) * stride + x + offset_x;
                let pixel = if in_bounds {
                    pixel_data.pixels.get(index).copied()
                } else {
                    None
                };

                let mut color = pixel.unwrap_or(0);
                let pixel_max = 1 << pixel_data.bit_depth;
                if pixel_data.pixel_type.is_delta() {
                    color = (color + pixel_max - 1) / 2;
                }
                if pixel_data.bit_depth > 8 {
                    color >>= pixel_data.bit_depth - 8;
                }
                let world_rect = Rect::from_min_size(pos2(x as f32, y as f32), vec2(1.0, 1.0));
                let screen_rect = world_bounds.world_rect_to_screen(world_rect, clip_rect);
                if let Some(hover_pos_world) = hover_pos_world {
                    if world_rect.contains(hover_pos_world) {
                        if let Some(pixel) = pixel {
                            hover_text = Some(format!(
                                "{}, Relative: (x={}, y={}), Absolute: (x={}, y={})",
                                pixel,
                                x,
                                y,
                                x + offset_x,
                                y + offset_y
                            ));
                        }
                    }
                }

                shapes.push(Shape::rect_filled(
                    screen_rect,
                    Rounding::ZERO,
                    Color32::from_gray(color as u8),
                ));
                if screen_rect.width() >= MIN_BLOCK_SIZE_FOR_GRID {
                    shapes.push(Shape::rect_stroke(
                        screen_rect,
                        Rounding::ZERO,
                        Stroke::new(1.0, Color32::from_gray(20)),
                    ));
                }

                if let Some(pixel) = pixel {
                    if screen_rect.height() >= MIN_BLOCK_HEIGHT_FOR_TEXT
                        && screen_rect.width() >= MIN_BLOCK_WIDTH_FOR_TEXT
                        && clip_rect.intersects(screen_rect)
                    {
                        let overlay_style = &state.settings.persistent.style.overlay;
                        shapes.extend(painter.fonts(|f| {
                            let colors = if overlay_style.enable_text_shadows {
                                vec![Color32::BLACK, overlay_style.pixel_viewer_text_color]
                            } else {
                                vec![overlay_style.pixel_viewer_text_color]
                            };
                            colors
                                .into_iter()
                                .enumerate()
                                .map(|(i, color)| {
                                    let pos_offset = vec2(i as f32, i as f32);
                                    Shape::text(
                                        f,
                                        screen_rect.center() + pos_offset,
                                        Align2::CENTER_CENTER,
                                        format!("{pixel}"),
                                        TextStyle::Body.resolve(&ui_style),
                                        color,
                                    )
                                })
                                .collect::<Vec<_>>()
                        }));
                    }
                }
            }
        }
        painter.extend(shapes);
        if let Some(hover_text) = hover_text {
            response.on_hover_text_at_pointer(hover_text);
        }
        Ok(())
    }
}

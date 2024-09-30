use anyhow::anyhow;
use egui::{Button, RichText, Ui};
use egui_extras::{Column, TableBuilder};
use log::warn;

use crate::views::stats_viewer::create_file_download;
use crate::{app_state::AppState, stream::CurrentFrame};

use crate::views::{RenderView, SelectedObject};

use super::SelectedObjectKind;

pub struct BlockInfoViewer;

// TODO(comc): Click on a MV to load the corresponding frame and block.
impl RenderView for BlockInfoViewer {
    fn title(&self) -> String {
        "Block Info".into()
    }

    fn render(&self, ui: &mut Ui, state: &mut AppState) -> anyhow::Result<()> {
        let Some(frame) = state.stream.current_frame() else {
            // No frame loaded yet.
            return Ok(());
        };
        let Some(SelectedObject {
            kind: selected_object_kind,
            ..
        }) = &state.settings.selected_object
        else {
            // Nothing is selected.
            return Ok(());
        };
        let selected_object_kind = selected_object_kind.clone();
        let is_transform_unit = matches!(selected_object_kind, SelectedObjectKind::TransformUnit(_));
        let is_superblock = matches!(selected_object_kind, SelectedObjectKind::Superblock(_));
        if ui.add_enabled(!is_superblock, Button::new("Go to parent")).clicked() {
            if let Some(parent) = selected_object_kind.get_parent(frame) {
                state.settings.selected_object = Some(SelectedObject::new(parent));
            }
        }
        ui.end_row();

        if ui.button("Show pixels for current block").clicked() {
            state.settings.sharable.show_pixel_viewer = true;
        }
        ui.end_row();

        if let SelectedObjectKind::CodingUnit(cu) = selected_object_kind {
            let ctx = cu.try_resolve(frame).ok_or(anyhow!("Invalid coding unit index"))?;
            if ui.button("Dump current block as JSON").clicked() {
                if let Some(stream) = &state.stream {
                    let Some(rect) = selected_object_kind.rect(frame) else {
                        return Err(anyhow!("Invalid coding unit index"));
                    };
                    let file_name = format!(
                        "{}_frame_{:04}_block_{}x{}_x{}_y{}.json",
                        stream.stream_info.stream_name,
                        stream.current_frame_index,
                        rect.width() as i32,
                        rect.height() as i32,
                        rect.left_top().x as i32,
                        rect.left_top().y as i32,
                    );

                    let data = serde_json::to_string_pretty(ctx.coding_unit).unwrap();

                    if let Err(err) = create_file_download(data.as_bytes(), &file_name) {
                        warn!("Failed to create file download: {err:?}");
                    }
                }
            }
        }

        // TODO(comc): Grey out instead of removing?
        if is_transform_unit && ui.button("Show transform coeffs").clicked() {
            state.settings.sharable.show_coeffs_viewer = true;
        }

        ui.separator();

        let Ok(info) = state
            .settings
            .selected_object
            .as_mut()
            .unwrap()
            .get_or_calculate_info(frame)
        else {
            return Ok(());
        };

        TableBuilder::new(ui)
            .column(Column::auto().resizable(true).clip(false).at_least(100.0))
            .column(Column::remainder().clip(false).at_least(100.0))
            .striped(true)
            .header(20.0, |mut header| {
                header.col(|ui| {
                    ui.label(RichText::new("Property").strong());
                });
                header.col(|ui| {
                    ui.label(RichText::new("Value").strong());
                });
            })
            .body(|mut body| {
                for field in info.fields.iter() {
                    body.row(30.0, |mut row| {
                        row.col(|col| {
                            col.label(&field.name);
                        });
                        row.col(|col| {
                            col.label(&field.value);
                        });
                    });
                }
            });
        Ok(())
    }
}

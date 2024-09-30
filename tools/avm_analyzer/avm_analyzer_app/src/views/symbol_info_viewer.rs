use crate::views::render_view::RenderView;
use egui::{RichText, TextBuffer, Ui};
use egui_extras::{Column, TableBuilder};

use crate::app_state::AppState;
use crate::stream::CurrentFrame;

const AVM_SOURCE_ROOT: &str = "avm/";

pub struct SymbolInfoViewer;

impl RenderView for SymbolInfoViewer {
    fn title(&self) -> String {
        "Symbol Info".into()
    }

    fn render(&self, ui: &mut Ui, state: &mut AppState) -> anyhow::Result<()> {
        let Some(frame) = state.stream.current_frame() else {
            // No frame loaded yet.
            return Ok(());
        };
        let Some(selected_object) = &mut state.settings.selected_object else {
            // No coding unit is selected.
            return Ok(());
        };
        let Ok(info) = selected_object.get_or_calculate_info(frame) else {
            return Ok(());
        };

        ui.horizontal(|ui| {
            ui.label("Filter:");
            ui.text_edit_singleline(&mut state.settings.sharable.symbol_info_filter);
        });
        ui.end_row();
        ui.checkbox(&mut state.settings.sharable.symbol_info_show_tags, "Show tags");

        ui.separator();
        TableBuilder::new(ui)
            .column(Column::auto().resizable(true).clip(false).at_least(200.0))
            .column(Column::auto().resizable(true).clip(false).at_least(50.0))
            .column(Column::remainder().clip(false).at_least(100.0))
            .striped(true)
            .header(20.0, |mut header| {
                header.col(|ui| {
                    ui.label(RichText::new("Symbol Type").strong());
                });
                header.col(|ui| {
                    ui.label(RichText::new("Value").strong());
                });
                header.col(|ui| {
                    ui.label(RichText::new("Bits").strong());
                });
            })
            .body(|body| {
                let symbols = info.symbols.clone();
                let symbols = if state.settings.sharable.symbol_info_filter.is_empty() {
                    symbols
                } else {
                    symbols
                        .into_iter()
                        .filter(|row| {
                            row.func.contains(&state.settings.sharable.symbol_info_filter)
                                || (state.settings.sharable.symbol_info_show_tags
                                    && row
                                        .tags
                                        .iter()
                                        .any(|tag| tag.contains(&state.settings.sharable.symbol_info_filter)))
                        })
                        .collect()
                };
                body.rows(30.0, symbols.len(), |mut row| {
                    let symbol = &symbols[row.index()];
                    row.col(|col| {
                        let prefix_start = symbol.file.rfind(AVM_SOURCE_ROOT);
                        let name = if symbol.tags.is_empty() || !state.settings.sharable.symbol_info_show_tags {
                            symbol.func.to_string()
                        } else {
                            format!("{} ({})", symbol.func, symbol.tags.join("+"))
                        };

                        if let Some(prefix_start) = prefix_start {
                            let relative_path = symbol
                                .file
                                .char_range(prefix_start + AVM_SOURCE_ROOT.len()..symbol.file.len());
                            let url = format!(
                                "{}/{}#L{}",
                                state.settings.persistent.avm_source_url, relative_path, symbol.line
                            );
                            col.hyperlink_to(name, url);
                        } else {
                            col.label(name);
                        }
                    });
                    row.col(|col| {
                        col.label(format!("{}", symbol.value));
                    });
                    row.col(|col| {
                        col.label(format!("{}", symbol.bits));
                    });
                });
            });
        Ok(())
    }
}

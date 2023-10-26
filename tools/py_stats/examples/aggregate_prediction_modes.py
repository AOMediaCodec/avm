#!/usr/bin/env python3
## Copyright (c) 2023, Alliance for Open Media. All rights reserved
##
## This source code is subject to the terms of the BSD 3-Clause Clear License and the
## Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear License was
## not distributed with this source code in the LICENSE file, you can obtain it
## at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance for Open Media Patent
## License 1.0 was not distributed with this source code in the PATENTS file, you
## can obtain it at aomedia.org/license/patent-license/.
##
from __future__ import annotations

import collections
import glob
import itertools
import pathlib
import tempfile
from typing import Sequence

from absl import app
from absl import flags
from absl import logging
from avm_stats.extract_proto import *
from avm_stats.frame_visualizations import *
from avm_stats.proto_helpers import *
from avm_stats.stats_aggregation import *
import matplotlib.pyplot as plt
import pandas as pd

_STREAM_GLOB = flags.DEFINE_multi_string(
    "stream_glob", None, "Path to AVM stream."
)
flags.mark_flag_as_required("stream_glob")

_EXTRACT_PROTO_BIN = flags.DEFINE_string(
    "extract_proto_bin", None, "Path to extract_proto binary."
)
flags.mark_flag_as_required("extract_proto_bin")

_OUTPUT_CSV = flags.DEFINE_string(
    "output_csv", None, "Path to output CSV file (optional)."
)


def prediction_mode_symbol_filter(symbol: Symbol):
  return symbol.source_function in (
      "read_inter_mode",
      "read_intra_luma_mode",
      "read_drl_index",
      "read_inter_compound_mode",
  )


class PredictionModeExtractor(CodingUnitExtractor):
  PredictionMode = collections.namedtuple(
      "PredictionMode",
      ["width", "height", "mode", "mode_bits", "is_intra_frame"],
  )

  def sample(self, coding_unit: CodingUnit):
    width = coding_unit.rect.width
    height = coding_unit.rect.height
    mode = coding_unit.get_prediction_mode()
    mode_bits = sum(
        sym.bits
        for sym in coding_unit.get_symbols(prediction_mode_symbol_filter)
    )
    is_intra_frame = coding_unit.frame.proto.frame_params.frame_type == 0
    yield self.PredictionMode(width, height, mode, mode_bits, is_intra_frame)


def compare_intra_inter(
    df: pd.DataFrame,
    column: str,
    *,
    aggregated_field: str = "count",
    aggregator: str = "count",
    plot_title: str | None = None,
):
  df_intra = df.query("is_intra_frame")[[column, aggregated_field]].groupby(
      column, as_index=False
  )
  df_intra = getattr(df_intra, aggregator)()
  df_inter = df.query("not is_intra_frame")[[column, aggregated_field]].groupby(
      column, as_index=False
  )
  df_inter = getattr(df_inter, aggregator)()
  _, axes = plt.subplots(1, 2)
  plot_title = plot_title or column
  axes[0].pie(
      x=df_intra[aggregated_field], labels=df_intra[column], autopct="%.1f%%"
  )
  axes[0].set_title(f"{plot_title} (Intra frames)")
  axes[1].pie(
      x=df_inter[aggregated_field], labels=df_inter[column], autopct="%.1f%%"
  )
  axes[1].set_title(f"{plot_title} (Inter frames)")
  plt.show()


def main(argv: Sequence[str]) -> None:
  if len(argv) > 1:
    raise app.UsageError("Too many command-line arguments.")

  stream_paths = []
  for stream_glob in _STREAM_GLOB.value:
    for stream in glob.glob(stream_glob, recursive=True):
      path = pathlib.Path(stream)
      stream_paths.append(path)

  with tempfile.TemporaryDirectory() as tmp_dir:
    tmp_path = pathlib.Path(tmp_dir)
    extract_proto_path = pathlib.Path(_EXTRACT_PROTO_BIN.value)

    def extract_to_temp_dir(stream_path: pathlib.Path) -> Frame:
      stream_name = stream_path.stem
      output_path = tmp_path / stream_name
      try:
        output_path.mkdir()
      except FileExistsError:
        logging.fatal(f"Duplicate stream name: {stream_name}")
      yield from extract_and_load_protos(
          extract_proto_path=extract_proto_path,
          stream_path=stream_path,
          output_path=output_path,
      )

    all_frames = itertools.chain.from_iterable(
        map(extract_to_temp_dir, stream_paths)
    )
    df = aggregate_to_dataframe(all_frames, PredictionModeExtractor())
    df["count"] = 1
    df["width_height"] = df.apply(
        lambda row: f"{row.width}x{row.height}", axis=1
    )

    compare_intra_inter(
        df, "width_height", plot_title="Block size distribution"
    )
    compare_intra_inter(df, "mode", plot_title="Prediction mode distribution")
    compare_intra_inter(
        df,
        "mode",
        aggregated_field="mode_bits",
        aggregator="sum",
        plot_title="Prediction modes by bits spent",
    )

    if _OUTPUT_CSV.value:
      df.to_csv(_OUTPUT_CSV.value)


if __name__ == "__main__":
  app.run(main)

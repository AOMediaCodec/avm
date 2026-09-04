# AVM CTC Testing Framework — notes for Claude

Python test harness for evaluating video codecs against the AOM Common Test
Conditions. Read `USER_GUIDE.md` for how to *use* it; this file records the
things that are easy to get wrong when *changing* it.

Scope: `tools/convexhull_framework/` only. The rest of this repository is the
AVM codec itself (C/C++) and is not covered here.

## Layout

| Path | Role |
|------|------|
| `src/config.yaml` | All user-configurable settings |
| `src/Config.py` | Loads config.yaml into module-level constants |
| `src/AV2CTCTest.py` | Driver for LD / RA / AI / STILL (and ECF) |
| `src/ConvexHullTest.py` | Driver for AS (adaptive streaming) |
| `src/AV2CTCProgress.py` | BD-rate analysis across releases; fills the CWG `.xlsm` |
| `src/Utils.py` | Shared helpers: clips, CSV, y4m→RGB, command execution |
| `src/CalculateQualityMetrics.py` | The single fan-out point for all metrics |
| `bin/` | Encoder/decoder binaries and the CWG Excel templates |
| `ctc_result/` | Committed per-release RD results (inputs to AV2CTCProgress) |

## Things that will bite you

### Commands must go through `Utils.ExecuteCmd`

`ExecuteCmd` (`Utils.py`) is what copies a command into the per-job `.sh`
under `test/cmdLogs/<cfg>/`, which `Launch.py` submits to the cluster. Work
implemented as direct in-process Python runs on the *submitting* host during
command generation and never reaches the cluster. Anything expensive must be
a shell command.

### `LogCmdOnly` defaults to **true**

So a bare `python AV2CTCTest.py -f encode ...` writes commands to a log and
encodes nothing. Pass `-CmdOnly false` to actually run locally.

### `AV2CTCProgress.WriteSheet` maps CSV → `.xlsm` by absolute column index

`if col >= 12 and col <= 30` and the template layout are positional
(`Bitrate`=12, quality metrics from 13, `EncT[s]`=26). **Inserting a column
into the RD CSV shifts every later field and silently corrupts the
workbooks.** The perceptual metrics sit mid-row, so `WriteSheet` strips them
before filling a sheet. If you add another column, either append it after
`DecMD5` or add it to the strip list.

Regression check: generate a workbook and confirm `EncT[s]` is still in
column 26.

### `Config.QualityList` is positional

`GatherQualityMetrics` returns a list whose *i*-th element corresponds to
`QualityList[i]`, and both CSV writers emit the header by iterating the same
list. Nothing zips names to values — alignment is by construction. The
per-frame CSV excludes `APSNR_*` (aggregate-only), so its column count
differs from the aggregate CSV.

### `ParseCSVFile` reads by name, `Record` takes ~30 positional args

Column order in the CSV is irrelevant to parsing (`DictReader`). What matters
is that the argument order in the `Record(...)` call matches
`Record.__init__`. Both are kept in RD CSV order so they can be compared by
eye — do not reorder one without the other.

New columns must use `data.get(name, "")`, not `data[name]`: the 20 committed
result directories under `ctc_result/` predate any recent column.

### BD-rate assumes higher is better

`CalcBDRate.BD_RATE` sorts by quality ascending and requires bitrate to be
non-decreasing. For a lower-is-better metric (LPIPS, DISTS) pass
`lower_is_better=True`, which flips that check. Without it the curve is
rejected and the BD-rate silently recorded as `0.0` — it does not raise.

`AV2CTCProgress.qtys` and `GetQty` must stay in step; `GetQty` ends in
`assert 0`, so an unmapped name is a hard failure.

### Three naming layers for the same metric

`VMAF_Y-NEG` (RD CSV column, libvmaf's naming, baked into `ctc_result/`),
`vmaf_neg` (BD-rate key and `Bdrate-Summary` column), `vmaf_y_neg` (`Record`
attribute). They cannot be unified: the first is pinned by committed data,
the last by Python identifier rules. `CalcBDRate.VMAF_METRIC_NAMES` is the
one place that lists them.

### y4m carries no colorimetry — state it explicitly

CTC `.y4m` files have no colour tags, so ffmpeg would default everything to
BT.709 and silently mis-convert BT.2020. `Utils.BuildY4MToRGBCmd` states the
matrix and range on the command line. Two flags are load-bearing:
`accurate_rnd` (without it limited→full lands ~2/255 short: Y=235 → 253) and
`full_chroma_int` (4:2:0 upsampling). Colour range *is* detected, from the
`XCOLORRANGE` tag, falling back to `perceptual_metrics.color_conversion.default_range`.

### Perceptual metrics are opt-in and must stay that way

`perceptual_metrics.enabled: false` is the default, and with it the framework
must never import torch. Keep the imports lazy
(`CalculateQualityMetrics.py`, `CalcPerceptualMetrics.py`) and the extras in
`requirements-perceptual.txt`, not `requirements.txt`.

`pyiqa` is **PolyForm Noncommercial** — fine for AOM standards work, not for
product use. `torchmetrics[image]` (Apache-2.0) is the drop-in alternative.

## Dependencies

`requirements.txt` is hash-pinned and installed with
`pip install --require-hashes`. Regenerate it **only** with
`pip-compile --generate-hashes`; without that flag `setup_env.sh` breaks.

`requirements-perceptual.txt` is deliberately *not* hash-pinned — torch ships
platform-specific wheels and a hashed lock is not portable between macOS and
the Linux cluster.

Neither file covers the external binaries: `ffmpeg`, `vmaf`, `avmenc`/`avmdec`,
`HDRConvert`, `lanczos_resample_y4m`, and the timing utility
(`perf` on Linux, `gtime` on macOS, `ptime` on Windows). These live in `bin/`
or on `PATH`.

## Verifying a change

There is no test suite. What actually catches regressions:

1. **No-op check.** With a feature disabled, the generated CSV header must be
   byte-identical to a committed one under `ctc_result/`.
2. **BD-rate regression.** Recompute over the real `ctc_result` data and diff
   per column; pre-existing metrics should be bit-identical. The BD-rate stage
   alone takes ~1 min (`populate_stats_files` → `CalcFullBDRate` →
   `write_bdrate`); the full script takes ~10 min because of PDF rendering.
3. **Excel column check.** `EncT[s]` in column 26 of a generated `.xlsm`.
4. **Cluster shape.** Run with `--LogCmdOnly 1` and confirm the command
   appears in `test/cmdLogs/<cfg>/<job>.sh`.

`AV2CTCProgress.py` rewrites files in `ctc_result/`, which is tracked — revert
them (`git checkout -- ctc_result/`) after a verification run unless the new
numbers are the point.

## Conventions

- Commit messages: summary line, then a numbered list of changes.
- Author is `Ryan Lei <ryanlei@meta.com>`; upstream is `AOMediaCodec/avm`,
  `origin` is the fork.
- `*.pdf`, `*.xlsm` and `venv/` are gitignored, except the CWG templates in
  `bin/`, which are required inputs.
- Run scripts from `src/`: `paths.root` is `".."`, so all derived paths are
  relative to that directory.

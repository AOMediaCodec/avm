# AVM CTC Testing Framework - User Guide

This guide provides instructions for running AV2 Common Test Conditions (CTC) (https://aomedia.org/docs/CWG-G082o_AV2_CTC_v9.pdf) tests using the AVM CTC testing framework.

## Table of Contents

1. [Overview](#overview)
2. [Prerequisites](#prerequisites)
3. [Environment Setup](#environment-setup)
4. [Configuration](#configuration)
5. [Framework Structure](#framework-structure)
6. [Running Tests](#running-tests)
   - [Regular CTC Tests (AV2CTCTest.py)](#regular-ctc-tests-av2ctctestpy)
   - [Adaptive Streaming Tests (ConvexHullTest.py)](#adaptive-streaming-tests-convexhulltestpy)
7. [ECF (Extended Chroma Format) Testing](#ecf-extended-chroma-format-testing)
8. [Distributed Cluster Execution (Launch.py)](#distributed-cluster-execution-launchpy)
9. [Analyzing Results (AV2CTCProgress.py)](#analyzing-results-av2ctcprogresspy)
10. [Output Structure](#output-structure)
11. [Perceptual Metrics (LPIPS, DISTS, ColorVideoVDP)](#perceptual-metrics-lpips-dists-colorvideovdp)
12. [Common Workflows](#common-workflows)
13. [Troubleshooting](#troubleshooting)

---

## Overview

The AVM CTC Testing Framework is a Python-based test harness for evaluating video codec performance according to the Alliance for Open Media (AOM) Common Test Conditions. It supports:

- **Multiple test configurations**: LD (Low Delay), RA (Random Access), AI (All Intra), AS (Adaptive Streaming), STILL (Still Images)
- **Extended Chroma Format (ECF) testing**: 4:4:4, 4:2:2, HDR 4:4:4, HDR 4:2:2, YCoCg, and Screen Content
- **Multiple codecs**: AV1, AV2, HEVC
- **Multiple encoders**: aomenc/avmenc, SVT-AV1, HM (HEVC)
- **Quality metrics**: PSNR, SSIM, MS-SSIM, VMAF, PSNR-HVS, CIEDE2000, CAMBI
- **Perceptual metrics** (optional, off by default): LPIPS, DISTS, ColorVideoVDP — for evaluating AI/learned coding tools, where pixel-fidelity metrics correlate poorly with perceived quality
- **Parallel GOP encoding**: For RA and AS configurations to speed up encoding
- **BD-rate calculation**: For comparing codec efficiency

### Main Test Scripts

| Script | Purpose |
|--------|---------|
| `AV2CTCTest.py` | Regular CTC tests (LD, RA, AI, STILL configurations) |
| `ConvexHullTest.py` | Adaptive Streaming (AS) tests with downscale/upscale |
| `Launch.py` | Submit encoding jobs to compute cluster |
| `AV2CTCProgress.py` | Analyze BD-Rate progress across AVM releases |
| `CheckEncoding.py` | Check encoding status and identify failed jobs |

---

## Prerequisites

### Software Requirements

- **Python 3.8 or later**
- **Video encoder binaries** (one or more):
  - `avmenc` - AVM encoder ([libavm](https://github.com/AOMediaCodec/avm))
  - `avmdec` - AVM decoder
  - `SvtAv1EncApp` - SVT-AV1 encoder ([SVT-AV1](https://github.com/OpenVisualCloud/SVT-AV1))
  - `TAppEncoderStatic` - HM (HEVC) encoder
- **Quality tools**:
  - `vmaf` (v2.1.1+) - VMAF quality metric tool ([VMAF releases](https://github.com/Netflix/vmaf/releases))
  - `ffmpeg` - For video processing
- **Scaling tools** (for AS tests):
  - `HDRConvert` - HDR tools for scaling ([HDRTools 0.22 branch](https://gitlab.com/standards/HDRTools))
  - `lanczos_resample_y4m` - AOM scaler

> **Note**: All executables should be placed in the `./bin` directory or configured with full paths in `config.yaml`.

### Additional Files in ./bin

The following files are required in the `./bin` directory:

| File | Purpose |
|------|---------|
| `HDRConvScalerY4MFile.cfg` | Template config file for HDRConvert scaling operations |
| `vbaProject-AV2.bin` | VBA macro binary for BD-Rate calculation in Excel files |
| `AVM_CWG_xxx.xlsm` | Excel template for CTC BD-Rate calculation |
| `s2-hm-01.cfg` | HM encoder configuration file (only needed for `-c hevc -m hm`; not shipped — supply your own) |

### VMAF Quality Metrics

AV2 CTC uses VMAF tool as the reference implementation for all quality metrics. The framework
adds the versioned CTC flag automatically based on `ctc_version` in `config.yaml`
(see `CalcQtyWithVmafTool.py`):

```bash
vmaf --aom_ctc v7.0 ...
```

| `ctc_version` | VMAF flag |
|---------------|-----------|
| 9.0 | `--aom_ctc v7.0` |
| 6.0, 7.0, 8.0 | `--aom_ctc v6.0` |
| 3.0, 4.0, 5.0 | `--aom_ctc v3.0` |
| other | `--aom_ctc v1.0` |

This generates all quality metrics required for AV2 CTC.

### Timing Information

When `enable_timing_info` is enabled in configuration, the framework wraps encode/decode
commands with a platform-specific timing utility:

| Platform | Utility | Notes |
|----------|---------|-------|
| Linux | `perf stat` | Used when `use_perf_util: true` (the default). Also produces instruction/cycle counts. |
| Linux | `/usr/bin/time --verbose` | Used when `use_perf_util: false` |
| macOS | `gtime --verbose` | GNU time — install with `brew install coreutils` |
| Windows | `ptime` | |

### Test Content

CTC test sequences should be available at the configured content path. These paths need to be configured in `config.yaml`:
- Regular tests: `<...>`
- Subjective tests: `<...>`

> **Note**: Only `.y4m` files are supported. The test framework parses the Y4M file header to get video properties (resolution, frame rate, bit depth, color format).

---

## Environment Setup

### Quick Setup (Linux/macOS)

Run the automated setup script:

```bash
cd /path/to/avm-ctc/tools/convexhull_framework
./setup_env.sh
```

This will:
1. Check Python version (requires 3.8+)
2. Create a virtual environment in `./venv` (an existing one at that path is deleted first)
3. Install the pinned dependencies from the committed `requirements.txt` using
   `pip install --require-hashes`

> **Note**: The setup script does **not** run `pip-compile`. `requirements.txt` is committed
> to the repository with hashes already generated; the script only installs it. See
> [Dependency Management](#dependency-management) if you need to regenerate it.

Then activate the environment:

```bash
source venv/bin/activate
```

> **Note**: If you get "Permission denied", make the script executable first:
> ```bash
> chmod +x setup_env.sh
> ```

### Manual Setup

#### Step 1: Create Virtual Environment

```bash
# Navigate to the framework directory
cd /path/to/avm-ctc/tools/convexhull_framework

# Create virtual environment
python3 -m venv venv
```

#### Step 2: Activate Virtual Environment

**Linux/macOS:**
```bash
source venv/bin/activate
```

**Windows (Command Prompt):**
```cmd
venv\Scripts\activate.bat
```

**Windows (PowerShell):**
```powershell
venv\Scripts\Activate.ps1
```

#### Step 3: Install Dependencies

```bash
# Upgrade pip
pip install --upgrade pip

# Install the committed, pinned dependencies
pip install --require-hashes -r requirements.txt
```

`requirements.txt` is already checked in with pinned versions and hashes, so no
`pip-compile` step is needed to install. Only regenerate it when changing dependencies
(see [Dependency Management](#dependency-management)).

### Verify Installation

After activation, verify the installation:

```bash
# Check Python version
python --version

# Check installed packages
pip list

# Test import of key packages
python -c "import numpy; import pandas; import yaml; print('All packages imported successfully!')"
```

### Custom Virtual Environment Location

To create the virtual environment in a custom location:

```bash
./setup_env.sh /path/to/custom/venv
```

Then activate with:
```bash
source /path/to/custom/venv/bin/activate
```

### Deactivating the Environment

When finished, deactivate the virtual environment:

```bash
deactivate
```

### Dependency Management

This framework uses a two-file dependency management pattern with `pip-tools`:

| File | Purpose |
|------|---------|
| `requirements.in` | **Source file** - Human-editable, contains high-level dependencies with flexible version constraints |
| `requirements.txt` | **Generated file** - Auto-generated by `pip-compile --generate-hashes`, contains exact pinned versions and hashes for reproducibility |

> **Important**: `setup_env.sh` installs with `pip install --require-hashes`, so
> `requirements.txt` must always be regenerated **with** `--generate-hashes`. Regenerating
> it without that flag will break `setup_env.sh`.

#### Updating Dependencies

To add or update dependencies:

1. **Install `pip-tools`** (not installed by `setup_env.sh`):
   ```bash
   pip install pip-tools
   ```
2. **Edit `requirements.in`** to add/modify dependencies
3. **Regenerate `requirements.txt`**:
   ```bash
   pip-compile --generate-hashes requirements.in -o requirements.txt
   ```
4. **Install updated dependencies**:
   ```bash
   pip install --require-hashes -r requirements.txt
   ```
5. **Commit both** `requirements.in` and `requirements.txt`

#### Upgrading All Packages

To upgrade all packages to their latest compatible versions:

```bash
pip-compile --upgrade --generate-hashes requirements.in -o requirements.txt
pip install --require-hashes -r requirements.txt
```

### Python Dependencies

| Package | Purpose |
|---------|---------|
| numpy | Numerical computing |
| pandas | Data processing and analysis |
| scipy | Scientific computing, interpolation |
| matplotlib | Plotting and visualization |
| openpyxl | Excel 2010+ file handling (.xlsx) |
| xlrd | Legacy Excel file reading (.xls) |
| xlsxwriter | Excel file writing |
| PyYAML | YAML configuration parsing |
| tabulate | Table formatting for terminal output |

**Optional** (only with `perceptual_metrics.enabled: true`, installed from
`requirements-perceptual.txt` — see [Perceptual Metrics](#perceptual-metrics-lpips-dists-colorvideovdp)):

| Package | Purpose |
|---------|---------|
| torch, torchvision | Runtime for the learned metrics |
| pyiqa | LPIPS and DISTS (non-commercial licence) |
| cvvdp | ColorVideoVDP |

---

## Configuration

### Configuration File

The framework uses a YAML configuration file (`src/config.yaml`) for all user-configurable settings. Edit this file to customize your test environment.

### Key Configuration Sections

#### 1. CTC Version

The CTC version is a critical parameter that determines which test sequences, encoder parameters, and template files are used:

```yaml
# CTC version determines test sequences, encoder parameters, and template files.
# Valid versions: "2.0", "3.0", "4.0", "5.0", "6.0", "7.0", "8.0", "9.0"
ctc_version: "9.0"
```

| CTC Version | Description | Regular template | AS template |
|-------------|-------------|------------------|-------------|
| 9.0 | Current default - Latest AV2 CTC specification. Only 8.0/9.0 enable ECF. | `AVM_CWG_Regular_CTCv5_v7.4.5.xlsm` | `AVM_CWG_AS_CTC_v10.0.xlsm` |
| 8.0 | Previous AV2 CTC version | `AVM_CWG_Regular_CTCv5_v7.4.5.xlsm` | `AVM_CWG_AS_CTC_v10.0.xlsm` |
| 7.0 | Earlier AV2 CTC version | `AVM_CWG_Regular_CTCv5_v7.4.5.xlsm` | `AVM_CWG_AS_CTC_v10.0.xlsm` |
| 6.0 | AV2 CTC with updated VMAF parameters | `AVM_CWG_Regular_CTCv5_v7.4.5.xlsm` | `AVM_CWG_AS_CTC_v10.0.xlsm` |
| 5.0 | AV2 CTC with expanded test set | `AVM_CWG_Regular_CTCv5_v7.4.5.xlsm` | `AVM_CWG_AS_CTC_v10.0.xlsm` |
| 4.0 | Earlier AV2 CTC version | `AVM_CWG_Regular_CTCv4_v7.3.2.xlsm` | `AVM_CWG_AS_CTC_v9.7.1.xlsm` |
| 3.0 | Legacy version | `AVM_CWG_Regular_CTC_v7.2.xlsm` (not shipped in `bin/`) | `AVM_CWG_AS_CTC_v9.7.xlsm` |
| 2.0 | Legacy version | `AVM_CWG_Regular_CTC_v7.1.xlsm` | `AVM_CWG_AS_CTC_v9.7.xlsm` |

> **Important**: The CTC version affects test clips, encoder command-line parameters, QP values, and Excel template files. Make sure to use the correct version for your testing requirements.

> **Note**: Any value outside the list above silently falls back to legacy behaviour —
> legacy QP values (`23, 31, 39, ...`), `--cq-level` instead of `--qp`, and the
> `AVM_CWG_Regular_CTC_v6.1.xlsm` / `AVM_CWG_AS_CTC_v9.6.xlsm` templates. The framework
> does not validate `ctc_version`, so a typo will not raise an error.

#### 2. Paths (User Must Configure)

These paths **must be configured** by the user before running tests:

```yaml
paths:
  # Root path, relative to the src directory (or an absolute path).
  root: ".."

  # Optional. Defaults to {root}/bin when omitted.
  # bin: "/absolute/path/to/bin"

  # Optional. Defaults to {root}/test when omitted.
  # work: "/absolute/path/to/test"

  # Path to video content for CTC testing (REQUIRED)
  # This should point to the directory containing AV2 CTC test sequences
  content: "/path/to/your/av2_ctc_sequences/"

  # Path to subjective test content (required only if enable_subjective_test is true)
  subjective_content: "/path/to/your/subjective_test_sequences/"
```

| Path | Description | Required |
|------|-------------|----------|
| `root` | Root directory for the framework. The shipped default `".."` resolves to the `convexhull_framework` directory when scripts are run from `src/`. | Yes |
| `bin` | Directory containing encoder/decoder/tool binaries and Excel templates. Commented out by default, so it resolves to `{root}/bin`. | No |
| `work` | Test output directory. Commented out by default, so it resolves to `{root}/test`. | No |
| `content` | Directory containing CTC test sequences (`.y4m` files) | Yes |
| `subjective_content` | Directory containing subjective test sequences | Only if `enable_subjective_test: true` |
| `ecf.content_path` | Directory containing ECF test sequences (see [ECF Testing](#ecf-extended-chroma-format-testing)) | Only if `ecf.enabled: true` |

> **Important**: The `content` path must point to a directory containing the AV2 CTC test sequences in `.y4m` format. Without valid test content, the framework cannot run any tests.

> **Important**: With the default relative `root`, every derived path (`bin`, `test`,
> `analysis`) is relative to `src/`, so all scripts must be run from the `src` directory.
> This also applies to the generated cluster job scripts — see
> [Relative vs. absolute paths on a cluster](#relative-vs-absolute-paths-on-a-cluster).

**Example configurations:**

```yaml
# Default: everything under the framework directory, run from src/
paths:
  root: ".."
  content: "/data/video/av2_ctc/"
  subjective_content: "/data/video/av2_subjective/"

# Absolute root, e.g. when jobs execute on a cluster with an arbitrary
# working directory, or when test output belongs on a different volume
paths:
  root: "/data/avm-ctc/tools/convexhull_framework"
  work: "/scratch/avm-ctc-run/test"
  content: "/data/video/av2_ctc/"
  subjective_content: "/data/video/av2_subjective/"

# Windows setup
paths:
  root: ".."
  content: "D:/VideoContent/av2_ctc/"
  subjective_content: "D:/VideoContent/av2_subjective/"
```

#### 3. Executables

All entries are resolved relative to the `bin` directory, so a bare name refers to
`{bin}/{name}`. Use an absolute path to point outside `bin`.

```yaml
executables:
  ffmpeg: "ffmpeg"
  aomenc: "avmenc-v14.0.0"        # AVM encoder binary name
  aomdec: "avmdec-v14.0.0"        # AVM decoder binary name
  svtav1: "SvtAv1EncApp"          # SVT-AV1 encoder
  av1enc: "av1enc"                # libaom AV1 encoder
  av1dec: "av1dec"                # libaom AV1 decoder
  hmenc: "TAppEncoderStatic"      # HM (HEVC) encoder
  vmaf: "vmaf"
  hdr_convert: "HDRConvert"       # HDRTools scaler
  aom_scaler: "lanczos_resample_y4m"
```

> **Note**: Update `aomenc`/`aomdec` to match the AVM release you are testing.

#### 4. Feature Flags

```yaml
features:
  enable_open_gop: false               # Open GOP structure
  enable_parallel_gop_encoding: true   # Parallel GOP encoding for RA/AS
  enable_subjective_test: false        # Subjective test mode (different QPs and content)
  enable_temporal_filter: false        # Temporal filter
  enable_verification_test_config: true
  enable_timing_info: true             # Collect encode/decode timing
  use_perf_util: true                  # Linux only: use `perf stat` instead of `/usr/bin/time`
  enable_md5: true                     # MD5 checksum verification
  enable_pre_interpolation: true       # Pre-interpolation for BD-rate calculation
  use_pchip_interpolation: false       # false = linear interpolation
  as_downscale_on_the_fly: false       # Downscale during encode for AS tests
```

> **Note**: `enable_subjective_test: true` overrides several of these — it forces
> `RA`-only testing, the subjective content path and QPs, temporal filtering on, and
> timing/`perf`/MD5 off (see `Config.py`).

#### 5. Test Parameters

```yaml
test:
  configurations: ["LD", "RA", "AI", "STILL"]  # Configs to run
  dataset: "CTC_TEST_SET"

encoding:
  min_gop_length: 16
  sub_gop_size: 16
  gop_size: 65

# Frames encoded per configuration when enable_verification_test_config is true
frame_counts:
  LD: 130
  RA: 130
  AI: 30
  AS: 130
  STILL: 1

# Used instead of frame_counts for CTC 7.0/8.0/9.0 when
# enable_verification_test_config is false (AI drops to 15 frames)
frame_counts_non_verification:
  AI: 15
  # ...

# Downscale ratios and scaling algorithms for AS tests
scaling:
  downscale_ratios: [1.0, 1.5, 2.0, 3.0, 4.0, 6.0]
  downscale_algos: ["lanczos"]
  upscale_algos: ["lanczos"]
```

> **Note**: `test.configurations` is only read by `AV2CTCTest.py`. `ConvexHullTest.py`
> always runs the `AS` configuration.

#### 6. QP Values

The framework uses different QP values for each test configuration. These are defined in `config.yaml`:

**AV1/AV2 QP Values (CTC versions 2.0-9.0):**

| Configuration | QP Values |
|---------------|-----------|
| LD (Low Delay) | 110, 135, 160, 185, 210, 235 |
| RA (Random Access) | 110, 135, 160, 185, 210, 235 |
| AI (All Intra) | 85, 110, 135, 160, 185, 210 |
| AS (Adaptive Streaming) | 110, 135, 160, 185, 210, 235 |
| STILL | 60, 85, 110, 135, 160, 185 |

**HEVC QP Values:**

| Configuration | QP Values |
|---------------|-----------|
| LD, RA, AI, AS, STILL | 22, 27, 32, 37, 42, 47 |

**Subjective Test QP Values (when `enable_subjective_test: true`):**

| Configuration | QP Values |
|---------------|-----------|
| RA | 110, 122, 135, 147, 160, 172, 185, 197, 210, 222, 235 |

To customize QP values, edit the `encoding.qps` section in `config.yaml`:

```yaml
encoding:
  qps:
    LD: [110, 135, 160, 185, 210, 235]
    RA: [110, 135, 160, 185, 210, 235]
    AI: [85, 110, 135, 160, 185, 210]
    AS: [110, 135, 160, 185, 210, 235]
    STILL: [60, 85, 110, 135, 160, 185]
```

---

## Framework Structure

```
avm-ctc/tools/convexhull_framework/
├── bin/                    # Encoder/decoder binaries
├── src/                    # Source code
│   ├── config.yaml         # User configuration file
│   ├── Config.py           # Configuration loader
│   ├── AV2CTCTest.py       # Main script for LD/RA/AI/STILL tests (and ECF)
│   ├── ConvexHullTest.py   # Main script for AS tests
│   ├── Launch.py           # Submit jobs to compute cluster
│   ├── Utils.py            # Utility functions
│   ├── VideoEncoder.py     # Encoder wrapper
│   ├── VideoDecoder.py     # Decoder wrapper
│   ├── VideoScaler.py      # Scaling functions
│   ├── ScalingTest.py      # Standalone downscale/upscale quality test
│   ├── EncDecUpscale.py    # Encode-decode-upscale pipeline
│   ├── CalculateQualityMetrics.py  # Quality metric calculation
│   ├── CalcQtyWithVmafTool.py      # VMAF tool invocation and log parsing
│   ├── CalcPerceptualMetrics.py    # LPIPS/DISTS/ColorVideoVDP (optional)
│   ├── PerceptualMetricsRunner.py  # Subprocess runner for the above
│   ├── CalcBDRate.py       # BD-Rate computation
│   ├── CheckEncoding.py    # Check encoding status
│   ├── AV2CTCVideo.py      # Test sequence definitions (CTC + ECF)
│   ├── AV2SubjectiveVideo.py       # Subjective test sequence definitions
│   └── AV2CTCProgress.py   # BD-Rate progress analysis
├── test/                   # Test output directory (created automatically)
│   ├── AV2CTC_TestCmd.log  # Command log file (when LogCmdOnly=1)
│   ├── cmdLogs/            # Shell scripts for cluster execution
│   │   ├── AI/             # One subfolder per configuration in
│   │   ├── LD/             # test.configurations (AS for ConvexHullTest.py)
│   │   ├── RA/
│   │   ├── AS/
│   │   └── STILL/
│   ├── bitstreams/         # Encoded bitstreams
│   │   ├── RA/             # Organized by test configuration
│   │   ├── LD/
│   │   ├── AI/
│   │   ├── AS/
│   │   └── STILL/
│   ├── decodedYUVs/        # Decoded video files
│   ├── downscaledYUVs/     # Downscaled sources (AS tests)
│   ├── upscaledYUVs/       # Upscaled sources (AS scaling test)
│   ├── decUpscaledYUVs/    # Decoded-then-upscaled video (AS tests)
│   ├── encLogs/            # Encoding logs
│   ├── decLogs/            # Decoding logs
│   ├── qualityLogs/        # Quality metric logs
│   ├── vmafLogs/           # VMAF tool logs
│   ├── perfLogs/           # Timing / perf counter logs
│   ├── configFiles/        # Generated tool config files (e.g. HDRConvert)
│   └── testLogs/           # Framework logs (AV2CTC_Test.log)
├── analysis/               # Analysis results
│   ├── rdresult/           # RD data files
│   ├── scalingresult/      # Scaling test results
│   └── summary/            # Summary reports
├── ctc_result/             # CTC progress analysis outputs
├── requirements.in         # Dependency source file (pip-compile input)
├── requirements.txt        # Pinned Python dependencies (with hashes)
├── requirements-perceptual.in   # Optional perceptual metric deps (source)
├── requirements-perceptual.txt  # Optional perceptual metric deps (not hashed)
├── setup_env.sh            # Environment setup script
└── USER_GUIDE.md           # This user guide
```

> **Note**: `test/` and `analysis/` are created under the `paths.work` and `paths.root`
> locations from `config.yaml`, which need not be inside the framework directory.

---

## Running Tests

### Regular CTC Tests (AV2CTCTest.py)

For LD, RA, AI, and STILL configurations.

#### Basic Usage

```bash
cd src

# When enable_parallel_gop_encoding is set to false, the following
# command will run all configurations defined in config.yaml

# Run encoding test (runs all configurations defined in config.yaml) and generate
# command log file (AV2CTC_TestCmd.log) to the test directory.
python AV2CTCTest.py -f encode -c av2 -m aom -p 0 --LogCmdOnly 1

# Launch encoding jobs to the compute cluster
python Launch.py AV2CTC_TestCmd.log

# Generate summary/quality results (after all encoding jobs are done)
python AV2CTCTest.py -f summary -c av2 -m aom -p 0

# For RA configuration, when enable_parallel_gop_encoding is set to true
# (the shipped default in config.yaml), the following commands should be run

# Generate the encoding command that encode each GOP (65 frames) of the
# input sequence into two separated OBU files in parallel.
python AV2CTCTest.py -f encode -c av2 -m aom -p 0 --LogCmdOnly 1

# Launch encoding jobs to the compute cluster
python Launch.py AV2CTC_TestCmd.log

# Concatenate the two OBU files into a single OBU file after all encoding jobs are done.
python AV2CTCTest.py -f concatenate -c av2 -m aom -p 0

# Generate command that decode and calculate quality metrics.
python AV2CTCTest.py -f decode -c av2 -m aom -p 0 --LogCmdOnly 1

# Launch decoding jobs to the compute cluster
python Launch.py AV2CTC_TestCmd.log

# Generate summary/quality results (after all decoding jobs are done)
python AV2CTCTest.py -f summary -c av2 -m aom -p 0
```

> **Note**: The test configurations (LD, RA, AI, STILL) are defined in `config.yaml` under `test.configurations`. The script will run tests for all configurations listed there.

> **Note**: The `concatenate` and `decode` functions apply to the `RA` configuration only, and
> only when `enable_parallel_gop_encoding: true`. They log an error otherwise.

#### Specifying Test Configurations

Edit `config.yaml` to select which test configurations to run:

```yaml
test:
  # Run only RA tests
  configurations: ["RA"]

  # Run multiple configurations
  # configurations: ["LD", "RA", "AI", "STILL"]
```

#### Command-line Options

| Option | Description | Values | Default |
|--------|-------------|--------|---------|
| `-f`, `--function` | Function to run | `clean`, `encode`, `decode`, `summary`, `concatenate` | Required |
| `-p`, `--EncodePreset` | Encoder preset. Passed to `--cpu-used` for aom/avm and `--preset` for SVT-AV1; the valid range is whatever the chosen encoder accepts (`0` is slowest/highest quality). | e.g. `0`–`13` | None |
| `-c`, `--CodecName` | Codec name | `av1`, `av2`, `hevc` | `av2` |
| `-m`, `--EncodeMethod` | Encoder method | `aom`, `svt`, `hm` | None |
| `-l`, `--LoggingLevel` | Logging level (0-5) | `0`:None, `1`:Critical, `2`:Error, `3`:Warning, `4`:Info, `5`:Debug | `3` (Warning) |
| `-CmdOnly`, `--LogCmdOnly` | Log commands to file without executing | `true`/`false` | `true` |
| `-s`, `--SaveMemory` | Delete intermediate files to save disk space | `true`/`false` | `true` |

> **Note**: By default, `LogCmdOnly` is `true`, which means the script will generate encoding commands to a log file instead of executing them. This allows you to review the commands or run them on a distributed compute cluster. Set `-CmdOnly false` to execute commands locally.

#### Complete Workflow Example

```bash
# Step 1: Edit config.yaml to set desired test configurations
# test:
#   configurations: ["RA"]

# Step 2: Clean previous results (optional)
python AV2CTCTest.py -f clean

# Step 3: Run encoding locally. -CmdOnly false is required — otherwise the
#         commands are only written to the log file and nothing is encoded.
python AV2CTCTest.py -f encode -p 0 -c av2 -m aom -l 4 -CmdOnly false

# Step 4: Generate summary/quality results
python AV2CTCTest.py -f summary -p 0 -c av2 -m aom -l 4 -CmdOnly false
```

### Adaptive Streaming Tests (ConvexHullTest.py)

For the AS (Adaptive Streaming) configuration with downscale/upscale pipeline.
`ConvexHullTest.py` always operates on the `AS` configuration; it ignores
`test.configurations` in `config.yaml`.

#### Basic Usage

```bash
cd src

# When enable_parallel_gop_encoding is set to false:

# Run encoding test and generate the command log file (AV2CTC_TestCmd.log)
# in the test directory.
python ConvexHullTest.py -f encode -c av2 -m aom -p 0 -t hdrtool --LogCmdOnly 1

# Launch encoding jobs to the compute cluster
python Launch.py AV2CTC_TestCmd.log

# Generate summary/quality results (after all encoding jobs are done)
python ConvexHullTest.py -f convexhull -c av2 -m aom -p 0 -t hdrtool
```

Run the same test with a different scaler by changing `-t`:

```bash
# ffmpeg scaler
python ConvexHullTest.py -f encode -p 0 -c av2 -m aom -t ffmpeg -l 4

# AOM Lanczos scaler
python ConvexHullTest.py -f encode -p 0 -c av2 -m aom -t aom -l 4
```

#### Parallel GOP Encoding Workflow

When `enable_parallel_gop_encoding: true` in `config.yaml` (the shipped default):

```bash
# Step 1: Encode each GOP (65 frames) of the input sequence into separate
#         OBU files in parallel.
python ConvexHullTest.py -f encode -c av2 -m aom -p 0 -t hdrtool --LogCmdOnly 1

# Step 2: Launch encoding jobs to the compute cluster
python Launch.py AV2CTC_TestCmd.log

# Step 3: Concatenate the OBU files into a single OBU file after all encoding
#         jobs are done. Note the function name is spelled "concatnate".
python ConvexHullTest.py -f concatnate -c av2 -m aom -p 0 -t hdrtool

# Step 4: Generate the decode + quality metric commands
python ConvexHullTest.py -f decode -c av2 -m aom -p 0 -t hdrtool --LogCmdOnly 1

# Step 5: Launch decoding jobs to the compute cluster
python Launch.py AV2CTC_TestCmd.log

# Step 6: Generate summary/quality results (after all decoding jobs are done)
python ConvexHullTest.py -f convexhull -c av2 -m aom -p 0 -t hdrtool
```

> **Note**: `concatnate` and `decode` require `enable_parallel_gop_encoding: true`; the
> script reports an error and does nothing otherwise.

#### Command-line Options

| Option | Description | Values | Default |
|--------|-------------|--------|---------|
| `-f`, `--function` | Function to run | `clean`, `scaling`, `encode`, `convexhull`, `concatnate`, `decode` | Required |
| `-p`, `--EncodePreset` | Encoder preset (see the `AV2CTCTest.py` table above) | e.g. `0`–`13` | None |
| `-c`, `--CodecName` | Codec name | `av1`, `av2`, `hevc` | None |
| `-m`, `--EncodeMethod` | Encoder method | `aom`, `svt`, `hm` | None |
| `-t`, `--ScaleMethod` | Scaling tool for downscale/upscale | `hdrtool`, `ffmpeg`, `aom` | None — pass it explicitly |
| `-l`, `--LoggingLevel` | Logging level (0-5) | `0`:None, `1`:Critical, `2`:Error, `3`:Warning, `4`:Info, `5`:Debug | `3` (Warning) |
| `-CmdOnly`, `--LogCmdOnly` | Log commands to file without executing | `true`/`false` | `true` |
| `-k`, `--KeepUpscaleOutput` | Keep upscaled YUV files after clean | `true`/`false` | `false` |
| `-s`, `--SaveMemory` | Delete intermediate files to save disk space | `true`/`false` | `true` |

> **Note**: There is no `summary` function in `ConvexHullTest.py` — use `convexhull` to
> produce the AS RD results. The concatenate function is spelled `concatnate` here, unlike
> `concatenate` in `AV2CTCTest.py`.

> **Note**: By default, `LogCmdOnly` is `true`, which means the script will generate encoding commands to a log file instead of executing them. This allows you to review the commands or run them on a distributed compute cluster. Set `-CmdOnly false` to execute commands locally.

### Scaling Tools

The `-t` parameter specifies which tool to use for video downscaling and upscaling in AS tests:

| Tool | Description |
|------|-------------|
| `hdrtool` | HDRTools (HDRConvert) - High-quality scaling with HDR support. Used in the examples throughout this guide. |
| `ffmpeg` | FFmpeg - Widely available, good quality scaling. |
| `aom` | AOM Lanczos scaler - Optimized for AV1/AV2 testing. |

> **Note**: `-t` has no default value. Pass it on every `ConvexHullTest.py` invocation
> other than `-f clean`.

> **Note**: Ensure the corresponding binary is available in the `bin/` directory or configured in `config.yaml`.

---

## ECF (Extended Chroma Format) Testing

The framework supports Extended Chroma Format (ECF) testing as defined in CTC v8.0+ Section 6. ECF tests evaluate codec performance on non-4:2:0 content including 4:4:4, 4:2:2, HDR, YCoCg, and Screen Content sequences.

### ECF Content Classes

| Class | Description | Chroma Format | Content |
|-------|-------------|---------------|---------|
| ECF-1 | SDR 4:4:4 | yuv444 | 8 sequences (4K to 360p) |
| ECF-2 | SDR 4:2:2 | yuv422 | 8 sequences (4K to 360p) |
| ECF-3 | HDR 4:4:4 | yuv444 | 4 sequences |
| ECF-4 | HDR 4:2:2 | yuv422 | 4 sequences |
| ECF-5 | YCoCg | ycgco | 2 sequences |
| ECF-6 | Screen Content / GBR | yuv444/gbrp/yuv422 | 7 sequences |

### Enabling ECF Testing

Set `ecf.enabled: true` in `config.yaml`:

```yaml
ecf:
  enabled: true
  content_path: "/path/to/ecf_test_sequences/"
  configurations: ["AI", "RA", "LD"]
  dataset: "ECF_TEST_SET"
  gop_size: 33
  frame_counts:
    AI: 5
    RA: 66
    LD: 33
  psnr_yuv_weights:
    "444":
      psnr_y_weight: 4.0
      psnr_u_weight: 1.0
      psnr_v_weight: 1.0
    "422":
      psnr_y_weight: 8.0
      psnr_u_weight: 1.0
      psnr_v_weight: 1.0
  template: "AOM_CWG_Regular_CTC_ECF_v2.2_Anchor_cmt_1f8a.xlsm"
```

> **Important**: When ECF is enabled, the framework switches to ECF test sequences, configurations, and parameters. Regular CTC tests (LD/RA/AI/AS/STILL with 4:2:0 content) are not run. Disable ECF (`ecf.enabled: false`) to return to regular CTC testing.

> **Important**: The ECF-specific encoder settings (10-bit coding, resolution-based tiling,
> screen-content and HDR flags) are additionally gated on `ctc_version` being `"8.0"` or
> `"9.0"`. With `ecf.enabled: true` and an older `ctc_version`, the framework will encode
> the ECF sequences using regular-CTC encoder settings. Keep `ctc_version: "9.0"` for ECF
> runs.

### ECF vs Regular CTC Differences

| Parameter | Regular CTC | ECF |
|-----------|------------|-----|
| Configurations | LD, RA, AI, AS, STILL | AI, RA, LD only |
| GOP size | 65 | 33 |
| Frame counts (RA/LD) | 130 | 66 / 33 |
| Frame counts (AI) | 30 (or 15) | 5 |
| Bit depth | Varies by content | Always 10-bit encoding |
| PSNR-YUV weights (4:4:4) | Y:U:V = 14:1:1 | Y:U:V = 4:1:1 |
| PSNR-YUV weights (4:2:2) | N/A | Y:U:V = 8:1:1 |
| Parallel GOP encoding | RA only, 65-frame GOPs | RA only, 33-frame GOPs |

### ECF Tiling and Threading Rules

ECF uses resolution-based tiling rules (CTC v8.0+ Section 6.2), different from the class-based rules in regular CTC:

**RA configuration:**

| Resolution | tile-rows | tile-columns | threads |
|------------|-----------|--------------|---------|
| 4K+ (w>=3840 **and** h>=2160) | 2 | 2 | 16 |
| FHD+ (w>=1920 **and** h>=858) | 1 | 1 | 4 |
| Everything else | 0 | 0 | 1 |

**LD configuration:**

| Resolution | tile-rows | tile-columns | threads |
|------------|-----------|--------------|---------|
| FHD+ (w>=1920 **and** h>=858) | 1 | 2 | 8 |
| 720p (w**==**1280 **and** h**==**720) | 0 | 1 | 2 |
| Everything else | 0 | 0 | 1 |

**AI configuration:**

| Resolution | tile-rows | tile-columns | threads |
|------------|-----------|--------------|---------|
| 4K+ (w>=3840 **and** h>=2160) | 0 | 1 | 2 |
| Everything else | 0 | 0 | 1 |

> **Note**: The conditions are evaluated in the order shown and all use `and`, so a clip
> must exceed the threshold in *both* dimensions to reach a tiling tier. The 720p LD tier
> is an exact `1280x720` match. Portrait ECF clips (e.g. `1920x2560`) therefore fall
> through to the single-tile, single-thread case. `--row-mt=0` is set in every case. See
> `EncodeWithAOM_AV2()` in `VideoEncoder.py`.

### ECF-Specific Encoder Flags

- **ECF-6 (Screen Content)**: Adds `--tune-content=screen --enable-intrabc-ext=1 --enable-extended-sdp=0`
- **ECF-3/ECF-4 (HDR)**: Adds `--color-primaries=bt2020 --transfer-characteristics=smpte2084 --matrix-coefficients=bt2020ncl --chroma-sample-position=topleft`

### ECF Content Directory Structure

ECF test sequences should be organized by class under the `ecf.content_path`:

```
ecf_test_sequences/
├── ECF-1/    # SDR 4:4:4 sequences (.y4m)
├── ECF-2/    # SDR 4:2:2 sequences (.y4m)
├── ECF-3/    # HDR 4:4:4 sequences (.y4m)
├── ECF-4/    # HDR 4:2:2 sequences (.y4m)
├── ECF-5/    # YCoCg sequences (.y4m)
└── ECF-6/    # Screen Content / GBR sequences (.y4m)
```

### Running ECF Tests

The ECF test workflow is the same as regular CTC tests — use `AV2CTCTest.py` with the same commands. The framework automatically uses ECF settings when `ecf.enabled: true`.

```bash
cd src

# Step 1: Edit config.yaml to enable ECF and set content path
# ecf:
#   enabled: true
#   content_path: "/path/to/ecf_sequences/"

# Step 2: Generate encoding commands
python AV2CTCTest.py -f encode -c av2 -m aom -p 0 --LogCmdOnly 1

# Step 3: Submit to cluster
python Launch.py AV2CTC_TestCmd.log

# Step 4: For RA with parallel GOP encoding, concatenate after encoding completes
python AV2CTCTest.py -f concatenate -c av2 -m aom -p 0

# Step 5: Decode (for RA parallel GOP)
python AV2CTCTest.py -f decode -c av2 -m aom -p 0 --LogCmdOnly 1
python Launch.py AV2CTC_TestCmd.log

# Step 6: Generate summary
python AV2CTCTest.py -f summary -c av2 -m aom -p 0
```

### ECF Report Template

When ECF is enabled, `AV2CTCProgress.py` uses the ECF-specific Excel template
(`CTC_ECFXLSTemplate`, resolved from `ecf.template`) instead of the regular CTC template,
and writes the result to `CTC_ECF_{anchor_release}-{test_release}.xlsm`. Ensure the
template file (e.g., `AOM_CWG_Regular_CTC_ECF_v2.2_Anchor_cmt_1f8a.xlsm`) is placed in the
`bin/` directory.

---

## Distributed Cluster Execution (Launch.py)

When running tests with `LogCmdOnly=1` (the default), the framework generates shell scripts for each encoding job instead of executing them directly. This enables distributed execution on compute clusters.

### How It Works

1. **Generate Commands**: Run `AV2CTCTest.py` or `ConvexHullTest.py` with `--LogCmdOnly 1`
2. **Shell Scripts Created**: Individual shell scripts are generated under `test/cmdLogs/{config}/`
3. **Command Log Generated**: A combined log file `*_TestCmd.log` is created in `test/`
4. **Submit Jobs**: Use `Launch.py` to submit jobs to your compute cluster

### Output Structure (LogCmdOnly Mode)

When running with `LogCmdOnly=1`, the following files are generated:

```
test/
├── AV2CTC_TestCmd.log          # Combined command log file
└── cmdLogs/                     # Shell scripts organized by configuration
    ├── AI/
    │   ├── Clip1_aom_av2_AI_Preset_0_QP_85.sh
    │   ├── Clip1_aom_av2_AI_Preset_0_QP_110.sh
    │   └── ...
    ├── LD/
    │   └── ...
    ├── RA/
    │   └── ...
    ├── AS/
    │   └── ...
    └── STILL/
        └── ...
```

### Using Launch.py

The `Launch.py` script reads the command log file, identifies encoding jobs, and submits the corresponding shell scripts to the compute cluster.

#### Basic Usage

```bash
cd src

# Auto-find *_TestCmd.log in WorkPath and launch all jobs
python Launch.py

# Specify a log file name (assumed to be in WorkPath)
python Launch.py AV2CTC_TestCmd.log

# Specify a full path to the log file
python Launch.py /path/to/test/AV2CTC_TestCmd.log
```

#### Command-line Options

| Argument | Description |
|----------|-------------|
| (none) | Auto-find `*_TestCmd.log` in WorkPath from config.yaml |
| `<filename>` | Use specified file in WorkPath (e.g., `AV2CTC_TestCmd.log`) |
| `<full_path>` | Use the specified full path (e.g., `/path/to/TestCmd.log`) |

#### How Launch.py Works

1. **Reads the log file**: Parses `*_TestCmd.log` to find job markers
2. **Extracts job information**: Identifies job names and test configurations (AI, LD, RA, AS, STILL)
3. **Locates shell scripts**: Finds corresponding `.sh` files in `cmdLogs/{config}/`
4. **Submits to cluster**: Calls the `submit_job()` function for each job

#### Customizing for Your Cluster

The `submit_job()` function in `Launch.py` should be customized for your compute cluster. Edit this function to use your cluster's job submission command:

```python
def submit_job(job_file_path):
    """Submit a single job to the compute cluster"""
    # need to be implemented based on the interface of the cluster
    print("submitting job %s" % job_file_path)
```

As shipped it only prints the job path — replace the body with your scheduler's
submission command (`sbatch`, `qsub`, etc.).

#### Relative vs. absolute paths on a cluster

The generated `.sh` files contain a `#!/bin/bash` line followed by the raw encode/decode
commands — **no `cd` is emitted**. Every path inside them (encoder binary, input clip,
output bitstream, log files) is written exactly as `Config.py` resolved it.

With the default `root: ".."`, those paths are relative to `src/`. A job script therefore
only runs correctly if its working directory is `src/`. You have three ways to handle this:

1. **Make `submit_job()` set the working directory** — the cleanest option, since it keeps
   `config.yaml` portable:

   ```python
   SRC_DIR = os.path.dirname(os.path.abspath(__file__))

   def submit_job(job_file_path):
       """Submit a single job to the compute cluster"""
       job = os.path.abspath(job_file_path)
       subprocess.run(["sbatch", "--chdir", SRC_DIR, job], check=True)
   ```

2. **Use absolute paths in `config.yaml`** — set `paths.root` (and optionally `bin`/`work`)
   to absolute paths before generating the commands. The job scripts then work from any
   working directory. Note the paths must be valid on the *cluster nodes*, not just on the
   submitting host.

3. **Wrap the script** — have `submit_job()` submit `bash -c "cd <src>; <job>"`.

> **Note**: `Launch.py` itself works fine with a relative `WorkPath`; it resolves the log
> file and `cmdLogs` directory before changing directory. Only the *contents* of the job
> scripts are affected.

### Complete Cluster Workflow

```bash
cd src

# Step 1: Generate encoding commands (LogCmdOnly=1 is the default)
python AV2CTCTest.py -f encode -p 0 -c av2 -m aom --LogCmdOnly 1

# Step 2: Review generated shell scripts (optional)
ls ../test/cmdLogs/RA/

# Step 3: Submit jobs to the cluster
python Launch.py AV2CTC_TestCmd.log

# Step 4: Wait for cluster jobs to complete...

# Step 5: Generate summary
python AV2CTCTest.py -f summary -p 0 -c av2 -m aom
```

### Job Naming Convention

Shell script names follow this pattern:

**Regular tests (AI, LD, STILL):**
```
{ClipName}_{Method}_{Codec}_{Config}_Preset_{Preset}_QP_{QP}.sh
```

**Parallel GOP tests (RA, AS):**
```
{ClipName}_{Method}_{Codec}_{Config}_Preset_{Preset}_QP_{QP}_start_{StartFrame}_frames_{NumFrames}.sh
```

`{ClipName}` is the source `.y4m` filename without its extension. For AS tests it is the
*downscaled* clip name, which already carries the coded resolution — either the
pre-downscaled file from `AS_Downscaled_Clips`, or, when `as_downscale_on_the_fly: true`,
a name of the form `{SourceName}_Scaled_{ScaleMethod}_{Algo}_{Width}x{Height}`. Example:

```
Tango_960x540_5994fps_10bit_420_aom_av2_AS_Preset_0_QP_110.sh
```

---

## Analyzing Results (AV2CTCProgress.py)

The `AV2CTCProgress.py` script analyzes and compares CTC test results across multiple AVM releases. It calculates BD-Rate improvements, generates RD curves, and produces summary reports.

### Purpose

- **Track AVM Progress**: Compare codec efficiency across releases (v1.0.0 through v15.0.0)
- **Calculate BD-Rate**: Compute Bjøntegaard Delta Rate for quality metrics
- **Generate RD Curves**: Visualize rate-distortion performance
- **Produce Excel Reports**: Fill CTC template spreadsheets with test data

### Supported Quality Metrics

The script calculates BD-Rate for the following metrics:

| Metric | Description |
|--------|-------------|
| `psnr_y` | PSNR for Y (luma) channel |
| `psnr_u` | PSNR for U (chroma) channel |
| `psnr_v` | PSNR for V (chroma) channel |
| `overall_psnr` | Weighted overall PSNR |
| `ssim_y` | SSIM for Y channel |
| `ms_ssim_y` | Multi-Scale SSIM for Y channel |
| `vmaf` | VMAF score |
| `vmaf_neg` | VMAF NEG (No Enhancement Gain) score |
| `psnr_hvs` | PSNR-HVS (Human Visual System) |
| `ciede2k` | CIEDE2000 color difference |
| `apsnr_y/u/v` | Arithmetic PSNR for Y/U/V channels |
| `overall_apsnr` | Weighted overall Arithmetic PSNR |
| `lpips` | LPIPS — optional, **lower is better** (negated before BD-rate) |
| `dists` | DISTS — optional, **lower is better** (negated before BD-rate) |
| `cvvdp` | ColorVideoVDP JOD — optional, higher is better |

> **Note**: `cambi` is written to the RD CSV but is not included in the BD-rate set. The
> three perceptual metrics are only present when `perceptual_metrics.enabled: true`;
> results from runs without them are excluded from those metrics' BD-rate rather than
> being counted as zero, so old and new result sets can be mixed safely.

### Basic Usage

```bash
cd src

# Run the progress analysis
python AV2CTCProgress.py
```

> **Note**: This script does not take command-line arguments. Configuration is done by editing the script directly.

### Configuration

The script uses hardcoded configuration in the following dictionaries at the top of the file:

#### 1. Input Data Paths (`csv_paths`)

Defines the location of CTC result CSV files for each release:

```python
csv_paths = {
    "v01.0.0": [
        "v01.0.0",         # Release label
        "av2",             # Codec (av1/av2)
        "aom",             # Encoder (aom/svt/hm)
        "0",               # Preset
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v1.0.0-alt-anchor-r4.0"),  # Path
    ],
    "v02.0.0": [
        "v02.0.0",
        "av2",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v2.0.0"),
    ],
    # ... more releases
}
```

The dictionary also contains a second anchor entry, `"v01.0.0-scale"`, pointing at
`AV2-CTC-v1.0.0-alt-anchor-r4.0-scale`.

#### 2. Anchor Version

The anchor (baseline) for BD-Rate comparison is chosen per release by `get_anchor_tag()`
rather than by a single module-level variable:

```python
def get_anchor_tag(tag):
    if tag in ["v01.0.0-scale"]:
        anchor_tag = "None"
    elif tag in ["v13.0.0", "v14.0.0", "v15.0.0"]:
        anchor_tag = "v01.0.0-scale"
    else:
        anchor_tag = "v01.0.0"
    return anchor_tag
```

Releases from v13.0.0 onward are compared against the `v01.0.0-scale` anchor; everything
else uses `v01.0.0`. A tag whose anchor is `"None"` is skipped in BD-Rate computation.

#### 3. Output Paths

```python
CTC_RESULT_PATH = "../ctc_result"
rd_curve_pdf = os.path.join(CTC_RESULT_PATH, "rdcurve.pdf")
bdrate_summary = os.path.join(CTC_RESULT_PATH, "Bdrate-Summary-AV1-vs-AV2.csv")
# ... more output paths
```

> **Note**: `CTC_RESULT_PATH` is relative, so run `AV2CTCProgress.py` from the `src`
> directory.

#### 4. Test Configurations

```python
CONFIG = ["AI", "LD", "RA", "Still", "AS"]
```

#### 5. Release Dates

Used for plot labels:

```python
dates = {
    "v01.0.0": "01/16/2021",
    "v02.0.0": "08/27/2021",
    "v03.0.0": "05/27/2022",
    # ... more dates
}
```

### Required Input Files

The script expects CTC result CSV files with the naming convention:

```
RDResults_{encoder}_{codec}_{config}_Preset_{preset}.csv
```

Examples:
- `RDResults_aom_av2_AI_Preset_0.csv`
- `RDResults_aom_av2_RA_Preset_0.csv`
- `RDResults_aom_av2_STILL_Preset_0.csv`

These files are typically generated by running `AV2CTCTest.py` or `ConvexHullTest.py` with the `-f summary` option.

### Output Files

After running the script, the following outputs are generated:

#### CSV Reports

| File | Description |
|------|-------------|
| `Bdrate-Summary-AV1-vs-AV2.csv` | Per-video BD-Rate for all releases |
| `AverageBdrateByTag-Summary-AV1-vs-AV2.csv` | Average BD-Rate by release and configuration |
| `AverageBdrateByTagClass-Summary-AV1-vs-AV2.csv` | Average BD-Rate by release, configuration, and video class |
| `PerVideoBdrate-Summary-AV1-vs-AV2.csv` | Detailed per-video BD-Rate by tag and class |

#### PDF Reports

| File | Description |
|------|-------------|
| `rdcurve.pdf` | Individual RD curves per video |
| `AverageBdrateByTag-Summary-AV1-vs-AV2.pdf` | Average BD-Rate trend lines by release |
| `AverageBdrateByTagClass-Summary-AV1-vs-AV2.pdf` | Trend lines by release and video class |
| `PerVideoBdrate-Summary-AV1-vs-AV2.pdf` | Bar charts of per-video BD-Rate |

> **Note**: `combined_rdcurve.pdf` and `combined_runtime.pdf` paths are still defined at the
> top of the script but nothing writes them, so those files are not produced.

#### Excel Files

The script fills CTC template Excel files with anchor and test data:

- `CTC_Regular_{anchor_release}-{test_release}.xlsm` - For AI, LD, RA, STILL configurations
- `CTC_AS_{anchor_release}-{test_release}.xlsm` - For AS configuration
- `CTC_ECF_{anchor_release}-{test_release}.xlsm` - When `ecf.enabled: true`

### Customizing the Analysis

#### Adding a New Release

1. Add an entry to the `csv_paths` dictionary:

```python
csv_paths = {
    # ... existing entries ...
    "v16.0.0": [
        "v16.0.0",
        "av2",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v16.0.0"),
    ],
}
```

2. Add format specification for plots:

```python
formats = {
    # ... existing entries ...
    "v16.0.0": ["g", "-", "s"],  # [color, line_style, marker]
}
```

3. Add release date (required — the plotting code looks up every tag in `dates`):

```python
dates = {
    # ... existing entries ...
    "v16.0.0": "09/01/2026",
}
```

4. If the new release should use a different baseline, add it to `get_anchor_tag()`.

#### Changing the Anchor

Edit `get_anchor_tag()` to map a release tag to its baseline. For example, to compare
v16.0.0 against v08.0.0:

```python
def get_anchor_tag(tag):
    if tag in ["v01.0.0-scale"]:
        anchor_tag = "None"
    elif tag in ["v16.0.0"]:
        anchor_tag = "v08.0.0"
    elif tag in ["v13.0.0", "v14.0.0", "v15.0.0"]:
        anchor_tag = "v01.0.0-scale"
    else:
        anchor_tag = "v01.0.0"
    return anchor_tag
```

#### Selecting Specific Configurations

Edit the `CONFIG` list:

```python
CONFIG = ["AI", "RA"]  # Only analyze AI and RA configurations
```

### Example Workflow

```bash
# Step 1: Ensure CTC results are available
# Results should be in ../ctc_result/ with the expected directory structure

# Step 2: Edit AV2CTCProgress.py if needed
# - Update csv_paths for your releases
# - Update get_anchor_tag() so each release maps to the right anchor
# - Verify dates and format specifications

# Step 3: Run the analysis
python AV2CTCProgress.py

# Step 4: Review outputs in ../ctc_result/
ls ../ctc_result/*.csv
ls ../ctc_result/*.pdf
ls ../ctc_result/*.xlsm
```

### Understanding BD-Rate Results

- **Negative BD-Rate**: Improvement (less bitrate needed for same quality)
- **Positive BD-Rate**: Regression (more bitrate needed for same quality)
- **-10% BD-Rate**: 10% bitrate savings at equivalent quality

Example interpretation:
```
v11.0.0 vs v01.0.0 (anchor):
- overall_psnr: -25.5% → 25.5% bitrate reduction at same PSNR
- vmaf: -22.3% → 22.3% bitrate reduction at same VMAF
```

### Notes

- The script processes all configurations in `CONFIG` (`["AI", "LD", "RA", "Still", "AS"]`) by default. Note the STILL entry is spelled `Still` here, though the input CSV is named `RDResults_..._STILL_Preset_0.csv`.
- For AS (Adaptive Streaming), it calculates convex hull BD-Rate across multiple resolutions
- Runtime/perf data is skipped for RA and AS (parallel GOP encoding makes the timing meaningless)
- Ensure all required releases have complete CTC result data before running

---

## Output Structure

After running tests, outputs are organized by test configuration:

```
test/
├── bitstreams/
│   ├── RA/
│   │   ├── Clip1_aom_av2_RA_Preset_0_QP_110.obu
│   │   ├── Clip1_aom_av2_RA_Preset_0_QP_135.obu
│   │   └── ...
│   ├── AS/
│   │   ├── Clip1_960x540_5994fps_10bit_420_aom_av2_AS_Preset_0_QP_110.obu
│   │   └── ...
│   └── ...
├── encLogs/
│   ├── RA/
│   │   ├── Clip1_aom_av2_RA_Preset_0_QP_110_EncLog.txt
│   │   └── ...
│   └── ...
├── decLogs/
│   ├── RA/
│   │   └── ...
│   └── ...
├── qualityLogs/
│   ├── RA/
│   │   └── ...
│   └── ...
└── decodedYUVs/
    ├── RA/
    │   └── ...
    └── ...
```

### Output File Naming Convention

**Bitstream files:**
```
{ClipName}_{Method}_{Codec}_{Config}_Preset_{Preset}_QP_{QP}.obu
```

**For parallel encoding (RA/AS):**
```
{ClipName}_{Method}_{Codec}_{Config}_Preset_{Preset}_QP_{QP}_start_{StartFrame}_frames_{NumFrames}.obu
```

**Concatenated file** — the two scripts name this differently:

```
# RA, from AV2CTCTest.py -f concatenate
{ClipName}_{Method}_{Codec}_RA_Preset_{Preset}_QP_{QP}.obu

# AS, from ConvexHullTest.py -f concatnate
{ClipName}_{Method}_{Codec}_AS_Preset_{Preset}_QP_{QP}_start_0_frames_{TotalFrames}.obu
```

---

## Perceptual Metrics (LPIPS, DISTS, ColorVideoVDP)

The framework can optionally compute three perceptual quality metrics alongside the
standard CTC set. They exist to support evaluation of **AI / learned coding tools**, where
PSNR-style pixel-fidelity metrics correlate poorly with what viewers actually see — a tool
can improve perceived quality without improving PSNR, or improve PSNR without looking
better.

> **Important**: These are **not** part of the AOM CTC mandatory metric set.
> `CWG-G082_AV2_CTC_v9` requires PSNR-Y, weighted PSNR-YUV, PSNR-HVS, SSIM, MS-SSIM,
> CIEDE2000, VMAF and CAMBI, all computed by libvmaf. Report the perceptual metrics as
> supplementary evidence, not as a substitute.

### The three metrics

| Metric | Type | Direction | Notes |
|--------|------|-----------|-------|
| **LPIPS** | Per-frame image | **Lower is better** | Learned perceptual similarity (Zhang et al., CVPR 2018). AlexNet backbone by default. |
| **DISTS** | Per-frame image | **Lower is better** | Deep Image Structure and Texture Similarity (Ding et al., TPAMI 2020). Strongly resolution-dependent. |
| **ColorVideoVDP** | **Video** | **Higher is better** | JOD scale, 10 = indistinguishable, can go below 0. Models colour *and* motion (Mantiuk et al., SIGGRAPH 2024). |

LPIPS and DISTS are image metrics with no temporal modelling; they are applied per frame
and averaged, which is the de facto convention but not a standard. ColorVideoVDP is a true
video metric and is the only one of the three that sees temporal artifacts such as flicker
or pumping. That is a reason to read them together rather than picking one.

> **Caveat worth knowing before you rely on LPIPS.** A 30-participant subjective study over
> AV1, VVC and DCVC at up to 4K (arXiv 2511.00969) found LPIPS the *worst*-correlating
> full-reference metric tested (PCC 0.646) against VMAF's 0.886. Treat LPIPS as one signal
> among several, not as a decision metric.

### Installation

The metrics need PyTorch and are therefore kept out of the main dependency set:

```bash
./setup_env.sh --perceptual
```

or, into an existing environment:

```bash
pip install -r requirements-perceptual.txt
```

For CUDA nodes, install torch from the PyTorch index first so you get a GPU build:

```bash
pip install torch torchvision --index-url https://download.pytorch.org/whl/cu121
pip install -r requirements-perceptual.txt
```

| File | Purpose |
|------|---------|
| `requirements-perceptual.in` | Source list for the optional extras |
| `requirements-perceptual.txt` | Install list. **Not** hash-pinned — torch ships platform-specific wheels, so a hashed lock is not portable between macOS and the Linux cluster. The core `requirements.txt` stays hash-pinned and unaffected. |

> **Licence**: `pyiqa` (which provides LPIPS and DISTS) is **PolyForm Noncommercial 1.0.0 +
> NTU S-Lab** — non-commercial use only. It is used here for AOM standards-group work. If
> this framework is reused in a product context, swap it for `torchmetrics[image]`
> (Apache-2.0), which provides both metrics and ships LPIPS weights identical to the
> reference implementation. `cvvdp` is MIT and has no such restriction.

### Configuration

```yaml
perceptual_metrics:
  enabled: false                # master switch
  metrics: ["LPIPS", "DISTS", "CVVDP"]
  frame_step: 1                 # 1 = every frame; ignored by CVVDP
  device: "auto"                # auto | cpu | cuda | cuda:N | mps
  lpips_net: "alex"             # alex | vgg | squeeze
  hdr_classes: ["G1", "G2", "ECF-3", "ECF-4"]
  cvvdp_display:
    "2160p": "standard_4k"
    "1080p": "standard_fhd"
    "default": "standard_fhd"
    "hdr": "standard_hdr_pq"
```

With `enabled: false` the framework behaves exactly as before and never imports torch, so
a normal CTC run is unaffected and needs none of the extras installed.

### HDR content

LPIPS and DISTS were trained on 8-bit SDR content and are out of distribution on PQ /
BT.2020 material, so **only ColorVideoVDP runs on the HDR classes** (`G1`, `G2`, `ECF-3`,
`ECF-4` by default). Their CSV cells are left empty for those clips — an empty cell means
"not computed", which is deliberately distinct from a `0.0`.

### ColorVideoVDP display presets

ColorVideoVDP models a **display**, not just an image. Judging 1080p content against a 4K
preset tells the metric the image occupies only the centre quarter of the screen, which
silently changes the score. The framework therefore picks the preset from the coded
resolution via the `cvvdp_display` map above.

| Preset | Resolution | Peak | Use |
|--------|-----------|------|-----|
| `standard_4k` | 3840x2160 | 200 cd/m² | 2160p SDR (A1, B, E) |
| `standard_fhd` | 1920x1080 | 200 cd/m² | 1080p and below |
| `standard_hdr_pq` | 3840x2160 | 1500 cd/m² | HDR PQ / BT.2020 |

Run `cvvdp --display ?` to list every available preset.

### Colour conversion

All three metrics consume display-encoded RGB, so the framework converts each `.y4m` with
an explicit ffmpeg pipeline rather than letting any tool infer the colorimetry — CTC `.y4m`
files carry no colour tags, and an inferred conversion would apply BT.709 to BT.2020
content:

```
ffmpeg -i in.y4m -vf "scale=in_color_matrix=bt709:in_range=tv:out_range=pc\
:flags=accurate_rnd+full_chroma_int" -pix_fmt rgb24 -f rawvideo -
```

BT.709 is used for SDR and BT.2020 non-constant luminance for the HDR classes. The
`accurate_rnd` flag is not cosmetic: without it swscale's limited-to-full expansion falls
about 2/255 short across the whole range (Y=235 maps to 253 instead of 255).
`full_chroma_int` gives proper chroma interpolation when upsampling 4:2:0. With both, the
conversion is exact on a greyscale ramp and within 1/255 on the primaries.

8-bit sources are converted to `rgb24`; anything deeper goes to `rgb48le` so the extra
precision survives. The exact command used is recorded in every output log.

### Running

No new commands — the metrics are computed as part of the normal quality step, in the same
place VMAF is, and against the same source/reconstruction pair (including the upscaled
output for AS tests). They are written into the per-job shell scripts like any other
command, so cluster execution works unchanged:

```bash
cd src
# Set perceptual_metrics.enabled: true in config.yaml first
python AV2CTCTest.py -f encode -c av2 -m aom -p 0 --LogCmdOnly 1
python Launch.py AV2CTC_TestCmd.log
python AV2CTCTest.py -f summary -c av2 -m aom -p 0
```

The measurement itself is done by `PerceptualMetricsRunner.py`, invoked as a subprocess.
You can run it standalone to check a single pair:

```bash
python PerceptualMetricsRunner.py --ref orig.y4m --dist recon.y4m \
    --metrics LPIPS,DISTS,CVVDP --cvvdp-display standard_fhd --out result.json
```

### Output

Three columns are added to `RDResults_*.csv`, grouped with the other quality metrics —
after `CAMBI` and before the timing columns:

```
...,APSNR_V,CAMBI,LPIPS,DISTS,CVVDP,EncT[s],DecT[s],...
```

> **Note**: this shifts `EncT[s]`, `DecT[s]`, the instruction/cycle columns and both MD5
> columns right by the number of enabled perceptual metrics. `AV2CTCProgress.WriteSheet`
> compensates by dropping the perceptual columns before filling a CTC `.xlsm` template,
> which is laid out by absolute column index. If you consume the RD CSV with your own
> tooling, read it by **column name** rather than position.

Per-frame values are appended to `Perframe_RDResults_*.csv` in the same way. A raw
`<clip>_perceptual.json` log is written to `qualityLogs/<cfg>/` containing the aggregate
and per-frame values, the ffmpeg command used, package versions, and ColorVideoVDP's
signature string.

> **Note**: ColorVideoVDP's aggregate JOD is an L*p* norm over frames, not a mean, so the
> per-frame series will not average to the reported aggregate.

BD-rate is computed for all three in `AV2CTCProgress.py`. LPIPS and DISTS are negated
first, so a negative BD-rate keeps its usual meaning of "bitrate saving" for every metric.
Encodes where a metric was not computed are excluded rather than counted as zero.

### Offline and cluster nodes

- **ColorVideoVDP** downloads nothing at runtime; all its parameters ship in the package.
- **LPIPS and DISTS** fetch ImageNet backbone weights (AlexNet, and VGG16 at ~528 MB) from
  `download.pytorch.org` on first use. On nodes without internet, pre-populate
  `$TORCH_HOME/hub/checkpoints` and export `TORCH_HOME` in the job environment.
- A real `ffmpeg` binary is required on every node.
- Set `device: "cpu"` if the nodes have no GPU. It works, but is substantially slower on
  large sequences; `frame_step` gives you a faster approximate signal for LPIPS/DISTS.

---

## Common Workflows

### Workflow 1: Full RA Test Run (local execution)

```bash
cd src

# Activate environment
source ../venv/bin/activate

# Edit config.yaml to run only RA tests
# test:
#   configurations: ["RA"]

# Run complete RA test. -CmdOnly false makes the framework execute the
# commands; without it, LogCmdOnly defaults to true and only the command
# log is written.
python AV2CTCTest.py -f encode -p 0 -c av2 -m aom -l 4 -CmdOnly false
python AV2CTCTest.py -f summary -p 0 -c av2 -m aom -l 4 -CmdOnly false
```

### Workflow 2: Generate Commands Only (Dry Run)

Useful for distributed execution on compute clusters:

```bash
# Step 1: Generate encoding commands without executing
python AV2CTCTest.py -f encode -p 0 -c av2 -m aom --LogCmdOnly 1

# Commands are saved to: test/AV2CTC_TestCmd.log
# Shell scripts are saved to: test/cmdLogs/{AI,LD,RA,STILL}/

# Step 2: Submit jobs to compute cluster
python Launch.py AV2CTC_TestCmd.log

# Step 3: Wait for jobs to complete on the cluster...

# Step 4: Run summary
python AV2CTCTest.py -f summary -p 0 -c av2 -m aom
```

### Workflow 3: Adaptive Streaming with Parallel Encoding (local execution)

```bash
cd src

# Run parallel encoding with hdrtool scaler. -CmdOnly false is required to
# actually execute the commands rather than only logging them.
python ConvexHullTest.py -f encode -p 0 -c av2 -m aom -t hdrtool -l 4 -CmdOnly false

# Concatenate chunks
python ConvexHullTest.py -f concatnate -p 0 -c av2 -m aom -t hdrtool -l 4 -CmdOnly false

# Decode and calculate quality
python ConvexHullTest.py -f decode -p 0 -c av2 -m aom -t hdrtool -l 4 -CmdOnly false

# Generate results
python ConvexHullTest.py -f convexhull -p 0 -c av2 -m aom -t hdrtool -l 4
```

### Workflow 4: Adaptive Streaming with Cluster Execution

```bash
cd src

# Step 1: Generate encoding commands for AS tests
python ConvexHullTest.py -f encode -p 0 -c av2 -m aom -t hdrtool --LogCmdOnly 1

# Commands are saved to: test/AV2CTC_TestCmd.log
# Shell scripts are saved to: test/cmdLogs/AS/

# Step 2: Submit jobs to compute cluster
python Launch.py AV2CTC_TestCmd.log

# Step 3: Wait for cluster jobs to complete...

# Step 4: Concatenate chunks (after encoding jobs complete)
python ConvexHullTest.py -f concatnate -p 0 -c av2 -m aom -t hdrtool --LogCmdOnly 0

# Step 5: Generate Decode and calculate quality commands
python ConvexHullTest.py -f decode -p 0 -c av2 -m aom -t hdrtool --LogCmdOnly 1

# Commands are saved to: test/AV2CTC_TestCmd.log
# Shell scripts are saved to: test/cmdLogs/AS/

# Step 6: Submit jobs to compute cluster
python Launch.py AV2CTC_TestCmd.log

# Step 7: Wait for cluster jobs to complete...

# Step 8: Generate results
python ConvexHullTest.py -f convexhull -p 0 -c av2 -m aom -t hdrtool
```

---

## Troubleshooting

### Environment Setup Issues

#### "Python not found" error

Ensure Python 3.8+ is installed and in your PATH:
```bash
python3 --version
```

#### Permission denied on setup_env.sh

Make the script executable:
```bash
chmod +x setup_env.sh
```

#### Package installation failures

Try upgrading pip first:
```bash
pip install --upgrade pip
```

If specific packages fail, install them individually:
```bash
pip install numpy
pip install pandas
# etc.
```

#### "THESE PACKAGES DO NOT MATCH THE HASHES" from setup_env.sh

`setup_env.sh` installs with `pip install --require-hashes`. This error means
`requirements.txt` is out of sync with what pip resolved — usually because it was
regenerated without `--generate-hashes`, or a package has no wheel for your platform and
pip fell back to a source distribution. Regenerate it with:

```bash
pip install pip-tools
pip-compile --generate-hashes requirements.in -o requirements.txt
```

#### xlrd/openpyxl issues with Excel files

For `.xlsx` files, ensure openpyxl is installed:
```bash
pip install openpyxl
```

For older `.xls` files, xlrd is needed:
```bash
pip install xlrd
```

### Runtime Issues

#### 1. "Encoder not found" error

**Solution**: Ensure encoder binaries are in the `bin/` directory or update paths in `config.yaml`:

```yaml
executables:
  aomenc: "/full/path/to/avmenc"
```

#### 2. "Content path not found" error

**Solution**: Update the content path in `config.yaml`:

```yaml
paths:
  content: "/your/path/to/test/sequences/"
```

#### 3. YAML configuration error

**Solution**: Ensure PyYAML is installed:

```bash
pip install PyYAML
```

#### 4. Missing quality metrics

**Solution**: Ensure VMAF tool is available and configured:

```yaml
executables:
  vmaf: "vmaf"  # or full path
```

#### 5. Parallel encoding chunks not concatenating

**Solution**: Ensure all encoding jobs completed successfully before running concatenation:

```bash
# Check for incomplete bitstreams
ls -la test/bitstreams/AS/
```

### Perceptual Metric Issues

#### "torch is not installed" / "pyiqa is not installed"

The perceptual metrics are enabled but their optional dependencies are not present:

```bash
./setup_env.sh --perceptual
# or
pip install -r requirements-perceptual.txt
```

Set `perceptual_metrics.enabled: false` in `config.yaml` if you did not intend to run them.

#### LPIPS/DISTS columns are empty for some clips

Expected for the HDR classes (`G1`, `G2`, `ECF-3`, `ECF-4`). Those metrics are trained on
8-bit SDR content and are out of distribution on PQ/BT.2020, so only ColorVideoVDP is run
there. An empty cell means "not computed" and is distinct from `0.0`.

#### Model weight download fails on cluster nodes

LPIPS and DISTS fetch ImageNet backbones on first use. On nodes without internet, populate
the cache on a connected machine and ship it:

```bash
export TORCH_HOME=/shared/torch_cache
python -c "import pyiqa; pyiqa.create_metric('lpips'); pyiqa.create_metric('dists')"
# then export the same TORCH_HOME in the cluster job environment
```

#### Out of memory during ColorVideoVDP

It processes blocks of frames on the GPU. Use `device: "cpu"` or reduce the resolution
under test. Note `frame_step` does **not** help here — ColorVideoVDP needs contiguous
frames for its temporal model and ignores that setting.

#### Scores are implausible or do not move with QP

Check the `ffmpeg_ref` / `ffmpeg_dist` fields in `qualityLogs/<cfg>/<clip>_perceptual.json`
to confirm the colour conversion. A BT.709 matrix applied to BT.2020 content produces
plausible-looking but wrong numbers.

### Debug Mode

For detailed logging, use DEBUG level:

```bash
python AV2CTCTest.py -f encode -p 0 -c av2 -m aom -l 5
```

### Checking Encoding Errors

Use the `CheckEncoding.py` utility. It requires a `-c` argument and supports the `RA` and
`AS` configurations only:

```bash
python CheckEncoding.py -c RA
python CheckEncoding.py -c AS
```

It re-decodes each bitstream listed in the command log, compares the decoded frame count
against the expected count, writes the failures to `{RA,AS}_decode_error.log`, and emits a
filtered command log (`AV2CTC_TestCmd_Update_{RA,AS}.log`) containing only the jobs that
need to be re-run.

> **Important**: `CheckEncoding.py` reads its paths from module-level constants at the top
> of the file (`root_path`, `decoder`, and the `*_cmd_log_file` names), **not** from
> `config.yaml`. Edit those constants to match your test run before using it.

---

## Additional Resources

- **Configuration Reference**: `src/config.yaml` (with inline comments)
- **AOM CTC Documentation**: [AOM CTC Specification](https://aomedia.org/)

---

## Contact

For questions or issues with this framework, contact:
- **Authors**: maggie.sun@intel.com, ryanlei@meta.com

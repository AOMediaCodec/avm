# AVM CTC Testing Framework - User Guide

This guide provides instructions for running AV2 Common Test Conditions (CTC) (https://aomedia.org/docs/CWG-F384o_AV2_CTC_v8.pdf) tests using the AVM CTC testing framework.

## Table of Contents

1. [Overview](#overview)
2. [Prerequisites](#prerequisites)
3. [Environment Setup](#environment-setup)
4. [Configuration](#configuration)
5. [Framework Structure](#framework-structure)
6. [Running Tests](#running-tests)
   - [Regular CTC Tests (AV2CTCTest.py)](#regular-ctc-tests-av2ctctestpy)
   - [Adaptive Streaming Tests (ConvexHullTest.py)](#adaptive-streaming-tests-convexhulltestpy)
7. [Distributed Cluster Execution (Launch.py)](#distributed-cluster-execution-launchpy)
8. [Analyzing Results (AV2CTCProgress.py)](#analyzing-results-av2ctcprogresspy)
9. [Output Structure](#output-structure)
10. [Common Workflows](#common-workflows)
11. [Troubleshooting](#troubleshooting)

---

## Overview

The AVM CTC Testing Framework is a Python-based test harness for evaluating video codec performance according to the Alliance for Open Media (AOM) Common Test Conditions. It supports:

- **Multiple test configurations**: LD (Low Delay), RA (Random Access), AI (All Intra), AS (Adaptive Streaming), STILL (Still Images)
- **Multiple codecs**: AV1, AV2, HEVC
- **Multiple encoders**: aomenc/avmenc, SVT-AV1, HM (HEVC)
- **Quality metrics**: PSNR, SSIM, MS-SSIM, VMAF, PSNR-HVS, CIEDE2000, CAMBI
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
  - `avmenc` / `aomenc` - AVM/AOM encoder ([libavm](https://gitlab.com/AOMediaCodec/avm))
  - `avmdec` / `aomdec` - AVM/AOM decoder
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

### VMAF Quality Metrics

AV2 CTC uses VMAF tool as the reference implementation for all quality metrics. Use the versioned flag for CTC compliance:

```bash
vmaf --avm_ctc v6.0 ...
```

This generates all quality metrics required for AV2 CTC.

### Timing Information

When `EnableTimingInfo` is enabled in configuration:
- **Linux/macOS**: Uses system `time` utility
- **Windows**: Uses `ptime` utility for timing capture

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
2. Create a virtual environment in `./venv`
3. Install `pip-tools` for dependency management
4. Generate pinned `requirements.txt` from `requirements.in` using `pip-compile`
5. Install all required dependencies with exact versions

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

# Install pip-tools for dependency management
pip install pip-tools

# Generate pinned requirements.txt from requirements.in
pip-compile requirements.in -o requirements.txt

# Install dependencies
pip install -r requirements.txt
```

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
| `requirements.txt` | **Generated file** - Auto-generated by `pip-compile`, contains exact pinned versions for reproducibility |

#### Updating Dependencies

To add or update dependencies:

1. **Edit `requirements.in`** to add/modify dependencies
2. **Regenerate `requirements.txt`**:
   ```bash
   pip-compile requirements.in -o requirements.txt
   ```
3. **Install updated dependencies**:
   ```bash
   pip install -r requirements.txt
   ```

#### Upgrading All Packages

To upgrade all packages to their latest compatible versions:

```bash
pip-compile --upgrade requirements.in -o requirements.txt
pip install -r requirements.txt
```

#### Generating Requirements with Hashes (Production)

For enhanced security with hash verification:

```bash
pip-compile --generate-hashes requirements.in -o requirements.txt
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

---

## Configuration

### Configuration File

The framework uses a YAML configuration file (`src/config.yaml`) for all user-configurable settings. Edit this file to customize your test environment.

### Key Configuration Sections

#### 1. CTC Version

The CTC version is a critical parameter that determines which test sequences, encoder parameters, and template files are used:

```yaml
# CTC version determines test sequences, encoder parameters, and template files.
# Valid versions: "2.0", "3.0", "4.0", "5.0", "6.0", "7.0", "8.0"
ctc_version: "8.0"
```

| CTC Version | Description |
|-------------|-------------|
| 8.0 | Current default - Latest AV2 CTC specification |
| 7.0 | Previous AV2 CTC version |
| 6.0 | AV2 CTC with updated VMAF parameters |
| 5.0 | AV2 CTC with expanded test set |
| 4.0 | Earlier AV2 CTC version |
| 3.0 | Legacy version |
| 2.0 | Legacy version |

> **Important**: The CTC version affects test clips, encoder command-line parameters, and Excel template files. Make sure to use the correct version for your testing requirements.

#### 2. Paths (User Must Configure)

These paths **must be configured** by the user before running tests:

```yaml
paths:
  # Root path relative to the src directory (or use absolute path)
  root: ".."

  # Path to video content for CTC testing (REQUIRED)
  # This should point to the directory containing AV2 CTC test sequences
  content: "/path/to/your/av2_ctc_sequences/"

  # Path to subjective test content (required only if enable_subjective_test is true)
  subjective_content: "/path/to/your/subjective_test_sequences/"
```

| Path | Description | Required |
|------|-------------|----------|
| `root` | Root directory for the framework. Default `".."` works for most setups. | Yes |
| `content` | Directory containing CTC test sequences (`.y4m` files) | Yes |
| `subjective_content` | Directory containing subjective test sequences | Only if `enable_subjective_test: true` |

> **Important**: The `content` path must point to a directory containing the AV2 CTC test sequences in `.y4m` format. Without valid test content, the framework cannot run any tests.

**Example configurations:**

```yaml
# Linux server setup
paths:
  root: ".."
  content: "/data/video/av2_ctc/"
  subjective_content: "/data/video/av2_subjective/"

# macOS local setup
paths:
  root: ".."
  content: "/Users/username/Videos/av2_ctc/"
  subjective_content: "/Users/username/Videos/av2_subjective/"

# Windows setup
paths:
  root: ".."
  content: "D:/VideoContent/av2_ctc/"
  subjective_content: "D:/VideoContent/av2_subjective/"
```

#### 3. Executables

```yaml
executables:
  aomenc: "avmenc-v13.0.0"    # Encoder binary name
  aomdec: "avmdec-v13.0.0"    # Decoder binary name
  ffmpeg: "ffmpeg"
  vmaf: "vmaf"
```

> **Note**: Executables should be placed in the `bin/` directory or provide absolute paths.

#### 4. Feature Flags

```yaml
features:
  enable_parallel_gop_encoding: true   # Enable parallel encoding for RA/AS
  enable_verification_test_config: true
  enable_timing_info: true
  enable_md5: true
```

#### 5. Test Parameters

```yaml
test:
  configurations: ["LD", "RA", "AI", "STILL"]  # Configs to run
  dataset: "CTC_TEST_SET"

encoding:
  gop_size: 65
```

#### 6. QP Values

The framework uses different QP values for each test configuration. These are defined in `config.yaml`:

**AV1/AV2 QP Values (CTC versions 2.0-8.0):**

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
│   ├── AV2CTCTest.py       # Main script for LD/RA/AI/STILL tests
│   ├── ConvexHullTest.py   # Main script for AS tests
│   ├── Launch.py           # Submit jobs to compute cluster
│   ├── Utils.py            # Utility functions
│   ├── VideoEncoder.py     # Encoder wrapper
│   ├── VideoDecoder.py     # Decoder wrapper
│   ├── VideoScaler.py      # Scaling functions
│   ├── EncDecUpscale.py    # Encode-decode-upscale pipeline
│   ├── CalculateQualityMetrics.py  # Quality metric calculation
│   ├── CheckEncoding.py    # Check encoding status
│   └── AV2CTCProgress.py   # BD-Rate progress analysis
├── test/                   # Test output directory (created automatically)
│   ├── AV2CTC_TestCmd.log  # Command log file (when LogCmdOnly=1)
│   ├── cmdLogs/            # Shell scripts for cluster execution
│   │   ├── AI/             # AI configuration job scripts
│   │   ├── LD/             # LD configuration job scripts
│   │   ├── RA/             # RA configuration job scripts
│   │   ├── AS/             # AS configuration job scripts
│   │   └── STILL/          # STILL configuration job scripts
│   ├── bitstreams/         # Encoded bitstreams
│   │   ├── RA/             # Organized by test configuration
│   │   ├── LD/
│   │   ├── AI/
│   │   ├── AS/
│   │   └── STILL/
│   ├── decodedYUVs/        # Decoded video files
│   ├── encLogs/            # Encoding logs
│   ├── decLogs/            # Decoding logs
│   ├── qualityLogs/        # Quality metric logs
│   └── ...
├── analysis/               # Analysis results
│   ├── rdresult/           # RD data files
│   └── summary/            # Summary reports
├── ctc_result/             # CTC progress analysis outputs
├── requirements.txt        # Python dependencies
├── setup_env.sh           # Environment setup script
└── USER_GUIDE.md          # This user guide
```

---

## Running Tests

### Regular CTC Tests (AV2CTCTest.py)

For LD, RA, AI, and STILL configurations.

#### Basic Usage

```bash
cd src

# When enable_parallel_gop_encoding is set to false (default), the following
# command will run all configurations defined in config.yaml

# Run encoding test (runs all configurations defined in config.yaml) and generate
# command log file (AV2CTC_TestCmd.log) to the test directory.
python AV2CTCTest.py -f encode -c av2 -m aom -p 0 --LogCmdOnly 1

# Launch encoding jobs to the compute cluster
python Launch.py AV2CTC_TestCmd.log

# Generate summary/quality results (after all encoding jobs are done)
python AV2CTCTest.py -f summary -c av2 -m aom -p 0

# For RA configuration, when enable_parallel_gop_encoding is set to true,
# The following commands should be run

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

### Adaptive Streaming CTC Tests (ConvexHullTest.py)

For AS configurations.

#### Basic Usage

```bash
cd src

# When enable_parallel_gop_encoding is set to false (default), the following
# command will run adaptive streaming configurations defined in config.yaml

# Run encoding test (runs all configurations defined in config.yaml) and generate
# command log file (AV2CTC_TestCmd.log) to the test directory.
python ConvexHullTest.py -f encode -c av2 -m aom -p 0 -t hdrtool --LogCmdOnly 1

# Launch encoding jobs to the compute cluster
python Launch.py AV2CTC_TestCmd.log

# Generate summary/quality results (after all encoding jobs are done)
python ConvexHullTest.py -f convexhull -c av2 -m aom -p 0 -t hdrtool

# For AS configuration, when enable_parallel_gop_encoding is set to true,
# The following commands should be run

# Generate the encoding command that encode each GOP (65 frames) of the
# input sequence into two separated OBU files in parallel.
python ConvexHullTest.py -f encode -c av2 -m aom -p 0 -t hdrtool --LogCmdOnly 1

# Launch encoding jobs to the compute cluster
python Launch.py AV2CTC_TestCmd.log

# Concatenate the two OBU files into a single OBU file after all encoding jobs are done.
python ConvexHullTest.py -f concatenate -c av2 -m aom -p 0 -t hdrtool

# Generate command that decode and calculate quality metrics.
python ConvexHullTest.py -f decode -c av2 -m aom -p 0 -t hdrtool --LogCmdOnly 1

# Launch decoding jobs to the compute cluster
python Launch.py AV2CTC_TestCmd.log

# Generate summary/quality results (after all decoding jobs are done)
python ConvexHullTest.py -f convexhull -c av2 -m aom -p 0
```


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
| `-p`, `--EncodePreset` | Encoder preset | `0` (slowest) to `13` (fastest) | None |
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

# Step 3: Run encoding
python AV2CTCTest.py -f encode -p 0 -c av2 -m aom -l 4

# Step 4: Generate summary/quality results
python AV2CTCTest.py -f summary -p 0 -c av2 -m aom -l 4
```

### Adaptive Streaming Tests (ConvexHullTest.py)

For AS (Adaptive Streaming) configuration with downscale/upscale pipeline.

#### Basic Usage

```bash
cd src

# Run convex hull test (encode + decode + upscale + quality) with hdrtool (default)
python ConvexHullTest.py -f encode -p 0 -c av2 -m aom -t hdrtool -l 4

# Run convex hull test with ffmpeg scaler
python ConvexHullTest.py -f encode -p 0 -c av2 -m aom -t ffmpeg -l 4

# Run convex hull test with AOM scaler
python ConvexHullTest.py -f encode -p 0 -c av2 -m aom -t aom -l 4
```

#### Parallel GOP Encoding Workflow

When `enable_parallel_gop_encoding: true` in config.yaml:

```bash
# Step 1: Run parallel encoding (encodes video in chunks)
python ConvexHullTest.py -f encode -p 0 -c av2 -m aom -t hdrtool -l 4

# Step 2: Concatenate encoded chunks
python ConvexHullTest.py -f concatnate -p 0 -c av2 -m aom -t hdrtool -l 4

# Step 3: Run decoding and quality calculation
python ConvexHullTest.py -f decode -p 0 -c av2 -m aom -t hdrtool -l 4

# Step 4: Generate summary
python ConvexHullTest.py -f convexhull -p 0 -c av2 -m aom -t hdrtool -l 4
```

#### Command-line Options

| Option | Description | Values | Default |
|--------|-------------|--------|---------|
| `-f`, `--function` | Function to run | `clean`, `scaling`, `encode`, `convexhull`, `concatnate`, `decode`, `summary` | Required |
| `-p`, `-preset` | Encoder preset | `0` to `13` | None |
| `-c`, `-codec` | Codec name | `av1`, `av2`, `hevc` | None |
| `-m`, `-method` | Encoder method | `aom`, `svt`, `hm` | None |
| `-t`, `--ScaleMethod` | Scaling tool for downscale/upscale | `hdrtool`, `ffmpeg`, `aom` | `hdrtool` |
| `-l`, `-LogLevel` | Logging level (0-5) | `0`:None, `1`:Critical, `2`:Error, `3`:Warning, `4`:Info, `5`:Debug | `3` (Warning) |
| `-CmdOnly`, `--LogCmdOnly` | Log commands to file without executing | `true`/`false` | `true` |
| `-k`, `--KeepUpscaleOutput` | Keep upscaled YUV files after clean | `true`/`false` | `false` |
| `-s`, `--SaveMemory` | Delete intermediate files to save disk space | `true`/`false` | `true` |

> **Note**: By default, `LogCmdOnly` is `true`, which means the script will generate encoding commands to a log file instead of executing them. This allows you to review the commands or run them on a distributed compute cluster. Set `-CmdOnly false` to execute commands locally.

### Scaling Tools

The `-t` parameter specifies which tool to use for video downscaling and upscaling in AS tests:

| Tool | Description |
|------|-------------|
| `hdrtool` | HDRTools (HDRConvert) - **Default**. High-quality scaling with HDR support. |
| `ffmpeg` | FFmpeg - Widely available, good quality scaling. |
| `aom` | AOM Lanczos scaler - Optimized for AV1/AV2 testing. |

> **Note**: Ensure the corresponding binary is available in the `bin/` directory or configured in `config.yaml`.

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
    # TODO: implement your cluster's job submission command here

    print("Submitting job: %s" % job_file_path)
```

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

**AS tests with downscaling:**
```
{ClipName}_{Resolution}_{Method}_{Codec}_AS_Preset_{Preset}_QP_{QP}.sh
```

---

## Analyzing Results (AV2CTCProgress.py)

The `AV2CTCProgress.py` script analyzes and compares CTC test results across multiple AVM releases. It calculates BD-Rate improvements, generates RD curves, and produces summary reports.

### Purpose

- **Track AVM Progress**: Compare codec efficiency across releases (v1.0.0 through v12.0.0)
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
        "v1.0.0",          # Release label
        "av2",             # Codec (av1/av2)
        "aom",             # Encoder (aom/svt/hm)
        "0",               # Preset
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v1.0.0-alt-anchor-r3.0"),  # Path
    ],
    "v02.0.0": [
        "v2.0.0",
        "av2",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v2.0.0"),
    ],
    # ... more releases
}
```

#### 2. Anchor Version

The anchor (baseline) for BD-Rate comparison:

```python
anchor = "v01.0.0"
```

#### 3. Output Paths

```python
CTC_RESULT_PATH = "../ctc_result"
rd_curve_pdf = os.path.join(CTC_RESULT_PATH, "rdcurve.pdf")
combined_rd_curve_pdf = os.path.join(CTC_RESULT_PATH, "combined_rdcurve.pdf")
bdrate_summary = os.path.join(CTC_RESULT_PATH, "Bdrate-Summary-AV1-vs-AV2.csv")
# ... more output paths
```

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
| `combined_rdcurve.pdf` | Combined RD curves (all videos in one plot per config) |
| `combined_runtime.pdf` | Encoding runtime comparisons |
| `AverageBdrateByTag-Summary-AV1-vs-AV2.pdf` | Bar charts of average BD-Rate by release |
| `AverageBdrateByTagClass-Summary-AV1-vs-AV2.pdf` | Bar charts by release and video class |
| `PerVideoBdrate-Summary-AV1-vs-AV2.pdf` | Bar charts of per-video BD-Rate |

#### Excel Files

The script fills CTC template Excel files with anchor and test data:

- `CTC_Regular_{anchor_release}-{test_release}.xlsm` - For AI, LD, RA, STILL configurations
- `CTC_AS_{anchor_release}-{test_release}.xlsm` - For AS configuration

### Customizing the Analysis

#### Adding a New Release

1. Add an entry to the `csv_paths` dictionary:

```python
csv_paths = {
    # ... existing entries ...
    "v12.0.0": [
        "v12.0.0",
        "av2",
        "aom",
        "0",
        os.path.join(CTC_RESULT_PATH, "AV2-CTC-v12.0.0"),
    ],
}
```

2. Add format specification for plots:

```python
formats = {
    # ... existing entries ...
    "v12.0.0": ["g", "-", "s"],  # [color, line_style, marker]
}
```

3. Add release date:

```python
dates = {
    # ... existing entries ...
    "v12.0.0": "10/27/2025",
}
```

#### Changing the Anchor

To use a different baseline for BD-Rate comparison:

```python
anchor = "v08.0.0"  # Use v8.0.0 as anchor instead of v1.0.0
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
# - Set the correct anchor
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

- The script processes all configurations (AI, LD, RA, AS, STILL) by default
- For AS (Adaptive Streaming), it calculates convex hull BD-Rate across multiple resolutions
- Runtime data is only analyzed for configurations other than RA (parallel encoding affects timing)
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
│   │   ├── Clip1_540p_aom_av2_AS_Preset_0_QP_110.obu
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

**Concatenated file:**
```
{ClipName}_{Method}_{Codec}_{Config}_Preset_{Preset}_QP_{QP}_start_0_frames_{TotalFrames}.obu
```

---

## Common Workflows

### Workflow 1: Full RA Test Run

```bash
cd src

# Activate environment
source ../venv/bin/activate

# Edit config.yaml to run only RA tests
# test:
#   configurations: ["RA"]

# Run complete RA test
python AV2CTCTest.py -f encode -p 0 -c av2 -m aom -l 4
python AV2CTCTest.py -f summary -p 0 -c av2 -m aom -l 4
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

### Workflow 3: Adaptive Streaming with Parallel Encoding

```bash
cd src

# Run parallel encoding with hdrtool scaler
python ConvexHullTest.py -f encode -p 0 -c av2 -m aom -t hdrtool -l 4

# Concatenate chunks
python ConvexHullTest.py -f concatnate -p 0 -c av2 -m aom -t hdrtool -l 4

# Decode and calculate quality
python ConvexHullTest.py -f decode -p 0 -c av2 -m aom -t hdrtool -l 4

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

### Debug Mode

For detailed logging, use DEBUG level:

```bash
python AV2CTCTest.py -f encode -p 0 -c av2 -m aom -l 5
```

### Checking Encoding Errors

Use the `CheckEncoding.py` utility:

```bash
python CheckEncoding.py
```

This checks decode logs for errors and identifies failed encodings.

---

## Additional Resources

- **Configuration Reference**: `src/config.yaml` (with inline comments)
- **AOM CTC Documentation**: [AOM CTC Specification](https://aomedia.org/)

---

## Contact

For questions or issues with this framework, contact:
- **Authors**: maggie.sun@intel.com, ryanlei@meta.com

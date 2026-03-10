# CLAUDE.md — AVM (AV2 Video Codec)

AVM is the reference implementation of the AV2 video codec, developed by the Alliance for Open Media. It provides an encoder/decoder library (libavm) and CLI tools (avmenc/avmdec).

## Build

Out-of-tree CMake build is required (CMake >= 3.16):

```bash
mkdir -p avm_build && cd avm_build
cmake /path/to/avm
make -j$(nproc)
```

Common CMake options:

```bash
# Debug build
cmake /path/to/avm -DCMAKE_BUILD_TYPE=Debug

# Sanitizer build (address, undefined, memory, thread, cfi)
cmake /path/to/avm -DSANITIZE=address

# Encoder-only or decoder-only
cmake /path/to/avm -DCONFIG_AV2_DECODER=0
cmake /path/to/avm -DCONFIG_AV2_ENCODER=0

# Disable tests / examples
cmake /path/to/avm -DENABLE_TESTS=OFF -DENABLE_EXAMPLES=OFF
```

## Testing

```bash
# Download test data (required before first test run)
make testdata

# Run all tests
make runtests

# Run a single test by filter
./test_libavm --gtest_filter="*EncodeDecode*"

# Shard tests across N parallel workers
GTEST_TOTAL_SHARDS=4 GTEST_SHARD_INDEX=0 ./test_libavm
```

## Code Formatting

- C/C++: Google C++ Style, enforced by clang-format 18 (`.clang-format` at repo root, 80-col limit, 2-space indent)
- CMake files: cmake-format
- Format changed files: `./avm-fix-style.sh <branch> [target-branch]`

## Repository Structure

```
avm/                  Public C API (avm_codec.h, avm_encoder.h, avm_decoder.h)
av2/
  common/             Shared encoder/decoder code (block structures, transforms, loop filters, entropy)
  encoder/            Encoder (RDO, motion estimation, rate control, bitstream writing)
  decoder/            Decoder (OBU parsing, frame reconstruction)
avm_dsp/              DSP kernels with SIMD variants (x86/, arm/) and RTCD dispatch
avm_mem/              Memory allocation
avm_ports/            Platform abstraction
avm_scale/            Image scaling/resampling
avm_util/             Threading, debug utilities
apps/                 CLI tools: avmenc.c, avmdec.c
examples/             Example programs (simple_encoder.c, simple_decoder.c, etc.)
test/                 gtest-based unit and integration tests
build/cmake/          Build system, toolchain files, config defaults
third_party/          Bundled dependencies (googletest, libyuv, etc.)
tools/                Analysis and utility tools
```

## Key Data Structures

| Struct | Header | Role |
|--------|--------|------|
| `AV2_COMMON` | `av2/common/av2_common_int.h` | Frame-level codec state shared by encoder and decoder |
| `AV2_COMP` | `av2/encoder/encoder.h` | Top-level encoder context |
| `AV2Decoder` | `av2/decoder/decoder.h` | Top-level decoder context |
| `MACROBLOCK` | `av2/encoder/block.h` | Per-block encoder working data |
| `MACROBLOCKD` | `av2/common/blockd.h` | Per-block decoder/reconstruction data |
| `MB_MODE_INFO` | `av2/common/blockd.h` | Coding mode info per block |

## Configuration System

Codec features are controlled by `CONFIG_*` variables and build options by `ENABLE_*` variables, both defined in `build/cmake/avm_config_defaults.cmake`. Set them on the CMake command line:

```bash
cmake /path/to/avm -DCONFIG_MULTITHREAD=0 -DENABLE_NASM=ON
```

Key CONFIG flags: `CONFIG_AV2_ENCODER`, `CONFIG_AV2_DECODER`, `CONFIG_MULTITHREAD`, `CONFIG_RUNTIME_CPU_DETECT`, `CONFIG_DENOISE`, `CONFIG_INSPECTION`, `CONFIG_ACCOUNTING`.

Key ENABLE flags: `ENABLE_TESTS`, `ENABLE_EXAMPLES`, `ENABLE_TOOLS`, `ENABLE_CCACHE`, `ENABLE_NASM`.

Experiment flags (`CONFIG_*` with value 0 or 1) gate in-development codec features and are managed alongside their dependencies in `build/cmake/avm_experiment_deps.cmake`.

## SIMD / RTCD

Performance-critical functions have architecture-specific implementations selected at runtime via RTCD (Runtime CPU Detection):

1. Function prototypes and dispatch tables are defined in Perl files: `avm_dsp/avm_dsp_rtcd_defs.pl`, `av2/common/av2_rtcd_defs.pl`
2. `build/cmake/rtcd.pl` generates C dispatch code during the build
3. SIMD intrinsics live in `avm_dsp/x86/` (SSE2/SSSE3/SSE4.1/AVX2) and `avm_dsp/arm/` (NEON)
4. C reference implementations are alongside the RTCD definitions in `avm_dsp/`

To add a SIMD variant: add the function signature to the relevant `*_rtcd_defs.pl`, implement the intrinsics file under the arch subdirectory, and register it in the corresponding `.cmake` file.

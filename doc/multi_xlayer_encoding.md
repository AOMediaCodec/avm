# Multi-XLayer Encoding Guide

This document describes how to use AVM's multi-xlayer encoding framework
to encode multiple extended layers (xlayers) into a single combined
bitstream. Xlayers enable use cases such as texture+depth, stereo video,
subpicture tiling, and spatially scalable encoding with embedded layers.

## Table of Contents

- [Quick Start](#quick-start)
- [CLI Usage](#cli-usage)
- [JSON Configuration Reference](#json-configuration-reference)
  - [Top-Level Fields](#top-level-fields)
  - [XLayer Entry Fields](#xlayer-entry-fields)
  - [Embedded Layers (MLayers)](#embedded-layers-mlayers)
  - [Per-Embedded-Layer Configuration](#per-embedded-layer-configuration)
  - [Global LCR](#global-lcr)
  - [Local LCR](#local-lcr)
  - [OPS (Operating Point Set)](#ops-operating-point-set)
  - [Atlas](#atlas)
  - [Input Sources](#input-sources)
  - [Codec Controls](#codec-controls)
  - [GOP Modes](#gop-modes)
- [Use Cases and Examples](#use-cases-and-examples)
  - [Texture + Depth](#texture--depth)
  - [Stereo Video](#stereo-video)
  - [Subpicture Tiling](#subpicture-tiling)
  - [Subpicture with Auxiliary Layers](#subpicture-with-auxiliary-layers)
  - [Spatial Scalability with Embedded Layers](#spatial-scalability-with-embedded-layers)
  - [Mixed Embedded Layer Counts](#mixed-embedded-layer-counts)
  - [Stereo via Embedded Layers](#stereo-via-embedded-layers)
  - [Subpicture Tiling via Embedded Layers](#subpicture-tiling-via-embedded-layers)
  - [Texture + Depth via Embedded Layers with XLayers](#texture--depth-via-embedded-layers-with-xlayers)
- [GOP Mode and Output Order](#gop-mode-and-output-order)
  - [Compatibility Matrix](#compatibility-matrix)
  - [Closed GOP, Non-Monotonic (Multi-XLayer + Multi-MLayer)](#closed-gop-non-monotonic-multi-xlayer--multi-mlayer)
  - [Closed GOP, Monotonic (Multi-XLayer + Multi-MLayer)](#closed-gop-monotonic-multi-xlayer--multi-mlayer)
  - [Open Leading, Non-Monotonic (Multi-XLayer + Multi-MLayer)](#open-leading-non-monotonic-multi-xlayer--multi-mlayer)
  - [Open SEF, Monotonic (Multi-XLayer + Multi-MLayer)](#open-sef-monotonic-multi-xlayer--multi-mlayer)
- [Decoding](#decoding)
- [Stream Demuxing](#stream-demuxing)
- [Constraints and Validation](#constraints-and-validation)

---

## Quick Start

1. Create a JSON configuration file describing your xlayers.
2. Encode with `avmenc --xlayer-config config.json`.
3. Decode with `avmdec --all-layers` to get all layers.

Minimal two-layer example:

```json
{
  "xlayers": [
    { "xlayer_id": 0, "input": "texture.y4m", "width": 1920, "height": 1080,
      "qp": 128, "cpu_used": 5 },
    { "xlayer_id": 1, "input": "depth.y4m", "width": 1920, "height": 1080,
      "layer_type": "auxiliary", "auxiliary_type": "depth",
      "qp": 160, "cpu_used": 5 }
  ],
  "output": "combined.obu"
}
```

```bash
avmenc --xlayer-config two_layer.json --limit=30
avmdec --all-layers -o decoded.y4m combined.obu
```

---

## CLI Usage

### Encoder

Multi-xlayer encoding is triggered by passing `--xlayer-config`:

```bash
avmenc --xlayer-config <path-to-json> [--limit=N] [--framerate=N/D]
```

When `--xlayer-config` is provided, the encoder ignores the normal
single-stream arguments (input file, `--width`, `--height`, etc.) and
reads all configuration from the JSON file. Standard arguments that are
still honored:

| Argument | Effect |
|----------|--------|
| `--limit=N` | Encode at most N source frames |
| `--framerate=N/D` | Override timebase for all xlayers |

### Decoder

```bash
avmdec --all-layers -o output.y4m input.obu
avmdec --all-layers --num-streams=N -o output_%d.y4m input.obu
avmdec --all-layers --atlas-composite --xlayer-config config.json -o composite.y4m input.obu
```

| Flag | Purpose |
|------|---------|
| `--all-layers` | Output all decoded frames (all xlayers, all mlayers) |
| `--num-streams=N` | Split output into N separate files (`output_0.y4m`, `output_1.y4m`, ...) |
| `--xlayer-config` | Provide atlas layout for `--atlas-composite` |
| `--atlas-composite` | Composite decoded xlayers onto an atlas canvas using the layout from the config |

---

## JSON Configuration Reference

### Top-Level Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `xlayers` | array | *required* | Array of xlayer entries (1-31) |
| `inputs` | array | `[]` | Named input sources (see [Input Sources](#input-sources)) |
| `source` | object | | Legacy single shared source (converted to `inputs[0]` internally) |
| `output` | string | `""` | Output bitstream path |
| `combined_tu` | bool | `true` | Combine all xlayer OBUs into shared TUs |
| `monotonic_output_order` | bool | `false` | Encoder outputs frames in monotonic order |
| `frame_rate` | number | `0` | Frame rate for aggregate level derivation (0 = use encoder timebase) |

### XLayer Entry Fields

Each entry in the `xlayers` array configures one extended layer:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `xlayer_id` | int | *required* | Unique ID, 0-30 |
| `input` | string | `""` | Input file path (Y4M or raw YUV). Not needed if using `input_source` or single `inputs` entry. |
| `input_source` | string | `""` | Reference to a named input source from `inputs` array. |
| `width` | int | 0 | Frame width (required for raw YUV or shared source) |
| `height` | int | 0 | Frame height |
| `profile` | int | 3 | AV2 profile (0-3 = Main 4:2:0 10-bit variants, 4 = Main 4:2:2 10-bit, 5 = Main 4:4:4 10-bit) |
| `tier` | int | 0 | Tier |
| `level` | int | 16 | Level index (e.g. 16 = Level 4.0) |
| `layer_type` | string | `"texture"` | `"texture"`, `"auxiliary"`, `"stereo"`, or `"dependent"` |
| `auxiliary_type` | string | `"alpha"` | Only when `layer_type` is `"auxiliary"`: `"alpha"`, `"depth"`, `"segmentation"`, `"gain_map"` |
| `view_type` | string | `"unspecified"` | `"unspecified"`, `"center"`, `"left"`, `"right"`, `"explicit"` |
| `qp` | int | -1 | Fixed QP (0-255). -1 = use global default. |
| `bitrate` | int | -1 | Target bitrate in kbps. -1 = use QP mode. |
| `cpu_used` | int | -1 | Encoder speed preset (0=slowest, 9=fastest). -1 = default (5). |
| `lag_in_frames` | int | -1 | Lookahead buffer size. -1 = default. |
| `kf_max_dist` | int | -1 | Maximum keyframe interval. -1 = default. |
| `subgop_config` | string | `""` | Path to sub-GOP JSON config file |
| `gop_mode` | string | `"closed"` | `"closed"`, `"open_leading"`, or `"open_sef"` |
| `fwd_kf_enabled` | int | -1 | Forward keyframe override. -1 = derive from `gop_mode`. |
| `enable_keyframe_filtering` | int | -1 | KF filtering override. -1 = derive. |
| `add_sef_for_hidden_frames` | int | -1 | SEF for hidden frames override. -1 = derive. |
| `num_temporal_layers` | int | 1 | Number of temporal layers (1-8) |
| `num_embedded_layers` | int | 1 | Number of spatial embedded layers (1-8) |
| `scaling_mode` | array | auto | Scaling mode per embedded layer (see [Embedded Layers](#embedded-layers-mlayers)) |
| `embedded_layers` | array | | Per-embedded-layer configuration (see [Per-Embedded-Layer Configuration](#per-embedded-layer-configuration)) |
| `color_primaries` | int | -1 | Color primaries. -1 = not signaled. |
| `transfer_characteristics` | int | -1 | Transfer characteristics. -1 = not signaled. |
| `matrix_coefficients` | int | -1 | Matrix coefficients. -1 = not signaled. |
| `full_range_flag` | int | -1 | Full range flag. -1 = not signaled. |
| `atlas_pos_x` | int | -1 | X position in atlas canvas (required for shared source mode) |
| `atlas_pos_y` | int | -1 | Y position in atlas canvas |
| `codec_controls` | array | `[]` | Generic codec controls applied after encoder init (see [Codec Controls](#codec-controls)) |

### Embedded Layers (MLayers)

Each xlayer can independently encode multiple spatial embedded layers
(mlayers). The encoder is called once per mlayer for each source frame,
with the appropriate scaling mode and mlayer ID set before each call.
The encoder internally rescales the source image.

**Configuration:**

```json
{
  "xlayer_id": 0,
  "num_embedded_layers": 3,
  "scaling_mode": ["1/4", "1/2", "1:1"]
}
```

The `scaling_mode` array specifies the spatial scale for each embedded
layer, from smallest to largest. The last entry must always be `"1:1"`
(full resolution).

**Scaling mode values:**

| String | Integer | Scale Factor |
|--------|---------|-------------|
| `"1:1"` or `"normal"` | 0 | Full resolution |
| `"4/5"` | 1 | 4/5 scale |
| `"3/5"` | 2 | 3/5 scale |
| `"3/4"` | 3 | 3/4 scale |
| `"1/4"` | 4 | 1/4 scale |
| `"1/8"` | 5 | 1/8 scale |
| `"1/2"` | 6 | 1/2 scale |

Both string and integer values are accepted in JSON.

**Default derivation:** When `num_embedded_layers > 1` and `scaling_mode`
is omitted, defaults are derived automatically:

| Layers | Default `scaling_mode` |
|--------|----------------------|
| 2 | `["1/2", "1:1"]` |
| 3 | `["1/4", "1/2", "1:1"]` |

**LCR signaling:** For each non-full-resolution mlayer, the LCR OBU
signals `lcr_same_sh_max_resolution_flag = 0` with
`lcr_max_expected_width/height` set to the xlayer's full resolution
(not the scaled size). This is because the encoder may produce
full-resolution frames (e.g., on keyframes), so the LCR must signal
the maximum possible dimensions. Full-resolution mlayers signal
`lcr_same_sh_max_resolution_flag = 1`.

#### Per-Embedded-Layer Configuration

When different embedded layers need genuinely different input content
(e.g., stereo views, subpicture tiles, overlay+base), use the
`"embedded_layers"` array to configure each mlayer independently.

```json
{
  "xlayer_id": 0,
  "num_embedded_layers": 2,
  "embedded_layers": [
    { "scaling_mode": "1/2", "input_source": "left",
      "atlas_pos_x": 0, "atlas_pos_y": 0, "width": 1920, "height": 1080,
      "depends_on": [] },
    { "scaling_mode": "1:1", "input_source": "right",
      "atlas_pos_x": 0, "atlas_pos_y": 0, "width": 1920, "height": 1080,
      "depends_on": [0] }
  ]
}
```

Each entry in `"embedded_layers"` corresponds to one mlayer (in order).
All fields are optional — omitted fields inherit from the parent xlayer.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `scaling_mode` | string/int | inherit | Encoder-internal scaling for this mlayer |
| `input_source` | string | inherit | Named input source for this mlayer |
| `atlas_pos_x` | int | inherit | Crop origin X within source |
| `atlas_pos_y` | int | inherit | Crop origin Y within source |
| `width` | int | inherit | Crop width |
| `height` | int | inherit | Crop height |
| `depends_on` | array of int | default linear | Which lower mlayer indices this depends on for inter-layer prediction |
| `color_primaries` | int | inherit | CICP color primaries (0-255), inherits from xlayer if omitted |
| `transfer_characteristics` | int | inherit | CICP transfer characteristics (0-255), inherits from xlayer if omitted |
| `matrix_coefficients` | int | inherit | CICP matrix coefficients (0-255), inherits from xlayer if omitted |
| `full_range_flag` | int | inherit | 0=limited range, 1=full range, inherits from xlayer if omitted |

**`depends_on` semantics:**
- Absent: default linear chain (each mlayer depends on all lower mlayers)
- `[]`: independent (no inter-layer prediction)
- `[0]`: depends only on mlayer 0
- `[0, 1]`: depends on mlayers 0 and 1

**Mutual exclusion:** `"embedded_layers"` and the flat `"scaling_mode"`
array cannot both be present on the same xlayer entry. Use one or the
other.

**Backward compatibility:** When `"embedded_layers"` is absent, all
behavior is unchanged — the existing flat `"scaling_mode"` array and
default scaling derivation work as before.

#### Content Interpretation (CI) Per MLayer

Each embedded layer can have its own Content Interpretation (CI) OBU with
distinct CICP color properties. This is useful when different mlayers carry
content with different color characteristics (e.g., HDR base layer with
SDR enhancement, or depth with different matrix coefficients).

**Inheritance rules:**
1. If an mlayer omits a CI field (or sets it to `-1`), it inherits
   from the parent xlayer's value.
2. At the bitstream level, if an mlayer's resolved CI is identical to its
   first dependent layer's CI (via `depends_on`), the CI OBU is omitted —
   the decoder inherits automatically.
3. CI is written at every random access point (CLK) for each mlayer that
   has distinct CI.
4. CI must not change within a coded video sequence (CVS).

**Example:** Stereo with different color primaries per view:
```json
{
  "xlayer_id": 0,
  "color_primaries": 1,
  "transfer_characteristics": 1,
  "matrix_coefficients": 1,
  "num_embedded_layers": 2,
  "embedded_layers": [
    { "scaling_mode": "1/2", "color_primaries": 9,
      "transfer_characteristics": 16, "matrix_coefficients": 9 },
    { "scaling_mode": "1:1" }
  ]
}
```

In this example, mlayer 0 uses BT.2020/PQ (CICP 9/16/9) while mlayer 1
inherits BT.709 (CICP 1/1/1) from the xlayer. Each gets its own CI OBU
in the bitstream with the correct `obu_layer` value.

### Global LCR

The Global Layer Configuration Record describes the overall multi-layer
structure in the bitstream.

```json
"global_lcr": {
  "enable": true,
  "purpose_id": 0,
  "dependent_xlayers": false,
  "doh_constraint": true
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enable` | bool | `true` | Write a Global LCR OBU |
| `purpose_id` | int | 0 | LCR purpose (0=unspecified, 6=multiview, etc.) |
| `dependent_xlayers` | bool | `false` | Signal dependent xlayers |
| `doh_constraint` | bool | `true` | Decode order hint constraint |

### Local LCR

Local LCR OBUs provide per-xlayer layer configuration.

```json
"local_lcr": {
  "enable": true,
  "mode": "both"
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enable` | bool | `false` | Write Local LCR OBUs |
| `mode` | string | `"both"` | `"both"` = Global + Local with identical xlayer_info; `"local_only"` = Global without payload, Local is authoritative |

### OPS (Operating Point Set)

Operating points define subsets of the bitstream that can be
independently decoded. Each operating point specifies which xlayers
(and optionally how many mlayers per xlayer) are included.

```json
"ops": [
  {
    "ops_id": 0,
    "priority": 0,
    "intent_present": true,
    "ptl_present": true,
    "operating_points": [
      { "intent": 0, "xlayer_map": [0], "mlayer_count": [1] },
      { "intent": 1, "xlayer_map": [0], "mlayer_count": [3] },
      { "intent": 2, "xlayer_map": [0, 1], "mlayer_count": [3, 1] }
    ]
  }
]
```

| Field | Type | Description |
|-------|------|-------------|
| `ops_id` | int | OPS identifier (0-15) |
| `priority` | int | OPS priority |
| `intent_present` | bool | Signal intent per operating point |
| `ptl_present` | bool | Signal profile/tier/level per operating point |
| `color_info_present` | bool | Signal color info per operating point |
| `mlayer_info_idc` | int | Mlayer info mode (0=none, 1=same, 2=explicit) |
| `operating_points` | array | Array of operating point definitions |

Each operating point entry:

| Field | Type | Description |
|-------|------|-------------|
| `intent` | int | Display intent |
| `xlayer_map` | array | List of xlayer IDs included in this OP |
| `mlayer_count` | array | Number of embedded layers per xlayer in this OP (0=all) |

### Atlas

Atlas signaling describes how xlayers are spatially composed into a
single canvas.

```json
"atlas": {
  "enable": true,
  "mode": 0,
  "width": 1920,
  "height": 1080,
  "uniform_spacing": false
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enable` | bool | `false` | Write Atlas OBU |
| `mode` | int | 0 | Atlas mode |
| `width` | int | 0 | Canvas width (0 = derive from xlayers) |
| `height` | int | 0 | Canvas height (0 = derive) |
| `uniform_spacing` | bool | `true` | Auto-grid (`true`) or explicit positions (`false`) |

When `uniform_spacing` is `false`, each xlayer must specify `atlas_pos_x`
and `atlas_pos_y`.

### Input Sources

Define multiple named input sequences. Each xlayer references one by
name and specifies crop coordinates within that input. The same input
can feed multiple xlayers with different crop regions.

```json
"inputs": [
  { "name": "texture", "filename": "video.yuv", "width": 1920, "height": 1080 },
  { "name": "alpha", "filename": "alpha.yuv", "width": 1920, "height": 1080,
    "format": "yuv420", "bit_depth": 8 }
],
"xlayers": [
  { "xlayer_id": 0, "input_source": "texture", "width": 960, "height": 540,
    "atlas_pos_x": 0, "atlas_pos_y": 0, ... },
  { "xlayer_id": 1, "input_source": "texture", "width": 960, "height": 540,
    "atlas_pos_x": 960, "atlas_pos_y": 0, ... },
  { "xlayer_id": 2, "input_source": "alpha", "width": 960, "height": 540,
    "atlas_pos_x": 0, "atlas_pos_y": 0, "layer_type": "auxiliary",
    "auxiliary_type": "alpha", ... }
]
```

Each input source entry:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `name` | string | *required* | Unique name to reference from xlayers |
| `filename` | string | *required* | Input file path (Y4M or raw YUV) |
| `width` | int | 0 | Frame width (required for raw YUV, 0 = auto-detect from Y4M) |
| `height` | int | 0 | Frame height |
| `format` | string | `""` | Chroma format: `"yuv420"`, `"yuv422"`, `"yuv444"` (auto-detected for Y4M) |
| `bit_depth` | int | 0 | Input bit depth (0 = auto-detect from Y4M, or default 8 for raw) |
| `frame_rate` | number or string | 0 | Frame rate as a number (e.g. `30`, `29.97`) or rational string (e.g. `"30000/1001"`). 0 = auto-detect from Y4M or use global timebase. |

**Input resolution priority** (per xlayer, in order):
1. `"input"` — xlayer reads its own file, no shared source
2. `"input_source"` — references a named input from `"inputs"`, uses
   `atlas_pos_x/y` as crop origin within that input
3. If neither is set and `"inputs"` has exactly 1 entry, all xlayers
   use that input
4. If neither is set and `"inputs"` has multiple entries, validation error

When an xlayer uses an input source:
- `atlas_pos_x` / `atlas_pos_y` are required (used as the crop origin)
- `width` / `height` are required (used as the crop size)
- All xlayers sharing the same input source must use the same chroma
  format (profile)

**Mixed mode:** Some xlayers can use `input_source` (shared sources)
while others use `input` (own files) in the same config.

**Backward compatibility:** The old single `"source"` object is
internally converted to `"inputs"` with a single entry named
`"default"`:

```json
{ "source": { "filename": "v.yuv", "width": 1920, "height": 1080 } }
```

is equivalent to:

```json
{ "inputs": [{ "name": "default", "filename": "v.yuv", "width": 1920, "height": 1080 }] }
```

`"inputs"` and `"source"` cannot both be present.

### Codec Controls

Generic codec controls allow per-xlayer override of encoder settings
that are normally only accessible via CLI flags. Controls are applied
after encoder initialization via `avm_codec_control()`.

```json
{
  "xlayer_id": 0,
  "codec_controls": [
    ["enable_deblocking", 0],
    ["enable_cdef", 0],
    ["enable_restoration", 0],
    ["enable_tpl_model", 0],
    ["enable_keyframe_filtering", 0],
    ["enable_global_motion", 0],
    ["enable_warped_motion", 0]
  ]
}
```

Each entry is a `[name, value]` pair where `name` is a string matching
the codec control name and `value` is an integer. Supported control
names map directly to the `AV2E_SET_*` codec control IDs:

| Control Name | CLI Equivalent | Description |
|-------------|----------------|-------------|
| `enable_deblocking` | `--enable-deblocking` | Deblocking filter |
| `enable_cdef` | `--enable-cdef` | CDEF filter |
| `enable_restoration` | `--enable-restoration` | Loop restoration |
| `enable_tpl_model` | `--enable-tpl-model` | Temporal dependency model |
| `enable_keyframe_filtering` | `--enable-keyframe-filtering` | Keyframe filtering |
| `enable_global_motion` | `--enable-global-motion` | Global motion estimation |
| `enable_warped_motion` | `--enable-warped-motion` | Warped motion compensation |
| `enable_intrabc` | `--enable-intrabc` | Intra block copy |
| `enable_palette` | `--enable-palette` | Palette mode |
| `enable_interintra_comp` | `--enable-interintra-comp` | Inter-intra compound |

This is particularly useful for creating fast debug configurations
that disable expensive coding tools. See the `*_fast.json` configs
in `cfg/xlayer/` for examples.

### GOP Modes

The `gop_mode` field controls the Group of Pictures structure, which
determines how keyframes and reference frames are managed across GOP
boundaries. Three modes are available:

#### `"closed"` (default)

Closed GOP: each GOP begins with a Closed Loop Key (CLK) frame that
resets all reference buffers. No inter-prediction is possible across
GOP boundaries. Works with both monotonic and non-monotonic output.

```json
{ "gop_mode": "closed" }
```

Derived settings: `fwd_kf_enabled = 0`, `enable_keyframe_filtering = 0`,
`add_sef_for_hidden_frames = 0`.

#### `"open_leading"` (non-monotonic only)

Open GOP with Open Loop Key (OLK) and leading pictures. The forward
keyframe is coded as a KEY_FRAME (OLK OBU) at the GOP boundary. An OLK
can be either **displayed** (implicit output — the decoder reorders it
to the correct display position) or **hidden** (followed by an overlay
or SEF in the same temporal unit). By default in this mode, the OLK is
displayed; setting `enable_keyframe_filtering` to 2 makes it hidden
with a filtered overlay. Frames before the OLK in display order but
after it in coding order are "leading pictures" (LEADING_TILE_GROUP
OBUs).

This mode requires `lag_in_frames > 0` (for the lookahead needed to
code the forward keyframe) and is incompatible with
`monotonic_output_order: true` (leading OBUs require non-monotonic output).

```json
{ "gop_mode": "open_leading", "lag_in_frames": 19, "kf_max_dist": 9 }
```

**Important constraints:**
- OLK OBUs cannot be in the same temporal unit as leading OBUs. TUs
  with leading OBUs contain only leading VCL OBUs.
- The OLK designation is at the **temporal unit level**, not the frame
  level. Higher embedded layers in an OLK TU can be inter OBUs.
- `enable_keyframe_filtering` is independent of GOP mode and defaults
  to 0. When set to 2, the OLK is hidden and a filtered overlay frame
  is produced in the same TU. When 0, the OLK is displayed directly.

Derived settings: `fwd_kf_enabled = 1`, `enable_keyframe_filtering = 0`,
`add_sef_for_hidden_frames = 0`.

#### `"open_sef"` (monotonic compatible)

Open GOP with hidden intra frame and SEF output. When combined with
`monotonic_output_order: true`, the forward keyframe is coded as a
hidden INTRA_ONLY_FRAME instead of KEY_FRAME. This preserves reference
buffers across the GOP boundary (no reset), enabling inter-prediction
from frames before the boundary. The hidden intra frame is later shown
via the Show Existing Frame (SEF) mechanism.

This mode requires `lag_in_frames > 0` for the lookahead. When
monotonic output is enabled, the `intra_only_fwd_kf` control is
automatically set.

```json
{
  "gop_mode": "open_sef",
  "lag_in_frames": 19,
  "kf_max_dist": 9
}
```

Derived settings: `fwd_kf_enabled = 1`, `enable_keyframe_filtering = 0`,
`add_sef_for_hidden_frames = 1`. When `monotonic_output_order` is also
`true`: `intra_only_fwd_kf = 1`.

**INTRA_ONLY_FRAME vs KEY_FRAME:** An INTRA_ONLY_FRAME is intra-coded
(no inter-prediction within the frame) but does NOT reset reference
buffers, frame number, or reference frame mappings. Subsequent frames
can still reference frames from before the GOP boundary. A KEY_FRAME,
in contrast, resets all reference state, creating a clean random access
point.

#### Multi-Mlayer Keyframe Management

For xlayers with multiple embedded layers (mlayers):

- **With `lag_in_frames = 0`:** The encoder-internal keyframe placement
  is disabled (`kf_mode = AVM_KF_DISABLED`) because the encoder's
  keyframe counter advances per encode call, not per temporal unit. The
  xlayer encode loop manages keyframes externally via `AVM_EFLAG_FORCE_KF`
  on independent mlayers (those with `depends_on: []` or `depends_on`
  absent and `mlayer_id == 0`).

- **With `lag_in_frames > 0`:** The encoder uses `multi_layers_lag_test`
  which fixes the per-encode-call keyframe counter and enables internal
  forward keyframe support for multi-mlayer encoding. This is required
  for `gop_mode: "open_leading"` and `gop_mode: "open_sef"` with
  multiple embedded layers.

**Multi-rate encoding:** Input sources can have different frame rates.
The encoder uses the highest frame rate as the master rate and encodes
at that cadence. Lower-rate sources must have frame rates that are exact
integer divisors of the master rate. On temporal units where a source is
not active, its xlayers are skipped.

Frame rates are stored internally as rational numbers (`num/den`) to
avoid floating-point precision issues. The JSON accepts both numeric
values (e.g. `30`, `29.97`) and rational strings (e.g. `"30000/1001"`).
Common conversions:

| JSON value | Internal `num/den` |
|------------|-------------------|
| `60` | 60/1 |
| `30` | 30/1 |
| `29.97` | 30000/1001 |
| `23.976` | 24000/1001 |
| `"30000/1001"` | 30000/1001 |

Example with 60 fps texture and 15 fps depth (depth encodes every 4th TU):

```json
{
  "inputs": [
    { "name": "texture", "filename": "video.yuv", "width": 1920, "height": 1080,
      "frame_rate": 60 },
    { "name": "depth", "filename": "depth.yuv", "width": 1920, "height": 1080,
      "frame_rate": 15 }
  ],
  "xlayers": [
    { "xlayer_id": 0, "input_source": "texture", "width": 1920, "height": 1080,
      "atlas_pos_x": 0, "atlas_pos_y": 0, "qp": 128, "cpu_used": 5 },
    { "xlayer_id": 1, "input_source": "depth", "width": 1920, "height": 1080,
      "atlas_pos_x": 0, "atlas_pos_y": 0,
      "layer_type": "auxiliary", "auxiliary_type": "depth",
      "qp": 160, "cpu_used": 5 }
  ],
  "output": "multi_rate.obu"
}
```

---

### Texture + Depth

Encode a texture layer and a depth map as two independent xlayers:

```json
{
  "xlayers": [
    { "xlayer_id": 0, "input": "texture.y4m", "width": 1920, "height": 1080,
      "layer_type": "texture", "qp": 128, "cpu_used": 5 },
    { "xlayer_id": 1, "input": "depth.y4m", "width": 1920, "height": 1080,
      "layer_type": "auxiliary", "auxiliary_type": "depth",
      "qp": 160, "cpu_used": 5 }
  ],
  "global_lcr": { "enable": true, "purpose_id": 0, "doh_constraint": true },
  "ops": [{
    "ops_id": 0, "priority": 0, "intent_present": true, "ptl_present": true,
    "operating_points": [
      { "intent": 0, "xlayer_map": [0] },
      { "intent": 1, "xlayer_map": [0, 1] }
    ]
  }],
  "output": "texture_depth.obu"
}
```

See: `cfg/xlayer/texture_depth_2layer.json`

### Stereo Video

Encode left and right views as separate xlayers (simulcast). Each
view is encoded independently — there is no inter-layer prediction
between views. For stereo with inter-layer prediction, see
[Stereo via Embedded Layers](#stereo-via-embedded-layers).

```json
{
  "xlayers": [
    { "xlayer_id": 0, "input": "left.y4m", "width": 1920, "height": 1080,
      "layer_type": "stereo", "view_type": "left", "qp": 128, "cpu_used": 5 },
    { "xlayer_id": 1, "input": "right.y4m", "width": 1920, "height": 1080,
      "layer_type": "stereo", "view_type": "right", "qp": 128, "cpu_used": 5 }
  ],
  "ops": [{
    "ops_id": 0, "priority": 0, "intent_present": true, "ptl_present": true,
    "operating_points": [
      { "intent": 0, "xlayer_map": [0] },
      { "intent": 1, "xlayer_map": [0, 1] }
    ]
  }],
  "output": "stereo.obu"
}
```

See: `cfg/xlayer/stereo_2layer.json`

### Subpicture Tiling

Tile a 1920x1080 frame into 4 quadrants from a single input source:

```json
{
  "inputs": [
    { "name": "default", "filename": "video.yuv", "width": 1920, "height": 1080 }
  ],
  "xlayers": [
    { "xlayer_id": 0, "width": 960, "height": 540,
      "atlas_pos_x": 0, "atlas_pos_y": 0, "qp": 128, "cpu_used": 5 },
    { "xlayer_id": 1, "width": 960, "height": 540,
      "atlas_pos_x": 960, "atlas_pos_y": 0, "qp": 128, "cpu_used": 5 },
    { "xlayer_id": 2, "width": 960, "height": 540,
      "atlas_pos_x": 0, "atlas_pos_y": 540, "qp": 128, "cpu_used": 5 },
    { "xlayer_id": 3, "width": 960, "height": 540,
      "atlas_pos_x": 960, "atlas_pos_y": 540, "qp": 128, "cpu_used": 5 }
  ],
  "atlas": { "enable": true, "width": 1920, "height": 1080,
             "uniform_spacing": false },
  "ops": [{
    "ops_id": 0, "priority": 0, "intent_present": true, "ptl_present": true,
    "operating_points": [
      { "intent": 0, "xlayer_map": [0] },
      { "intent": 1, "xlayer_map": [0, 1, 2, 3] }
    ]
  }],
  "output": "subpicture_4q.obu"
}
```

See: `cfg/xlayer/subpicture_4quadrant.json`

### Subpicture with Auxiliary Layers

Encode texture and alpha from separate source files, each tiled into
subpictures. The texture tiles crop from the texture source, and the
alpha tiles crop from the alpha source:

```json
{
  "inputs": [
    { "name": "texture", "filename": "video.yuv", "width": 1920, "height": 1080 },
    { "name": "alpha", "filename": "alpha.yuv", "width": 1920, "height": 1080 }
  ],
  "xlayers": [
    { "xlayer_id": 0, "input_source": "texture", "width": 960, "height": 540,
      "atlas_pos_x": 0, "atlas_pos_y": 0, "layer_type": "texture", ... },
    { "xlayer_id": 1, "input_source": "texture", "width": 960, "height": 540,
      "atlas_pos_x": 960, "atlas_pos_y": 0, "layer_type": "texture", ... },
    { "xlayer_id": 2, "input_source": "alpha", "width": 960, "height": 540,
      "atlas_pos_x": 0, "atlas_pos_y": 0,
      "layer_type": "auxiliary", "auxiliary_type": "alpha", ... },
    { "xlayer_id": 3, "input_source": "alpha", "width": 960, "height": 540,
      "atlas_pos_x": 960, "atlas_pos_y": 0,
      "layer_type": "auxiliary", "auxiliary_type": "alpha", ... }
  ],
  "output": "subpic_tex_alpha.obu"
}
```

See: `cfg/xlayer/subpicture_texture_alpha_4q.json`

### Spatial Scalability with Embedded Layers

Encode a texture layer with 3 spatial scales (1/4, 1/2, full) and a
depth layer at full resolution only:

```json
{
  "xlayers": [
    { "xlayer_id": 0, "input": "texture.y4m", "width": 1920, "height": 1080,
      "num_embedded_layers": 3,
      "scaling_mode": ["1/4", "1/2", "1:1"],
      "layer_type": "texture", "qp": 128, "cpu_used": 9 },
    { "xlayer_id": 1, "input": "depth.y4m", "width": 1920, "height": 1080,
      "num_embedded_layers": 1,
      "layer_type": "auxiliary", "auxiliary_type": "depth",
      "qp": 160, "cpu_used": 9 }
  ],
  "ops": [{
    "ops_id": 0, "priority": 0, "intent_present": true, "ptl_present": true,
    "operating_points": [
      { "intent": 0, "xlayer_map": [0], "mlayer_count": [1] },
      { "intent": 1, "xlayer_map": [0], "mlayer_count": [3] },
      { "intent": 2, "xlayer_map": [0, 1], "mlayer_count": [3, 1] }
    ]
  }],
  "output": "scalable_texture_depth.obu"
}
```

This produces three operating points:
- OP0: texture at 1/4 resolution (480x270) — lowest bandwidth
- OP1: texture at all 3 scales (480x270, 960x540, 1920x1080) — full quality
- OP2: texture at all scales + depth — complete bitstream

See: `cfg/xlayer/texture_depth_2layer_3ml.json`

### Mixed Embedded Layer Counts

Different xlayers can have different numbers of embedded layers. For
example, a main texture layer could use 3 embedded layers for spatial
scalability while an auxiliary depth layer uses only 1. The constraint
is that output frames within a TU must have matching order hints and
synchronized random access points — NOT that embedded layer counts
match across xlayers.

### Stereo via Embedded Layers

Encode left and right views as two embedded layers within a single
xlayer, each reading from a different input source. This allows
inter-layer prediction between views when `depends_on` is set:

```json
{
  "inputs": [
    { "name": "left", "filename": "left.yuv", "width": 1920, "height": 1080 },
    { "name": "right", "filename": "right.yuv", "width": 1920, "height": 1080 }
  ],
  "xlayers": [
    { "xlayer_id": 0, "input_source": "left", "width": 1920, "height": 1080,
      "atlas_pos_x": 0, "atlas_pos_y": 0,
      "layer_type": "stereo", "view_type": "left",
      "num_embedded_layers": 2,
      "embedded_layers": [
        { "scaling_mode": "1:1", "input_source": "left",
          "atlas_pos_x": 0, "atlas_pos_y": 0, "width": 1920, "height": 1080,
          "depends_on": [] },
        { "scaling_mode": "1:1", "input_source": "right",
          "atlas_pos_x": 0, "atlas_pos_y": 0, "width": 1920, "height": 1080,
          "depends_on": [0] }
      ],
      "qp": 128, "cpu_used": 5 }
  ],
  "ops": [{
    "ops_id": 0, "priority": 0, "intent_present": true, "ptl_present": true,
    "mlayer_info_idc": 2,
    "operating_points": [
      { "intent": 0, "xlayer_map": [0], "mlayer_count": [1] },
      { "intent": 1, "xlayer_map": [0], "mlayer_count": [2] }
    ]
  }],
  "output": "stereo_embedded.obu"
}
```

This produces two operating points:
- OP0: left view only (mlayer 0)
- OP1: both views (mlayers 0 and 1)

See: `cfg/xlayer/stereo_embedded_2ml.json`

### Subpicture Tiling via Embedded Layers

Tile a 1920x1080 frame into 4 quadrants using 4 embedded layers within
a single xlayer, each cropping from a different region of the same
input source. This avoids needing 4 separate xlayers:

```json
{
  "inputs": [
    { "name": "video", "filename": "video.yuv", "width": 1920, "height": 1080 }
  ],
  "xlayers": [
    { "xlayer_id": 0, "input_source": "video", "width": 960, "height": 540,
      "atlas_pos_x": 0, "atlas_pos_y": 0,
      "num_embedded_layers": 4,
      "embedded_layers": [
        { "scaling_mode": "1:1", "input_source": "video",
          "atlas_pos_x": 0, "atlas_pos_y": 0, "width": 960, "height": 540,
          "depends_on": [] },
        { "scaling_mode": "1:1", "input_source": "video",
          "atlas_pos_x": 960, "atlas_pos_y": 0, "width": 960, "height": 540,
          "depends_on": [] },
        { "scaling_mode": "1:1", "input_source": "video",
          "atlas_pos_x": 0, "atlas_pos_y": 540, "width": 960, "height": 540,
          "depends_on": [] },
        { "scaling_mode": "1:1", "input_source": "video",
          "atlas_pos_x": 960, "atlas_pos_y": 540, "width": 960, "height": 540,
          "depends_on": [] }
      ],
      "qp": 128, "cpu_used": 5 }
  ],
  "output": "subpic_embedded.obu"
}
```

Note `depends_on: []` on each mlayer — the quadrants are spatially
independent so inter-layer prediction is disabled.

See: `cfg/xlayer/subpicture_embedded_4q.json`

### Texture + Depth via Embedded Layers with XLayers

Combine xlayers and per-mlayer embedded layers. One xlayer uses 3
embedded layers for spatial scalability (1/4, 1/2, full), while a
second xlayer has 2 embedded layers reading from texture and depth
sources separately:

```json
{
  "inputs": [
    { "name": "texture", "filename": "texture.yuv", "width": 1920, "height": 1080 },
    { "name": "depth", "filename": "depth.yuv", "width": 1920, "height": 1080 }
  ],
  "xlayers": [
    { "xlayer_id": 0, "input_source": "texture", "width": 1920, "height": 1080,
      "atlas_pos_x": 0, "atlas_pos_y": 0,
      "layer_type": "texture",
      "num_embedded_layers": 3,
      "embedded_layers": [
        { "scaling_mode": "1/4" },
        { "scaling_mode": "1/2" },
        { "scaling_mode": "1:1" }
      ],
      "qp": 128, "cpu_used": 5 },
    { "xlayer_id": 1, "input_source": "texture", "width": 1920, "height": 1080,
      "atlas_pos_x": 0, "atlas_pos_y": 0,
      "layer_type": "auxiliary", "auxiliary_type": "depth",
      "num_embedded_layers": 2,
      "embedded_layers": [
        { "scaling_mode": "1/2", "input_source": "texture",
          "atlas_pos_x": 0, "atlas_pos_y": 0, "width": 1920, "height": 1080,
          "depends_on": [] },
        { "scaling_mode": "1:1", "input_source": "depth",
          "atlas_pos_x": 0, "atlas_pos_y": 0, "width": 1920, "height": 1080,
          "depends_on": [] }
      ],
      "qp": 160, "cpu_used": 5 }
  ],
  "ops": [{
    "ops_id": 0, "priority": 0, "intent_present": true, "ptl_present": true,
    "mlayer_info_idc": 2,
    "operating_points": [
      { "intent": 0, "xlayer_map": [0], "mlayer_count": [1] },
      { "intent": 1, "xlayer_map": [0], "mlayer_count": [3] },
      { "intent": 2, "xlayer_map": [0, 1], "mlayer_count": [3, 2] }
    ]
  }],
  "output": "texture_depth_embedded.obu"
}
```

This produces three operating points:
- OP0: texture at 1/4 resolution
- OP1: texture at all 3 scales
- OP2: texture at all scales + depth via independent embedded layers

See: `cfg/xlayer/texture_depth_embedded_3ml_2ml.json`

---

## GOP Mode and Output Order

The `gop_mode` and `monotonic_output_order` settings interact to control
how keyframes, reference frames, and hidden frames are managed. This
section covers the valid combinations with multi-xlayer + multi-mlayer
examples.

### Compatibility Matrix

| GOP Mode | Non-Monotonic (`false`) | Monotonic (`true`) |
|----------|:-----------------------:|:------------------:|
| `closed` | Yes | Yes |
| `open_leading` | Yes | **No** |
| `open_sef` | Yes* | Yes |

\* `open_sef` with non-monotonic is valid but uses KEY_FRAME (not
INTRA_ONLY_FRAME) as the forward keyframe, which resets reference
buffers. With monotonic output, `open_sef` uses INTRA_ONLY_FRAME to
preserve references across the GOP boundary.

**Key differences:**

- **Non-monotonic**: ARF and INTNL_ARF frames are implicit output
  frames (the decoder reorders them to display order). No SEF OBUs are
  needed for these. Zero overhead.
- **Monotonic**: ARF and INTNL_ARF frames are genuinely hidden. SEF
  OBUs are inserted at the correct monotonic position to display them.
  SEFs have zero coding cost.
- **`open_leading`**: The forward keyframe is an OLK. By default it is
  **displayed** (implicit output), but it can be made hidden with
  `enable_keyframe_filtering = 2` (producing a filtered overlay in the
  same TU). The OLK designation is at the TU level; higher embedded
  layers in the OLK TU can be inter OBUs.
- **`open_sef` + monotonic**: The forward keyframe is a **hidden
  INTRA_ONLY_FRAME** that does NOT reset reference buffers. Inter-
  prediction across the GOP boundary is possible.

### Closed GOP, Non-Monotonic (Multi-XLayer + Multi-MLayer)

Each GOP begins with a CLK that resets all reference buffers. ARF and
INTNL_ARF frames are implicit output (decoder reorders). This is the
simplest and most robust configuration.

```json
{
  "xlayers": [
    { "xlayer_id": 0, "input": "texture.y4m", "width": 1920, "height": 1080,
      "num_embedded_layers": 2, "scaling_mode": ["1/2", "1:1"],
      "qp": 128, "cpu_used": 5, "lag_in_frames": 19,
      "gop_mode": "closed" },
    { "xlayer_id": 1, "input": "depth.y4m", "width": 1920, "height": 1080,
      "num_embedded_layers": 1,
      "layer_type": "auxiliary", "auxiliary_type": "depth",
      "qp": 160, "cpu_used": 5, "lag_in_frames": 19,
      "gop_mode": "closed" }
  ],
  "monotonic_output_order": false,
  "output": "closed_nonmono.obu"
}
```

See: `cfg/xlayer/texture_depth_2xl_2ml_closed_nonmono.json`

### Closed GOP, Monotonic (Multi-XLayer + Multi-MLayer)

Same as above but with monotonic output. Hidden frames (ARF, INTNL_ARF)
are output via SEF at the correct display position. This is required
when the application needs frames in strict display order (e.g.,
low-delay playback without reordering).

```json
{
  "xlayers": [
    { "xlayer_id": 0, "input": "texture.y4m", "width": 1920, "height": 1080,
      "num_embedded_layers": 2, "scaling_mode": ["1/2", "1:1"],
      "qp": 128, "cpu_used": 5, "lag_in_frames": 19,
      "gop_mode": "closed" },
    { "xlayer_id": 1, "input": "depth.y4m", "width": 1920, "height": 1080,
      "num_embedded_layers": 1,
      "layer_type": "auxiliary", "auxiliary_type": "depth",
      "qp": 160, "cpu_used": 5, "lag_in_frames": 19,
      "gop_mode": "closed" }
  ],
  "monotonic_output_order": true,
  "output": "closed_mono.obu"
}
```

See: `cfg/xlayer/texture_depth_2xl_2ml_closed_mono.json`

### Open Leading, Non-Monotonic (Multi-XLayer + Multi-MLayer)

The forward keyframe is an OLK at each GOP boundary. By default it is
displayed (implicit output), but `enable_keyframe_filtering` can make
it hidden with a filtered overlay. Frames before the OLK in display
order are coded as leading pictures after the OLK in coding order. The
OLK allows random access while preserving some coding efficiency
through leading-picture prediction.

Requires `lag_in_frames > 0` and `monotonic_output_order: false`.

```json
{
  "xlayers": [
    { "xlayer_id": 0, "input": "texture.y4m", "width": 1920, "height": 1080,
      "num_embedded_layers": 2, "scaling_mode": ["1/2", "1:1"],
      "qp": 128, "cpu_used": 5,
      "lag_in_frames": 19, "kf_max_dist": 9,
      "gop_mode": "open_leading" },
    { "xlayer_id": 1, "input": "depth.y4m", "width": 1920, "height": 1080,
      "num_embedded_layers": 1,
      "layer_type": "auxiliary", "auxiliary_type": "depth",
      "qp": 160, "cpu_used": 5,
      "lag_in_frames": 19, "kf_max_dist": 9,
      "gop_mode": "open_leading" }
  ],
  "monotonic_output_order": false,
  "output": "open_leading_nonmono.obu"
}
```

See: `cfg/xlayer/texture_depth_2xl_2ml_open_leading.json`

### Open SEF, Monotonic (Multi-XLayer + Multi-MLayer)

The forward keyframe is a hidden INTRA_ONLY_FRAME that does not reset
reference buffers. Inter-prediction from frames before the GOP boundary
is preserved. The hidden frame is output via SEF in monotonic display
order. This gives the best coding efficiency at GOP boundaries while
maintaining strict display-order output.

Requires `lag_in_frames > 0`.

```json
{
  "xlayers": [
    { "xlayer_id": 0, "input": "texture.y4m", "width": 1920, "height": 1080,
      "num_embedded_layers": 2, "scaling_mode": ["1/2", "1:1"],
      "qp": 128, "cpu_used": 5,
      "lag_in_frames": 19, "kf_max_dist": 9,
      "gop_mode": "open_sef" },
    { "xlayer_id": 1, "input": "depth.y4m", "width": 1920, "height": 1080,
      "num_embedded_layers": 1,
      "layer_type": "auxiliary", "auxiliary_type": "depth",
      "qp": 160, "cpu_used": 5,
      "lag_in_frames": 19, "kf_max_dist": 9,
      "gop_mode": "open_sef" }
  ],
  "monotonic_output_order": true,
  "output": "open_sef_mono.obu"
}
```

See: `cfg/xlayer/texture_depth_2xl_2ml_open_sef_mono.json`

---

## Decoding

### Basic multi-layer decode

```bash
# Decode all layers into a single interleaved y4m
avmdec --all-layers -o decoded.y4m combined.obu

# Decode all layers into separate per-stream files
avmdec --all-layers --num-streams=2 -o decoded_%d.y4m combined.obu
```

### Atlas composite decode

Reconstruct the original composite canvas from subpicture tiles:

```bash
avmdec --all-layers --atlas-composite \
  --xlayer-config subpicture_4quadrant.json \
  -o composite.y4m subpicture_4q.obu
```

This reads the atlas layout from the JSON config and composites each
decoded xlayer back into its position on the canvas.

---

## Stream Demuxing

The `stream_demuxer` tool (built alongside `avmenc` and `avmdec`) can
extract individual xlayer bitstreams from a combined multi-xlayer OBU
file:

```bash
stream_demuxer input.obu output_prefix
```

This produces separate `.obu` files for each xlayer discovered in the
Global LCR: `output_prefix_0.obu`, `output_prefix_1.obu`, etc. Each
extracted stream can be decoded independently with the standard decoder.

---

## Constraints and Validation

The JSON config is validated before encoding. The following constraints
are enforced:

1. **xlayer_id** must be unique and in range 0-30.
2. Each xlayer must have an `input` file, an `input_source` reference,
   or a single default `inputs` entry must be configured.
3. **Input source names** must be unique and non-empty.
4. `"inputs"` and `"source"` cannot both be present.
5. When multiple `inputs` are defined, each xlayer without its own
   `input` file must have an explicit `input_source`.
6. **num_embedded_layers** must be 1-8.
7. When `num_embedded_layers > 1`:
   - The last entry in `scaling_mode` must be `"1:1"` (full resolution).
   - All scaling mode values must be valid (0-6).
8. **Input source** mode requires `atlas_pos_x`, `atlas_pos_y`,
   `width`, and `height` for every xlayer using that source. Xlayers
   sharing the same input source must use the same chroma format.
9. **OPS** operating points may only reference xlayer IDs that exist
   in the config.
10. When **`monotonic_output_order` is `false`**, all xlayers must use
   the same coding structure: `num_temporal_layers`, `lag_in_frames`,
   `kf_max_dist`, `subgop_config`, and `gop_mode`. Different
   `num_embedded_layers` is allowed.
11. **`gop_mode: "open_leading"`** is not allowed when
   `monotonic_output_order` is `true` (leading OBUs require
   non-monotonic output).
12. **Input source frame rates** must be exact integer divisors of the
   highest frame rate among all input sources (e.g. 60/30/15 is valid,
   but 30/24 is not).
13. **`embedded_layers`** and the flat `scaling_mode` array are mutually
   exclusive on the same xlayer entry.
14. **`embedded_layers`** array length must match `num_embedded_layers`.
15. Per-mlayer **`input_source`** requires `width`, `height`,
   `atlas_pos_x`, and `atlas_pos_y`.
16. **`depends_on`** entries must reference mlayer indices strictly less
   than the current mlayer index. mlayer 0 cannot depend on anything.
17. **CLK/OLK alignment:** When a CLK (Closed Layer Key) OBU appears
   in a temporal unit, the first embedded layer (mlayer 0) and all
   independent embedded layers (those with `depends_on: []`) must
   also have CLK OBUs. The same rule applies to OLK (Open Layer Key)
   OBUs. The encoder enforces this automatically.
18. **Monotonic output order and hidden frames:** When
   `monotonic_output_order` is `true`, implicit output frames are not
   allowed. All hidden frames (ARFs, forward keyframes) must be output
   via SEF (Show Existing Frame) instead. The encoder automatically
   enables `add_sef_for_hidden_frames` when monotonic output is
   requested. This precludes `gop_mode: "open_leading"` (which uses
   implicit output for OLK overlays and leading frames).
19. **Open GOP with monotonic output:** When `gop_mode: "open_sef"` and
   `monotonic_output_order` is `true`, the forward keyframe is coded as
   INTRA_ONLY_FRAME (not KEY_FRAME). This preserves reference buffers
   across the GOP boundary, enabling inter-prediction from pre-boundary
   frames. The hidden intra frame is later shown via SEF.

---

## Reference Configs

The `cfg/xlayer/` directory contains ready-to-use configuration files:

| Config | Description |
|--------|-------------|
| `texture_depth_2layer.json` | Texture + depth, 2 xlayers |
| `texture_depth_2layer_3ml.json` | Texture (3 embedded layers) + depth |
| `texture_depth_2layer_clk.json` | Texture + depth, closed GOP |
| `texture_depth_2layer_open_leading.json` | Texture + depth, open leading GOP (1 mlayer each) |
| `texture_depth_2layer_open_sef.json` | Texture + depth, open SEF GOP (1 mlayer each) |
| `texture_depth_2xl_2ml_closed_nonmono.json` | **2 xlayers × 2 mlayers, closed GOP, non-monotonic** |
| `texture_depth_2xl_2ml_closed_mono.json` | **2 xlayers × 2 mlayers, closed GOP, monotonic** |
| `texture_depth_2xl_2ml_open_leading.json` | **2 xlayers × 2 mlayers, open leading, non-monotonic** |
| `texture_depth_2xl_2ml_open_sef_mono.json` | **2 xlayers × 2 mlayers, open SEF, monotonic** |
| `texture_depth_2layer_local_only.json` | Texture + depth, local-only LCR |
| `texture_depth_2layer_fast.json` | Texture + depth, fast debug settings (coding tools disabled) |
| `texture_2mlayer_fast.json` | Single xlayer with 2 embedded layers, fast debug settings |
| `texture_alpha_depth_3layer.json` | Texture + alpha + depth, 3 xlayers |
| `stereo_2layer.json` | Stereo simulcast: left + right as separate xlayers (no inter-layer prediction) |
| `subpicture_3region.json` | 3-region subpicture tiling |
| `subpicture_4quadrant.json` | 4-quadrant subpicture tiling (single input source) |
| `subpicture_texture_alpha_4q.json` | 4-quadrant with separate texture + alpha input sources |
| `annexG2_360degree_9xlayer.json` | 360-degree video, 9 subpictures with 3 embedded layers each |
| `annexG3_videoconf_3xlayer.json` | Video conferencing, 3 participants |
| `annexG4_roi_scalable_2xlayer.json` | ROI scalable, base + enhancement |
| `stereo_embedded_2ml.json` | Stereo views via 2 embedded layers with inter-layer prediction |
| `subpicture_embedded_4q.json` | 4-quadrant subpicture via 4 embedded layers |
| `texture_depth_embedded_3ml_2ml.json` | Texture (3 mlayers) + depth via embedded layers with xlayers |

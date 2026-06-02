# AVM - Multi-Layer Branch

This branch extends [AVM](https://github.com/AOMediaCodec/avm) with a
**multi-xlayer encoding framework**, allowing multiple extended layers
(xlayers), each optionally containing embedded layers (mlayers), to be
encoded into a single combined AV2 bitstream.

Typical use cases: texture + alpha, stereo video, texture + depth,
subpicture tiling, and spatial scalability.

> **Status:** Implements the multi-layer functionality of the AV2 spec.
> Maintained as a separate branch so the single-stream codebase on
> `main` stays simple and easy to optimize. The standard single-stream
> encoder/decoder paths on `main` are unchanged.

## Quick start

Build is identical to `main` (out-of-source CMake):

```bash
cmake -S . -B build
cmake --build build -j
```

Encode stereo (left + right views) as two xlayers into a single
combined bitstream:

```bash
build/avmenc --xlayer-config cfg/xlayer/stereo_2layer.json --limit=30
```

Decode both views into separate files:

```bash
build/avmdec --all-layers --num-streams=2 -o decoded_%d.y4m stereo_muxed.obu
```

## What's added

- New encoder mode: `avmenc --xlayer-config <json>`
- New decoder flags: `--all-layers`, `--num-streams=N`,
  `--atlas-composite`, `--xlayer-config`
- Ready-made JSON configs under [`cfg/xlayer/`](cfg/xlayer/)
  (texture+depth, stereo, subpicture tiling, spatial scalability, …)
- TU assembler that combines per-xlayer encoder outputs into a single
  combined temporal unit stream

## Documentation

- **[Multi-XLayer Encoding Guide](doc/multi_xlayer_encoding.md)**:
  full reference covering JSON schema, embedded layers, atlas, OPS,
  GOP modes, use cases, and constraints.

## Build, test, contribute

For everything not specific to this branch (building variants,
running unit tests, sanitizers, coding style, CTC, and submitting
patches), see the
[main branch README](https://github.com/AOMediaCodec/avm/blob/main/README.md).

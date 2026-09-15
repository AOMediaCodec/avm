# Bit-exactness evidence, round 0915

Method: encode the same clip with the unpatched and patched binaries, compare
`md5sum` of the `.obu`. Identical md5 = bit-exact on that configuration.
Anchor `7368e76`. All runs `--passes=1 --lag-in-frames=19 --end-usage=q
--threads=1 --obu`.

A sweep varies **clip × preset × qindex**, and each patch's sweep includes at
least one configuration that exercises the code path the patch touches. A sweep
is evidence, not proof; where the argument for exactness is weaker than
"function-local buffer", the sweep is the load-bearing part and is widened
accordingly.

---

## Patch 0034 — `winner_mode_stats`, skip the palette map

| config | md5 |
|---|---|
| 4 configurations: 2 clips × 3 presets × 2 QPs | `a271350b93a4a028`, `4073825a8e6d892f`, `5713e61f499d46d2`, `d434144611e1e035` |

All matched. (md5s recorded in short form from the August verification, re-run
and re-confirmed this round.)

**Measured saving**, retired instructions under callgrind
(320x192, 3 frames, cpu-used=5, qp=160, 1 thread):

```
unpatched   176,252,929,353
patched     171,545,473,002
removed       4,707,456,351   =  2.67%
```

---

## Patch 0035 — pre-trellis gate, self-calibrated

This patch is an **approximation**, so it is not expected to be bit-exact. What
is verified is that the *refactor* is exact: compiled with
`-DAVM_TX_GATE_CALIBRATE=0` it must reproduce the shipped gate byte for byte,
so that every difference measured in the calibrated arms is attributable to
calibration alone.

| config | md5 |
|---|---|
| 3 configurations | `63802d27c9d898a5`, `b7174767a48dd3bf`, `2ef026d2c926744d` |

All matched the shipped-gate build.

---

## Patch 0036 — intra, second MRL line only when read

Guards writes to **function-local stack buffers**, so the exactness argument is
a locality property: nothing outside the function can observe the skipped
writes. The sweep includes low-speed presets because MRL (`mrl_index > 0`) is
the path on which the `_2nd` buffers *are* read — the guard has to let the work
through there, not just skip it.

| config | clip | preset | qp | frames | result |
|---|---|---|---|---|---|
| s5q160_192 | 320x192 | 5 | 160 | 3 | MATCH `5d7efbacc3729635fd4c90e3af471296` |
| s2q110_192 | 320x192 | 2 | 110 | 4 | MATCH `4b7c45d8dd9814df7aa9dd9da109ec21` |
| s1q90_360 | 640x360 | 1 | 90 | 3 | MATCH `dde00f88ac03ed37c4b702a086e8ec11` |
| s3q200_360 | 640x360 | 3 | 200 | 4 | MATCH `08e5f7bbf2a98e1a15a938fe40aa8714` |
| s4q150_720 | 1280x720 | 4 | 150 | 2 | MATCH `17413d53011b31e0f1b410eb951d9bdf` |
| s0q60_192 | 320x192 | 0 | 60 | 2 | MATCH `7c3c118aa166c8382984a07449e68dc2` |

**6/6**, presets 0 through 5, three resolutions, QPs 60-200.

---

## Patch 0037 — inter, clear `mv_refined` only when OPFL can read it

Guards writes to **`MACROBLOCKD` state that persists across blocks**, so the
exactness argument is a reading of the call tree, not a locality property. It
is weaker than 0036's, and the sweep is correspondingly the load-bearing
evidence. Runs use 6-10 frames rather than 2-4 so inter prediction, OPFL and
TIP actually fire.

### Sweep 1 — clip × preset × qindex, default tool configuration

| config | clip | preset | qp | frames | result |
|---|---|---|---|---|---|
| s5q160_10f | 320x192 | 5 | 160 | 10 | MATCH `747f49b354982d0077d761f3dddc09a6` |
| s2q110_10f | 320x192 | 2 | 110 | 10 | MATCH `4b7c45d8dd9814df7aa9dd9da109ec21` |
| s1q60_8f | 320x192 | 1 | 60 | 8 | MATCH `bdd08b92c8c065ed3ab2ac8f4be0ace7` |
| s3q200_360 | 640x360 | 3 | 200 | 8 | MATCH `d01f250ac17c19677078436f3e50e8af` |
| s4q90_360 | 640x360 | 4 | 90 | 8 | MATCH `81aa400a9da616cb1810e6cd2f05f759` |
| s5q150_720 | 1280x720 | 5 | 150 | 6 | MATCH `3d580dabf9948d5ebf9c6c4094898d9d` |

**6/6.**

### Sweep 2 — SDP × OPFL stress

The case that would break the argument is a block whose chroma pass evaluates
the OPFL predicate true while its luma pass evaluated it false. SDP
(semi-decoupled partitioning) is what makes luma and chroma trees separable, so
this sweep drives SDP and OPFL to their extremes rather than leaving OPFL at its
default "switchable per block".

| config | what it stresses | result |
|---|---|---|
| `--enable-opfl-refine=2 --enable-sdp=1 --enable-extended-sdp=1`, speed 2 | OPFL in **all** blocks, SDP on both key and inter frames | MATCH `cc37a5a73b6235a5d92556e999aedb94` |
| same, speed 5 | ditto at a different preset | MATCH `f66c9c02dab60b213a00859c3b9c04c9` |
| same, 640x360 speed 3 | ditto at a different resolution | MATCH `e915ffb35584957f2a885e8ee3153816` |
| `--enable-opfl-refine=0`, speed 2 | OPFL **off** — the skip path is taken on every block | MATCH `b2978dfe9e8c110894f5714f88acec3e` |
| `--enable-opfl-refine=0`, speed 5 | ditto | MATCH `2a7c3541736ea440186550fc12f972fa` |
| `--enable-opfl-refine=2 --enable-tip-refinemv=0` | the forced-on TIP branch under a different TIP config | MATCH `1a5d4701a7b8030eb2a1f3ed6070b29b` |
| `--enable-opfl-refine=2 --enable-sdp=0 --enable-extended-sdp=0` | OPFL everywhere, SDP off — isolates OPFL from SDP | MATCH `81df627b254b9cc181f6636b29ff36e5` |

**7/7.** Combined with sweep 1, **13/13** for patch 0037.

The two sweeps cover the skip path at both extremes: `opfl_off_*` takes it on
every block in the sequence, and `sdp_opfl_all_*` takes it on none of them
while driving the reader path as hard as the encoder allows. If the guard
disagreed with the readers anywhere in between, the graded configurations in
sweep 1 — where OPFL is switchable per block, so both paths occur within the
same frame — are where that would show.

---

## What a passing sweep does and does not establish

It establishes that on every configuration tried, the patched encoder produced
the identical bitstream. It does not establish exactness over the whole
configuration space. For 0034 and 0036 the code argument carries most of the
weight and the sweep confirms it. For 0037 the sweep carries most of the
weight, which is why it is the widest of the three — and why the patch header
says to widen it further rather than treating a green sweep as closure.

# Round 0915: sync, a profile that changes the Speed-2 plan, and eight ideas

Anchor `7368e76` (2026-09-14). First session since 2026-08-28.

---

## 0. Three things to know before the ideas

### Your patch 0027 landed, at ratio 101.5

`d4ad634`, "Skip trellis based on an estimated rdcost", Sept 10 — the
pre-trellis RD gate, scoped to **speed ≥ 5 and 4K only**:

```
A1 (4K), RA, speed 5:   +4.06% speedup / +0.04% BD-rate   ratio 101.5
```

That is the best ratio anything from this project has achieved in production.
§2 argues it is also, by a wide margin, the most under-deployed.

### I could not read `avm-patches` from this session

`raw.githubusercontent.com` returns 404, the API returns "GitHub access to this
repository is not enabled for this session", and `add_repo` refuses it as a
cross-owner add (this session is scoped to `aomediacodec`). It worked earlier in
this same conversation, so something changed — visibility, or the session's
allowlist.

**So I have not read your recent updates there.** Nothing below is informed by
them. If there are results I should have accounted for, paste them or the raw
file contents and I will redo this round's prioritisation against them.

### The working branch and the ledger were deleted

`claude/av2-encoder-speed-optimization-3ukgg3` no longer exists on the remote —
`git ls-remote` shows seven branches and none is mine. That took with it
`experiments/` (registry, DECISIONS, protocol), `patches/0001`–`0033`, and the
architecture study. My own handover note said that directory "is the only thing
that survives you"; it did not.

I have rebuilt the ledger in this round's commit (`experiments/CLAUDE.md`,
`DECISIONS.md`, `registry.csv`) from conversation state, so a future session is
not starting blind. **Nothing was lost from this conversation's knowledge.** But
please keep a copy outside this repo.

---

## 1. What else changed upstream

| commit | why it matters |
|---|---|
| `d4ad634` | **patch 0027 landed**, speed ≥ 5 + 4K, ratio 101.5 |
| `db16baa` | "Properly disable uneven 4-way partitions for > 270p at speed 1" — the earlier attempt "was not working as intended, because `seq_params.enable_uneven_4way_partitions` was set [elsewhere]". **That is the multi-writer ordering bug** patch 0031's dumper was built to expose, confirmed independently upstream. |
| `720e889` | SMS partition pre-screener switched to TMVP MVs and the MLP retrained. **This invalidates the model behind my Speed-2 attribution** — see §7. |
| `16a4fd7` | transform-search result cache at speed 1 — reuse keyed on residual + contexts |
| `12c553a` | `check_primary_quant_all_zero` deprecated |
| `e2d8c01`, `ae9b126` | **AVX-512 kernels are now landing** (convolve-y SR, OPFL). My architecture study said AVX-512 was absent; that is now wrong — 5 specializations in `av2_rtcd_defs.pl`. NEON went 19 → 26 there too. |

---

## 2. The finding: at Speed 2, ~40% of the encoder is trellis quantization

I profiled the current tip with callgrind at **cpu-used=2** and **cpu-used=5**
(320x192, 3 frames, 1 thread, qp=160). Retired instructions, by function with
caller context:

| function | speed 5 | **speed 2** |
|---|---|---|
| `av2_trellis_loop_diagonal_st8_avx2` | 16.83% | **29.96%** |
| `av2_find_best_path_avx2` | 1.46% | 2.69% |
| `av2_update_nbr_diagonal_avx2` | 1.44% | 2.66% |
| `av2_trellis_quant` (self) | 0.92% | 1.98% |
| `av2_optimize_txb_new` | 0.99% | 1.52% |
| `trellis_first_pos` | — | 1.15% |
| `get_tx_type_cost` (via trellis) | — | 0.50% |
| **total RDOQ / TCQ** | **~21.6%** | **~40.5%** |

Nothing else is close. The next largest single line at Speed 2 is 4.65%.

### Why it nearly doubles from Speed 5 to Speed 2

`av2_optimize_b` is called **once per transform-type candidate per block**,
inside `search_tx_type`. Speed 2 evaluates a much wider tx_type candidate set
than Speed 5. Every extra candidate pays a full 8-state trellis over its whole
coefficient run, and all but one of those trellises is thrown away.

### Why no speed-feature study can find this

Under TCQ the trellis is **not optional**. `tx_search.c` is explicit:

```c
// Skip RDOQ in the dry pass, except under TCQ whose state-machine dequant
// needs trellis-quantized coefficients for correct distortion.
```

and both per-block trellis speed features are bypassed when `tcq_enable()`:

```c
if (tcq_enable(cm->features.tcq_mode, is_lossless, plane, TX_CLASS_2D)) {
  perform_block_coeff_opt = 1;          // unconditionally ON
} else {
  perform_block_coeff_opt = (block_mse_q8 <= coeff_opt_dist_threshold * qstep * qstep);
}
```

That is a correctness requirement, not an oversight: TCQ's dequantizer is a
state machine driven by the parity of previously coded coefficients, so
coefficients that did not come out of the trellis dequantize to the wrong
values. **So there is no speed feature that turns this off**, which is exactly
why 40% of the encoder never appeared in your 101-feature study or in the
25-feature Speed-2 experiment.

**The only legal lever is to run the trellis on fewer candidates.** That is
precisely and only what patch 0027's gate does.

### What this means for your 4%-BD Speed-2 target

You have been buying Speed-2 time from the partition search, at marginal ratios
between 7 and 55. Recall the arithmetic (`experiments/CLAUDE.md`): the Speed-2
bar is ratio 30, the ratio ceiling at BD = 4.0% is 25, and adding a change at
marginal ratio below the operating ratio makes the point *worse*.

The gate is a measured **ratio-101** mechanism pointed at the largest single
block of work in the encoder, and at Speed 2 that block is twice the share it
was where the gate was measured. Extending it is worth more than anything left
in the partition space.

### Caveats, stated plainly

- **Ir is not time.** These are AVX2 kernels with high IPC; 30% of instructions
  is probably less than 30% of cycles. Treat the ranking as solid and the
  absolute percentages as indicative.
- 320x192, 3 frames including a keyframe, one QP. Resolution-gated features
  differ from CTC and intra paths are over-represented.
- The clip trips screen-content detection despite being built to avoid it —
  `av2_is_dv_valid` (4.12%) and `av2_optimize_fsc_block` (1.38%) are IntraBC/FSC
  artefacts. **Do not motivate work from those two lines.** Removing them would
  raise the trellis share, not lower it.
- The trellis share is measured at qp=160. Lower QP means longer coefficient
  runs and a longer trellis, but also more work everywhere else.

---

## 3. Idea 1 — let the gate off the 4K leash (patch `0035`, built and verified)

The gate is live on one class at one preset. Earlier CTC at Speed 3 showed why:
A1 42.1 (pass), A2 20.2 (miss). The reason is structural.

The gate compares an **untrellised** estimate against a **trellised** `best_rd`.
The estimate is biased high by however much the trellis would have lowered it,
and the fixed `>> 3` margin is a constant pad for that bias. **The bias is not
constant** — it varies with qindex, coefficient density, block size and content.
A bias that differs 2× between A1 and A2 cannot be paid for by one constant, and
42.1 / 20.2 is what that looks like.

The correction is already in hand: every candidate that passes the gate has its
estimate computed by the gate and its true post-trellis RD computed a few
statements later. Accumulate both over the block's own candidates →
`alpha = Σrd_post / Σpre_rd` → compare `alpha · pre_rd` against `best_rd`.

**The refactor is proven exact.** With `-DAVM_TX_GATE_CALIBRATE=0` it produces
byte-identical bitstreams to today's encoder on three configurations
(`63802d27c9d898a5`, `b7174767a48dd3bf`, `2ef026d2c926744d`), so every measured
difference is attributable to calibration alone and the shipped operating point
stays exactly reproducible.

Arms, reordered after the Speed-2 profile:

```
1  -DAVM_TX_GATE_MIN_DIM=0                                gate extended to A2, speed 5
2  -DAVM_TX_GATE_MIN_DIM=0 -DAVM_TX_GATE_MIN_SPEED=2      ... and down to speed 2   <-- the prize
3  -DAVM_TX_GATE_MIN_DIM=0 -DAVM_TX_GATE_MIN_SPEED=2 -DAVM_TX_GATE_MARGIN64=4
```

Arm 1 is the cheap decisive test of whether calibration fixed the class gap.
**Arm 2 is now the headline arm**, not arm 3 as I had it: it aims a ratio-100
mechanism at the preset where the target block is 40% of the encoder.

A rough sizing, offered as an order of magnitude and not a prediction: at Speed
5 / 4K the gate bought 4.06% of encode time against a ~21.6% trellis share, so
it removed roughly a fifth of trellis work. The same fraction against a ~40%
share would be ~7-8% of encode time. Even at one third of the measured ratio
that clears the Speed-2 bar comfortably.

Local wall clock could not resolve the calibration change (−0.5% and +0.9%, reps
disagreeing in sign) and **is not quoted as a speedup**. That is also the
expected shape: at a fixed margin calibration prunes *less*, and the gain has to
come from the widening, which a 4K-only gate cannot be tested for locally.

---

## 4. Idea 2 — stop zeroing the palette map (patch `0034`, built, bit-exact)

`av2_zero(x->winner_mode_stats)` clears **132,480 bytes** at two call sites, of
which **131,072 (98.9%) is `color_index_map[MAX_SB_SQUARE]`** — palette
reconstruction state that is written before it is read. A 4x4 block's intra mode
search zeroes 128 KB before it starts.

Profile: `__memset_avx2` via `av2_rd_pick_intra_sby_mode` is **2.45%** of
retired instructions at Speed 5, **1.42%** at Speed 2.

This is a rebuild of the patch verified in August that never landed because the
branch was deleted; the code is unchanged upstream. Re-verified **bit-exact on
four configurations** here (`a271350b93a4a028`, `4073825a8e6d892f`,
`5713e61f499d46d2`, `d434144611e1e035`) — two clips, three presets, two QPs.

**Measured**, retired instructions under callgrind, same clip and settings as
the profile:

```
unpatched   176,252,929,353
patched     171,545,473,002
removed       4,707,456,351   =  2.67%
```

**2.67% of the encoder's instructions for 0.00% BD-rate.** Bit-exact ⇒ ΔBD = 0
⇒ unbounded ratio ⇒ cannot fail the bar at any preset. The cheapest arm on the
board, and it is no longer a speculative one.

Instruction counts probably *understate* the wall-time gain here — the work
removed is memory traffic, and a vectorised memset retires few instructions per
byte written. Treat 2.67% as a floor. **Do not try to confirm it with local
wall clock**; the noise floor is several percent.

---

## 5. Idea 3 — only prepare the second MRL reference line when it is read (patch `0036`, NEW, built)

`av2_build_intra_predictors_high()` keeps two neighbour reference lines. The
second pair (`above_row_2nd` / `left_col_2nd`) is consumed at exactly two call
sites, both inside the directional branch, both guarded by

```c
xd->mi[0]->multi_line_mrl && mrl_index && (tx_size != TX_4X4)
```

Everything that *produces* those buffers runs unconditionally: two
`avm_memset16` of 192 uint16 each at function entry, the left-column copy loops
and their edge-extension memsets, the above-row `memcpy` and its extension, and
the above-left corner writes. **`mrl_index` is 0 for the large majority of
blocks**, so in the common case all of it is written and never read.

The patch hoists the predicate to the top as `need_2nd_line` and guards every
producer with it. The two consumers are rewritten to test `need_2nd_line`
instead of re-deriving the predicate, so "written iff read" is enforced by the
compiler rather than by hand in two places.

Bit-exact by construction — it removes writes to function-local stack buffers
that are provably never read on the paths where the writes are skipped.

Profile: `avm_memset16` via `av2_build_intra_predictors_high` is 1.00% (Speed 5)
/ 0.81% (Speed 2); roughly half is the two `_2nd` memsets, and the `_2nd` copy
loops add more on top inside the 0.62-0.76% self cost and the memcpy lines.

Verification status is in §9.

---

## 5b. Idea 4 — only clear `mv_refined` when optical flow can read it (patch `0037`, NEW, built)

The **second-largest line in the entire Speed-2 profile** is a memset:

```
4.65% (speed 2) / 4.28% (speed 5)   __memset_avx2 via av2_build_inter_predictors
```

```c
if (plane == AVM_PLANE_Y)
  memset(xd->mv_refined, 0, 2 * N_OF_OFFSETS * sizeof(int_mv));   // 8192 bytes
```

`xd->mv_refined` is sized for the optical-flow subblock grid of a whole
256×256 superblock. It is cleared on every luma call, so **an 8×8 inter block
pays all 8192 bytes.**

I parked this in August because `make_inter_pred_of_nxn` indexes one past the
range the caller initializes and another site uses a TIP-specific offset — so
*narrowing* the memset to the block's own range is not safe. This patch does
something different and much simpler: it **skips the memset entirely when no
reader can run.**

Every reader inside this function's call tree sits under
`use_optflow_refinement`, and `build_inter_predictors_sub8x8` — the other
branch — never touches `mv_refined` at all. For the non-TIP case the predicate
reduces to `opfl_allowed_cur_pred_mode()`, which does **not** depend on `plane`,
so its value at luma is the value the chroma passes will compute. For TIP the
guard is forced on rather than reasoned about, because `tip.c` carries
`xd->mv_refined[0..1]` across call boundaries of its own.

**This argument is weaker than 0036's and I want to say so plainly.** 0036
guards function-local stack buffers, so nothing can observe the skipped writes;
`xd->mv_refined` is `MACROBLOCKD` state that persists across blocks, so the
claim rests on a reading of the call tree. The case that would break it is a
block whose chroma pass evaluates the predicate true while its luma pass
evaluated it false — and SDP is what makes luma and chroma trees separable.

So the sweep here is the evidence, not a formality, and it is the widest of the
three: **13/13**. Six configurations of clip x preset x qindex with 6-10 frame
runs so OPFL and TIP actually fire, plus a seven-configuration SDP x OPFL
stress sweep that drives both tools to their extremes rather than leaving OPFL
at its default switchable-per-block setting:

```
OPFL in ALL blocks + SDP on key and inter frames   speeds 2, 5, and 360p speed 3
OPFL OFF entirely (skip path on every block)       speeds 2, 5
OPFL everywhere, TIP refinemv off                  speed 2
OPFL everywhere, SDP off                           speed 2
```

That covers the skip path at both extremes — taken on every block, and on none
of them while the reader path runs as hard as the encoder allows. The graded
configurations in the first sweep, where OPFL is switchable per block so both
paths occur within the same frame, are where a disagreement in between would
show.

Worth noting that today's memset does not protect against cross-block staleness
either — it is itself gated on `plane == AVM_PLANE_Y`, so a chroma call whose mi
had no luma call already sees whatever the previous block left behind.

---

## 6. Idea 5 — the chroma intra mode search copies `MB_MODE_INFO` repeatedly

**Identified, unbuilt, and the largest unexplored memcpy.** 2.40% of
instructions at Speed 5 (1.25% at Speed 2) is `memcpy` reached through
`av2_rd_pick_intra_sbuv_mode`:

```c
MB_MODE_INFO best_mbmi = *mbmi;      // 632 bytes, once per call
  ...
  best_mbmi = *mbmi;                 // 632 bytes, per improving UV mode
  av2_copy_array(tmp_cctx_type_map, xd->cctx_type_map, ctx->num_4x4_blk_chroma);
  ...
*mbmi = best_mbmi;                   // 632 bytes, once per call
```

The whole 632-byte mode-info struct is saved and restored to preserve what is,
for a *chroma* mode search, a handful of chroma fields. The same pattern is in
`av2_rd_pick_intra_sby_mode`.

Two ways to attack it, in increasing risk:

1. **Save only the chroma-relevant fields.** Bit-exact *if* the field list is
   complete — and an incomplete list fails silently, which is exactly the class
   of bug worth being afraid of. Needs the list derived from what the UV loop
   writes, not from intuition.
2. **Split `MB_MODE_INFO`** so luma and chroma mode state are separately
   copyable. Larger change, benefits every save/restore in the encoder.

Before either, one more profile run with finer caller separation is needed to
split the 2.40% between the struct copy and the `av2_copy_array` calls. That
decides whether it is worth the risk. I have not built it.

---

## 7. Idea 6 — re-measure the Speed-2 attribution, because the model changed

Last round's finding was that `simple_motion_search_split` and
`simple_motion_search_early_term_none` jointly cost **1.64% BD on A1 at ratio
7.4**, and that the 101-feature study missed it because both tests edited a
block that later setters overwrite.

**`720e889` retrained that model** — the SMS pre-screener now uses TMVP-projected
MVs instead of motion-search MVs, with retrained MLP weights and 9-bit logit
rounding. My attribution was measured on the old model.

So before acting on patch `0033`'s prediction, **re-run its two arms on the
current anchor.** The retrain may have fixed some of the damage for free, or
moved it. This is cheap, and it prevents building on a stale measurement — the
failure that made patch 0018 a no-op.

Note this is now a lower priority than it was last round: §2 says the partition
space is not where the Speed-2 money is.

---

## 8. Ideas 7 and 8 — two small bit-exact SIMD gaps

**`av2_get_nz_map_contexts_skip` has no SIMD, and is not even dispatched.**
Its sibling `av2_get_nz_map_contexts` has `sse2`; the skip variant has an
`add_proto` but no `specialize` line — **and all three call sites in
`encodetxb.c` (lines 816, 1603, 4072) call `av2_get_nz_map_contexts_skip_c`
directly**, hardcoding the C symbol. So even adding a kernel would do nothing
until the call sites are switched to the dispatched name. The kernel itself is a
two-tap stencil (`clip_max3[left] + clip_max3[above]`, clamped to 6) over scan
positions, and the sibling is a working template.

**Temporal filtering still does 12-tap convolves in C.** `MULTITAP_SHARP2` has
exactly one user — `tf_build_predictor` — and `highbd_convolve_2d_facade_single`
routes every filter with more than 8 taps to the C convolve
(`// TODO(any): need SIMD for > 8 taps filters`). The SSE2 temporal-filter patch
vectorised the *filter apply*; this is the *predictor build* that feeds it, and
the two stack. More attractive than in August now that AVX-512 kernels are
landing, so there is appetite and precedent.

Both are bit-exact class. Neither is large. Neither appears in these profiles,
because temporal filtering does not run on a 3-frame clip.

---

## 9. What I would run, and what is verified

| priority | arm | why |
|---|---|---|
| 1 | `0035` arm 2 (`MIN_DIM=0 MIN_SPEED=2`) | ratio-101 mechanism at the preset where its target is 40% of the encoder |
| 2 | `0035` arm 1 (`MIN_DIM=0`) | cheap decisive test that calibration closed the A1/A2 gap; run alongside arm 2 |
| 3 | `0034` at any preset | bit-exact, cannot fail the bar, cheapest arm |
| 4 | `0036` at any preset | bit-exact, same reasoning, smaller |
| 5 | `0033` re-run, both arms | its premise predates the SMS retrain |

Verification status of what is in `patches/`:

| patch | built | bit-exact verified | notes |
|---|---|---|---|
| `0034` | yes | **yes, 4 configs** | md5s in `registry.csv` |
| `0035` | yes | **yes, 3 configs** with `CALIBRATE=0` | proves the refactor is exact; calibration itself is an approximation and is the thing under test |
| `0036` | yes | see the patch header | sweep across 6 configs incl. low-speed presets where MRL is live |

And the one I have now asked for six times: **one anchor-vs-anchor CTC arm** to
price the cluster's EncTime repeatability. Two of this round's judgements ("the
MLP threshold is inert", "local timing cannot resolve this") turn on differences
of ~1%, and nobody knows whether that is signal.

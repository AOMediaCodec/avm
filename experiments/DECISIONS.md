# Decisions and findings

Rebuilt 2026-09-15 after the branch (and the original ledger) was deleted from
the remote. Entries are carried forward from conversation state; the original
per-round reports are in `avm-patches/Claude/` if that copy survived.

---

## D1 — Ratio, not speedup, is the objective. (settled)

A patch that saves time but costs more BD-rate than the operating point's
ratio allows makes the preset *worse*. Every proposal is scored as
`Δspeed / ΔBD` against the bar for its preset, on A1 and A2 separately.

## D2 — Bit-exact changes are the highest-value class. (settled)

ΔBD = 0 ⇒ unbounded ratio ⇒ cannot fail any bar. The encoder spends ~10% of
retired instructions on `memset`/`memcpy` that no coding tool accounts for,
so this class is far from exhausted. Prefer it over anything that trades
quality.

## D3 — Attribute groups by differencing against the fully-reverted arm. (settled)

Leave-one-out understates a group's cost by ~4× when its members prune the
same decisions. Established on the partition group.

## D4 — The Speed-1 → Speed-2 gap is concentrated, not diffuse. (measured, needs re-check)

The user's 25-feature "disable the non-significant ones" experiment barely
moved the gap, which is consistent with the damage being in a few features.
Differencing against the fully-reverted arm found:

| feature(s) | ΔBD (A1/A2) | ratio | verdict |
|---|---|---|---|
| extended-partition disable | — | 54.9 / 48.7 | strong keep |
| MLP none-threshold | +0.04% / −0.01% | — | inert; not the defect |
| `simple_motion_search_split` + `simple_motion_search_early_term_none` | +1.64% / +1.19% | 7.4 / 14.0 | **the culprit** |

This inverted the conclusion of the user's own report.

**Caveat as of 2026-09-15:** upstream `720e889` retrained the SMS pre-screener
(TMVP-projected MVs, new MLP weights, 9-bit logit rounding). The attribution
above was measured on the *old* model. Re-run patch 0033's two arms on the
current anchor before acting on it. (Trap #1.)

## D5 — 10 of the 29 "Speed 2 features" are not in the S1→S2 step at all. (measured)

They are already at their Speed-2 values at Speed 1, via the
`speed <= 1 && is_720p_or_larger` qindex-dependent block. Corrected the
unexplained share of the gap from 65% to 71%.

## D6 — TCQ's bypass of the trellis speed features is a correctness requirement. (settled)

`perform_coeff_opt` and `perform_coeff_opt_based_on_satd` are both ignored when
`tcq_enable()` is true. That is deliberate, not an oversight; do not "fix" it.
All 12 TCQ kernels have AVX2 and are dispatched, so the trellis cost is
algorithmic, not a SIMD gap.

## D7 — `xd->mv_refined` memset is real but has no clean fix. (investigated, parked)

`av2_build_inter_predictors` clears 8,192 bytes per call — 4.28% of retired
instructions, the single largest memset site. Narrowing it to the range the
caller initializes is **not safe**: `make_inter_pred_of_nxn` reads
`mv_refined[n_blocks * 2 + ref]`, one past that range, and another site indexes
at a TIP-specific offset. Would need the read paths fixed first.

## D8 — Patch 0027 landed upstream. (shipped)

`d4ad634`, "Skip trellis based on an estimated rdcost", 2026-09-10. Scoped to
speed ≥ 5 and 4K only. **A1 RA speed 5: +4.06% speedup / +0.04% BD ⇒ ratio
101.5** — the best ratio this project has produced in production.

## D9 — The landed gate's 4K-only scope is a symptom, not a preference. (analysed)

At Speed 3 it measured A1 42.1 (pass) / A2 20.2 (miss). The gate compares an
*untrellised* estimate against a *trellised* `best_rd`; the bias between them
is not constant across qindex, coefficient density, block size and content, so
one fixed `>> 3` pad cannot serve both classes. Patch 0035 replaces the
constant with a per-block self-calibrated `alpha`, measured from candidates the
gate already evaluates. See `ROUND_0915_SYNC_AND_IDEAS.md` §4.

## D10 — Local wall clock cannot resolve anything under ~3%. (settled)

Repeatedly demonstrated; the calibrated-gate paired timing gave −0.5% and +0.9%
with reps disagreeing in sign. Use callgrind instruction counts for sizing and
the cluster for deciding. Never quote a local wall-clock delta as a speedup.

## D11 — Three memory/transform leads investigated and closed. (negative results)

Recorded so they are not re-opened:

- **IST / secondary transform (~3.1% at Speed 2) is not wasted work.**
  `prune_sec_txfm_rd_eval` consumes `sec_tx_sse_to_be_coded`, which is produced
  by the `av2_xform` call that would be skipped. The transform computes its own
  pruning signal. Both IST pruning features are also already on at Speed 2
  (`prune_tx_rd_eval_sec_tx_sse` unconditionally for every preset,
  `prune_intra_ist_stx_by_zero_eob` under `speed >= 1`).
- **The inverse transform in the tx search is already guarded.**
  `get_tx_blk_distortion` routes to `dist_block_tx_domain` under
  `use_transform_domain_distortion`, and `prune_tx_type_rd_calc_using_tx_domain_dist`
  skips even that when an optimistic estimate already loses.
- **`MB_MODE_INFO` has no fat member to hoist out.** 632 bytes total; the
  largest member is `wm_params` at 88 (14%), then `palette_mode_info` at 50.
  Splitting the struct does not produce a quick win for the chroma copies.

## D12 — The chroma `MB_MODE_INFO` copy is the last item in the memset/memcpy seam, and the hardest. (open)

The struct copies dominate the 2.40% memcpy line arithmetically: ~3 x 632 bytes
against `num_4x4_blk_chroma` (pixels/16) bytes for the `av2_copy_array` calls.
A grep of `av2_rd_pick_intra_sbuv_mode` gives a short field list, but the
function calls into the RD machinery and the chroma palette search, which write
`mbmi` fields that never appear textually in it. **An incomplete field list
fails silently.** Needs a callee audit, a `sizeof` static assert to force
re-audit on struct changes, and a load-bearing sweep. Not worth doing before
patch 0035 has been answered.

## D13 — At speeds 0-3, intra blocks get no cost-based transform-type pruning. (measured)

`get_tx_mask()` picks between two regimes on `prune_tx_type_est_rd`:

```
if (num_allowed > 2 && prune_tx_type_est_rd)  prune_txk_type()   <- intra AND inter
else if (prune_2d_txfm_mode >= PRUNE_1 && is_inter && ...)  prune_tx_2D()  <- inter ONLY
```

Resolved values dumped from an instrumented build at the point of use:

| speed | `prune_tx_type_est_rd` | `prune_2d_txfm_mode` | `winner_mode_tx_type_pruning` |
|---|---|---|---|
| 0, 1 | 0 | 1 | 0 |
| 2, 3 | 0 | 2 | 0 |
| 4, 5 | 1 | 4 | 1 |
| 6 | 0 | 5 | 2 |

Three tx-type pruning mechanisms switch on together in the 3 -> 4 step. Because
the else branch requires `is_inter`, intra blocks at speeds 0-3 fall through
with no cost-based prune at all, and every survivor pays a full TCQ trellis.
Patch 0039 exposes the flag as an arm. Not free: `prune_txk_type` calls
`av2_cost_coeffs_txb_laplacian` (1.09% of instructions at speed 5), so it
trades estimate cost for trellis calls and will move BD-rate.

## D14 — The speed-feature setter's first call does not carry the configured preset. (measured, trap)

`set_good_speed_features_framesize_independent` is called more than once per
encode, and the **first call reports `speed=0` whatever `--cpu-used` says**. A
print-once diagnostic latches onto that call and reports the wrong preset --
the same "you measured the value you did not mean" failure that instrumentation
exists to prevent. Dump on change, and print `cpi->oxcf.speed` alongside the
setter's own parameter.

Related: the value of a field at the end of one setter is not its value at the
point of use. `prune_2d_txfm_mode` reads 2 at the end of the framesize-independent
setter at speed 4, but 4 at the use site in `tx_search.c`, because a later
setter in the chain raises it. **Always dump at the use site.**

## D15 — Callgrind instruction counts drift ~0.002% across container restarts. (measured)

Baseline on the same binary and clip: 176,252,929,353 before a container
restart, 176,249,501,877 after. 0.0019%. Far below any effect being measured,
but it means a baseline from a previous container should be re-measured rather
than reused when comparing arms.

## D16 — CORRECTION: patch 0038 was built on a misattributed profile line. (measured)

I attributed the 0.53% `__memset_avx2 via init_encode_rd_sb` line to
`x->picked_ref_frames_mask` and built patch 0038 to guard it. **That was wrong.**

```
sizeof(picked_ref_frames_mask)  =      32,768 bytes   <- what 0038 guards
sizeof(SimpleMotionDataBufs)    =  15,687,312 bytes   <- what dominates the line
```

`init_encode_rd_sb` also calls `av2_init_sms_data_bufs()`, which is a flat
`memset(data_bufs, 0, sizeof(*data_bufs))` over **15.0 MB**, 479x larger. The
third clear in that function, `reset_hash_records`, is two integer assignments.

Patch 0038 is correct and bit-exact (8/8) but removes ~0.2% of that line:
**measured -0.0010%**, against a container-restart drift of 0.0019% on the same
binary (D15). It is below the measurement floor. Downgraded to
"verified, negligible" -- carry it or drop it, but do not spend a CTC slot.

**The check that caught it, and the rule:** 32 KB x ~6 superblocks is ~6,000
instructions; the profile line was 1.65 *billion*. Those cannot be the same
thing. **Before attributing a profile line to a specific buffer, multiply
size x call count and confirm it reaches the observed magnitude.** A caller
name in a callgrind chain identifies the function, not which of its several
memsets is the expensive one.

## D17 — The SMS data buffer clear is the largest single memset in the encoder. (sized, unbuilt)

`av2_init_sms_data_bufs()` clears **15,687,312 bytes** (103,206 entries x 152)
per call, from `init_encode_rd_sb`, which has seven call sites in
`encodeframe.c` (multi-pass superblock encoding), so it runs many times per
superblock.

`av2_get_sms_data()` gates entirely on one field:

```c
SimpleMotionData *cur_block = av2_get_sms_data_entry(...);
if (!cur_block->valid) compute_sms_data(...);
```

So the clear's only job is `valid == 0` on every entry -- 4 bytes, not 152:

```
full clear   15,687,312 bytes
flags only      412,824 bytes     38x reduction
```

Same shape as patch 0034 (clear the bookkeeping, not the payload), which
measured -2.67%.

**Do not build this from the sizing alone** -- that is the mistake D16 records.
It needs: (a) an audit that `compute_sms_data` writes every field any consumer
later reads, since stale payload would otherwise leak between superblocks;
(b) a check that nothing reaches `av2_get_sms_data_entry` directly, bypassing
the `valid` gate; (c) the CONFIG_ML_PART_SPLIT `residual_stats_valid` flag
handled alongside `valid`.


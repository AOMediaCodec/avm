# Round 0916: one more patch, three leads closed, and what is left

Anchor `7368e76`. Follow-on to `ROUND_0915_SYNC_AND_IDEAS.md`.

---

## 0. I still cannot read your EDA report

You pointed me at
`avm-patches/blob/main/shared/eda_report_0915_claude_comprehensive.md`.
I tried four ways and all of them failed:

```
WebFetch raw.githubusercontent.com/...       HTTP 404
curl     raw.githubusercontent.com/...       HTTP 404
curl     api.github.com/repos/chengchen-...  HTTP 403
GitHub MCP get_file_contents                 "not configured for this session"
add_repo chengchen-google/avm-patches        "cross-tier adds are not supported
                                              in v1: session already has repos
                                              from owner(s) [aomediacodec]"
```

**So none of what follows is informed by your results.** I have not seen a
single BD-rate or encode-time number from the 0915 patches. The prioritisation
below is unchanged from what I would have said before your report existed,
which is exactly the thing your report was supposed to fix.

Two ways to unblock it, in order of how well they work:

1. **Paste the report contents into the chat.** Works immediately, no setup.
2. **Start a session with `chengchen-google/avm-patches` as the initial
   source.** The `add_repo` error says this explicitly — a session can only add
   repos from owners it already has, and this one is pinned to `aomediacodec`.
   A session started on `avm-patches` could then add `aomediacodec/avm`.

Until then I am guessing about what landed and what failed, and you should
weight everything below accordingly.

---

## 1. New patch `0038` — clear `picked_ref_frames_mask` only when it is used

`init_encode_rd_sb()` clears a **32,768-byte** array once per superblock:

```c
av2_zero(x->picked_ref_frames_mask);   // MAX_MIB_SIZE^2 uint64 = 64*64*8
```

Profile: `__memset_avx2` via `init_encode_rd_sb` is **0.53%** of retired
instructions at Speed 2.

Both of its accessors are behind the same speed feature, and the clear is not:

| | location | guard |
|---|---|---|
| writer | `av2_update_picked_ref_frames_mask`, `partition_search.c` | `cpi->sf.inter_sf.prune_ref_frames && tree_type != CHROMA_PART` |
| reader | `fetch_picked_ref_frames_mask`, `rdopt.c` | `inter_sf->prune_ref_frames && !x->inter_mode_cache[0]` |
| **clear** | `init_encode_rd_sb`, `encodeframe.c` | **none** |

With `prune_ref_frames == 0` the array is neither written nor read. That is not
a rare case — the flag initialises to 0, is set conditionally in two places, and
is forced back to 0 in two others.

Same shape as `0036` and `0037`: bit-exact by a "written iff read" argument,
one-line guard. The flag is resolved per frame, so the guard is re-evaluated per
superblock against the current frame's features, and nothing carries over from a
frame where the clear was skipped.

Small — per-superblock, not per-block. Carried for the same reason as `0036`:
it costs nothing and cannot fail the ratio bar.

---

## 2. Three leads I opened and closed

These are negative results. They are here so nobody spends a CTC slot on them.

### IST / secondary transform (~3.1% at Speed 2) — not wasted work

`av2_fwd_stxfm` is 2.18% plus `fwd_stxfm_avx2` at 0.91%, called from `av2_xform`
inside `search_tx_type`. The obvious idea is that the secondary transform is
computed for candidates that are then discarded by `prune_sec_txfm_rd_eval` a
few lines later.

**It is not wasted.** The pruning test consumes `sec_tx_sse_to_be_coded`, which
is *produced by* the very `av2_xform` call that would be skipped. The transform
computes its own pruning signal; you cannot prune before paying for it.

And both IST pruning features are already on at Speed 2 — `prune_tx_rd_eval_sec_tx_sse`
is set unconditionally for every preset including Speed 0, and
`prune_intra_ist_stx_by_zero_eob` is set under `speed >= 1`. There is no free
speed-feature lever here.

### Inverse transform in the tx search (0.81%) — already guarded

`inv_txfm_avx2` via `av2_inverse_transform_block`. The idea is that the encoder
inverse-transforms candidates only to compute pixel-domain distortion, and
should use transform-domain distortion instead.

**It already does.** `get_tx_blk_distortion` takes
`use_transform_domain_distortion` and routes to `dist_block_tx_domain` when set,
and there is a second gate, `prune_tx_type_rd_calc_using_tx_domain_dist`, that
skips even the transform-domain call when an optimistic estimate already loses.
The inverse transforms that remain are the ones genuinely needed.

### Shrinking `MB_MODE_INFO` — no fat member to hoist

Last round I proposed splitting `MB_MODE_INFO` so the chroma search's repeated
632-byte copies get cheaper. I measured the layout:

```
sizeof(MB_MODE_INFO) = 632
  wm_params           88      <- largest single member
  palette_mode_info   50
  mv                  16
  interinter_comp     16
  uv_intra_mode_list  14
  mapped_intra_mode   10
  ...
```

**There is no dominant member.** The largest is `wm_params` at 14% of the
struct; removing it entirely would cut the copy by a seventh. This is death by a
thousand fields, so the "split the struct" route does not produce a quick win.

---

## 3. The chroma memcpy, re-ranked downward with a concrete plan

Still **1.25% at Speed 2 / 2.40% at Speed 5**, still unbuilt, and now known to
be harder than I said last round, because both easy routes are closed (§2).

What the copies are, per call to `av2_rd_pick_intra_sbuv_mode`:

```c
MB_MODE_INFO best_mbmi = *mbmi;   // 632 B, once at entry
  best_mbmi = *mbmi;              // 632 B, per IMPROVING uv mode
*mbmi = best_mbmi;                // 632 B, once at exit
av2_copy_array(tmp_cctx_type_map, ..., ctx->num_4x4_blk_chroma);  // small
```

`num_4x4_blk_chroma` is pixels/16, so for the chroma block sizes this function
sees it is tens to a couple of hundred bytes against ~3 x 632. **The struct
copies dominate** — that answers the attribution question I left open last
round, arithmetically rather than by another profile run.

The remaining route is to save and restore only the fields the UV search
perturbs. A direct grep of the function gives a short list:

```
cfl_idx  uv_mode  uv_mode_idx  mh_dir  use_dpcm_uv  dpcm_mode_uv
angle_delta  uv_intra_mode_list  is_wide_angle
```

**Do not build from that list.** The function calls into the RD machinery and
the chroma palette search, which can write `mbmi` fields that never appear
textually in this function. An incomplete list fails *silently* — it produces a
wrong bitstream only on the configurations that touch the missed field, which is
the worst failure mode available.

What it actually needs, in order:

1. An audit of every callee reachable from the UV loop for `mbmi` writes, not a
   grep of the caller.
2. A `_Static_assert` on `sizeof(MB_MODE_INFO)` so that adding a member breaks
   the build and forces a re-audit — the same mitigation used in `0034`, which
   clears field by field rather than by span for exactly this reason.
3. A wide bit-exactness sweep treated as load-bearing evidence, as with `0037`.

That is a session's work for ~1-2%, against `0035` arm 2 which is a config-flag
change aimed at 40% of the encoder. **I would not do this before `0035` has been
answered.**

---

## 4. Where the memory-traffic seam stands

Round 0915 found ~10% of instructions in memset/memcpy and has now harvested
most of it:

| site | share | status |
|---|---|---|
| `mv_refined` via `av2_build_inter_predictors` | 4.28% | **`0037`, −4.24% measured** |
| `winner_mode_stats` via intra mode search | 2.45% | **`0034`, −2.67% measured** |
| chroma `MB_MODE_INFO` copies | 2.40% | open, §3 — hardest of the set |
| `avm_memset16` via intra predictors | 1.00% | **`0036`, −0.42% measured** |
| `picked_ref_frames_mask` via `init_encode_rd_sb` | 0.53% | **`0038`, this round** |

Bit-exact total so far: **−7.33%**, measured as a composed build, exactly
additive. With `0038` it should be a little over 7.5%.

The seam is close to exhausted. What is left in it is the chroma copy, which is
the one item in the whole set that cannot be done with a one-line guard.

---

## 5. Run order

Unchanged from last round except for `0038`, and still written without sight of
your results:

| priority | arm | why |
|---|---|---|
| 1 | `0034`+`0036`+`0037`+`0038` as one arm | bit-exact, measured, composes exactly; one run settles all four |
| 2 | `0035` arm 2 (`MIN_DIM=0 MIN_SPEED=2`) | ratio-101 mechanism at the preset where its target is ~40% of the encoder |
| 3 | `0035` arm 1 (`MIN_DIM=0`) | decisive test that calibration closed the A1/A2 gap; run with arm 2 |
| 4 | `0033` re-run | premise predates the `720e889` SMS retrain |

And, seventh time: **one anchor-vs-anchor arm** to price EncTime repeatability.

# AV2 encoder speed project — working memory

Read this file first. It is the only thing that survives a session.

**This ledger was deleted once already** (branch `claude/av2-encoder-speed-optimization-3ukgg3`
was removed from the remote between 2026-08-28 and 2026-09-15, taking patches
0001–0033, the architecture study, and the full registry with it). Rebuilt
2026-09-15 from conversation state. Keep a copy outside this repo.

---

## The bar

**Complexity-to-efficiency ratio = speedup% / BD-rate%.**

| preset | bar |
|---|---|
| Speed 1 | ≥ 35 |
| Speed 2 | ≥ 30 |
| Speed 3 | ≥ 25 |
| Speed 4 | ≥ 20 |

Must pass on **A1 (4K, 17 frames) and A2 (1080p, 33 frames) independently** —
an average that passes while one class fails does not count.

Computed against Speed 0: a preset at encode time `t`% of Speed 0 with BD-rate
`BD`% has ratio `(100 − t) / BD`.

### The two arithmetic facts that decide most proposals

1. **Ratio ceiling.** Max achievable ratio at a given BD-rate is `100 / BD`
   (the limit as encode time → 0). At BD = 4.0% that is 25 — *below* the
   Speed-2 bar of 30, at any encode time. Passing at 30 needs BD ≤ 2.88%.
2. **Marginal ratio.** A change improves an operating point only if its own
   marginal ratio `Δspeed / ΔBD` exceeds the ratio already there. Adding a
   ratio-20 change to a ratio-40 point makes it worse even though the change
   "saves time".

A **bit-exact** change has ΔBD = 0, so its ratio is unbounded. It cannot fail
any bar at any preset. Always prefer these.

---

## Measurement protocol

| tier | instrument | use for |
|---|---|---|
| 0 | `md5sum` of the `.obu` across ≥ 3 configs | proving bit-exactness — do this **first**, always |
| 1 | `valgrind --tool=callgrind` retired-instruction count (`summary:` line) | sizing a bit-exact change; deterministic to ≪ 0.1% |
| 2 | local wall clock | **do not trust** — noise floor here is several percent, reps have disagreed in sign |
| 3/4 | user's EDA cluster CTC | the only number that decides |

`perf` is unavailable in this container. Wall-clock differences below ~3%
locally are not measurable; never quote one as a speedup.

### Bit-exactness check
Vary **clip × preset × qindex**, at minimum 3 combinations, and include a
config that exercises the code path you touched. Same `.obu` md5 = exact.

---

## Traps that have actually cost this project rounds

1. **Stale tree.** Verify the working tree is at the *same commit the CTC
   anchor used* before writing a patch. Patch 0018 was a no-op because the
   code it targeted had moved. `git fetch origin av2-enc && git log --oneline -1 origin/av2-enc`.
2. **Five speed-feature setters, last writer wins.**
   `av2_set_speed_features_framesize_independent` →
   `av2_set_speed_features_framesize_dependent` (→ `av2_disable_ml_based_partition_sf`) →
   `av2_set_speed_features_qindex_dependent` (→ `set_erp_speed_features` →
   `set_erp_speed_features_framesize_dependent` → `set_erp_speed_features_qindex_dependent`
   → `set_two_pass_partition_level`).
   Editing an early block changes nothing if a later one rewrites the field.
   This is why the 101-feature study missed its largest item, and upstream
   `db16baa` confirms the same bug class independently. **Always verify a
   feature is actually off by dumping the resolved struct at frame time**
   (patch 0031's instrumentation), not by reading the setter you edited.
3. **Resolution gates.** `is_270p_or_lesser`, `is_720p_or_larger` etc. mean a
   speed-feature dump on a small test clip is not what CTC runs. Dump at
   1920x1080 and 3840x2160.
4. **Super-additive BD, sub-additive time.** Leave-one-out tests measure
   marginal damage with every other feature still ON; features that prune the
   same decision each leave the others an escape route. Measured 4× on the
   partition group (members sum to +0.47%, joint recovery 2.08%/1.69%).
   **To attribute a group, difference against the fully-reverted arm**, not
   against the baseline.
5. **Uniform vs partial approximation.** An approximation applied uniformly
   across a comparison set preserves ordering and is usually cheap in BD.
   Applied to only part of the set, it corrupts ordering. This is exactly why
   patch 0026 failed.
6. **Screen-content detection on synthetic clips.** A generated test clip can
   trip SCC detection, making `av2_is_dv_valid` / `av2_optimize_fsc_block` /
   IntraBC appear hot as artefacts. Set `AVM_FORCE_NO_SCC=1` (local override)
   or check for these lines before motivating work from a profile.
7. **Shell exit-status through a pipe.** `git push ... | tail -2` tests
   `tail`, not `push`. A 403 printed "PUSH_OK". Capture `rc=$?` from the
   command itself.

---

## Standing constraints

- **Never push to `AOMediaCodec/avm`.** Work lives on
  `claude/av2-encoder-speed-optimization-3ukgg3`.
- Never push to a different branch without explicit permission.
- Do not open pull requests unless explicitly asked.

---

## Open questions the user has not yet answered

- **Cluster EncTime repeatability is still unmeasured** (asked 5×). One
  anchor-vs-anchor CTC arm would price it. Several judgements turn on ~1%
  differences and nobody knows whether that is signal or noise.

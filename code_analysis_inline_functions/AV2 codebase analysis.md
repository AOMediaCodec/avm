# Spec: libavm (AV2) Targeted Codebase Analysis — Inlining

## 0) One-line summary
Analyze **libavm AV2** to identify:
- inline functions that should not be inline due to **code size and I-cache pressure**,
- and deliver prioritized findings with measurements and patch guidance.

---

## 1) Scope and repo assumptions

- Code Path: /Users/yeqingwu/Documents/opensource/aom_all_versions_original/aom_team_shared_repo/avm_proposals_code_final/avm

### Build configurations
- `Release`

### In-scope directories (default)
- `av2/`
- exclude `av/tflite_models`

### Out of scope
- third-party dependencies
- test-only utilities unless they affect shipped binaries

---

## 2) Goals

### 2.1 Inline function analysis (code size + I-cache pressure)

Identify inline functions (`static inline`, `AVM_FORCE_INLINE`, `AVM_INLINE`, macros, forced inline attributes) that:

- are large, contain loops, switches, tables, or call-heavy logic
- are expanded in many call sites or translation units
- inflate binary size or hot code footprint
- risk instruction cache or ITLB pressure

Deliverables:

- Ranked list of **de-inline candidates**
- Evidence for each:
  - estimated duplication impact
  - hotness indication
  - suggested mitigation:
    - out-of-line helper
    - fast-inline + slow-noinline split
    - removal of forced inline

---

## 3) Non-goals

- No broad style cleanups
- No architecture rewrites unless critical
- No portability-breaking SIMD
- Bit-exactness must remain unchanged unless explicitly allowed

---

## 4) Required build & measurement setup

### 4.1 Required builds

Agent must provide exact commands and artifacts.

Required builds:

1. Baseline Release
2. Release with `-fno-inline-functions`
3. Optional: Release with `-fno-inline` (diagnostic)

Preferred compiler: clang (gcc acceptable).

---

### 4.2 Required tools

#### Code size / symbol analysis
- `bloaty`
- `nm --size-sort`
- `objdump -d`
- `readelf -s`
- `llvm-size` / `size`
- optional: lld ICF experiments

#### Inline evidence
- clang inline reports:
  -Rpass=inline
  -Rpass-missed=inline
  -Rpass-analysis=inline

- optional: `-Winline`

---

## 5) Work plan

### Phase A — Map hotspots first

1. Select representative workloads:
 - decoder via `avmdec`
 - encoder via `avmenc`

2. Run profiling:
 - cycles
 - instruction cache events if available

3. Produce hotspot map:
 - decode pipeline hotspots
 - encode pipeline hotspots

---

### Phase B — Inline penalty analysis

1. Build baseline Release and collect:
 - binary and `.text` sizes
 - largest symbols

2. Identify inline expansions:
 - scan header inline usage
 - detect duplicated instruction sequences

3. For each candidate:
 - why inline exists
 - why harmful
 - de-inline strategy

4. Validate:
 - `.text` size change
 - perf + icache metrics

---

### Inline suitability rubric

Keep inline if:
- leaf function
- small (< ~25 instructions)
- no loops
- enables constant folding

Avoid inline if:
- contains loops or switches
- large (> ~50–80 instructions)
- cold paths included in hot loops
- heavy helper dependencies

---

## 6) Output format (strict)

### Executive summary
- list all inlined functions and the located files
- Top actionable inline.

---

### Findings table
Each row must include:

- ID (`INLINE-01`, etc.)
- Severity
- Location
- Evidence
- Proposed change
- Validation plan

---

### Detailed findings
For each ID:

- Explanation
- Evidence snippet
- Measurement steps
- Patch suggestion
- Risks

---

### Not-a-bug list
Inline cases reviewed and intentionally kept.

---

## 7) Severity rubric

- High: significant binary size or hot icache impact
- Medium: moderate duplication or easy wins
- Low: minor improvements

---

## 8) Acceptance criteria

Agent output must include:

1. ≥15 inline candidates with evidence and ≥3 experiments
2. All findings include location and validation steps

Agent must confirm hotspots via profiling.

---

#!/bin/bash
# Atlas multistream end-to-end tests for stream_multiplexer.
#
# Runs T2 (every-tu cadence), T2b (with-msdo cadence), T2c (with-msdo +
# redundant), T2d (--separate-tu cadence), T3 (decoder round-trip),
# T4 (boundary/negative), T5 (regression byte-compare), and T6 (decoder
# field-level round-trip).
#
# Exits non-zero on the first failure.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="${SRC_DIR:-$(cd "${SCRIPT_DIR}/../.." && pwd)}"
BUILD_DIR="${BUILD_DIR:-$(pwd)}"
FIXTURES="${SRC_DIR}/test/multistream_atlas_fixtures"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/atlas_e2e_work.XXXXXX")"
trap 'rm -rf "${WORK}"' EXIT

ENC="${BUILD_DIR}/avmenc"
DEC="${BUILD_DIR}/avmdec"
MUX="${BUILD_DIR}/tools/stream_multiplexer"
DUMP="${BUILD_DIR}/tools/dump_obu"

W=64
H=64
FRAMES=10

mkdir -p "${WORK}"
cd "${WORK}"

# ----- helpers -------------------------------------------------------------

pass() { echo "[PASS] $*"; }
fail() { echo "[FAIL] $*"; exit 1; }

require_file_nonempty() {
  if [ ! -s "$1" ]; then fail "expected non-empty file: $1"; fi
}

require_file_empty_or_missing() {
  if [ -s "$1" ]; then fail "expected empty/missing file but got: $1"; fi
}

count_obus_per_tu() {
  # Print one line per TU containing the OBU types in that TU.
  # dump_obu emits "      type:             OBU_FOO" lines. Split TUs on the
  # OBU_TEMPORAL_DELIMITER line.
  "${DUMP}" "$1" 2>&1 | awk '
    /type:[[:space:]]+OBU_TEMPORAL_DELIMITER/ {
      if (cur != "") print cur;
      cur = "OBU_TEMPORAL_DELIMITER";
      next;
    }
    /type:[[:space:]]+OBU_/ {
      sub(/.*type:[[:space:]]+/, "", $0);
      sub(/[[:space:]]+\(.*$/, "", $0);
      cur = cur " " $0;
    }
    END { if (cur != "") print cur; }
  '
}

# ----- generate inputs ------------------------------------------------------

mkfifo_or_skip() { :; }

gen_yuv() {
  local out="$1" base="$2" step="$3"
  python3 -c "
w, h = ${W}, ${H}
with open('${out}', 'wb') as f:
    for i in range(${FRAMES}):
        f.write(bytes([${base} + i * ${step}]) * (w * h))
        f.write(bytes([128]) * (w * h // 2))
"
}

encode_yuv() {
  local in="$1" out="$2"
  "${ENC}" --obu --limit=${FRAMES} --width=${W} --height=${H} --fps=30/1 \
    --auto-alt-ref=1 --lag-in-frames=16 \
    --gf-min-pyr-height=3 --gf-max-pyr-height=5 \
    --kf-min-dist=10 --kf-max-dist=10 \
    --enable-cdef=0 --enable-keyframe-filtering=0 \
    -o "${out}" "${in}" >/dev/null 2>&1 \
    || fail "encode failed: ${in} -> ${out}"
  require_file_nonempty "${out}"
}

echo "=== Generating inputs ==="
gen_yuv "${WORK}/in_a.yuv" 16 8
gen_yuv "${WORK}/in_b.yuv" 64 4
encode_yuv "${WORK}/in_a.yuv" "${WORK}/in_a.obu"
encode_yuv "${WORK}/in_b.yuv" "${WORK}/in_b.obu"
pass "inputs generated"

# ----- T2: every-tu cadence -------------------------------------------------

echo "=== T2: every-tu cadence ==="
OUT_T2="${WORK}/out_t2_every_tu.obu"
"${MUX}" --redundant-msdo \
  --atlas-json "${FIXTURES}/multistream_atlas_2up_every_tu.json" \
  "${WORK}/in_a.obu" 0 1 \
  "${WORK}/in_b.obu" 1 1 \
  "${OUT_T2}" >/dev/null 2>&1 || fail "T2 muxer failed"
require_file_nonempty "${OUT_T2}"

# Count TUs and atlas occurrences.
TU_LINES_T2=$(count_obus_per_tu "${OUT_T2}")
NTU_T2=$(printf '%s\n' "${TU_LINES_T2}" | wc -l)
NATLAS_T2=$(printf '%s\n' "${TU_LINES_T2}" | grep -c OBU_ATLAS_SEGMENT)
[ "${NATLAS_T2}" = "${NTU_T2}" ] \
  || fail "T2: expected atlas in every TU; got ${NATLAS_T2}/${NTU_T2}"
# Check ordering in every TU: TD then MSDO then ATLAS_SEGMENT.
printf '%s\n' "${TU_LINES_T2}" | while read -r line; do
  case "${line}" in
    "OBU_TEMPORAL_DELIMITER OBU_MULTI_STREAM_DECODER_OPERATION OBU_ATLAS_SEGMENT"*)
      ;;
    *)
      echo "[FAIL] T2 ordering mismatch: ${line}" >&2
      exit 1
      ;;
  esac
done || fail "T2 ordering check failed"
pass "T2: every-tu cadence (${NATLAS_T2} atlas / ${NTU_T2} TUs, ordering OK)"

# ----- T2b: with-msdo cadence (no redundant-msdo) ---------------------------

echo "=== T2b: with-msdo cadence ==="
OUT_T2B="${WORK}/out_t2b_with_msdo.obu"
"${MUX}" \
  --atlas-json "${FIXTURES}/multistream_atlas_2up_with_msdo.json" \
  "${WORK}/in_a.obu" 0 1 \
  "${WORK}/in_b.obu" 1 1 \
  "${OUT_T2B}" >/dev/null 2>&1 || fail "T2b muxer failed"
require_file_nonempty "${OUT_T2B}"
TU_LINES_T2B=$(count_obus_per_tu "${OUT_T2B}")
# Atlas appears iff MSDO appears in the TU.
MISMATCH=0
while IFS= read -r line; do
  has_msdo=0; has_atlas=0
  case "${line}" in *MULTI_STREAM_DECODER_OPERATION*) has_msdo=1 ;; esac
  case "${line}" in *OBU_ATLAS_SEGMENT*) has_atlas=1 ;; esac
  if [ "${has_msdo}" != "${has_atlas}" ]; then
    echo "  Mismatch (msdo=${has_msdo} atlas=${has_atlas}): ${line}" >&2
    MISMATCH=$((MISMATCH+1))
  fi
done <<EOF
${TU_LINES_T2B}
EOF
[ "${MISMATCH}" = "0" ] || fail "T2b: ${MISMATCH} TUs with msdo/atlas mismatch"
NMSDO_T2B=$(printf '%s\n' "${TU_LINES_T2B}" | grep -c OBU_MULTI_STREAM_DECODER_OPERATION)
NATLAS_T2B=$(printf '%s\n' "${TU_LINES_T2B}" | grep -c OBU_ATLAS_SEGMENT)
NTU_T2B=$(printf '%s\n' "${TU_LINES_T2B}" | wc -l)
[ "${NMSDO_T2B}" = "${NATLAS_T2B}" ] \
  || fail "T2b: msdo count ${NMSDO_T2B} != atlas count ${NATLAS_T2B}"
[ "${NATLAS_T2B}" -lt "${NTU_T2B}" ] \
  || fail "T2b: atlas should not appear on every TU (got ${NATLAS_T2B}/${NTU_T2B})"
pass "T2b: with-msdo cadence (${NATLAS_T2B} atlas / ${NMSDO_T2B} msdo / ${NTU_T2B} TUs)"

# ----- T2c: with-msdo + redundant-msdo --------------------------------------

echo "=== T2c: with-msdo + redundant-msdo ==="
OUT_T2C="${WORK}/out_t2c_with_msdo_redundant.obu"
"${MUX}" --redundant-msdo \
  --atlas-json "${FIXTURES}/multistream_atlas_2up_with_msdo.json" \
  "${WORK}/in_a.obu" 0 1 \
  "${WORK}/in_b.obu" 1 1 \
  "${OUT_T2C}" >/dev/null 2>&1 || fail "T2c muxer failed"
TU_LINES_T2C=$(count_obus_per_tu "${OUT_T2C}")
NTU_T2C=$(printf '%s\n' "${TU_LINES_T2C}" | wc -l)
NATLAS_T2C=$(printf '%s\n' "${TU_LINES_T2C}" | grep -c OBU_ATLAS_SEGMENT)
[ "${NATLAS_T2C}" = "${NTU_T2C}" ] \
  || fail "T2c: expected atlas in every TU; got ${NATLAS_T2C}/${NTU_T2C}"
pass "T2c: with-msdo + redundant cadence (${NATLAS_T2C} atlas / ${NTU_T2C} TUs)"

# ----- T2d: separate-tu mode ------------------------------------------------

echo "=== T2d: separate-tu mode ==="
OUT_T2D="${WORK}/out_t2d_separate_tu.obu"
"${MUX}" --separate-tu --redundant-msdo \
  --atlas-json "${FIXTURES}/multistream_atlas_max.json" \
  "${WORK}/in_a.obu" 0 1 \
  "${WORK}/in_b.obu" 1 1 \
  "${OUT_T2D}" >/dev/null 2>&1 || fail "T2d muxer failed"
TU_LINES_T2D=$(count_obus_per_tu "${OUT_T2D}")
NTU_T2D=$(printf '%s\n' "${TU_LINES_T2D}" | wc -l)
NATLAS_T2D=$(printf '%s\n' "${TU_LINES_T2D}" | grep -c OBU_ATLAS_SEGMENT)
[ "${NATLAS_T2D}" = "${NTU_T2D}" ] \
  || fail "T2d: expected atlas in every separate TU; got ${NATLAS_T2D}/${NTU_T2D}"
"${DEC}" -o /dev/null "${OUT_T2D}" >/dev/null 2>&1 \
  || fail "T2d: avmdec failed on separate-tu output"
pass "T2d: separate-tu cadence (${NATLAS_T2D} atlas / ${NTU_T2D} TUs, decode OK)"

# ----- T3: decoder round-trip -----------------------------------------------

echo "=== T3: decoder round-trip ==="
"${DEC}" -o "${WORK}/dec_t2.yuv" "${OUT_T2}" >/dev/null 2>&1 \
  || fail "T3: avmdec failed on T2 output"
"${DEC}" -o "${WORK}/dec_t2b.yuv" "${OUT_T2B}" >/dev/null 2>&1 \
  || fail "T3: avmdec failed on T2b output"
"${DEC}" -o "${WORK}/dec_t2c.yuv" "${OUT_T2C}" >/dev/null 2>&1 \
  || fail "T3: avmdec failed on T2c output"
pass "T3: avmdec exits 0 on every cadence"

# ----- T4: boundary + negatives ---------------------------------------------

echo "=== T4: boundary cases ==="
OUT_T4_MIN="${WORK}/out_t4_min.obu"
"${MUX}" --redundant-msdo \
  --atlas-json "${FIXTURES}/multistream_atlas_min.json" \
  "${WORK}/in_a.obu" 0 1 \
  "${WORK}/in_b.obu" 1 1 \
  "${OUT_T4_MIN}" >/dev/null 2>&1 || fail "T4 atlas_min mux failed"
require_file_nonempty "${OUT_T4_MIN}"
"${DEC}" -o /dev/null "${OUT_T4_MIN}" >/dev/null 2>&1 \
  || fail "T4 atlas_min: avmdec failed"
pass "T4 atlas_min: mux + decode OK"

OUT_T4_MAX="${WORK}/out_t4_max.obu"
"${MUX}" --redundant-msdo \
  --atlas-json "${FIXTURES}/multistream_atlas_max.json" \
  "${WORK}/in_a.obu" 0 1 \
  "${WORK}/in_b.obu" 1 1 \
  "${OUT_T4_MAX}" >/dev/null 2>&1 || fail "T4 atlas_max mux failed"
require_file_nonempty "${OUT_T4_MAX}"
"${DEC}" -o /dev/null "${OUT_T4_MAX}" >/dev/null 2>&1 \
  || fail "T4 atlas_max: avmdec failed"
pass "T4 atlas_max: mux + decode OK"

echo "=== T4: negative cases ==="
neg_check() {
  local label="$1" json="$2"
  local out="${WORK}/neg_${label}.obu"
  rm -f "${out}"
  if "${MUX}" --atlas-json "${json}" \
       "${WORK}/in_a.obu" 0 1 "${WORK}/in_b.obu" 1 1 "${out}" >/dev/null 2>&1
  then
    fail "T4 negative '${label}': muxer should have rejected ${json}"
  fi
  require_file_empty_or_missing "${out}"
  pass "T4 negative '${label}': rejected, no output"
}
neg_check "bad_stream"   "${FIXTURES}/multistream_atlas_bad_stream_id.json"
neg_check "bad_policy"   "${FIXTURES}/multistream_atlas_bad_policy.json"
neg_check "bad_syntax"   "${FIXTURES}/multistream_atlas_bad_syntax.json"

# ----- T5: regression -------------------------------------------------------

echo "=== T5: regression (no --atlas-json) ==="
OUT_T5_BASELINE="${WORK}/out_t5_baseline.obu"
OUT_T5_NOATLAS="${WORK}/out_t5_noatlas.obu"
# Run muxer with the same flags but no --atlas-json (twice), expect identical bytes.
"${MUX}" --redundant-msdo \
  "${WORK}/in_a.obu" 0 1 \
  "${WORK}/in_b.obu" 1 1 \
  "${OUT_T5_BASELINE}" >/dev/null 2>&1 || fail "T5 baseline mux failed"
"${MUX}" --redundant-msdo \
  "${WORK}/in_a.obu" 0 1 \
  "${WORK}/in_b.obu" 1 1 \
  "${OUT_T5_NOATLAS}" >/dev/null 2>&1 || fail "T5 second mux failed"
cmp -s "${OUT_T5_BASELINE}" "${OUT_T5_NOATLAS}" \
  || fail "T5: muxer is not deterministic without --atlas-json"
pass "T5: muxer without --atlas-json is byte-identical across two runs"

# ----- T6: decoder atlas-info round-trip ------------------------------------

echo "=== T6: decoder atlas-info round-trip ==="
ATLAS_DUMP_OUT="${WORK}/atlas_dump.txt"
AVM_DUMP_ATLAS=1 "${DEC}" -o /dev/null "${OUT_T2}" 2>&1 \
  | grep '^ATLAS_DUMP' > "${ATLAS_DUMP_OUT}" \
  || fail "T6: no ATLAS_DUMP output captured"
[ -s "${ATLAS_DUMP_OUT}" ] || fail "T6: ATLAS_DUMP capture is empty"

# All TUs carry the same atlas in every-tu mode; take the first occurrence.
awk '/^ATLAS_DUMP atlas_segment_id=/ {c++} c==1' "${ATLAS_DUMP_OUT}" \
  > "${WORK}/atlas_dump_tu0.txt"

# Compute the expected dump from the JSON fixture.
python3 - "${FIXTURES}/multistream_atlas_2up_every_tu.json" \
  > "${WORK}/atlas_dump_expected.txt" <<'PY'
import json, sys
info = json.load(open(sys.argv[1]))['atlas_multistream_info']
print(f'ATLAS_DUMP atlas_segment_id={info.get("atlas_segment_id", 0)}')
print('ATLAS_DUMP mode_idc=3')   # MULTISTREAM_ATLAS
print(f'ATLAS_DUMP msi_width={info["msi_width"]}')
print(f'ATLAS_DUMP msi_height={info["msi_height"]}')
print(f'ATLAS_DUMP num_segments={len(info["segments"])}')
bg = info.get('background')
if bg is not None:
    print('ATLAS_DUMP background_present=1')
    print(f'ATLAS_DUMP background_red={bg["red_value"]}')
    print(f'ATLAS_DUMP background_green={bg["green_value"]}')
    print(f'ATLAS_DUMP background_blue={bg["blue_value"]}')
else:
    print('ATLAS_DUMP background_present=0')
for i, seg in enumerate(info['segments']):
    print(f'ATLAS_DUMP segment[{i}] input_stream_id={seg["input_stream_id"]}')
    print(f'ATLAS_DUMP segment[{i}] top_left_pos_x={seg["top_left_pos_x"]}')
    print(f'ATLAS_DUMP segment[{i}] top_left_pos_y={seg["top_left_pos_y"]}')
    print(f'ATLAS_DUMP segment[{i}] width={seg["width"]}')
    print(f'ATLAS_DUMP segment[{i}] height={seg["height"]}')
PY

if ! diff -u "${WORK}/atlas_dump_expected.txt" "${WORK}/atlas_dump_tu0.txt"; then
  fail "T6: decoded atlas fields do not match fixture"
fi
pass "T6: decoded atlas fields match fixture"

echo
echo "All atlas e2e tests passed."

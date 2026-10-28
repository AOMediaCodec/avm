#!/bin/bash
# Annex D atlas-composition end-to-end tests for avmdec --compose-annex-d.
#
# Produces a multistream-atlas bitstream with stream_multiplexer (using the
# 2-up every-tu fixture: 128x64 canvas, two 64x64 segments for streams 0 and 1)
# and decodes it with avmdec --compose-annex-d, verifying:
#   D2 - one composited frame per TU, correct canvas size, correct per-segment
#        content (left half = stream 0, right half = stream 1).
#   D3 - decoder exits 0.
#   D4 - monotonic_output_order_flag==0 stream is rejected; --compose-annex-d with
#        --num-streams is rejected.
#   D5 - decoding the same stream WITHOUT --compose-annex-d still succeeds.
#
# Designed to run on the remote build server inside cmake-build-master.
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="${SRC_DIR:-$(cd "${SCRIPT_DIR}/../.." && pwd)}"
BUILD_DIR="${BUILD_DIR:-$(pwd)}"
FIXTURES="${SRC_DIR}/test/multistream_atlas_fixtures"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/atlas_compose_work.XXXXXX")"
trap 'rm -rf "${WORK}"' EXIT

ENC="${BUILD_DIR}/avmenc"
DEC="${BUILD_DIR}/avmdec"
MUX="${BUILD_DIR}/tools/stream_multiplexer"

W=64
H=64
FRAMES=10
CANVAS_W=128
CANVAS_H=64
COLOR0=80
COLOR1=160

mkdir -p "${WORK}"
cd "${WORK}" || exit 1

pass() { echo "[PASS] $*"; }
fail() { echo "[FAIL] $*"; exit 1; }

gen_yuv() {  # out base
  python3 -c "
w,h=${W},${H}
with open('$1','wb') as f:
  for i in range(${FRAMES}):
    f.write(bytes([$2])*(w*h)); f.write(bytes([128])*(w*h//2))
"
}

gen_yuv10() {  # out luma  (10-bit C420p10, little-endian 16-bit)
  python3 -c "
import struct
w,h=${W},${H}
y=struct.pack('<H',$2)*(w*h)
c=struct.pack('<H',512)*(w*h//2)
with open('$1','wb') as f:
  f.write(b'YUV4MPEG2 W%d H%d F30:1 Ip A1:1 C420p10\n'%(w,h))
  for i in range(${FRAMES}):
    f.write(b'FRAME\n'); f.write(y); f.write(c)
"
}

gen_mono() {  # out luma  (monochrome 8-bit Cmono)
  python3 -c "
w,h=${W},${H}
with open('$1','wb') as f:
  f.write(b'YUV4MPEG2 W%d H%d F30:1 Ip A1:1 Cmono\n'%(w,h))
  for i in range(${FRAMES}):
    f.write(b'FRAME\n'); f.write(bytes([$2])*(w*h))
"
}

enc_monotonic() {  # in out
  "${ENC}" --obu --limit=${FRAMES} --width=${W} --height=${H} --fps=30/1 \
    --enable-keyframe-filtering=0 --monotonic-output-order=1 \
    --auto-alt-ref=0 --lag-in-frames=0 --kf-max-dist=10 --enable-cdef=0 \
    -o "$2" "$1" >/dev/null 2>&1 || fail "monotonic encode failed: $1"
}

enc_nonmonotonic() {  # in out
  "${ENC}" --obu --limit=${FRAMES} --width=${W} --height=${H} --fps=30/1 \
    --auto-alt-ref=1 --lag-in-frames=16 --gf-min-pyr-height=3 \
    --gf-max-pyr-height=5 --kf-max-dist=10 --enable-cdef=0 \
    --enable-keyframe-filtering=0 \
    -o "$2" "$1" >/dev/null 2>&1 || fail "non-monotonic encode failed: $1"
}

enc_monotonic_y4m() {  # in.y4m out  (avmenc reads dimensions/format from Y4M)
  "${ENC}" --obu --limit=${FRAMES} --fps=30/1 \
    --enable-keyframe-filtering=0 --monotonic-output-order=1 \
    --auto-alt-ref=0 --lag-in-frames=0 --kf-max-dist=10 --enable-cdef=0 \
    -o "$2" "$1" >/dev/null 2>&1 || fail "y4m monotonic encode failed: $1"
}

enc_mono_y4m() {  # in.y4m out  (monochrome 4:0:0)
  "${ENC}" --obu --limit=${FRAMES} --fps=30/1 --monochrome \
    --enable-keyframe-filtering=0 --monotonic-output-order=1 \
    --auto-alt-ref=0 --lag-in-frames=0 --kf-max-dist=10 --enable-cdef=0 \
    -o "$2" "$1" >/dev/null 2>&1 || fail "mono monotonic encode failed: $1"
}

echo "=== Generating + encoding monotonic inputs ==="
gen_yuv "${WORK}/in0.yuv" ${COLOR0}
gen_yuv "${WORK}/in1.yuv" ${COLOR1}
enc_monotonic "${WORK}/in0.yuv" "${WORK}/in0.obu"
enc_monotonic "${WORK}/in1.yuv" "${WORK}/in1.obu"
pass "monotonic inputs encoded"

echo "=== Muxing 2-up every-tu atlas ==="
OUT="${WORK}/atlas.obu"
"${MUX}" --redundant-msdo \
  --atlas-json "${FIXTURES}/multistream_atlas_2up_every_tu.json" \
  "${WORK}/in0.obu" 0 1 "${WORK}/in1.obu" 1 1 "${OUT}" >/dev/null 2>&1 \
  || fail "muxing failed"
[ -s "${OUT}" ] || fail "muxed output empty"
pass "atlas bitstream muxed"

echo "=== D2/D3: decode with --compose-annex-d ==="
COMP="${WORK}/composite.yuv"
"${DEC}" --compose-annex-d --rawvideo -o "${COMP}" "${OUT}" >/dev/null 2>&1 \
  || fail "D3: avmdec --compose-annex-d exited non-zero"
[ -s "${COMP}" ] || fail "D2: composite output empty"

# 420 8-bit frame size for the canvas.
FRAME_SZ=$(( CANVAS_W * CANVAS_H * 3 / 2 ))
FILE_SZ=$(stat -c%s "${COMP}")
NFRAMES=$(( FILE_SZ / FRAME_SZ ))
[ "${NFRAMES}" = "${FRAMES}" ] \
  || fail "D2: expected ${FRAMES} composite frames, got ${NFRAMES} (file ${FILE_SZ}, frame ${FRAME_SZ})"
[ $(( FILE_SZ % FRAME_SZ )) -eq 0 ] \
  || fail "D2: composite size ${FILE_SZ} not a multiple of frame size ${FRAME_SZ} (wrong canvas dims?)"

# Verify left half ~ COLOR0, right half ~ COLOR1 (luma center of each segment).
python3 - "${COMP}" ${CANVAS_W} ${CANVAS_H} ${COLOR0} ${COLOR1} <<'PY' || fail "D2: per-segment content mismatch"
import sys
path,W,H,c0,c1=sys.argv[1],int(sys.argv[2]),int(sys.argv[3]),int(sys.argv[4]),int(sys.argv[5])
fsz=W*H*3//2
data=open(path,'rb').read()
n=len(data)//fsz
tol=10
for f in range(n):
    y=data[f*fsz:f*fsz+W*H]
    left=y[32*W+32]      # (x=32,y=32) in stream-0 segment
    right=y[32*W+96]     # (x=96,y=32) in stream-1 segment
    if abs(left-c0)>tol:  sys.exit(f"frame {f}: left {left} != ~{c0}")
    if abs(right-c1)>tol: sys.exit(f"frame {f}: right {right} != ~{c1}")
print("content OK")
PY
pass "D2/D3: ${NFRAMES} composites, ${CANVAS_W}x${CANVAS_H}, segment content correct"

echo "=== D4: --compose-annex-d + --num-streams rejected ==="
if "${DEC}" --compose-annex-d --num-streams=2 --rawvideo -o "${WORK}/x.yuv" \
     "${OUT}" >/dev/null 2>&1; then
  fail "D4: --compose-annex-d with --num-streams should be rejected"
fi
pass "D4: --compose-annex-d + --num-streams rejected"

echo "=== D4: non-monotonic stream rejected ==="
gen_yuv "${WORK}/nm0.yuv" ${COLOR0}
gen_yuv "${WORK}/nm1.yuv" ${COLOR1}
enc_nonmonotonic "${WORK}/nm0.yuv" "${WORK}/nm0.obu"
enc_nonmonotonic "${WORK}/nm1.yuv" "${WORK}/nm1.obu"
NMOUT="${WORK}/atlas_nm.obu"
"${MUX}" --redundant-msdo \
  --atlas-json "${FIXTURES}/multistream_atlas_2up_every_tu.json" \
  "${WORK}/nm0.obu" 0 1 "${WORK}/nm1.obu" 1 1 "${NMOUT}" >/dev/null 2>&1 \
  || fail "non-monotonic muxing failed"
if "${DEC}" --compose-annex-d --rawvideo -o "${WORK}/nm.yuv" \
     "${NMOUT}" >/dev/null 2>&1; then
  fail "D4: non-monotonic stream should be rejected by --compose-annex-d"
fi
pass "D4: non-monotonic stream rejected"

echo "=== D5: decode WITHOUT --compose-annex-d still works ==="
"${DEC}" --rawvideo -o "${WORK}/plain.yuv" "${OUT}" \
  >/dev/null 2>&1 || fail "D5: plain decode failed"
[ -s "${WORK}/plain.yuv" ] || fail "D5: plain decode produced no output"
pass "D5: plain decode without --compose-annex-d OK"

echo "=== D6: atlas signaled only in the first TU (not re-signaled) ==="
# emit_policy "with_msdo" + NO --redundant-msdo => MSDO (and the atlas) appear
# only on the key-frame TU, i.e. the atlas OBU is signaled exactly once. The
# decoder must persist that atlas (carried at GLOBAL_XLAYER_ID) across every
# subsequent TU and still composite each one.
ONCE="${WORK}/atlas_once.obu"
"${MUX}" --atlas-json "${FIXTURES}/multistream_atlas_2up_with_msdo.json" \
  "${WORK}/in0.obu" 0 1 "${WORK}/in1.obu" 1 1 "${ONCE}" >/dev/null 2>&1 \
  || fail "D6: muxing (not re-signaled) failed"
[ -s "${ONCE}" ] || fail "D6: muxed output empty"
NATLAS=$("${BUILD_DIR}/tools/dump_obu" "${ONCE}" 2>&1 | grep -c "OBU_ATLAS_SEGMENT")
[ "${NATLAS}" = "1" ] \
  || fail "D6: expected exactly 1 atlas OBU in the stream, got ${NATLAS}"

COMP1="${WORK}/composite_once.yuv"
"${DEC}" --compose-annex-d --rawvideo -o "${COMP1}" "${ONCE}" >/dev/null 2>&1 \
  || fail "D6: avmdec --compose-annex-d failed on the not-re-signaled stream"
[ -s "${COMP1}" ] || fail "D6: composite output empty"
FRAME_SZ=$(( CANVAS_W * CANVAS_H * 3 / 2 ))
FILE_SZ=$(stat -c%s "${COMP1}")
NFRAMES=$(( FILE_SZ / FRAME_SZ ))
[ $(( FILE_SZ % FRAME_SZ )) -eq 0 ] \
  || fail "D6: composite size ${FILE_SZ} not a multiple of canvas frame size ${FRAME_SZ} (atlas not persisted across TUs?)"
[ "${NFRAMES}" = "${FRAMES}" ] \
  || fail "D6: expected ${FRAMES} composite frames (atlas persisted for every TU), got ${NFRAMES}"
# Every TU must composite correctly even though the atlas was signaled only once.
python3 - "${COMP1}" ${CANVAS_W} ${CANVAS_H} ${COLOR0} ${COLOR1} <<'PY' || fail "D6: per-segment content mismatch"
import sys
path,W,H,c0,c1=sys.argv[1],int(sys.argv[2]),int(sys.argv[3]),int(sys.argv[4]),int(sys.argv[5])
fsz=W*H*3//2
data=open(path,'rb').read()
n=len(data)//fsz
tol=10
for f in range(n):
    y=data[f*fsz:f*fsz+W*H]
    left=y[32*W+32]      # (x=32,y=32) in stream-0 segment
    right=y[32*W+96]     # (x=96,y=32) in stream-1 segment
    if abs(left-c0)>tol:  sys.exit(f"frame {f}: left {left} != ~{c0}")
    if abs(right-c1)>tol: sys.exit(f"frame {f}: right {right} != ~{c1}")
print("content OK")
PY
pass "D6: once-signaled atlas persists; all ${NFRAMES} TUs composited (${CANVAS_W}x${CANVAS_H}, content correct)"

echo "=== D7: MULTISTREAM_ALPHA_ATLAS, alpha disabled -> composites ==="
# Mode-4 atlas with ats_msi_alpha_segments_present_flag = 0 has no alpha
# segments, so it composites exactly like a plain multistream atlas.
ALPHA_OFF="${WORK}/atlas_alpha_off.obu"
"${MUX}" --redundant-msdo \
  --atlas-json "${FIXTURES}/multistream_atlas_2up_alpha_off.json" \
  "${WORK}/in0.obu" 0 1 "${WORK}/in1.obu" 1 1 "${ALPHA_OFF}" >/dev/null 2>&1 \
  || fail "D7: muxing alpha-off failed"
[ -s "${ALPHA_OFF}" ] || fail "D7: muxed output empty"
# Confirm the emitted atlas is mode 4 (MULTISTREAM_ALPHA_ATLAS).
MODE=$(AVM_DUMP_ATLAS=1 "${DEC}" --compose-annex-d -o /dev/null "${ALPHA_OFF}" 2>/dev/null \
  | grep -m1 "ATLAS_DUMP mode_idc" | sed -E "s/.*mode_idc=//")
[ "${MODE}" = "4" ] || fail "D7: expected mode_idc=4, got '${MODE}'"
COMP7="${WORK}/composite_alpha_off.yuv"
"${DEC}" --compose-annex-d --rawvideo -o "${COMP7}" "${ALPHA_OFF}" >/dev/null 2>&1 \
  || fail "D7: avmdec --compose-annex-d failed on alpha-off stream"
FRAME_SZ=$(( CANVAS_W * CANVAS_H * 3 / 2 ))
FILE_SZ=$(stat -c%s "${COMP7}")
[ $(( FILE_SZ % FRAME_SZ )) -eq 0 ] \
  || fail "D7: composite size ${FILE_SZ} not a multiple of frame size ${FRAME_SZ}"
NFRAMES=$(( FILE_SZ / FRAME_SZ ))
[ "${NFRAMES}" = "${FRAMES}" ] \
  || fail "D7: expected ${FRAMES} composite frames, got ${NFRAMES}"
python3 - "${COMP7}" ${CANVAS_W} ${CANVAS_H} ${COLOR0} ${COLOR1} <<'PY' || fail "D7: per-segment content mismatch"
import sys
path,W,H,c0,c1=sys.argv[1],int(sys.argv[2]),int(sys.argv[3]),int(sys.argv[4]),int(sys.argv[5])
fsz=W*H*3//2
data=open(path,'rb').read()
n=len(data)//fsz
tol=10
for f in range(n):
    y=data[f*fsz:f*fsz+W*H]
    if abs(y[32*W+32]-c0)>tol: sys.exit(f"frame {f}: left {y[32*W+32]} != ~{c0}")
    if abs(y[32*W+96]-c1)>tol: sys.exit(f"frame {f}: right {y[32*W+96]} != ~{c1}")
print("content OK")
PY
pass "D7: alpha-disabled mode-4 atlas composites ${NFRAMES} TUs (${CANVAS_W}x${CANVAS_H}, content correct)"

echo "=== D8: MULTISTREAM_ALPHA_ATLAS, alpha enabled -> composites ==="
# An alpha-enabled atlas is now composited (alpha blending implemented). Here the
# alpha and texture segments are side-by-side, so the texture is opaque (the
# alpha matte does not overlap it); it must still decode/composite cleanly.
ALPHA_ON="${WORK}/atlas_alpha_on.obu"
"${MUX}" --redundant-msdo \
  --atlas-json "${FIXTURES}/multistream_atlas_2up_alpha_on.json" \
  "${WORK}/in0.obu" 0 1 "${WORK}/in1.obu" 1 1 "${ALPHA_ON}" >/dev/null 2>&1 \
  || fail "D8: muxing alpha-on failed"
[ -s "${ALPHA_ON}" ] || fail "D8: muxed output empty"
"${DEC}" --compose-annex-d --rawvideo -o "${WORK}/alpha_on.yuv" \
     "${ALPHA_ON}" >/dev/null 2>&1 \
  || fail "D8: alpha-enabled atlas should now composite"
ON_SZ=$(stat -c%s "${WORK}/alpha_on.yuv")
[ $(( ON_SZ % ((CANVAS_W*CANVAS_H*3)/2) )) -eq 0 ] \
  || fail "D8: composite size ${ON_SZ} not a multiple of ${CANVAS_W}x${CANVAS_H} frame"
pass "D8: alpha-enabled mode-4 atlas composites (${CANVAS_W}x${CANVAS_H})"

echo "=== D9: MULTISTREAM_ALPHA_ATLAS alpha blending ==="
# Co-located alpha matte + texture (64x64). Constant half alpha -> the composite
# is the per-pixel blend of texture over the (limited-black) background.
TEXY=160; ALPHAV=128; BGY=16
gen_yuv "${WORK}/abtex.yuv" ${TEXY}
gen_yuv "${WORK}/abalpha.yuv" ${ALPHAV}
enc_monotonic "${WORK}/abtex.yuv" "${WORK}/abtex.obu"
enc_monotonic "${WORK}/abalpha.yuv" "${WORK}/abalpha.obu"
ABOUT="${WORK}/atlas_alpha_blend.obu"
# segment 0 (id 0) = alpha matte, segment 1 (id 1) = texture
"${MUX}" --atlas-json "${FIXTURES}/multistream_atlas_alpha_blend.json" \
  "${WORK}/abalpha.obu" 0 1 "${WORK}/abtex.obu" 1 1 "${ABOUT}" >/dev/null 2>&1 \
  || fail "D9: muxing alpha-blend failed"
ABCOMP="${WORK}/composite_alpha_blend.yuv"
"${DEC}" --compose-annex-d --rawvideo -o "${ABCOMP}" "${ABOUT}" >/dev/null 2>&1 \
  || fail "D9: avmdec --compose-annex-d failed on alpha-blend stream"
FRAME_SZ=$(( W * H * 3 / 2 ))
FILE_SZ=$(stat -c%s "${ABCOMP}")
[ $(( FILE_SZ % FRAME_SZ )) -eq 0 ] || fail "D9: composite size ${FILE_SZ} not a multiple of ${W}x${H}"
python3 - "${ABCOMP}" ${W} ${H} ${BGY} ${ALPHAV} ${TEXY} <<'PY' || fail "D9: alpha-blend value mismatch"
import sys
path,W,H,bg,a,tex=(sys.argv[1],)+tuple(int(x) for x in sys.argv[2:7])
fsz=W*H*3//2
d=open(path,'rb').read()
n=len(d)//fsz
am=1<<8
exp=((am-a)*bg + a*tex + (1<<7))>>8   # Round2((alphaMax-a)*bg + a*tex, 8)
tol=12
for f in range(n):
    y=d[f*fsz:f*fsz+W*H]
    c=y[(H//2)*W + W//2]   # center luma
    if abs(c-exp)>tol: sys.exit(f"frame {f}: center luma {c} != ~{exp} (bg={bg} a={a} tex={tex})")
print(f"{n} frames, center luma ~= {exp} (blend of bg={bg}, tex={tex} @ alpha={a}/256)")
PY
pass "D9: alpha blending composites correctly (${W}x${H}, center ~= blended value)"

echo "=== D10: mixed bit-depth textures (8-bit + 10-bit) ==="
# seg0 stream = 8-bit texture, seg1 stream = 10-bit texture. The composite must
# be 10-bit (max), with the 8-bit segment up-scaled (<<2).
gen_yuv "${WORK}/md8.yuv" 100
gen_yuv10 "${WORK}/md10.y4m" 600
enc_monotonic "${WORK}/md8.yuv" "${WORK}/md8.obu"
enc_monotonic_y4m "${WORK}/md10.y4m" "${WORK}/md10.obu"
MDOUT="${WORK}/atlas_mixeddepth.obu"
"${MUX}" --atlas-json "${FIXTURES}/multistream_atlas_2up_every_tu.json" \
  "${WORK}/md8.obu" 0 1 "${WORK}/md10.obu" 1 1 "${MDOUT}" >/dev/null 2>&1 \
  || fail "D10: muxing mixed-depth failed"
MDY4M="${WORK}/composite_mixeddepth.y4m"
"${DEC}" --compose-annex-d -o "${MDY4M}" "${MDOUT}" >/dev/null 2>&1 \
  || fail "D10: avmdec --compose-annex-d failed on mixed-depth stream"
sed -n 1p "${MDY4M}" | grep -q "C420p10" \
  || fail "D10: composite is not 10-bit ($(sed -n 1p "${MDY4M}"))"
MDCOMP="${WORK}/composite_mixeddepth.yuv"
"${DEC}" --compose-annex-d --rawvideo -o "${MDCOMP}" "${MDOUT}" >/dev/null 2>&1 \
  || fail "D10: rawvideo decode failed"
python3 - "${MDCOMP}" ${CANVAS_W} ${CANVAS_H} ${FRAMES} <<'PY' || fail "D10: mixed-depth value mismatch"
import sys, struct
path,W,H,F=(sys.argv[1],)+tuple(int(x) for x in sys.argv[2:5])
fsz=W*H*3                      # 10-bit 4:2:0 = W*H*1.5*2
d=open(path,'rb').read()
n=len(d)//fsz
if n!=F: sys.exit(f"expected {F} frames, got {n} (size {len(d)}, frame {fsz})")
def Y(f,x,y): return struct.unpack_from('<H', d, f*fsz + (y*W+x)*2)[0]
tol=12
for f in range(n):
    l=Y(f,32,32)   # seg0 (8-bit -> up-scaled 100<<2=400)
    r=Y(f,96,32)   # seg1 (10-bit native 600)
    if abs(l-(100<<2))>tol: sys.exit(f"frame {f}: left {l} != ~400")
    if abs(r-600)>tol: sys.exit(f"frame {f}: right {r} != ~600")
print(f"{n} frames; left~=400 (8-bit upconverted), right~=600 (10-bit native)")
PY
pass "D10: mixed bit-depth composites at 10-bit (8-bit segment up-scaled)"

# Note: mixed chroma-format rejection is covered by the unit test
# AtlasComposeTest.MixedChromaErrors (av2_annex_d_compose_tu returns -1 for a
# monochrome texture against a color canvas). An e2e here is unreliable because
# the per-stream monochrome flag on muxed layers is not always carried into the
# decoded buffer used by the compositor.

echo "=== D11: film-grain multistream composition (12 segments) ==="
cat > "${WORK}/fgs.tbl" <<'FGS'
filmgrn1
E 0 9223372036854775807 1 7391 1
  p 0 0 1 8 8 0 0 0 0 0 0 0
  sY 2  0 40  255 40
  sCb 0
  sCr 0
  cY
  cCb 0
  cCr 0
FGS
"${ENC}" --obu --limit=3 --width=${W} --height=${H} --fps=30/1 \
  --enable-keyframe-filtering=0 --monotonic-output-order=1 \
  --auto-alt-ref=0 --lag-in-frames=0 --kf-max-dist=10 --enable-cdef=0 \
  --film-grain-table="${WORK}/fgs.tbl" \
  -o "${WORK}/fgs0.obu" "${WORK}/in0.yuv" >/dev/null 2>&1 \
  || fail "D11: film-grain encode 0 failed"
"${ENC}" --obu --limit=3 --width=${W} --height=${H} --fps=30/1 \
  --enable-keyframe-filtering=0 --monotonic-output-order=1 \
  --auto-alt-ref=0 --lag-in-frames=0 --kf-max-dist=10 --enable-cdef=0 \
  --film-grain-table="${WORK}/fgs.tbl" \
  -o "${WORK}/fgs1.obu" "${WORK}/in1.yuv" >/dev/null 2>&1 \
  || fail "D11: film-grain encode 1 failed"
python3 - "${WORK}/fgs12.json" <<'PY'
import json, sys
doc = {
  "atlas_multistream_info": {
    "atlas_segment_id": 0,
    "atlas_segment_mode_idc": "MULTISTREAM_ATLAS",
    "msi_width": 256,
    "msi_height": 192,
    "segments": [
      {
        "input_stream_id": i % 2,
        "top_left_pos_x": (i % 4) * 64,
        "top_left_pos_y": (i // 4) * 64,
        "width": 64,
        "height": 64,
      }
      for i in range(12)
    ],
  },
}
with open(sys.argv[1], "w") as f:
    json.dump(doc, f)
PY
"${MUX}" --atlas-json "${WORK}/fgs12.json" \
  "${WORK}/fgs0.obu" 0 1 "${WORK}/fgs1.obu" 1 1 "${WORK}/fgs12.obu" >/dev/null 2>&1 \
  || fail "D11: muxing 12-segment FGS atlas failed"
"${DEC}" --compose-annex-d --rawvideo -o "${WORK}/fgs12.yuv" "${WORK}/fgs12.obu" >/dev/null 2>&1 \
  || fail "D11: avmdec --compose-annex-d failed on 12-segment FGS stream"
FGS_SZ=$(stat -c%s "${WORK}/fgs12.yuv")
[ "${FGS_SZ}" = "$(( 256 * 192 * 3 / 2 * 3 ))" ] \
  || fail "D11: expected $(( 256 * 192 * 3 / 2 * 3 )) bytes, got ${FGS_SZ}"
pass "D11: 12-segment film-grain multistream composition OK (${FGS_SZ} bytes)"

echo
echo "All atlas-composition e2e tests passed."

#!/bin/sh
##
## Multistream Tests
##
## multistream_mux_tests:
##   Test 1.1: Multiplex two streams (stream_id 0,6), verify per-stream ordering
##             (basic two-stream multiplexing)
##   Test 1.2: Multiplex two streams (stream_id 7,1), verify ordering
##             (same as Test 1.1 with different stream_ids)
##   Test 1.3: Multiplex two streams (stream_id 0,6) with redundant MSDO, verify identical to Test 1.1
##             (tests decoder handles redundant MSDO OBUs)
##   Test 1.4: Concatenate the Test 1.1 multistream with itself, verify output is Test 1.1 x 2
##             (tests CVS boundary handling within a stream_id)
##   Test 1.5: Concatenate Test 1.1 multistream with Test 1.2 multistream, verify pixel-exact
##             (tests CVS boundary with different stream_id sets)
##
## multistream_cmvs_tests:
##   Test 2.1: Concatenate two singlestreams, verify pixel-exact match
##             (tests CVS boundary handling)
##   Test 2.2: Multiplex a 20-frame singlestream with the Test 2.1 concatenated stream (stream_id 0,6), verify ordering
##             (tests muxing a singlestream with a multi-CVS singlestream)
##   Test 2.3: Concatenate 20-frame singlestream with Test 1.1-style multistream (stream_id 0,6), verify strict order
##             (tests singlestream followed by multistream)
##   Test 2.4: Concatenate 20-frame singlestream with Test 1.2-style multistream (stream_id 7,1), verify strict order
##             (same as Test 2.3 with different stream_ids)
##   Test 2.5: Concatenate Test 1.1-style multistream (stream_id 0,6) with 20-frame singlestream
##             (DISABLED: known DOH bug — multistream followed by singlestream)
##   Test 2.6: Concatenate Test 1.2-style multistream (stream_id 7,1) with 20-frame singlestream
##             (DISABLED: known DOH bug — same as Test 2.5 with different stream_ids)
##
## multistream_three_and_four_stream_tests:
##   Test 3.1: Multiplex three streams (stream_id 12,5,2), verify per-stream ordering
##             (tests multiplexing with three sub-streams)
##   Test 3.2: Multiplex four streams (stream_id 3,8,4,9), verify per-stream ordering
##             (tests multiplexing with four sub-streams)
##   Test 3.3: Concatenate the Test 3.1 multistream with Test 3.2 multistream, verify strict order
##             (tests CMVS boundary between two different multistreams)
##   Test 3.4: Concatenate the Test 3.2 multistream with Test 3.1 multistream, verify strict order
##             (same as Test 3.3 with reversed order)
##
. $(dirname $0)/tools_common.sh

multistream_tests_verify_environment() {
  if [ -z "$(avm_tool_path avmenc)" ]; then
    elog "avmenc not found."
    return 1
  fi
  if [ -z "$(avm_tool_path avmdec)" ]; then
    elog "avmdec not found."
    return 1
  fi
  if [ -z "$(avm_tool_path stream_multiplexer)" ]; then
    elog "stream_multiplexer not found."
    return 1
  fi
}

multistream_cmvs_tests() {
  local encoder="$(avm_tool_path avmenc)"
  local decoder="$(avm_tool_path avmdec)"
  local muxer="$(avm_tool_path stream_multiplexer)"
  local w=64
  local h=64
  local frames=10
  local frames20=20
  local frame_size=$((w * h * 3 / 2))

  local input_a="${AVM_TEST_OUTPUT_DIR}/cs_input_a.yuv"
  local input_b="${AVM_TEST_OUTPUT_DIR}/cs_input_b.yuv"
  local input_c="${AVM_TEST_OUTPUT_DIR}/cs_input_c.yuv"
  local bs_a="${AVM_TEST_OUTPUT_DIR}/cs_stream_a.bin"
  local bs_b="${AVM_TEST_OUTPUT_DIR}/cs_stream_b.bin"
  local bs_c="${AVM_TEST_OUTPUT_DIR}/cs_stream_c.bin"
  local bs_concat_ab="${AVM_TEST_OUTPUT_DIR}/cs_concat_ab.bin"
  local bs_mux2="${AVM_TEST_OUTPUT_DIR}/cs_mux2.bin"
  local bs_mux2a="${AVM_TEST_OUTPUT_DIR}/cs_mux2a.bin"
  local dec_a="${AVM_TEST_OUTPUT_DIR}/cs_decoded_a.yuv"
  local dec_b="${AVM_TEST_OUTPUT_DIR}/cs_decoded_b.yuv"
  local dec_c="${AVM_TEST_OUTPUT_DIR}/cs_decoded_c.yuv"
  local dec_concat_ab="${AVM_TEST_OUTPUT_DIR}/cs_decoded_concat_ab.yuv"
  local dec_mux2="${AVM_TEST_OUTPUT_DIR}/cs_decoded_mux2.yuv"
  local dec_mux2a="${AVM_TEST_OUTPUT_DIR}/cs_decoded_mux2a.yuv"

  # Generate inputs: A,B are 10 frames, C is 20 frames
  vlog "  Generating synthetic YUV inputs..."
  python3 -c "
w, h = ${w}, ${h}
for path, n, base, step in [('${input_a}', ${frames}, 16, 8), ('${input_b}', ${frames}, 20, 8), ('${input_c}', ${frames20}, 100, 4)]:
    with open(path, 'wb') as f:
        for i in range(n):
            f.write(bytes([base + i * step]) * (w * h))
            f.write(bytes([128]) * (w * h // 2))
" || return 1

  # Encode A, B (10 frames each), C (20 frames, TIP disabled)
  vlog "  Encoding sequence A (${frames} frames)..."
  "${encoder}" \
    --obu --limit=${frames} --width=${w} --height=${h} --fps=30/1 \
    --auto-alt-ref=1 --lag-in-frames=16 \
    --gf-min-pyr-height=3 --gf-max-pyr-height=5 \
    --cpu-used=8 -o "${bs_a}" "${input_a}" > /dev/null 2>&1 || return 1

  vlog "  Encoding sequence B (${frames} frames)..."
  "${encoder}" \
    --obu --limit=${frames} --width=${w} --height=${h} --fps=30/1 \
    --auto-alt-ref=1 --lag-in-frames=16 \
    --gf-min-pyr-height=3 --gf-max-pyr-height=5 \
    --cpu-used=8 -o "${bs_b}" "${input_b}" > /dev/null 2>&1 || return 1

  vlog "  Encoding sequence C (${frames20} frames, TIP disabled)..."
  "${encoder}" \
    --obu --limit=${frames20} --width=${w} --height=${h} --fps=30/1 \
    --auto-alt-ref=1 --lag-in-frames=16 \
    --gf-min-pyr-height=3 --gf-max-pyr-height=5 \
    --cpu-used=8 --enable-tip=0 -o "${bs_c}" "${input_c}" > /dev/null 2>&1 || return 1

  # Prepare shared artifacts
  vlog "  Concatenating A+B..."
  cat "${bs_a}" "${bs_b}" > "${bs_concat_ab}" || return 1

  vlog "  Multiplexing A+B (stream_id 0,6)..."
  "${muxer}" "${bs_a}" 0 1 "${bs_b}" 6 1 "${bs_mux2}" > /dev/null 2>&1 || return 1

  vlog "  Multiplexing A+B (stream_id 7,1)..."
  "${muxer}" "${bs_a}" 7 1 "${bs_b}" 1 1 "${bs_mux2a}" > /dev/null 2>&1 || return 1

  # Decode shared references
  vlog "  Decoding stream A..."
  "${decoder}" --codec=av2 -o "${dec_a}" "${bs_a}" > /dev/null 2>&1 || return 1
  vlog "  Decoding stream B..."
  "${decoder}" --codec=av2 -o "${dec_b}" "${bs_b}" > /dev/null 2>&1 || return 1
  vlog "  Decoding stream C..."
  "${decoder}" --codec=av2 -o "${dec_c}" "${bs_c}" > /dev/null 2>&1 || return 1
  vlog "  Decoding concat(A+B)..."
  "${decoder}" --codec=av2 -o "${dec_concat_ab}" "${bs_concat_ab}" > /dev/null 2>&1 || return 1
  vlog "  Decoding mux2..."
  "${decoder}" --codec=av2 -o "${dec_mux2}" "${bs_mux2}" > /dev/null 2>&1 || return 1
  vlog "  Decoding mux2a..."
  "${decoder}" --codec=av2 -o "${dec_mux2a}" "${bs_mux2a}" > /dev/null 2>&1 || return 1

  # --- Test 2.1: Verify concat(A+B) == dec_A + dec_B ---
  vlog "  [Test 2.1] Verifying concatenation pixel-exact match..."
  python3 -c "
def extract_frames(filename, frame_size):
    with open(filename, 'rb') as f:
        f.readline()
        frames = []
        while True:
            line = f.readline()
            if not line: break
            if line.startswith(b'FRAME'): frames.append(f.read(frame_size))
        return frames

frame_size = ${frame_size}
a = extract_frames('${dec_a}', frame_size)
b = extract_frames('${dec_b}', frame_size)
c = extract_frames('${dec_concat_ab}', frame_size)

assert len(c) == len(a) + len(b), 'Frame count mismatch: %d != %d + %d' % (len(c), len(a), len(b))
assert len(a) == len(set(a)), 'Stream A has duplicate frames'
assert len(b) == len(set(b)), 'Stream B has duplicate frames'
assert b''.join(c) == b''.join(a) + b''.join(b), 'Pixel data mismatch'
print('Test 2.1 passed: %d + %d = %d frames, pixel-exact match' % (len(a), len(b), len(c)))
" || return 1

  # --- Test 2.2: Mux C with concat(A+B), verify per-stream ordering ---
  local bs_t5="${AVM_TEST_OUTPUT_DIR}/cs_t5_muxed.bin"
  local dec_t5="${AVM_TEST_OUTPUT_DIR}/cs_t5_decoded.yuv"

  vlog "  [Test 2.2] Multiplexing C with concat(A+B)..."
  "${muxer}" "${bs_c}" 0 1 "${bs_concat_ab}" 6 1 "${bs_t5}" > /dev/null 2>&1 || return 1
  vlog "  [Test 2.2] Decoding multiplexed bitstream..."
  "${decoder}" --codec=av2 -o "${dec_t5}" "${bs_t5}" > /dev/null 2>&1 || return 1
  vlog "  [Test 2.2] Verifying per-stream ordering..."
  python3 -c "
def extract_frames(f, fs):
    with open(f, 'rb') as fh:
        fh.readline()
        frames = []
        while True:
            l = fh.readline()
            if not l: break
            if l.startswith(b'FRAME'): frames.append(fh.read(fs))
        return frames

def check_sub(muxed, sf):
    it = iter(muxed)
    for f in sf:
        while True:
            m = next(it, None)
            if m is None: return False
            if m == f: break
    return True

fs = ${frame_size}
muxed = extract_frames('${dec_t5}', fs)
c = extract_frames('${dec_c}', fs)
ab = extract_frames('${dec_concat_ab}', fs)
assert len(muxed) == len(c) + len(ab), 'Frame count mismatch: %d != %d + %d' % (len(muxed), len(c), len(ab))
assert sorted(muxed) == sorted(c + ab), 'Frame sets differ'
assert check_sub(muxed, c), 'Stream C not in correct order'
assert check_sub(muxed, ab), 'Concat(A+B) not in correct order'
print('Test 2.2 passed: %d + %d = %d frames, per-stream ordering correct' % (len(c), len(ab), len(muxed)))
" || return 1

  # --- Test 2.3: cat(C, mux2) -> verify strict concat order ---
  local bs_t6="${AVM_TEST_OUTPUT_DIR}/cs_t6_concat.bin"
  local dec_t6="${AVM_TEST_OUTPUT_DIR}/cs_t6_decoded.yuv"

  vlog "  [Test 2.3] Concatenating C with mux2..."
  cat "${bs_c}" "${bs_mux2}" > "${bs_t6}" || return 1
  vlog "  [Test 2.3] Decoding..."
  "${decoder}" --codec=av2 -o "${dec_t6}" "${bs_t6}" > /dev/null 2>&1 || return 1
  vlog "  [Test 2.3] Verifying strict concatenation order..."
  python3 -c "
def extract_pixels(f, fs):
    with open(f, 'rb') as fh:
        fh.readline()
        p = bytearray()
        while True:
            l = fh.readline()
            if not l: break
            if l.startswith(b'FRAME'): p.extend(fh.read(fs))
        return bytes(p)

fs = ${frame_size}
d = extract_pixels('${dec_t6}', fs)
c = extract_pixels('${dec_c}', fs)
m2 = extract_pixels('${dec_mux2}', fs)
assert d == c + m2, 'Test 2.3: pixel mismatch (len %d != %d + %d)' % (len(d), len(c), len(m2))
print('Test 2.3 passed: cat(C, mux2) strict concatenation order correct')
" || return 1

  # --- Test 2.4: cat(C, mux2a) -> verify strict concat order ---
  local bs_t6a="${AVM_TEST_OUTPUT_DIR}/cs_t6a_concat.bin"
  local dec_t6a="${AVM_TEST_OUTPUT_DIR}/cs_t6a_decoded.yuv"

  vlog "  [Test 2.4] Concatenating C with mux2a..."
  cat "${bs_c}" "${bs_mux2a}" > "${bs_t6a}" || return 1
  vlog "  [Test 2.4] Decoding..."
  "${decoder}" --codec=av2 -o "${dec_t6a}" "${bs_t6a}" > /dev/null 2>&1 || return 1
  vlog "  [Test 2.4] Verifying strict concatenation order..."
  python3 -c "
def extract_pixels(f, fs):
    with open(f, 'rb') as fh:
        fh.readline()
        p = bytearray()
        while True:
            l = fh.readline()
            if not l: break
            if l.startswith(b'FRAME'): p.extend(fh.read(fs))
        return bytes(p)

fs = ${frame_size}
d = extract_pixels('${dec_t6a}', fs)
c = extract_pixels('${dec_c}', fs)
m2a = extract_pixels('${dec_mux2a}', fs)
assert d == c + m2a, 'Test 2.4: pixel mismatch'
print('Test 2.4 passed: cat(C, mux2a) strict concatenation order correct')
" || return 1

  # --- Test 2.5: DISABLED - known DOH bug when muxed precedes multi-GOP stream ---
  # To re-enable, change ENABLE_TEST_2_5 to 1
  ENABLE_TEST_2_5=0
  if [ "${ENABLE_TEST_2_5}" = "1" ]; then
    local bs_t7="${AVM_TEST_OUTPUT_DIR}/cs_t7_concat.bin"
    local dec_t7="${AVM_TEST_OUTPUT_DIR}/cs_t7_decoded.yuv"

    vlog "  [Test 2.5] Concatenating mux2 with C..."
    cat "${bs_mux2}" "${bs_c}" > "${bs_t7}" || return 1
    vlog "  [Test 2.5] Decoding..."
    "${decoder}" --codec=av2 -o "${dec_t7}" "${bs_t7}" > /dev/null 2>&1 || return 1
    vlog "  [Test 2.5] Verifying strict concatenation order..."
    python3 -c "
def extract_pixels(f, fs):
    with open(f, 'rb') as fh:
        fh.readline()
        p = bytearray()
        while True:
            l = fh.readline()
            if not l: break
            if l.startswith(b'FRAME'): p.extend(fh.read(fs))
        return bytes(p)

fs = ${frame_size}
d = extract_pixels('${dec_t7}', fs)
m2 = extract_pixels('${dec_mux2}', fs)
c = extract_pixels('${dec_c}', fs)
assert d == m2 + c, 'Test 2.5: pixel mismatch'
print('Test 2.5 passed: cat(mux2, C) strict concatenation order correct')
" || return 1
  else
    vlog "  [Test 2.5] SKIPPED: known DOH bug"
  fi

  # --- Test 2.6: DISABLED - same DOH bug as Test 2.5 ---
  ENABLE_TEST_2_6=0
  if [ "${ENABLE_TEST_2_6}" = "1" ]; then
    local bs_t7a="${AVM_TEST_OUTPUT_DIR}/cs_t7a_concat.bin"
    local dec_t7a="${AVM_TEST_OUTPUT_DIR}/cs_t7a_decoded.yuv"

    vlog "  [Test 2.6] Concatenating mux2a with C..."
    cat "${bs_mux2a}" "${bs_c}" > "${bs_t7a}" || return 1
    vlog "  [Test 2.6] Decoding..."
    "${decoder}" --codec=av2 -o "${dec_t7a}" "${bs_t7a}" > /dev/null 2>&1 || return 1
    vlog "  [Test 2.6] Verifying strict concatenation order..."
    python3 -c "
def extract_pixels(f, fs):
    with open(f, 'rb') as fh:
        fh.readline()
        p = bytearray()
        while True:
            l = fh.readline()
            if not l: break
            if l.startswith(b'FRAME'): p.extend(fh.read(fs))
        return bytes(p)

fs = ${frame_size}
d = extract_pixels('${dec_t7a}', fs)
m2a = extract_pixels('${dec_mux2a}', fs)
c = extract_pixels('${dec_c}', fs)
assert d == m2a + c, 'Test 2.6: pixel mismatch'
print('Test 2.6 passed: cat(mux2a, C) strict concatenation order correct')
" || return 1
  else
    vlog "  [Test 2.6] SKIPPED: known DOH bug"
  fi
}

multistream_mux_tests() {
  local encoder="$(avm_tool_path avmenc)"
  local decoder="$(avm_tool_path avmdec)"
  local muxer="$(avm_tool_path stream_multiplexer)"
  local w=64
  local h=64
  local frames=10
  local frame_size=$((w * h * 3 / 2))

  local input_a="${AVM_TEST_OUTPUT_DIR}/mux_input_a.yuv"
  local input_b="${AVM_TEST_OUTPUT_DIR}/mux_input_b.yuv"
  local bs_a="${AVM_TEST_OUTPUT_DIR}/mux_stream_a.bin"
  local bs_b="${AVM_TEST_OUTPUT_DIR}/mux_stream_b.bin"
  local dec_a="${AVM_TEST_OUTPUT_DIR}/mux_decoded_a.yuv"
  local dec_b="${AVM_TEST_OUTPUT_DIR}/mux_decoded_b.yuv"

  # Shared encode step
  vlog "  Generating synthetic YUV inputs (${w}x${h}, ${frames} frames)..."
  python3 -c "
w, h = ${w}, ${h}
for path, base, step in [('${input_a}', 24, 8), ('${input_b}', 28, 8)]:
    with open(path, 'wb') as f:
        for i in range(${frames}):
            f.write(bytes([base + i * step]) * (w * h))
            f.write(bytes([128]) * (w * h // 2))
" || return 1

  vlog "  Encoding sequence A..."
  "${encoder}" \
    --obu --limit=${frames} --width=${w} --height=${h} --fps=30/1 \
    --auto-alt-ref=1 --lag-in-frames=16 \
    --gf-min-pyr-height=3 --gf-max-pyr-height=5 \
    --cpu-used=8 -o "${bs_a}" "${input_a}" > /dev/null 2>&1 || return 1

  vlog "  Encoding sequence B..."
  "${encoder}" \
    --obu --limit=${frames} --width=${w} --height=${h} --fps=30/1 \
    --auto-alt-ref=1 --lag-in-frames=16 \
    --gf-min-pyr-height=3 --gf-max-pyr-height=5 \
    --cpu-used=8 -o "${bs_b}" "${input_b}" > /dev/null 2>&1 || return 1

  vlog "  Decoding stream A..."
  "${decoder}" --codec=av2 -o "${dec_a}" "${bs_a}" > /dev/null 2>&1 || return 1
  vlog "  Decoding stream B..."
  "${decoder}" --codec=av2 -o "${dec_b}" "${bs_b}" > /dev/null 2>&1 || return 1

  # --- Test 1.1: Mux with stream_id 0 and 6 ---
  local bs_mux2="${AVM_TEST_OUTPUT_DIR}/mux2_muxed.bin"
  local dec_mux2="${AVM_TEST_OUTPUT_DIR}/mux2_decoded.yuv"

  vlog "  [Test 1.1] Multiplexing streams (stream_id 0 and 6)..."
  "${muxer}" "${bs_a}" 0 1 "${bs_b}" 6 1 "${bs_mux2}" > /dev/null 2>&1 || return 1
  vlog "  [Test 1.1] Decoding multiplexed bitstream..."
  "${decoder}" --codec=av2 -o "${dec_mux2}" "${bs_mux2}" > /dev/null 2>&1 || return 1
  vlog "  [Test 1.1] Verifying per-stream ordering..."
  python3 -c "
def extract_frames(filename, frame_size):
    with open(filename, 'rb') as f:
        f.readline()
        frames = []
        while True:
            line = f.readline()
            if not line:
                break
            if line.startswith(b'FRAME'):
                frames.append(f.read(frame_size))
        return frames

def check_subsequence(muxed, stream_frames):
    it = iter(muxed)
    for frame in stream_frames:
        while True:
            m = next(it, None)
            if m is None:
                return False
            if m == frame:
                break
    return True

frame_size = ${frame_size}
muxed = extract_frames('${dec_mux2}', frame_size)
a = extract_frames('${dec_a}', frame_size)
b = extract_frames('${dec_b}', frame_size)

assert len(muxed) == len(a) + len(b), 'Frame count mismatch'
assert len(a) == len(set(a)), 'Stream A has duplicate frames'
assert len(b) == len(set(b)), 'Stream B has duplicate frames'
assert sorted(muxed) == sorted(a + b), 'Frame sets differ'
assert check_subsequence(muxed, a), 'Stream A not in correct order'
assert check_subsequence(muxed, b), 'Stream B not in correct order'
print('Test 1.1 passed: %d + %d = %d frames, per-stream ordering correct' % (len(a), len(b), len(muxed)))
" || return 1

  # --- Test 1.2: Mux with stream_id 7 and 1 ---
  local bs_mux2a="${AVM_TEST_OUTPUT_DIR}/mux2a_muxed.bin"
  local dec_mux2a="${AVM_TEST_OUTPUT_DIR}/mux2a_decoded.yuv"

  vlog "  [Test 1.2] Multiplexing streams (stream_id 7 and 1)..."
  "${muxer}" "${bs_a}" 7 1 "${bs_b}" 1 1 "${bs_mux2a}" > /dev/null 2>&1 || return 1
  vlog "  [Test 1.2] Decoding multiplexed bitstream..."
  "${decoder}" --codec=av2 -o "${dec_mux2a}" "${bs_mux2a}" > /dev/null 2>&1 || return 1
  vlog "  [Test 1.2] Verifying per-stream ordering..."
  python3 -c "
def extract_frames(filename, frame_size):
    with open(filename, 'rb') as f:
        f.readline()
        frames = []
        while True:
            line = f.readline()
            if not line:
                break
            if line.startswith(b'FRAME'):
                frames.append(f.read(frame_size))
        return frames

def check_subsequence(muxed, stream_frames):
    it = iter(muxed)
    for frame in stream_frames:
        while True:
            m = next(it, None)
            if m is None:
                return False
            if m == frame:
                break
    return True

frame_size = ${frame_size}
muxed = extract_frames('${dec_mux2a}', frame_size)
a = extract_frames('${dec_a}', frame_size)
b = extract_frames('${dec_b}', frame_size)

assert len(muxed) == len(a) + len(b), 'Frame count mismatch'
assert len(a) == len(set(a)), 'Stream A has duplicate frames'
assert len(b) == len(set(b)), 'Stream B has duplicate frames'
assert sorted(muxed) == sorted(a + b), 'Frame sets differ'
assert check_subsequence(muxed, a), 'Stream A not in correct order'
assert check_subsequence(muxed, b), 'Stream B not in correct order'
print('Test 1.2 passed: %d + %d = %d frames, per-stream ordering correct' % (len(a), len(b), len(muxed)))
" || return 1

  # --- Test 1.3: Mux with --redundant-msdo, verify identical to Test 1.1 ---
  local bs_mux3="${AVM_TEST_OUTPUT_DIR}/mux3_muxed.bin"
  local dec_mux3="${AVM_TEST_OUTPUT_DIR}/mux3_decoded.yuv"

  vlog "  [Test 1.3] Multiplexing with --redundant-msdo..."
  "${muxer}" --redundant-msdo "${bs_a}" 0 1 "${bs_b}" 6 1 "${bs_mux3}" > /dev/null 2>&1 || return 1
  vlog "  [Test 1.3] Decoding multiplexed bitstream..."
  "${decoder}" --codec=av2 -o "${dec_mux3}" "${bs_mux3}" > /dev/null 2>&1 || return 1
  vlog "  [Test 1.3] Verifying pixel-identical to Test 1.1..."
  python3 -c "
def extract_pixels(filename, frame_size):
    with open(filename, 'rb') as f:
        f.readline()
        pixels = bytearray()
        while True:
            line = f.readline()
            if not line:
                break
            if line.startswith(b'FRAME'):
                pixels.extend(f.read(frame_size))
        return bytes(pixels)

frame_size = ${frame_size}
t2 = extract_pixels('${dec_mux2}', frame_size)
t3 = extract_pixels('${dec_mux3}', frame_size)
assert t2 == t3, 'Redundant MSDO output differs from Test 1.1 (len %d vs %d)' % (len(t3), len(t2))
print('Test 1.3 passed: redundant MSDO output pixel-identical to Test 1.1')
" || return 1

  # --- Test 1.4: Concatenate muxed with itself, verify == Test 1.1 x 2 ---
  local bs_cat4="${AVM_TEST_OUTPUT_DIR}/mux4_concat.bin"
  local dec_cat4="${AVM_TEST_OUTPUT_DIR}/mux4_decoded.yuv"

  vlog "  [Test 1.4] Concatenating Test 1.1 muxed with itself..."
  cat "${bs_mux2}" "${bs_mux2}" > "${bs_cat4}" || return 1
  vlog "  [Test 1.4] Decoding concatenated bitstream..."
  "${decoder}" --codec=av2 -o "${dec_cat4}" "${bs_cat4}" > /dev/null 2>&1 || return 1
  vlog "  [Test 1.4] Verifying pixel data == Test 1.1 x 2..."
  python3 -c "
def extract_pixels(filename, frame_size):
    with open(filename, 'rb') as f:
        f.readline()
        pixels = bytearray()
        while True:
            line = f.readline()
            if not line:
                break
            if line.startswith(b'FRAME'):
                pixels.extend(f.read(frame_size))
        return bytes(pixels)

frame_size = ${frame_size}
t2 = extract_pixels('${dec_mux2}', frame_size)
t4 = extract_pixels('${dec_cat4}', frame_size)
assert t4 == t2 + t2, 'Pixel mismatch: expected Test 1.1 x 2 (len %d vs %d)' % (len(t4), len(t2) * 2)
print('Test 1.4 passed: concatenated muxed output == Test 1.1 x 2')
" || return 1

  # --- Test 1.5: Concatenate Test 1.1 muxed + Test 1.2 muxed, verify pixels ---
  local bs_cat4a="${AVM_TEST_OUTPUT_DIR}/mux4a_concat.bin"
  local dec_cat4a="${AVM_TEST_OUTPUT_DIR}/mux4a_decoded.yuv"

  vlog "  [Test 1.5] Concatenating Test 1.1 muxed with Test 1.2 muxed..."
  cat "${bs_mux2}" "${bs_mux2a}" > "${bs_cat4a}" || return 1
  vlog "  [Test 1.5] Decoding concatenated bitstream..."
  "${decoder}" --codec=av2 -o "${dec_cat4a}" "${bs_cat4a}" > /dev/null 2>&1 || return 1
  vlog "  [Test 1.5] Verifying pixel data == Test 1.1 + Test 1.2..."
  python3 -c "
def extract_pixels(filename, frame_size):
    with open(filename, 'rb') as f:
        f.readline()
        pixels = bytearray()
        while True:
            line = f.readline()
            if not line:
                break
            if line.startswith(b'FRAME'):
                pixels.extend(f.read(frame_size))
        return bytes(pixels)

frame_size = ${frame_size}
t2 = extract_pixels('${dec_mux2}', frame_size)
t2a = extract_pixels('${dec_mux2a}', frame_size)
t4a = extract_pixels('${dec_cat4a}', frame_size)
assert t4a == t2 + t2a, 'Pixel mismatch: expected Test 1.1 + Test 1.2 (len %d vs %d)' % (len(t4a), len(t2) + len(t2a))
print('Test 1.5 passed: concatenated output == Test 1.1 + Test 1.2')
" || return 1
}

multistream_three_and_four_stream_tests() {
  local encoder="$(avm_tool_path avmenc)"
  local decoder="$(avm_tool_path avmdec)"
  local muxer="$(avm_tool_path stream_multiplexer)"
  local w=64
  local h=64
  local frames=10
  local frame_size=$((w * h * 3 / 2))
  local enc_common="--obu --limit=${frames} --width=${w} --height=${h} --fps=30/1 --auto-alt-ref=1 --lag-in-frames=16 --gf-min-pyr-height=3 --gf-max-pyr-height=5 --cpu-used=8"

  # Generate 7 inputs: 3 for Test 3.1, 4 for Test 3.2
  vlog "  Generating synthetic YUV inputs (7 streams)..."
  python3 -c "
w, h, n = ${w}, ${h}, ${frames}
specs = [
    ('${AVM_TEST_OUTPUT_DIR}/mm_8a.yuv', 32, 8),
    ('${AVM_TEST_OUTPUT_DIR}/mm_8b.yuv', 36, 8),
    ('${AVM_TEST_OUTPUT_DIR}/mm_8c.yuv', 40, 8),
    ('${AVM_TEST_OUTPUT_DIR}/mm_9a.yuv', 130, 4),
    ('${AVM_TEST_OUTPUT_DIR}/mm_9b.yuv', 132, 4),
    ('${AVM_TEST_OUTPUT_DIR}/mm_9c.yuv', 134, 4),
    ('${AVM_TEST_OUTPUT_DIR}/mm_9d.yuv', 136, 4),
]
for path, base, step in specs:
    with open(path, 'wb') as f:
        for i in range(n):
            f.write(bytes([base + i * step]) * (w * h))
            f.write(bytes([128]) * (w * h // 2))
" || return 1

  # Encode all 7 streams
  vlog "  Encoding 7 streams..."
  local s
  for s in 8a 8b 8c 9a 9b 9c 9d; do
    "${encoder}" ${enc_common} \
      -o "${AVM_TEST_OUTPUT_DIR}/mm_${s}.bin" \
      "${AVM_TEST_OUTPUT_DIR}/mm_${s}.yuv" > /dev/null 2>&1 || return 1
  done

  # --- Test 3.1: Mux 3 streams (id 12, 5, 2) ---
  local bs_mux8="${AVM_TEST_OUTPUT_DIR}/mm_mux8.bin"
  local dec_mux8="${AVM_TEST_OUTPUT_DIR}/mm_dec_mux8.yuv"

  vlog "  [Test 3.1] Multiplexing 3 streams (stream_id 12, 5, 2)..."
  "${muxer}" \
    "${AVM_TEST_OUTPUT_DIR}/mm_8a.bin" 12 1 \
    "${AVM_TEST_OUTPUT_DIR}/mm_8b.bin" 5 1 \
    "${AVM_TEST_OUTPUT_DIR}/mm_8c.bin" 2 1 \
    "${bs_mux8}" > /dev/null 2>&1 || return 1

  vlog "  [Test 3.1] Decoding muxed and individual streams..."
  "${decoder}" --codec=av2 -o "${dec_mux8}" "${bs_mux8}" > /dev/null 2>&1 || return 1
  for s in 8a 8b 8c; do
    "${decoder}" --codec=av2 \
      -o "${AVM_TEST_OUTPUT_DIR}/mm_dec_${s}.yuv" \
      "${AVM_TEST_OUTPUT_DIR}/mm_${s}.bin" > /dev/null 2>&1 || return 1
  done

  vlog "  [Test 3.1] Verifying per-stream ordering..."
  python3 -c "
def extract_frames(f, fs):
    with open(f, 'rb') as fh:
        fh.readline()
        frames = []
        while True:
            l = fh.readline()
            if not l: break
            if l.startswith(b'FRAME'): frames.append(fh.read(fs))
        return frames

def check_sub(muxed, sf):
    it = iter(muxed)
    for f in sf:
        while True:
            m = next(it, None)
            if m is None: return False
            if m == f: break
    return True

fs = ${frame_size}
d = '${AVM_TEST_OUTPUT_DIR}'
muxed = extract_frames('${dec_mux8}', fs)
streams = [extract_frames(d + '/mm_dec_%s.yuv' % s, fs) for s in ['8a', '8b', '8c']]
for i, s in enumerate(streams):
    assert len(s) == len(set(s)), 'Stream %d has duplicate frames' % i
assert sorted(muxed) == sorted(sum(streams, [])), 'Frame sets differ'
for i, s in enumerate(streams):
    assert check_sub(muxed, s), 'Stream %d not in correct order' % i
print('Test 3.1 passed: 3-stream mux, %d frames, per-stream ordering correct' % len(muxed))
" || return 1

  # --- Test 3.2: Mux 4 streams (id 3, 8, 4, 9) ---
  local bs_mux9="${AVM_TEST_OUTPUT_DIR}/mm_mux9.bin"
  local dec_mux9="${AVM_TEST_OUTPUT_DIR}/mm_dec_mux9.yuv"

  vlog "  [Test 3.2] Multiplexing 4 streams (stream_id 3, 8, 4, 9)..."
  "${muxer}" \
    "${AVM_TEST_OUTPUT_DIR}/mm_9a.bin" 3 1 \
    "${AVM_TEST_OUTPUT_DIR}/mm_9b.bin" 8 1 \
    "${AVM_TEST_OUTPUT_DIR}/mm_9c.bin" 4 1 \
    "${AVM_TEST_OUTPUT_DIR}/mm_9d.bin" 9 1 \
    "${bs_mux9}" > /dev/null 2>&1 || return 1

  vlog "  [Test 3.2] Decoding muxed and individual streams..."
  "${decoder}" --codec=av2 -o "${dec_mux9}" "${bs_mux9}" > /dev/null 2>&1 || return 1
  for s in 9a 9b 9c 9d; do
    "${decoder}" --codec=av2 \
      -o "${AVM_TEST_OUTPUT_DIR}/mm_dec_${s}.yuv" \
      "${AVM_TEST_OUTPUT_DIR}/mm_${s}.bin" > /dev/null 2>&1 || return 1
  done

  vlog "  [Test 3.2] Verifying per-stream ordering..."
  python3 -c "
def extract_frames(f, fs):
    with open(f, 'rb') as fh:
        fh.readline()
        frames = []
        while True:
            l = fh.readline()
            if not l: break
            if l.startswith(b'FRAME'): frames.append(fh.read(fs))
        return frames

def check_sub(muxed, sf):
    it = iter(muxed)
    for f in sf:
        while True:
            m = next(it, None)
            if m is None: return False
            if m == f: break
    return True

fs = ${frame_size}
d = '${AVM_TEST_OUTPUT_DIR}'
muxed = extract_frames('${dec_mux9}', fs)
streams = [extract_frames(d + '/mm_dec_%s.yuv' % s, fs) for s in ['9a', '9b', '9c', '9d']]
for i, s in enumerate(streams):
    assert len(s) == len(set(s)), 'Stream %d has duplicate frames' % i
assert sorted(muxed) == sorted(sum(streams, [])), 'Frame sets differ'
for i, s in enumerate(streams):
    assert check_sub(muxed, s), 'Stream %d not in correct order' % i
print('Test 3.2 passed: 4-stream mux, %d frames, per-stream ordering correct' % len(muxed))
" || return 1

  # --- Test 3.3: cat(mux8, mux9) -> verify strict concat order ---
  local bs_t10="${AVM_TEST_OUTPUT_DIR}/mm_t10_concat.bin"
  local dec_t10="${AVM_TEST_OUTPUT_DIR}/mm_t10_decoded.yuv"

  vlog "  [Test 3.3] Concatenating mux8 with mux9..."
  cat "${bs_mux8}" "${bs_mux9}" > "${bs_t10}" || return 1
  vlog "  [Test 3.3] Decoding..."
  "${decoder}" --codec=av2 -o "${dec_t10}" "${bs_t10}" > /dev/null 2>&1 || return 1
  vlog "  [Test 3.3] Verifying strict concatenation order..."
  python3 -c "
def extract_pixels(f, fs):
    with open(f, 'rb') as fh:
        fh.readline()
        p = bytearray()
        while True:
            l = fh.readline()
            if not l: break
            if l.startswith(b'FRAME'): p.extend(fh.read(fs))
        return bytes(p)

fs = ${frame_size}
d = extract_pixels('${dec_t10}', fs)
m8 = extract_pixels('${dec_mux8}', fs)
m9 = extract_pixels('${dec_mux9}', fs)
assert d == m8 + m9, 'Test 3.3: pixel mismatch'
print('Test 3.3 passed: cat(mux8, mux9) strict concatenation order correct')
" || return 1

  # --- Test 3.4: cat(mux9, mux8) -> verify strict concat order ---
  local bs_t10a="${AVM_TEST_OUTPUT_DIR}/mm_t10a_concat.bin"
  local dec_t10a="${AVM_TEST_OUTPUT_DIR}/mm_t10a_decoded.yuv"

  vlog "  [Test 3.4] Concatenating mux9 with mux8..."
  cat "${bs_mux9}" "${bs_mux8}" > "${bs_t10a}" || return 1
  vlog "  [Test 3.4] Decoding..."
  "${decoder}" --codec=av2 -o "${dec_t10a}" "${bs_t10a}" > /dev/null 2>&1 || return 1
  vlog "  [Test 3.4] Verifying strict concatenation order..."
  python3 -c "
def extract_pixels(f, fs):
    with open(f, 'rb') as fh:
        fh.readline()
        p = bytearray()
        while True:
            l = fh.readline()
            if not l: break
            if l.startswith(b'FRAME'): p.extend(fh.read(fs))
        return bytes(p)

fs = ${frame_size}
d = extract_pixels('${dec_t10a}', fs)
m9 = extract_pixels('${dec_mux9}', fs)
m8 = extract_pixels('${dec_mux8}', fs)
assert d == m9 + m8, 'Test 3.4: pixel mismatch'
print('Test 3.4 passed: cat(mux9, mux8) strict concatenation order correct')
" || return 1
}

multistream_tests_tests="multistream_cmvs_tests multistream_mux_tests multistream_three_and_four_stream_tests"
run_tests multistream_tests_verify_environment "${multistream_tests_tests}"

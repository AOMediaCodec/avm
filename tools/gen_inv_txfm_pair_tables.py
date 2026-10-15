#!/usr/bin/env python3
##
## Copyright (c) 2026, Alliance for Open Media. All rights reserved
##
## This source code is subject to the terms of the BSD 3-Clause Clear License and the
## Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear License was
## not distributed with this source code in the LICENSE file, you can obtain it
## at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance for Open Media Patent
## License 1.0 was not distributed with this source code in the PATENTS file, you
## can obtain it at aomedia.org/license/patent-license/.
##
"""Generate the packed tap-pair weight tables used by the 8-bit AVX-512
inverse transform kernels.

vpmaddwd reads each 32-bit lane as two int16 values and computes

    lane = lo(a) * lo(b) + hi(a) * hi(b)

so two transform taps (ta, tb) contributing to the same output column k can be
evaluated with one instruction, provided their two kernel weights are glued
into a single 32-bit lane:

    w[pair][k] = TX_PW(tx_mat[ta][k], tx_mat[tb][k])

Output k of a 1D pass is by definition sum over taps j of tx_mat[j][k] * s[j],
so the weight for a tap is simply the raw matrix entry, sign included. The
existing kernels instead broadcast a handful of row-1 entries and recover the
rest by permuting them and flipping signs by hand; this script goes back to the
matrix so the tables do not have to be transcribed from that folded form.

Reads the tx_kernel_* tables out of av2/common/txb_common.c and writes the
corresponding static tx_pair_* definitions, which live in
av2/common/x86/lowbd_inv_txfm_avx512.c. The output is committed; this script is
kept so the derivation stays auditable and can be re-run if a kernel matrix
ever changes.

Output is emitted unwrapped and is expected to be passed through clang-format
before being committed, so re-running this and diffing against the tree needs
the same treatment:

  tools/gen_inv_txfm_pair_tables.py \
    | clang-format --style=file --assume-filename=x.c

Usage:
  tools/gen_inv_txfm_pair_tables.py [--src av2/common/txb_common.c]
"""

import argparse
import re
import sys

# Tap pairs per transform stage.
#
# The DCT2 butterflies already group their multiplies two taps at a time, and
# those existing groupings are reused verbatim so the conversion stays local to
# each stage. The generic matmul kernels have no butterfly structure, so
# adjacent taps are paired.
#
# The DC stages, (0, 8) for size 16 and (0, 16) for size 32, are deliberately
# absent. Their weights are 64 and +/-64, so the kernels evaluate them as an
# add or subtract followed by a shift, with no multiply to eliminate. Packing
# them would cost two instructions to build the pair plus two vpmaddwd, against
# the four instructions they take today.
ADJACENT_16 = [(i, i + 1) for i in range(0, 16, 2)]

TABLES = [
    ("tx_pair_dct2_size16_odd", "tx_kernel_dct2_size16", 16,
     [(1, 3), (5, 7), (9, 11), (13, 15)], 8),
    ("tx_pair_dct2_size16_st2", "tx_kernel_dct2_size16", 16,
     [(2, 6), (10, 14)], 4),
    ("tx_pair_dct2_size16_st3", "tx_kernel_dct2_size16", 16,
     [(4, 12)], 2),
    # Size-32 stages 2 to 4, tap pairs (2, 6) .. (26, 30), (4, 12) (20, 28) and
    # (8, 24), reuse the three size-16 tables above: row 2k of the 32-point
    # matrix is row k of the 16-point one (checked in main()).
    ("tx_pair_dct2_size32_odd", "tx_kernel_dct2_size32", 32,
     [(1, 3), (5, 7), (9, 11), (13, 15),
      (17, 19), (21, 23), (25, 27), (29, 31)], 16),
    ("tx_pair_adst_size16", "tx_kernel_adst_size16", 16, ADJACENT_16, 16),
    ("tx_pair_fdst_size16", "tx_kernel_fdst_size16", 16, ADJACENT_16, 16),
    # FDDT needs no table of its own: inv_txfm_fddt_size16_lowbd_avx512 reuses
    # the DDTX weights with reverse_out set.
    ("tx_pair_ddtx_size16", "tx_kernel_ddtx_size16", 16, ADJACENT_16, 16),
]

INT16_MIN, INT16_MAX = -32768, 32767


def parse_kernel(src, name, n):
    """Return the INV_TXFM block of tx_kernel_<name> as an n x n list."""
    start = src.index("const int %s[TXFM_DIRECTIONS]" % name)
    end = src.index("};", start)
    values = [int(v) for v in re.findall(r"-?\d+", src[src.index("{", start) + 1:end])]
    if len(values) != 2 * n * n:
        raise SystemExit("%s: expected %d entries, parsed %d" %
                         (name, 2 * n * n, len(values)))
    # FWD_TXFM is index 0, INV_TXFM is index 1.
    inv = values[n * n:]
    return [inv[r * n:(r + 1) * n] for r in range(n)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--src", default="av2/common/txb_common.c")
    args = parser.parse_args()

    with open(args.src) as f:
        src = f.read()

    matrices = {}
    for _, mat_name, n, _, _ in TABLES:
        if mat_name not in matrices:
            matrices[mat_name] = parse_kernel(src, mat_name, n)

    dct16 = matrices["tx_kernel_dct2_size16"]
    dct32 = matrices["tx_kernel_dct2_size32"]
    if any(dct32[2 * r][:16] != dct16[r] for r in range(16)):
        raise SystemExit("tx_kernel_dct2_size32 even rows no longer match "
                         "tx_kernel_dct2_size16; size-32 needs its own tables")

    out = []
    for name, mat_name, n, pairs, n_out in TABLES:
        mat = matrices[mat_name]
        labels = ["(%d, %d)" % p for p in pairs]
        if len(labels) > 4:
            # Long lists are elided to fit the 80-column comment.
            labels = labels[:2] + ["..."] + labels[-1:]
        out.append("// %s, tap pairs %s." % (mat_name, " ".join(labels)))
        out.append("static const int32_t %s[%d][%d] = {" %
                   (name, len(pairs), n_out))
        for ta, tb in pairs:
            entries = []
            for k in range(n_out):
                lo, hi = mat[ta][k], mat[tb][k]
                for v in (lo, hi):
                    if not INT16_MIN <= v <= INT16_MAX:
                        raise SystemExit("%s: weight %d exceeds int16" % (name, v))
                entries.append("TX_PW(%d, %d)" % (lo, hi))
            out.append("  { %s }," % ", ".join(entries))
        out.append("};")
        out.append("")

    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()

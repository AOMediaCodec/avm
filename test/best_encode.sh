#!/bin/bash
#
# Copyright (c) 2021, Alliance for Open Media. All rights reserved
#
# This source code is subject to the terms of the BSD 3-Clause Clear License and the
# Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear License was
# not distributed with this source code in the LICENSE file, you can obtain it
# at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance for Open Media Patent
# License 1.0 was not distributed with this source code in the PATENTS file, you
# can obtain it at aomedia.org/license/patent-license/.
#
# Author: jimbankoski@google.com (Jim Bankoski)

if [[ $# -ne 2 ]]; then
  echo "Encodes a file using best known settings (slow!)"
  echo "  Usage:    be [FILE] [BITRATE]"
  echo "  Example:  be akiyo_cif.y4m 200"
  exit
fi

f=$1  # file is first parameter
b=$2  # bitrate is second parameter

if [[ -e $f.fpf ]]; then
  # First-pass file found, do second pass only
  avmenc \
    $f \
    -o $f-$b.av2.webm \
    -p 2 \
    --pass=2 \
    --fpf=$f.fpf \
    --best \
    --cpu-used=0 \
    --target-bitrate=$b \
    --auto-alt-ref=1 \
    -v \
    --minsection-pct=0 \
    --maxsection-pct=800 \
    --lag-in-frames=25 \
    --kf-min-dist=0 \
    --kf-max-dist=99999 \
    --static-thresh=0 \
    --min-qp=0 \
    --max-qp=255 \
    --drop-frame=0 \
    --bias-pct=50 \
    --minsection-pct=0 \
    --maxsection-pct=800 \
    --psnr \
    --arnr-maxframes=7 \
    --arnr-strength=3 \
    --arnr-type=3
else
  # No first-pass file found, do 2-pass encode
  avmenc \
    $f \
    -o $f-$b.av2.webm \
    -p 2 \
    --pass=1 \
    --fpf=$f.fpf \
    --best \
    --cpu-used=0 \
    --target-bitrate=$b \
    --auto-alt-ref=1 \
    -v \
    --minsection-pct=0 \
    --maxsection-pct=800 \
    --lag-in-frames=25 \
    --kf-min-dist=0 \
    --kf-max-dist=99999 \
    --static-thresh=0 \
    --min-qp=0 \
    --max-qp=255 \
    --drop-frame=0

  avmenc \
    $f \
    -o $f-$b.av2.webm \
    -p 2 \
    --pass=2 \
    --fpf=$f.fpf \
    --best \
    --cpu-used=0 \
    --target-bitrate=$b \
    --auto-alt-ref=1 \
    -v \
    --minsection-pct=0 \
    --maxsection-pct=800 \
    --lag-in-frames=25 \
    --kf-min-dist=0 \
    --kf-max-dist=99999 \
    --static-thresh=0 \
    --min-qp=0 \
    --max-qp=255 \
    --drop-frame=0 \
    --bias-pct=50 \
    --minsection-pct=0 \
    --maxsection-pct=800 \
    --psnr \
    --arnr-maxframes=7 \
    --arnr-strength=3 \
    --arnr-type=3
fi

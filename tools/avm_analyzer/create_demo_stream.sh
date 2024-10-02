#!/bin/bash
# This script will recreate the AVM Analyzer demo stream, overwriting the
# existing leo_qcif.ivf and leo_qcif.zip files under avm_analyzer_app/assets.

set -e
GIT_ROOT=$(git rev-parse --show-toplevel)


while [[ "$#" -gt 0 ]]; do
    case $1 in
        --avm_build_dir) avm_build_dir="$2"; shift ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
    shift
done

if [[ -z ${avm_build_dir} ]]; then
  echo "Usage: ./create_demo_stream.sh --avm_build_dir <AVM_BUILD_DIR>"
  exit 1
fi


ASSETS_FOLDER=${GIT_ROOT}/tools/avm_analyzer/avm_analyzer_app/assets

${avm_build_dir}/aomenc -w 176 -h 144 --limit=3 ${ASSETS_FOLDER}/leo_qcif.yuv \
  -o ${ASSETS_FOLDER}/leo_qcif.ivf --tile-columns=0 --threads=1 --cpu-used=1  \
   --passes=1 --lag-in-frames=0 --min-gf-interval=16 --max-gf-interval=16     \
   --gf-min-pyr-height=4 --gf-max-pyr-height=4 --kf-min-dist=9999             \
   --kf-max-dist=9999 --use-fixed-qp-offsets=1 --deltaq-mode=0                \
   --enable-tpl-model=0 --enable-keyframe-filtering=0 --subgop-config-str=ld  \
   --end-usage=q --qp=160

${GIT_ROOT}/tools/avm_analyzer/convert_stream.sh \
  --avm_build_dir ${avm_build_dir} --stream ${ASSETS_FOLDER}/leo_qcif.ivf     \
  --output ${ASSETS_FOLDER}/leo_qcif.zip --yuv ${ASSETS_FOLDER}/leo_qcif.yuv
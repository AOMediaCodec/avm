#!/bin/bash

if [ "$#" -ne 3 ]; then
    echo "Usage: ./generate_testdata.sh AVM_ROOT AVM_BUILD_DIR LIBAVM_TEST_DATA_PATH"
    exit 1
fi

AVM_ROOT=$1
AVM_BUILD_DIR=$2
LIBAVM_TEST_DATA_PATH=$3
YUV_NAME=park_joy_90p_8_420.y4m
STREAM_NAME="${YUV_NAME%.*}"
AVMENC=${AVM_BUILD_DIR}/avmenc
EXTRACT_PROTO=${AVM_BUILD_DIR}/extract_proto
YUV_PATH=${LIBAVM_TEST_DATA_PATH}/${YUV_NAME}
STREAM_PATH=${LIBAVM_TEST_DATA_PATH}/${STREAM_NAME}.ivf
DUMP_YUVS="python3 ${AVM_ROOT}/tools/py_stats/avm_stats/dump_yuvs.py"

encoder_args="--tile-columns=0 --threads=1 --cpu-used=0 --passes=1 --lag-in-frames=0 \
  --min-gf-interval=16 --max-gf-interval=16 --gf-min-pyr-height=4 \
  --gf-max-pyr-height=4 --kf-min-dist=9999 --kf-max-dist=9999 \
  --use-fixed-qp-offsets=1 --deltaq-mode=0 --enable-tpl-model=0 \
  --enable-keyframe-filtering=0 --end-usage=q --qp=185"

${AVMENC} --limit=2 ${YUV_PATH} -o ${STREAM_PATH} ${encoder_args}

encoder_args_comma=$(echo "${encoder_args}" | tr -s ' ' | tr ' ' ',')
${EXTRACT_PROTO} --stream=${STREAM_PATH} --output_folder=${LIBAVM_TEST_DATA_PATH} \
  --orig_yuv=${YUV_PATH} --encoder_args ${encoder_args_comma}

PROTO_FRAME_0=${LIBAVM_TEST_DATA_PATH}/${STREAM_NAME}_frame_0000.pb
${DUMP_YUVS} --proto=${PROTO_FRAME_0} --output_folder=${LIBAVM_TEST_DATA_PATH}

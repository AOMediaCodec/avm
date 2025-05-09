#!/bin/bash
set -e
trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM EXIT
GIT_ROOT=$(git rev-parse --show-toplevel)

port=8080
# Use cargo watch / trunk watch to automatically rebuild on source changes.
watch=0

while [[ "$#" -gt 0 ]]; do
    case $1 in
        -p|--port) port="$2"; shift ;;
        -s|--streams_dir) streams_dir="$2"; shift ;;
        -a|--avm_build_dir) avm_build_dir="$2"; shift ;;
        -w|--watch) watch="1" ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
    shift
done

if [[ -z ${streams_dir} || -z ${avm_build_dir} ]]; then
  echo "Usage: ./launch_server_local.sh --streams_dir <STREAMS_PATH> --avm_build_dir <AVM_BUILD_PATH> [--port <PORT>]"
  exit 1
fi
mkdir -p ${streams_dir}

SERVER_ARGS=(--extract-proto ${avm_build_dir}/extract_proto\
  --dump-obu ${avm_build_dir}/tools/dump_obu --working-dir ${streams_dir}\
  --frontend-root ${GIT_ROOT}/tools/avm_analyzer/avm_analyzer_app/dist --ip "127.0.0.1" --port ${port})

if [[ ${watch} -eq 1 ]]; then
trunk watch --config avm_analyzer_app --release &
WATCH_CMD="run --release --bin avm-analyzer-server -- ${SERVER_ARGS[@]}"
cargo watch -w avm_analyzer_server -x "${WATCH_CMD}"
else
trunk build --config avm_analyzer_app --release
cargo run --release --bin avm-analyzer-server -- ${SERVER_ARGS[@]}
fi

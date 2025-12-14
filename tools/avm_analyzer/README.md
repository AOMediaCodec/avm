# AVM Analyzer

## Building

### With Docker

```
./launch_server_docker.sh --streams_dir <STREAMS_PATH> [--port <PORT>]
```

### Local Build
1. Install dependencies
```
# libavm (if not already installed)
apt install cmake yasm perl
# Protobuf compiler
apt install protobuf-compiler libprotobuf-dev
# Rust toolchain (see rustup.rs)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
# Trunk
cargo install --locked trunk
# WebAssembly build target
rustup target add wasm32-unknown-unknown
```

2. Build AVM
```
export AVM_ROOT=/path/to/git/root
export AVM_BUILD_DIR=/path/to/avm/build
./build_avm.sh --avm_source_dir ${AVM_ROOT} --avm_build_dir ${AVM_BUILD_DIR}
```

3. Build and launch AVM Analyzer
```
./launch_server_local.sh --streams_dir <STREAMS_PATH> --avm_build_dir ${AVM_BUILD_DIR} [--port <PORT>]
```

## Troubleshooting
Please contact comc@google.com.
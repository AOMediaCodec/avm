# TODO(comc): cargo chef caching doesn't seem to be working properly.
FROM lukemathwalker/cargo-chef:latest-rust-1 AS chef
RUN apt-get update
RUN apt-get install -y make pkg-config git unzip cmake protobuf-compiler libssl-dev --no-install-recommends
RUN cargo install trunk
RUN rustup target add wasm32-unknown-unknown
RUN mkdir /extract_proto
COPY tools/extract_proto/avm_frame.proto /extract_proto/avm_frame.proto
WORKDIR /app

FROM chef as planner_frontend
COPY tools/avm_analyzer .
RUN cargo chef prepare --recipe-path recipe.json --bin avm_analyzer_app

FROM chef AS cacher_frontend
COPY --from=planner_frontend /app/recipe.json recipe.json
RUN cargo chef cook --release --target wasm32-unknown-unknown --recipe-path recipe.json --bin avm-analyzer-app

FROM chef as planner_backend
COPY tools/avm_analyzer /app
RUN cargo chef prepare --recipe-path recipe.json --bin avm_analyzer_server

FROM chef AS cacher_backend
COPY --from=planner_backend /app/recipe.json recipe.json
RUN cargo chef cook --release --recipe-path recipe.json --bin avm-analyzer-server

FROM chef AS builder_frontend
COPY --from=cacher_frontend /app .
COPY tools/avm_analyzer .
RUN trunk build --config avm_analyzer_app --release

FROM chef AS builder_backend
COPY --from=cacher_backend /app .
COPY tools/avm_analyzer .
RUN cargo build --release --bin avm-analyzer-server

FROM avm_analyzer_runtime as avm_builder
COPY . /avm
RUN mkdir /avm_build
RUN /scripts/build_avm.sh --avm_build_dir /avm_build --avm_source_dir /avm
RUN rm -r /avm

FROM avm_analyzer_runtime as runtime
WORKDIR /app
COPY --from=builder_frontend /app/avm_analyzer_app/dist dist
COPY --from=builder_backend /app/target/release/avm-analyzer-server avm-analyzer-server
COPY --from=avm_builder /avm_build /avm_build

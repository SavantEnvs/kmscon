# syntax=docker/dockerfile:1.7
#
# kmscon commit image: C (meson) + the in-tree Rust libtsm, on the org C/C++ base.
#   DOCKER_BUILDKIT=1 docker build -f mayhem/Dockerfile --build-arg COMMIT=$(git rev-parse HEAD) -t kmscon .
FROM ghcr.io/savantenvs/base:20261023

ARG COMMIT
ARG REPO_URL
ARG SOURCE_DATE_EPOCH
ARG SANITIZER_FLAGS
ARG COVERAGE_FLAGS=
ARG MAYHEM_JOBS=

# Build dependencies kmscon requires (xkbcommon, libudev, zlib) + `check` for its test suite.
USER root
RUN [ -n "$SOURCE_DATE_EPOCH" ] && repro-sources-list.sh || true ; \
    apt-get update && apt-get install -yq --no-install-recommends \
      libudev-dev libxkbcommon-dev zlib1g-dev check python3-venv curl ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# meson >= 1.11 (upstream's meson_version; rust.workspace() support) — Debian ships 1.7.
ARG MESON_VERSION=1.12.1
RUN python3 -m venv /opt/toolchains/meson \
 && /opt/toolchains/meson/bin/pip install --no-cache-dir "meson==${MESON_VERSION}" \
 && /opt/toolchains/meson/bin/meson --version

# Rust (libtsm is edition 2024 and uses let-chains: rustc >= 1.88; Debian ships 1.85), pinned.
ARG RUST_VERSION=1.91.1
ENV RUSTUP_HOME=/opt/toolchains/rust/rustup \
    CARGO_HOME=/opt/toolchains/rust/cargo \
    PATH=/opt/toolchains/rust/cargo/bin:/opt/toolchains/meson/bin:$PATH \
    MESON_PACKAGE_CACHE_DIR=/opt/toolchains/deps/meson-packagecache
RUN curl -sSf https://sh.rustup.rs | sh -s -- -y -q --profile minimal \
      --default-toolchain "${RUST_VERSION}" --no-modify-path \
 && rustc --version && chmod -R a+rX /opt/toolchains/rust

# libtsm's crates (vte, unicode-width, arrayvec, memchr) are meson wraps generated from Cargo.lock
# (checksummed there). Download them once into the package cache so build.sh resolves them offline.
COPY --chown=0:0 Cargo.toml Cargo.lock meson.build meson.options /tmp/kmscon-crates/
RUN mkdir -p "$MESON_PACKAGE_CACHE_DIR" \
 && cd /tmp/kmscon-crates && printf "project('crates', meson_version: '>=1.11')\nrust = import('rust')\nrust.workspace()\n" > meson.build \
 && : > meson.options && mkdir -p subprojects \
 && meson setup /tmp/kmscon-crates/b . \
 && ls "$MESON_PACKAGE_CACHE_DIR" && chmod -R a+rX "$MESON_PACKAGE_CACHE_DIR" \
 && cd / && rm -rf /tmp/kmscon-crates

USER mayhem
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

RUN if [ -x /opt/mayhem/ccache/prefill ]; then /opt/mayhem/ccache/prefill bash mayhem/build.sh; \
    else bash mayhem/build.sh; fi

RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem

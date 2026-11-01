# diskOS UI build environment - builds the pinned mipsel musl cross compiler from source, then builds `mq_ui`.
#
# The compiler is musl-cross-make at a fixed commit with toolchain/mcm-config.mak (GCC 14.3.0 with C and C++, musl 1.2.6,
# binutils 2.44), targeting the stock firmware's float ABI (MIPS32r2, FP64, NaN2008). musl-cross-make checks every
# downloaded source tarball against its own pinned hashes, so the toolchain cannot drift silently.
#
# Build the image:   docker build -t diskos-ui-builder .        (from this ui/ directory; builds GCC: about 10 minutes)
# Build the UI:       see README.md (CMake preset "device"; needs the stock V2.57 sysroot and FFmpeg headers mounted)
#   LVGL is vendored at ui/lvgl; FreeType and the other libraries at ui/third_party.

# Debian 13 by digest, so every build (and CI's cache) starts from the same base.
FROM debian:13-slim@sha256:f6b08b09a05725f9431502d3b5714f5c4576e82c1a84fa1debe0c24a7e0c3ec4

# What the toolchain build itself needs. Kept apart from the tools below, so changing those never rebuilds GCC.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential ca-certificates git wget xz-utils bzip2 patch rsync file \
    && rm -rf /var/lib/apt/lists/*

ARG MCM_URL=https://github.com/richfelker/musl-cross-make.git
ARG MCM_COMMIT=227df8b99103f9c59f6570babf892978e293082f
COPY toolchain/mcm-config.mak /tmp/mcm-config.mak
RUN git clone "$MCM_URL" /tmp/mcm \
    && cd /tmp/mcm && git checkout --detach "$MCM_COMMIT" \
    && test "$(git rev-parse HEAD)" = "$MCM_COMMIT" \
    && cp /tmp/mcm-config.mak config.mak \
    && make -j"$(nproc)" && make install \
    && cd / && rm -rf /tmp/mcm /tmp/mcm-config.mak
ENV CROSS=/opt/mipsel-n2008-musl-cross/bin/mipsel-linux-musl-

# The UI build and its checks. cmake/ninja: the build. python3: tools/build-native-deps.py and
# tools/embed_audiodec.py. gcc-mipsel-linux-gnu and libc6-dev-mipsel-cross: the dynamically linked diskos-audiodec
# helper, built against the stock glibc sysroot. The host's g++ 14 (build-essential) matches the cross GCC's C++23,
# so the host presets (Catch2 tests, sanitizers) build here too; clang-tidy and cppcheck analyse the C++ modules.
RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake ninja-build python3 \
        gcc-mipsel-linux-gnu binutils-mipsel-linux-gnu libc6-dev-mipsel-cross \
        catch2 clang-tidy cppcheck \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

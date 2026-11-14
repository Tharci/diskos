# diskOS UI build environment - builds the pinned mipsel musl cross compiler from source, then builds `mq_ui`.
#
# The compiler is musl-cross-make at a fixed commit with toolchain/mcm-config.mak (GCC 14.3.0 with C and C++, musl 1.2.6,
# binutils 2.44), targeting the stock firmware's float ABI (MIPS32r2, FP64, NaN2008). musl-cross-make checks every
# downloaded source tarball against its own pinned hashes, so the toolchain cannot drift silently.
#
# Two stages:
#   builder  the toolchain and build tools (CI, plain docker builds)
#   dev      builder plus the editor, Python and device tools: the dev container (.devcontainer/)
#
# Build the image:   docker build --target builder -t diskos-ui-builder .   (from ui/; builds GCC: about 10 minutes)
# Build the UI:       see README.md (CMake preset "device"; needs the stock V2.57 sysroot and FFmpeg headers mounted)
#   LVGL is vendored at ui/lvgl; FreeType and the other libraries at ui/third_party.

# Debian 13 by digest, so every build (and CI's cache) starts from the same base.
FROM debian:13-slim@sha256:f6b08b09a05725f9431502d3b5714f5c4576e82c1a84fa1debe0c24a7e0c3ec4 AS builder

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


# The dev container: everything above, plus what working on the project needs. x86-64 hosts only (clangd
# publishes no arm64 Linux build).
FROM builder AS dev

# gdb: debugging the host tests. openssh-client and sshpass: git over SSH and the Disc (tools/diskos-deploy.sh).
# squashfs-tools: the stock root filesystem (tools/diskos-dev.sh inputs) and the installer's tests, with
# libusb-1.0-0-dev. sudo: installing something extra in a running container.
RUN apt-get update && apt-get install -y --no-install-recommends \
        gdb openssh-client sshpass squashfs-tools libusb-1.0-0-dev less sudo \
    && rm -rf /var/lib/apt/lists/*

# clangd, the C/C++ language server, on the same LLVM major as pre-commit's clang-format, so formatting on save
# gives what the hook gives. The release ships its own builtin headers (lib/clang/<major>/include).
ARG CLANGD_VERSION=23.1.0
ARG CLANGD_SHA256=e53b1a96196095faedb7642cf64964f7fb9ad4a0c1f00dd2c172a3d9dcbafdfd
RUN wget -q -O /tmp/clangd.zip \
        "https://github.com/clangd/clangd/releases/download/${CLANGD_VERSION}/clangd-linux-${CLANGD_VERSION}.zip" \
    && echo "${CLANGD_SHA256}  /tmp/clangd.zip" | sha256sum -c --quiet - \
    && python3 -m zipfile -e /tmp/clangd.zip /opt \
    && chmod 755 "/opt/clangd_${CLANGD_VERSION}/bin/clangd" \
    && ln -s "/opt/clangd_${CLANGD_VERSION}/bin/clangd" /usr/local/bin/clangd \
    && rm /tmp/clangd.zip

# uv for the Python tooling (keep equal to the uv-pre-commit rev and CI's setup-uv), and the Python it runs
# (keep equal to ../.python-version), installed where every user can read it. Copy mode: uv's cache (a volume)
# and the checkout's .venv are on different file systems, so they can't share hard links.
ARG UV_VERSION=0.13.6
ARG UV_SHA256=f29c3e94ffabd7e6f3ba521dba169a9b7ec12100bf3257087f0f24f6123f3645
ENV UV_PYTHON_INSTALL_DIR=/opt/uv-python UV_LINK_MODE=copy
RUN wget -q -O /tmp/uv.tar.gz \
        "https://github.com/astral-sh/uv/releases/download/${UV_VERSION}/uv-x86_64-unknown-linux-gnu.tar.gz" \
    && echo "${UV_SHA256}  /tmp/uv.tar.gz" | sha256sum -c --quiet - \
    && tar -xzf /tmp/uv.tar.gz -C /usr/local/bin --strip-components=1 \
        uv-x86_64-unknown-linux-gnu/uv uv-x86_64-unknown-linux-gnu/uvx \
    && rm /tmp/uv.tar.gz \
    && uv python install 3.12

# A non-root user. The dev container gives it your uid and gid, so what it writes in the checkout is yours.
# ~/.cache is a volume (uv, pre-commit), created here so the volume starts out owned by this user.
RUN useradd --create-home --uid 1000 --shell /bin/bash dev \
    && echo 'dev ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/dev \
    && install -d -o dev -g dev /home/dev/.cache
USER dev

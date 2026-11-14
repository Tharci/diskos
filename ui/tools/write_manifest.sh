#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Write <artifact>.manifest next to a built binary: name, MD5, size, build time, compiler, and the MD5 of
# the sources it was built from (informational; nothing consumes it).
# Usage: write_manifest.sh ARTIFACT COMPILER SOURCE...
set -eu
artifact=$1 compiler=$2
shift 2
{
    echo "artifact: $(basename "$artifact")"
    echo "md5: $(md5sum "$artifact" | cut -d' ' -f1)"
    echo "size: $(stat -c%s "$artifact")"
    echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "cc: $("$compiler" --version | head -1)"
    echo "src_md5: $(cat "$@" | md5sum | cut -d' ' -f1)"
} > "$artifact.manifest"
cat "$artifact.manifest"

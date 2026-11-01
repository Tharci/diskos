#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fetch and verify the device build's two external inputs into DIR.

  DIR/ffmpeg-4.2.2.tar.xz   FFmpeg's release archive (the build uses its public headers only)
  DIR/stock-v257/           lib/ and usr/lib/ of FiiO's stock V2.57 root filesystem

The firmware ZIP is FiiO's official download, checked against diskos_installer's catalogue pin, and
the root filesystem extracted from it against PINNED_ROOTFS. Only what is missing is downloaded.
Nothing here is redistributed: the embedded audiodec helper only links against these libraries.

Usage (from the repository root, after `uv sync`): uv run python ui/tools/fetch_stock_inputs.py DIR
Needs unsquashfs (squashfs-tools).
"""

import hashlib
import shutil
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
# diskos_installer is not an installed package (tool.uv package = false).
sys.path.insert(0, str(REPO))
from diskos_installer import firmware_catalogue, imagebuild  # noqa: E402

FFMPEG_URLS = (
    "https://ffmpeg.org/releases/ffmpeg-4.2.2.tar.xz",
    # The identical file in Debian's archive: ffmpeg.org refuses some cloud addresses,
    # GitHub's runners among them.
    "https://snapshot.debian.org/archive/debian/20200126T032723Z/pool/main/f/ffmpeg/ffmpeg_4.2.2.orig.tar.xz",
)
FFMPEG_SHA256 = "cb754255ab0ee2ea5f66f8850e1bd6ad5cac1cd855d0a2f4990fb8c668b0d29c"
FIRMWARE = "257"
# What the helper links against; their presence proves the extraction.
SYSROOT_FILES = (
    "lib/ld-linux-mipsn8.so.1",
    "lib/libc.so.6",
    "usr/lib/libavformat.so.58",
    "usr/lib/libavcodec.so.58",
    "usr/lib/libavutil.so.56",
)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fetch(url, target):
    """Download url to target; False, with the reason printed, if the transfer fails."""
    print(f">> downloading {url}")
    partial = target.with_name(target.name + ".part")
    try:
        response = urllib.request.urlopen(url, timeout=60)  # noqa: S310 - fixed URLs
        with response, partial.open("wb") as file:
            shutil.copyfileobj(response, file)
    except OSError as error:
        print(f"   failed: {error}")
        return False
    partial.replace(target)
    return True


def download(urls, target, expected_sha256=None, attempts=3):
    """Fetch target from the first source that delivers it (intact, when a hash is given)."""
    if target.exists():
        return
    for url in urls:
        for attempt in range(attempts):
            if fetch(url, target):
                if expected_sha256 is None or sha256(target) == expected_sha256:
                    return
                print("   SHA-256 mismatch")
                target.unlink()
                break
            time.sleep(5 * (attempt + 1))
    sys.exit(f"could not download {target.name}")


def fetch_ffmpeg(inputs):
    archive = inputs / "ffmpeg-4.2.2.tar.xz"
    download(FFMPEG_URLS, archive, FFMPEG_SHA256)
    if sha256(archive) != FFMPEG_SHA256:
        sys.exit(f"{archive}: SHA-256 mismatch")


def extract_rootfs(firmware_zip, rootfs):
    work = rootfs.parent / "work"
    work.mkdir(exist_ok=True)
    imagebuild.extract_stock_rootfs(str(firmware_zip), str(rootfs), str(work))
    shutil.rmtree(work)
    pinned_sha, pinned_size = imagebuild.PINNED_ROOTFS[FIRMWARE]
    if (sha256(rootfs), rootfs.stat().st_size) != (pinned_sha, pinned_size):
        sys.exit(f"{rootfs}: does not match PINNED_ROOTFS[{FIRMWARE}]")


def fetch_sysroot(inputs):
    sysroot = inputs / "stock-v257"
    if all((sysroot / name).exists() for name in SYSROOT_FILES):
        return
    pin = firmware_catalogue.CATALOGUE[FIRMWARE]
    firmware_zip = inputs / Path(pin.url).name
    download([pin.url], firmware_zip)
    firmware_catalogue.verify(str(firmware_zip), FIRMWARE)
    rootfs = inputs / "rootfs-v257.squashfs"
    if not rootfs.exists():
        extract_rootfs(firmware_zip, rootfs)
    unsquashfs = ["unsquashfs", "-q", "-n", "-f", "-d", str(sysroot), str(rootfs), "lib", "usr/lib"]
    subprocess.run(unsquashfs, check=True)  # noqa: S603 - fixed arguments
    missing = [name for name in SYSROOT_FILES if not (sysroot / name).exists()]
    if missing:
        sys.exit(f"stock sysroot is missing {', '.join(missing)}")


def main(argv):
    if len(argv) != 1:
        sys.exit(__doc__)
    inputs = Path(argv[0]).resolve()
    inputs.mkdir(parents=True, exist_ok=True)
    fetch_ffmpeg(inputs)
    fetch_sysroot(inputs)
    print(f">> stock inputs ready in {inputs}")


if __name__ == "__main__":
    main(sys.argv[1:])

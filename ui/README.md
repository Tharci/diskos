# diskOS UI (`mq_ui`) - native player interface for the FiiO Snowsky Disc

The on-device UI for [diskOS](https://github.com/b0hemia/diskos): a custom, round-screen music-player
interface for the **FiiO Snowsky Disc** ($80 round-display DAP; Ingenic X2000 / MIPS32, Linux 4.4).
It replaces the stock `mq_ui` while keeping the stock `mq_player` audio engine untouched, talking to
it over the device's POSIX message-queue IPC.

Built with **LVGL 9.2.2** (software renderer) on a static-musl mipsel toolchain. This produces the
`mq_ui` binary that the diskOS installer bakes into the flashed image.

> **License:** this UI is **GPL-3.0-or-later** (see [`COPYING`](COPYING)). That is a deliberate,
> different choice from the diskOS *installer/tooling*, which is MIT - the UI is the part meant to be
> forked and kept open. Bundled third-party components keep their own licenses (see [License](#license)).

## Hardware target
- **Display:** `/dev/fb0`, 360x360, 32bpp XRGB8888; partial redraws into the active page
  of a three-page framebuffer; panel mounted 180 degrees
  (default flush rotation in `fb_pan.c`; Screen Rotation can change it).
- **Touch:** `cst816t` capacitive controller on `/dev/input/event1` (multitouch evdev), calibrated
  for the 180-degree panel. Physical keys on `/dev/input/event0`.
- **IPC:** POSIX message queues `/ui` (state/metadata player->UI) and `/player` (commands UI->player);
  ASCII frames start with `TAG(4)+TOTAL_LEN(4hex)`; length includes the header.
  Most commands add `VALUE1(4hex)` and optional payload, which can be JSON.
  Some commands add another value field; `02020008` has no value field.
- **Library:** on-device SQLite `song.db` (bundled SQLite amalgamation, compiled in-process).

## Source layout
| Area | Files |
|---|---|
| C++ rewrite | [`src/`](src/README.md): the modules that replace the C below, one folder per area |
| Core / loop / nav | `main.c`, `screenmgr.c` / `screens.h`, `fb_pan.c`, `ipc.c` / `ipc.h`, `config.c` |
| Now Playing + art | `ui.c` / `ui.h`, `art.c` / `art.h`, `artcache.c`, `saver.c`, `songinfo.c`, `npmenus.c` |
| Library / browse | `home.c`, `library.c`, `musicdb.c`, `search.c`, `playlistview.c`, `scanner.c` |
| Settings / features | `settings.c`, `eqcustom.c`, `quicksettings.c`, `modes.c`, `kbinput.c`, `colorpick.c`, `toast.c`, `apps.c`, `debug_ui.c`, `fwcaps.c` |
| Connectivity | `wifi.c`, `bt.c`, `weather.c`, `lyrics.c`, `lastfm.c` / `lastfm_ui.c` |
| Bundled third-party | `third_party/freetype-2.13.3/` (FTL), `sqlite3.c` (public domain), `jsmn.h` (MIT), `md5.c` (public domain), `font_*.c` (generated glyph data) |
| Dev/diagnostic tools | `fbshot.c`, and other small on-device helpers (not part of the shipped UI) |

## Build
Requires:
- A static **mipsel-linux-musl** cross toolchain built for the stock firmware's float ABI: **MIPS32r2, FP64,
  NaN2008** (GCC 14.3.0 with C and C++, musl 1.2.6). Build it with [musl-cross-make](https://github.com/richfelker/musl-cross-make)
  and the pinned config in [`toolchain/mcm-config.mak`](toolchain/mcm-config.mak), or just use the Docker builder
  below. Generic prebuilt mipsel toolchains (e.g. musl.cc) target the older MIPS-I / FP32 ABI: the result runs, but
  the Disc's kernel then emulates every floating-point instruction in software, which is much slower.
- **LVGL 9.2.2** - **vendored in `./lvgl`** (MIT). It is *lightly customized* (a merged Source Han
  Sans + Font Awesome CJK font, plus font/TJPG tweaks), so use the vendored copy to retain these
  customizations.

The embedded stock decoder also needs two external inputs, supplied by the builder:

- `STOCK_AV_HEADERS`: the FFmpeg 4.2.2 archive from
  <https://ffmpeg.org/releases/ffmpeg-4.2.2.tar.xz>, SHA-256
  `cb754255ab0ee2ea5f66f8850e1bd6ad5cac1cd855d0a2f4990fb8c668b0d29c`.
  The build checks this pin and extracts public headers only; it does not compile FFmpeg.
- `STOCK_SYSROOT`: your own extracted stock V2.57 root filesystem, containing
  `lib/ld-linux-mipsn8.so.1`, `lib/libc.so.6`, and `usr/lib/libav{format,codec,util}.so.*`.
  Stock binaries are not supplied in this source tree. The helper links against these
  stock libraries. It also requires `mipsel-linux-gnu-gcc` and `mipsel-linux-gnu-strip`
  on PATH (override with `STOCK_CC` and `STOCK_STRIP`).

The build itself needs **CMake 3.25+**, **Ninja** and **Python 3** (all in the Docker builder below).

From this `ui/` directory, with its bundled `third_party/`, `tools/` and `lvgl/`:

```sh
export STOCK_AV_HEADERS=/path/to/ffmpeg-4.2.2.tar.xz STOCK_SYSROOT=/path/to/stock-v257
export CROSS=/opt/mipsel-n2008-musl-cross/bin/mipsel-linux-musl-   # if the toolchain is not on PATH
cmake --preset device
cmake --build --preset device
```

This produces the static, stripped `build/device/mq_ui`, its unstripped twin `mq_ui.debug` for crash
symbolication, and `mq_ui.manifest`. `build/device/compile_commands.json` describes every compile for
editors and analyzers. Native vendor dependencies are built offline from the included sources and recipes;
set `DISKOS_DEP_OUT` to keep them in a directory shared between build directories.

- **Helpers** installed by the image builder: `cmake --build --preset device -t diskos-bootprobe diskos-artdec`.
- **Special builds** go in their own build directory, so they never mix with the normal one, e.g.
  `cmake --preset device -B build/profile -DDISKOS_PROFILE=ON && cmake --build build/profile`.
  Options: `DISKOS_PROFILE` (the `prof.c` measurement hooks), `DISKOS_FRAME_LOG` (logs every frame to and from the
  player, byte-exact, to `/tmp/diskos_frames.log` on the Disc), `DISKOS_TEST_OUTPUTS` (shows SPDIF Output and
  USB Audio), `DISKOS_TEST_BT_PREPARE` (the Bluetooth preparation experiment).
- **Checks:** `tools/check_theme.py` (colours and fonts only through theme roles) runs before every link of
  `mq_ui` and as a pre-commit hook; `tools/check_punct.py` (no punctuation the device fonts lack) runs as a
  pre-commit hook.

### C++ modules and host tests

The UI is being rewritten in C++23 under [`src/`](src/README.md), module by module. The device build compiles
the modules too, and `cmake --build --preset device -t tidy` runs clang-tidy on them. Their Catch2 tests run on
the host, with the host's GCC 14, under sanitizers:

```sh
cmake --preset host && cmake --build --preset host && ctest --preset host   # ASan + UBSan
```

`host-tsan` is the same with ThreadSanitizer. The host presets need no stock inputs or cross toolchain.

## Build in Docker (recommended)

The container builds the cross compiler from musl-cross-make at a pinned commit, with source-download
hashes from that recipe, and carries CMake, Ninja, Python, the stock helper's compiler, and for the C++
modules the host's GCC 14, Catch2 v3 and clang-tidy:

```sh
docker build -t diskos-ui-builder .                                  # from this ui/ directory
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -v /path/to/inputs:/inputs:ro \
  -e STOCK_AV_HEADERS=/inputs/ffmpeg-4.2.2.tar.xz -e STOCK_SYSROOT=/inputs/stock-v257 \
  diskos-ui-builder sh -c 'cmake --preset device && cmake --build --preset device'
```

The `-u` flag keeps the build outputs owned by you, not root. The first `docker build` compiles GCC (about
ten minutes); later UI builds reuse the image. The host tests need no inputs:

```sh
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" diskos-ui-builder \
  sh -c 'cmake --preset host && cmake --build --preset host && ctest --preset host'
```

## Troubleshooting

- **`musl` / SQLite `fcntl64` errors:** check the toolchain and compile flags. SQLite is built with its own
  flags, without `_GNU_SOURCE` (the `third_party_sqlite` library in `cmake/third_party.cmake`): keep it separate.
  If your cross-compiler errors on `fcntl64`, check that it uses the pinned musl headers and libraries, or use
  the Docker builder.

## Deploy / flash
`mq_ui` is normally delivered by the diskOS **installer**, which bakes it into a rootfs image and
flashes it over the chip's mask-ROM USB mode - see the [main diskOS repo](https://github.com/b0hemia/diskos).
The installer keeps the stock `mq_player` audio engine; only the UI is replaced.

## License
- **This UI:** GPL-3.0-or-later - [`COPYING`](COPYING). (c) diskOS contributors.
- **LVGL 9.2.2** (vendored in `lvgl/`): MIT - [`licenses/LICENSE-LVGL-MIT.txt`](licenses/LICENSE-LVGL-MIT.txt).
  Enabled LVGL-bundled helpers: **TJpgDec** (JPEG decode, BSD-style (c) ChaN) and **qrcodegen**
  (QR codes, MIT (c) Project Nayuki) - texts in the repository
  [`../licenses/LICENSE-TJpgDec.txt`](../licenses/LICENSE-TJpgDec.txt) and
  [`../licenses/MIT-qrcodegen.txt`](../licenses/MIT-qrcodegen.txt).
- **FreeType 2.13.3:** FreeType License (FTL) -
  [`third_party/freetype-2.13.3/docs/FTL.TXT`](third_party/freetype-2.13.3/docs/FTL.TXT).
  This UI is based in part on the work of the FreeType Team. FreeType renders the stock
  `/usr/project/config/fonts/SourceHanSans-Regular.ttc` for CJK fallback.
- **SQLite amalgamation** (`sqlite3.c/.h`): public domain.
- **jsmn** (`jsmn.h`): MIT (Serge Zaitsev).
- **md5, RFC 1321** (`md5.c`): public domain (Alexander Peslyak).
- **Embedded fonts** (generated glyph arrays), all under the SIL Open Font License 1.1:
  - **Montserrat** - Latin UI text - [`licenses/OFL-1.1-Montserrat.txt`](licenses/OFL-1.1-Montserrat.txt)
  - **Source Han Sans** - CJK glyph fallback - [`licenses/OFL-1.1-SourceHanSans.txt`](licenses/OFL-1.1-SourceHanSans.txt)
  - **Noto Sans** - Latin-extended / Greek / Cyrillic glyphs (`font_intl_14.c` .. `font_intl_20.c`) - [`licenses/OFL-1.1-NotoSans.txt`](licenses/OFL-1.1-NotoSans.txt)
  - **Font Awesome 4.7.0** - UI + weather icon glyphs (`font_icons_20.c`, `font_icons_28.c`, `font_weather16.c`) - [`licenses/OFL-1.1-FontAwesome.txt`](licenses/OFL-1.1-FontAwesome.txt)
  - **IBM Plex Mono** (SemiBold/Bold) - "something" theme text (`font_mono_*.c`); converted under the name `diskos_mono`
    because the licence reserves the name "Plex" - [`licenses/OFL-1.1-IBMPlexMono.txt`](licenses/OFL-1.1-IBMPlexMono.txt)
  - **Doto** (weight 700, square dots) - "something" theme titles and date (`font_dot_*.c`), converted as `diskos_dot` - [`licenses/OFL-1.1-Doto.txt`](licenses/OFL-1.1-Doto.txt)
  - **Inter** (SemiBold) - hi-fi theme text (`font_hifi_*.c`) - [`licenses/OFL-1.1-Inter.txt`](licenses/OFL-1.1-Inter.txt)
  - **Archivo** (expanded, SemiBold / Light) - the diskOS wordmark on the boot splash (`font_brand_*.c`) - [`licenses/OFL-1.1-Archivo.txt`](licenses/OFL-1.1-Archivo.txt)
  - **DSEG7 Classic** (Bold) - hi-fi theme clock and time counters (`font_hifi_*.c`), converted without the name (Reserved Font Name "DSEG") - [`licenses/OFL-1.1-DSEG.txt`](licenses/OFL-1.1-DSEG.txt)
  - **JetBrains Mono** (Medium / ExtraBold) - terminal theme text and clock (`font_term_*.c`) - [`licenses/OFL-1.1-JetBrainsMono.txt`](licenses/OFL-1.1-JetBrainsMono.txt)
  - **Space Grotesk** (Medium / Bold) - bauhaus theme text, titles and clock (`font_bauh_*.c`) - [`licenses/OFL-1.1-SpaceGrotesk.txt`](licenses/OFL-1.1-SpaceGrotesk.txt)
  - **IBM Plex Sans** (Medium) - blueprint theme text (`font_blue_*.c`), converted without the name (Reserved Font Name "Plex") - [`licenses/OFL-1.1-IBMPlexSans.txt`](licenses/OFL-1.1-IBMPlexSans.txt)
  - **Nunito** (Bold / ExtraBold) - stone theme text and clock (`font_stone_*.c`) - [`licenses/OFL-1.1-Nunito.txt`](licenses/OFL-1.1-Nunito.txt)
  - **Courier Prime** (Regular / Bold) - zine theme text and clock (`font_zine_*.c`) - [`licenses/OFL-1.1-CourierPrime.txt`](licenses/OFL-1.1-CourierPrime.txt)
  - **Rubik Glitch** (Regular) - zine theme titles (`font_zine_*.c`) - [`licenses/OFL-1.1-RubikGlitch.txt`](licenses/OFL-1.1-RubikGlitch.txt)

The "something" theme is an independent design; it is not affiliated with or endorsed by Nothing Technology Limited.

Bundled components retain their own licences, including LGPL-2.1-or-later for libplist.
See [the complete component index](../NOTICE.md) and [licence texts](../licenses/README.md).

## Contributing
Issues and patches welcome via the [diskOS repo](https://github.com/b0hemia/diskos). By contributing
to this directory you agree your changes are licensed GPL-3.0-or-later.

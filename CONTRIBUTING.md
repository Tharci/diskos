# Contributing to diskOS

Contributions are welcome. Follow these rules for device safety and licensing.

## Ground rules

1. **Check hardware-affecting changes on a Snowsky Disc before sharing them.** Changes to the image
   builder, boot hook, NAND writer, SPL, or device targeting require a flash and boot on the device.
   Include the firmware version and DRAM/NAND variant.
2. **Never weaken the fail-closed contract.** The first-boot hook must always fall back to the stock
   UI when it cannot verify the UI against the baked manifest. The flasher must fail closed rather
   than commit an unverified or wrong-device write.
3. **Add an error code for every new failure mode** (`E1xx` preflight, `E2xx` build, `E3xx` flash,
   `F1xx` device-writer aborts, `F2xx` firmware-base refusals) and document it in [`docs/INSTALL.md`](docs/INSTALL.md#error-codes).
4. **Keep the honest-limitations tone.** If something is blocked by hardware or a toolchain gap, say
   so plainly.

## Licensing and provenance

- The diskOS UI source in [`ui/`](ui/) is **GPL-3.0-or-later**; the project's installer and build
  tooling are **MIT**. Bundled third-party components retain their own licenses; see [`NOTICE.md`](NOTICE.md).
  By contributing you agree your contribution is under the license that already governs the files
  you change. Please sign off your
  commits (`git commit -s`, Developer Certificate of Origin).
- **Do not upload FiiO firmware, stock rootfs images, or generated `diskos_*.bin` images** to issues
  or PRs - they contain FiiO's software. The installer builds images locally from a user's own
  firmware; that is fine, redistributing them is not.
- If you touch how a GPL/LGPL component is bundled (usbboot, squashfs-tools, the SPL, libusb),
  keep its corresponding source and `NOTICE.md` in sync.
- Do not commit device-identifying data (serials, MAC addresses) or secrets.

## Filing issues

Include: firmware version + hash, DRAM/NAND variant if known, host OS, the exact error code, the
bad-block count if the flasher printed one, and redacted logs. **Do not** post serial numbers, MAC
addresses, firmware zips, stock rootfs, or generated images.

## Development

See [`agents/AGENTS.md`](agents/AGENTS.md) for hardware facts, the repo layout, and device-safety
rules. Build the installer with `bash build/build.sh`.

### Python setup

The Python tooling is managed with [uv](https://docs.astral.sh/uv/). `pyproject.toml` declares the
dependencies and the tool settings, and `uv.lock` pins every package with its hash. End users never
need uv: `install.sh` installs `requirements.txt`, which is generated from the lock.

```sh
uv sync                       # .venv with the runtime and dev dependencies (Python from .python-version)
uv run pre-commit install     # run the checks below on every commit
uv run pytest                 # the test suite
```

The pre-commit hooks format and lint Python with **ruff**, type-check it with **ty**, check shell
scripts with **shellcheck**, keep `uv.lock` and `requirements.txt` in sync, and fix whitespace. CI
runs the same hooks plus the tests on Linux, macOS and Windows with Python 3.10 (the minimum) and
the newest Python.

- To change a dependency, edit `pyproject.toml` (runtime pins also need their `licenses/` and
  `NOTICE.md` entries updated: `tests/test_project.py` checks), then commit the updated
  `uv.lock` and `requirements.txt` that the hooks produce.
- Existing files are listed in a per-file **legacy baseline** at the end of the ruff and ty sections
  of `pyproject.toml`. New files get the full rule set. Never add a baseline entry: fix the code. When
  you clean a listed file, delete its entry.

For iterating on the on-device UI, see [`docs/DEV_WORKFLOW.md`](docs/DEV_WORKFLOW.md) (build, deploy over
SSH, hot-reload, and the watchdog trap to avoid) and the helper scripts in [`tools/`](tools/) for
deploying a build, capturing screenshots, and driving the touchscreen.

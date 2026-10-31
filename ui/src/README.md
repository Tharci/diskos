# diskOS UI: C++ modules

The UI is being rewritten from the C in `ui/` into C++23 here, one module at a time.

## Layout

One folder per area. A folder is created with its first module.

| Folder      | Holds                                                       | May use                 |
|-------------|-------------------------------------------------------------|-------------------------|
| `player/`   | The stock player's protocol: frames, messages, its state    | no other module         |
| `services/` | Playback, library, books, Bluetooth, Wi-Fi: logic and state | `player/`               |
| `platform/` | The device: message queues, files, input, display, processes | `player/`, `services/` (implements their interfaces) |
| `ui/`       | Screens and widgets (LVGL)                                  | `player/`, `services/`  |
| `app/`      | Startup and the main loop: connects the parts               | all of the above        |

`player/` and `services/` contain no LVGL and no system calls. They take what they need through small
interfaces, which `platform/` implements on the device and the tests implement with hand-written fakes.
So all protocol and state-machine logic runs on the host under the sanitizers.

A module is a static library declared in its folder's `CMakeLists.txt` with
`diskos_module(<name> SOURCES ...)`. Headers are included by path from `src/`:
`#include "player/frame.hpp"`. Its tests sit next to it as `<file>_test.cpp`, declared with
`diskos_test()`.

## Style

The tools enforce most of it: `.clang-format` (layout), `.clang-tidy` (naming, size limits,
bug patterns) and the warnings in `cmake/cxx.cmake`. What they cannot check:

- **Errors are values.** Device code is built without exceptions or RTTI. A function that can fail
  returns `std::expected<T, Error>` (or `std::optional<T>` when there is only one way to fail),
  with `Error` an `enum class` of the reasons.
- **A type is a struct or a class, never both.** A struct has only public data and no invariants.
  A class keeps all its data private, with a trailing `_`, and exposes
  member functions.
- **Small and single-purpose.** Functions stay short and files hold one idea. Split by responsibility,
  not by line count.
- **Public functions get a comment** saying what they do and what they return on failure, in the
  header. Implementation comments say why, not what, and only when extra context is required.
  Compact comments are preferred.
- **Every module has tests** for its behaviour, through its public interface. No need to test every
  small piece of logic and helper method.
- **Player quirks carry over.** The legacy C records many reverse-engineered player behaviours in
  its comments. When a module replaces legacy code, keep that knowledge, with its evidence.
- **No `std::format` or `std::print` in device code.** They add about 830 KB to the binary.
- **Silencing clang-tidy** takes `// NOLINT(check-name): reason`. A bare `NOLINT` fails the
  pre-commit hook.

## Building and testing

The device build (`cmake --preset device`) compiles the modules with the cross GCC.
`cmake --build --preset device -t tidy` runs clang-tidy on them with the device's compile
commands. The host presets build the modules and their tests with the host's GCC 14:

```sh
cmake --preset host && cmake --build --preset host && ctest --preset host           # ASan + UBSan
cmake --preset host-tsan && cmake --build --preset host-tsan && ctest --preset host-tsan  # TSan
```

Both need GCC 14, CMake and Catch2 v3, which the `ui/Dockerfile` image has (see `ui/README.md`).

TSan cannot start on kernels with high address-space randomisation (WSL, recent Ubuntu): it stops
with `unexpected memory mapping`. Run it with randomisation off, `setarch -R ctest --preset
host-tsan` (in Docker this also needs `--security-opt seccomp=unconfined`).

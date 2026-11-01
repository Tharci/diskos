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
- **Every module has tests** for its behaviour, through its public interface. No need to test every
  small piece of logic and helper method.
- **Player quirks carry over.** The legacy C records many reverse-engineered player behaviours in
  its comments. When a module replaces legacy code, keep that knowledge, with its evidence.
- **No `std::format` or `std::print` in device code.** They add about 830 KB to the binary.
- **Silencing clang-tidy** takes `// NOLINT(check-name): reason`. A bare `NOLINT` fails the
  pre-commit hook.

### Classes and names

- **Public first.** A class lists its public interface first and its private members last. When
  the public part grows, group it under short `//` labels such as `// Properties` and
  `// Configuration`.
- **Accessors.** A getter is named after what it returns (`current()`, `loop()`), a setter starts
  with `set_`. Accessors that cannot fail are `const noexcept`.
- **Data members** get a default initialiser, and a short trailing comment when the name alone does
  not say what they hold (`std::vector<MusicItem> queue_;  // The original queue, unsorted`).
- **Results that must be checked** are `[[nodiscard]]`. When `true` or `false` would not say what
  happened at the call site, return an `enum class` instead of a `bool`.
- **Long types** get a name with a `using` alias (`using OptionalMusicItemRef = ...`).
- **Enums** are `enum class` with an explicit underlying type
  (`enum class PlaybackMode : std::uint8_t`).
- **Full words in names.** The accepted abbreviations are `prev`, `idx` and unit suffixes such as
  `_ms`.
- **Includes**: the standard library first, then a blank line and the project headers, each included
  from `src/` (`"services/media/media_item.hpp"`), also from within the same folder.

### Comments

Public types and functions are documented in the header with a `/** ... */` comment. It starts
with an imperative summary, then states exactly what happens at the edges, as a list when there
are several outcomes. What a function returns on failure is always part of it. A class comment
also states who owns what the class hands out and how long references or views stay valid.

```cpp
/**
 * Step to the next song in the queue.
 * After the last song in the queue:
 *  - Returns nullopt, if loop is false or the queue is empty.
 *  - Steps to the first one otherwise.
 */
[[nodiscard]] OptionalMusicItemRef next();

/** Get the current song, or nullopt if the queue is empty. */
[[nodiscard]] OptionalMusicItemRef current() const;
```

Everything else uses `//`: section labels, trailing notes on members and enum values, TODOs
(`// TODO: ...`, continuation lines indented by one extra space) and implementation comments.
Implementation comments say why, not what, and only when extra context is required. Compact
comments are preferred.

## Building and testing

The device build (`cmake --preset device`) compiles the modules with the cross GCC.
`cmake --build --preset device -t tidy` and `-t cppcheck` analyse them with the device's
compile commands. The host presets build the modules and their tests with the host's GCC 14.
CI (`.github/workflows/ui.yml`) runs all of these.

```sh
cmake --preset host && cmake --build --preset host && ctest --preset host           # ASan + UBSan
cmake --preset host-tsan && cmake --build --preset host-tsan && ctest --preset host-tsan  # TSan
```

Both need GCC 14, CMake and Catch2 v3, which the `ui/Dockerfile` image has (see `ui/README.md`).

TSan cannot start on kernels with high address-space randomisation (WSL, recent Ubuntu): it stops
with `unexpected memory mapping`. Run it with randomisation off, `setarch -R ctest --preset
host-tsan` (in Docker this also needs `--security-opt seccomp=unconfined`).

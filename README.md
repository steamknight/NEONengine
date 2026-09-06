[![MIT][license-shield]][license-text]

# NEONengine
A C/C++ port of the NEONnoir engine, powered by ACE.

Very much in the early stages.

# MVPs

- https://github.com/AmigaPorts/ACE
- https://github.com/nandee95/Auto_Build_Number_Increment

# Building

Built with CMake + Ninja and the Bartman `m68k-amiga-elf` toolchain. Needs CMake 3.25 or newer.

On Windows, use native Windows CMake, not the MSYS version: MSYS misreads the
toolchain's Windows drive paths. The workspace selects
`C:\Program Files\CMake\bin\cmake.exe` through `cmake.cmakePath`, which the configure
tasks also use. New VS Code terminals put `%ProgramFiles%\CMake\bin` first on
`PATH`. If CMake lives elsewhere, update both workspace settings. Existing
terminals need to be reopened.

The toolchain lives outside the repo, so its paths are yours rather than the
project's. Copy the template and edit the three paths in it:

```
cp CMakeUserPresets.json.example CMakeUserPresets.json
```

Then, for the game or for the on-device test runner:

```
cmake --preset game    && cmake --build --preset game
cmake --preset tests   && cmake --build --preset tests
```

Both write to `build/`, and each reconfigures it in place — they differ by one
cache variable, so there is no second build directory and none should be made.
**Build both after changing a shared header**: the test runner compiles files the
game build does not.

Warnings are errors in our own sources. ACE is built with warnings off and its
headers arrive as `-isystem`, so anything the compiler reports is ours to answer.
Configure with `--compile-no-warning-as-error` to get through a build without
fixing one first.

ACE's asset converters (`palette_conv`, `bitmap_conv`, `font_conv`) are host tools under `modules/ACE/tools`, output to `modules/ACE/tools/bin/`, and are **not** rebuilt automatically. **After updating the ACE submodule, rebuild them** or asset conversion will silently use stale binaries (e.g. old-format `.plt` palettes the engine won't load):

```
cmake -S modules/ACE/tools -B modules/ACE/tools/build
cmake --build modules/ACE/tools/build --config Debug
```

[license-text]: https://opensource.org/license/mit/
[license-shield]: https://img.shields.io/badge/license-MIT-brightgreen

[![MIT][license-shield]][license-text]

# NEONengine
A C/C++ port of the NEONnoir engine, powered by ACE.

Very much in the early stages.

# MVPs

- https://github.com/AmigaPorts/ACE
- https://github.com/nandee95/Auto_Build_Number_Increment

# Building

Built with CMake + Ninja and the Bartman `m68k-amiga-elf` toolchain (VS Code task **CMake: build**).

ACE's asset converters (`palette_conv`, `bitmap_conv`, `font_conv`) are host tools under `modules/ACE/tools`, output to `modules/ACE/tools/bin/`, and are **not** rebuilt automatically. **After updating the ACE submodule, rebuild them** or asset conversion will silently use stale binaries (e.g. old-format `.plt` palettes the engine won't load):

```
cmake -S modules/ACE/tools -B modules/ACE/tools/build
cmake --build modules/ACE/tools/build --config Debug
```

[license-text]: https://opensource.org/license/mit/
[license-shield]: https://img.shields.io/badge/license-MIT-brightgreen

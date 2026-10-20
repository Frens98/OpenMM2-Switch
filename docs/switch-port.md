# Nintendo Switch port

This fork adds an unofficial Nintendo Switch homebrew target to
[OpenMM2](https://github.com/lowlevelmetal/OpenMM2), the GPL-3.0-or-later
reimplementation by lowlevelmetal and its contributors. The Switch port is
maintained separately from the upstream project's Linux and Windows builds.

## Install

1. Build `openmm2.nro` from source or download it from a Switch-port release.
2. Copy it to `sd:/switch/OpenMM2/openmm2.nro`.
3. Copy the game files from your own Midtown Madness 2 installation to
   `sd:/switch/OpenMM2/game/`. Keep the game's `.AR` archives together in that
   directory.
4. Launch OpenMM2 from your homebrew menu.

Settings and saves are stored under `sd:/switch/OpenMM2/user/`.

The NRO does not include the game's models, textures, sounds, music, text,
movie, or retail files. You must provide your own copy of Midtown Madness 2.
The port does not run or modify the original executable and does not bypass
its copy protection.

## Build

Install devkitPro with the Switch toolchain, `switch-sdl3`, EGL/GLES and the
other packages needed by the project's CMake dependencies. In a devkitPro
shell, configure and build with:

```sh
cmake --preset switch
cmake --build --preset switch
```

The NRO is written to `out/build/switch/bin/openmm2.nro`. The Switch preset
builds the OpenGL ES renderer and disables desktop-only tests and tools.
To create an SD-card ZIP with the NRO, install notes and required license
notices, run:

```sh
cmake --build --preset switch --target package
```

The ZIP contains `switch/OpenMM2/openmm2.nro`; it never includes game data.
See the [Switch roadmap](switch-roadmap.md) for planned controller and
performance work.

## Mods

OpenMM2 already mounts `.AR` archives found in the selected game directory,
including add-on archives. Keep any mods in their own folder until you have
checked what they contain, then install only the archives you choose. Do not
share game files or mod packs that include retail game data.

## Credits and trademarks

- OpenMM2 upstream: lowlevelmetal and all upstream contributors.
- Nintendo Switch port and packaging: Frens98.
- OpenMM2 source is distributed under GNU GPL version 3 or later; see
  [LICENSE](../LICENSE).
- Midtown Madness 2 and related content are owned by their respective rights
  holders. This project is not affiliated with or endorsed by Microsoft,
  Rockstar Games, Angel Studios, or Nintendo.

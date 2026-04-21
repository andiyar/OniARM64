# OniARM64

Native ARM64 / Apple Silicon port of Oni (Bungie, 2001).

## Status

Work in progress. The binary compiles and runs as native ARM64 on macOS.

As of the latest commit:

- Full init pipeline runs through every subsystem — Template Manager, Sound System 2, Motoko 3D, Physics, Animation, Environment, Text, AI 2, Window Manager, Scripting, Cinematics.
- Main menu renders and accepts input (SDL2 / OpenAL / OpenGL path).
- **New Game → Start** loads the first level (`warehouse`), runs its init script (`warehouse_main.bsl`) to completion, and clears the splash screen.
- Crashes inside the main game loop (`ONiRunGame`) before any gameplay frame renders. Investigation in progress.

Most fixes so far chase a single class of bug: the original codebase assumes 32-bit pointers, and the modern 64-bit ABI surfaces every `UUtUns32`-holding-a-pointer site as a truncation crash.

## Requirements

- Apple Silicon Mac (M-series)
- macOS 13+ (Ventura / Sonoma / Sequoia)
- Homebrew with `cmake` and `sdl2`
- A legitimate copy of Oni. This repo contains **no game assets** — you'll need the original `.dat` / `.raw` / `.sep` data files from your own install (Anniversary Edition, a Windows CrossOver / Wine prefix, or the retail CD).

## Build

```sh
cd build
cmake .. -DPlatform_SDL=ON
make -j8
```

Binary lands at `build/bin/Oni`. Copy it into a directory that contains (or symlinks to) the game data and run it there:

```sh
cp build/bin/Oni /path/to/oni-data/
cd /path/to/oni-data
./Oni
```

Crash-report-less debugging: macOS doesn't emit `.ips` reports for the ad-hoc-signed binary under the sandbox, so `lldb ./Oni` is the usual way to get a backtrace. Sparse observability breadcrumbs are compiled in and write to `startup.txt` in the run directory.

## Scope

Intentionally minimal — the goal is a working port, not a remaster.

In scope for Anniversary Edition features:
- Developer mode
- Widescreen
- FPS smoothing
- Texture pack support

Out of scope:
- Multiplayer / netcode
- Modernized renderer / shaders
- New content or gameplay changes
- Windows / Linux build targets (handled by upstream / other forks)

## Origin

Originally forked from [hogsy/OniFoxed](https://github.com/hogsy/OniFoxed), which derives from the 2001 Oni source release. Divergence is significant at this point — the Window Manager message API, the Template Manager bridge layer, the memory allocator, the OpenAL sound init, and numerous 64-bit-pointer sites are all rewritten. This fork is not tracking upstream.

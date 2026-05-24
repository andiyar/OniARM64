# Third-party software licenses

`OniARM64.app` redistributes the following libraries in `Contents/Frameworks/`, dynamically linked from `Contents/MacOS/Oni` via `@executable_path/`. Each library retains its original copyright and is governed by the license shown below.

The README's [Bundled third-party software section](README.md#bundled-third-party-software) carries the at-a-glance summary table. This file carries the full license texts required for compliance.

For convenience, this file is also copied into the redistributed bundle at `Contents/Resources/THIRD_PARTY_LICENSES.txt`.

> **No GPL components.** As of 2026-05-24 ([issue #19](https://github.com/andiyar/OniARM64/issues/19)) the bundled `ffmpeg` is built from source via [`scripts/build-ffmpeg.sh`](scripts/build-ffmpeg.sh) with `--disable-gpl --disable-nonfree --disable-everything --enable-decoder=adpcm_ms`. The only enabled component is the ADPCM_MS audio decoder. None of x264, x265, libvpx, dav1d, SVT-AV1, mp3lame, Opus, OpenSSL, or any other Homebrew transitive dep ends up in the bundle.

---

## SDL2

- **Version bundled:** 2.32.10 (from Homebrew's `sdl2` formula)
- **Homepage:** https://www.libsdl.org/
- **Source:** https://github.com/libsdl-org/SDL
- **SPDX identifier:** Zlib
- **Required obligations:** Retain copyright notice. Do not misrepresent origin. Alterations must be marked as such.

<details>
<summary>Full license text</summary>

```
Simple DirectMedia Layer
Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the
use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

3. This notice may not be removed or altered from any source distribution.
```

</details>

---

## FFmpeg — libavcodec, libavutil (custom minimal build)

- **Version bundled:** 8.1.1 (matches the Homebrew bottle OniARM64 was historically tested with)
- **Homepage:** https://ffmpeg.org/
- **Source:** https://ffmpeg.org/releases/ffmpeg-8.1.1.tar.xz (canonical upstream)
- **Build script:** [`scripts/build-ffmpeg.sh`](scripts/build-ffmpeg.sh) (reproducible, repo-local; output lives in `extern/ffmpeg/`, gitignored)
- **SPDX identifier:** LGPL-2.1-or-later
- **Configure flags:** `--disable-gpl --disable-nonfree --enable-pic --enable-shared --disable-static --disable-doc --disable-programs --disable-everything --enable-decoder=adpcm_ms` plus a long list of explicit `--disable-*` flags that strip every unused library, codec, container, protocol, encoder, filter, and Apple framework integration. Result: libavcodec is ~350 KB and libavutil is ~720 KB — vs Homebrew's ~10 MB+ default build.
- **Required obligations:** Distribute under LGPL-2.1 terms. Dynamic linking (which OniARM64 uses) satisfies the "user can replace the library" requirement without source-disclosure of the calling application.

**Replacement provision:** To replace these libraries with modified or alternative versions, substitute the corresponding `.dylib` file in `OniARM64.app/Contents/Frameworks/`. No relinking of the main `Oni` binary is required — the load commands resolve to `@executable_path/../Frameworks/<name>.dylib` at runtime.

**Full license text:** https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt (LGPL-2.1)

**Corresponding source:** the canonical upstream tarball at the source URL above. The repo's `scripts/build-ffmpeg.sh` downloads and builds it deterministically; running the script reproduces the bundled dylibs bit-for-bit modulo build-environment differences.

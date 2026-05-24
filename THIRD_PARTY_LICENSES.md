# Third-party software licenses

`OniARM64.app` redistributes the following libraries from Homebrew. They live in `Contents/Frameworks/` of the `.app` bundle, dynamically linked from `Contents/MacOS/Oni` via `@executable_path/`. Each library retains its original copyright and is governed by the license shown below.

The README's [Bundled third-party software section](README.md#bundled-third-party-software) carries the at-a-glance summary table. This file carries the full license texts required for compliance.

For convenience, this file is also copied into the redistributed bundle at `Contents/Resources/THIRD_PARTY_LICENSES.txt`.

> **Note on GPL components:** `libx264` and `libx265` (GPL-2.0-or-later) are transitively pulled in by Homebrew's GPL-enabled `ffmpeg` build. Oni does not call them at runtime — only the audio + container-demux parts of ffmpeg are used. The bundled-x264/x265 cleanup path is tracked in [issue #19](https://github.com/andiyar/OniARM64/issues/19). Until that lands, the redistributed bundle is subject to GPL terms; corresponding source is available via Homebrew's formula repository at https://github.com/Homebrew/homebrew-core and the upstream projects linked in each component's entry below.

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

## FFmpeg — libavcodec, libavutil, libswresample

- **Version bundled:** 8.1.1 (from Homebrew's `ffmpeg` formula)
- **Homepage:** https://ffmpeg.org/
- **Source:** https://git.ffmpeg.org/ffmpeg.git
- **SPDX identifier:** LGPL-2.1-or-later (for the three libraries OniARM64 uses; ffmpeg as a whole is GPL-3.0-or-later in the Homebrew build because of GPL-enabled options)
- **Required obligations:** Distribute under LGPL terms. Dynamic linking (which OniARM64 uses) satisfies the "user can replace the library" requirement without source-disclosure of the calling application.

**Replacement provision:** To replace these libraries with modified or alternative versions, substitute the corresponding `.dylib` file in `OniARM64.app/Contents/Frameworks/`. No relinking of the main `Oni` binary is required — the load commands resolve to `@executable_path/../Frameworks/<name>.dylib` at runtime.

**Full license text:** https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt (LGPL-2.1)
**Corresponding source:** https://github.com/Homebrew/homebrew-core/blob/master/Formula/f/ffmpeg.rb (Homebrew formula, which references the upstream tarball) and https://ffmpeg.org/download.html (canonical upstream).

---

## LAME — libmp3lame

- **Version bundled:** 3.100 (from Homebrew's `lame` formula)
- **Homepage:** https://lame.sourceforge.io/
- **Source:** https://sourceforge.net/projects/lame/files/lame/
- **SPDX identifier:** LGPL-2.0-or-later
- **Required obligations:** Distribute under LGPL terms. Dynamic linking satisfies the relink provision (see FFmpeg entry above).

**Replacement provision:** Replace `OniARM64.app/Contents/Frameworks/libmp3lame.0.dylib` to swap in an alternative build.

**Full license text:** https://www.gnu.org/licenses/old-licenses/lgpl-2.0.txt (LGPL-2.0)
**Corresponding source:** https://github.com/Homebrew/homebrew-core/blob/master/Formula/l/lame.rb (Homebrew formula).

---

## Opus — libopus

- **Version bundled:** 1.6.1 (from Homebrew's `opus` formula)
- **Homepage:** https://opus-codec.org/
- **Source:** https://gitlab.xiph.org/xiph/opus
- **SPDX identifier:** BSD-3-Clause

<details>
<summary>Full license text</summary>

```
Copyright 2001-2023 Xiph.Org, Skype Limited, Octasic,
                    Jean-Marc Valin, Timothy B. Terriberry,
                    CSIRO, Gregory Maxwell, Mark Borgerding,
                    Erik de Castro Lopo

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

- Neither the name of Internet Society, IETF or IETF Trust, nor the
names of specific contributors, may be used to endorse or promote
products derived from this software without specific prior written
permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

</details>

---

## libvpx

- **Version bundled:** 1.16.0 (from Homebrew's `libvpx` formula)
- **Homepage:** https://www.webmproject.org/code/
- **Source:** https://chromium.googlesource.com/webm/libvpx
- **SPDX identifier:** BSD-3-Clause

<details>
<summary>Full license text</summary>

```
Copyright (c) 2010, The WebM Project authors. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in
    the documentation and/or other materials provided with the
    distribution.

  * Neither the name of Google, nor the WebM Project, nor the names
    of its contributors may be used to endorse or promote products
    derived from this software without specific prior written
    permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

</details>

---

## dav1d — libdav1d

- **Version bundled:** 1.5.3 (from Homebrew's `dav1d` formula)
- **Homepage:** https://www.videolan.org/projects/dav1d.html
- **Source:** https://code.videolan.org/videolan/dav1d
- **SPDX identifier:** BSD-2-Clause

<details>
<summary>Full license text</summary>

```
Copyright © 2018-2019, VideoLAN and dav1d authors
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

</details>

---

## SVT-AV1 — libSvtAv1Enc

- **Version bundled:** 4.1.0 (from Homebrew's `svt-av1` formula)
- **Homepage:** https://gitlab.com/AOMediaCodec/SVT-AV1
- **Source:** https://gitlab.com/AOMediaCodec/SVT-AV1
- **SPDX identifier:** BSD-3-Clause-Clear with additional AV1 patent license

**License notes:** Beyond standard BSD-3-Clause-Clear terms, this library is also subject to the Alliance for Open Media Patent License 1.0, which grants royalty-free use of AV1 essential patents. See the upstream `LICENSE.md` (https://gitlab.com/AOMediaCodec/SVT-AV1/-/blob/master/LICENSE.md) and `PATENTS.md` (https://gitlab.com/AOMediaCodec/SVT-AV1/-/blob/master/PATENTS.md) for the canonical texts.

---

## OpenSSL — libssl, libcrypto

- **Version bundled:** 3.6.2 (from Homebrew's `openssl@3` formula)
- **Homepage:** https://www.openssl.org/
- **Source:** https://github.com/openssl/openssl
- **SPDX identifier:** Apache-2.0

**License notes:** OpenSSL 3.x is licensed under Apache 2.0 (a change from the dual OpenSSL/SSLeay license of 1.x). The Apache 2.0 license requires preservation of copyright, patent, trademark, and attribution notices; any modifications carry a notice; the included NOTICE file is preserved in redistributions.

**Full license text:** https://www.apache.org/licenses/LICENSE-2.0.txt (Apache 2.0 canonical)
**Upstream NOTICE file:** https://github.com/openssl/openssl/blob/master/NOTICE.md
**Corresponding source:** https://github.com/openssl/openssl

---

## x264 — libx264

- **Version bundled:** r3222 (from Homebrew's `x264` formula, transitive dep of `ffmpeg --enable-gpl`)
- **Homepage:** https://www.videolan.org/developers/x264.html
- **Source:** https://code.videolan.org/videolan/x264
- **SPDX identifier:** GPL-2.0-or-later
- **Cleanup status:** Tracked in [issue #19](https://github.com/andiyar/OniARM64/issues/19). Oni does not call x264 at runtime; it is inert weight pulled in transitively. The fix is to either rebuild ffmpeg without `--enable-gpl` or move cinematic playback to AVFoundation entirely (the latter would drop ffmpeg and 12 dylibs in one stroke).

**Corresponding Source notice** (per GPL section 3): the complete corresponding source for `libx264` as bundled is available from:

- Homebrew formula (build recipe): https://github.com/Homebrew/homebrew-core/blob/master/Formula/x/x264.rb
- Upstream source repository: https://code.videolan.org/videolan/x264

**Full license text:** https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt (GPL-2.0)

---

## x265 — libx265

- **Version bundled:** 4.2 (from Homebrew's `x265` formula, transitive dep of `ffmpeg --enable-gpl`)
- **Homepage:** http://x265.org/
- **Source:** https://bitbucket.org/multicoreware/x265_git/src/master/
- **SPDX identifier:** GPL-2.0-or-later
- **Cleanup status:** Same as x264 — tracked in [issue #19](https://github.com/andiyar/OniARM64/issues/19). Inert weight; cleanup is the same fix.

**Corresponding Source notice** (per GPL section 3): the complete corresponding source for `libx265` as bundled is available from:

- Homebrew formula (build recipe): https://github.com/Homebrew/homebrew-core/blob/master/Formula/x/x265.rb
- Upstream source repository: https://bitbucket.org/multicoreware/x265_git/src/master/

**Full license text:** https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt (GPL-2.0)

---

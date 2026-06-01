# Mac Retail Data Support — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the original 2001 Mac retail `GameDataFolder` load and play on the ARM64 port with no PC/Windows data required.

**Architecture:** The 32→64 template bridge applies one compiled PC layout per template. The 4 templates whose Mac on-disk layout differs (SNDD, OSBD, BINA, TXMP) get an **alternate translation, selected per source file**: a new `isMac` flag on `TMtInstanceFile` (set from the file's template checksum at load), and a new `BFW_TM_MacLayout.c` that owns all Mac-record→engine-struct translation. The bridge translate site gains one branch; everything else is untouched, so the PC/CX shipping path stays byte-for-byte identical.

**Tech Stack:** C, ARM64 macOS, little-endian. Bungie template manager + 32→64 bridge. OniSplit (C#/mono) as the offline format oracle. No unit-test framework — see Verification Model.

---

## Status (2026-06-01)

**Stage 1 (SNDD) — DONE, user-verified.** Mac level 1 loads + plays clean (menu music + in-level gunfire), no SIGSEGV; PC/CX regression-checked unchanged. Landed in HISTORY session 43.

The **IMA4 byte-order** work (originally Stage 4) got pulled into Stage 1: stopping the crash immediately exposed uniform static, root-caused to the big-endian packet state word (commit `5ca88b2` had removed the swap for PC). Fixed via a per-sound `SScSoundDataFlag_MacIMA4` gated swap — so Stage 4's by-ear question is now answered (Mac IMA4 **is** big-endian; PC stays unswapped, zero regression). Remaining: **Stage 2 (OSBD/BINA)** + **Stage 3 (TXMP, needs visual verify)**.

---

## Conventions for this plan (read first)

- **Spec:** [`docs/mac-data-support-spec.md`](mac-data-support-spec.md) is the design of record (§5 resolved, §5a verified code map). This plan implements it.
- **Branch/worktree:** work directly on `main` (project convention — source commits go to `andiyar/OniARM64` `main`). The uncommitted cutscene WIP (`Oni_Camera.c`, `Oni_GameState.c`, `BFW_Akira_Render.c`, `BFW_ScriptLang_*`, `Oni_AI_Script.c`, `Oni_Level.c`) does **not** overlap any file this plan touches — **stage only the files named in each Commit step**, never `git add -A`.
- **Verification Model (replaces TDD):** there is no ctest/gtest harness. Each task's "test" is one or more of:
  - **(B) Build** — `cd build && make -j8` (clean compile + link).
  - **(O) Offline oracle** — `OniSplit -export`/`-extract` on the Mac `.dat` gives ground-truth field values to diff against engine output. No game launch.
  - **(V) Behavioural verify — GATED.** Launching the game is gated by the project's launch rule. **Claude does NOT launch the game unprompted.** The behavioural-verify step switches the data symlink to Mac data and **asks the user** to run it (or to OK a capture task), then reads the `startup.txt`/`debugger.txt` they produce. Use `addresses #37` on commits until the user confirms; never `fixes #37` before user verdict.
- **Regression guard:** after each stage, re-point the symlink at PC/CX data and confirm it still loads (the `isMac=false` short-circuit must leave the PC path unchanged).
- **Issue discipline:** every behaviour-changing commit references `#37`. Comment on #37 when a stage's fix attempt ships.

## Data switching (used by every (V) step)

```sh
# → Mac retail disc data:
rm ~/Library/Application\ Support/OniARM64/gamedata
ln -s /Volumes/Oni/Oni\ ƒ/GameDataFolder ~/Library/Application\ Support/OniARM64/gamedata
# → PC/CX data (the working shipping path, for the regression guard):
rm ~/Library/Application\ Support/OniARM64/gamedata
ln -s "/Users/andiyar/Developer/oni/CXOni/Oni/drive_c/Program Files (x86)/Oni/GameDataFolder" ~/Library/Application\ Support/OniARM64/gamedata
```
Mac disc is currently mounted at `/Volumes/Oni` (verified — `.dat`/`.raw`/`.sep` present for all levels).

---

## File Structure

**Created:**
- `BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_MacLayout.h` — public surface: the Mac checksum constants + `TMrMacLayout_Translate`. Includes only `BFW.h` (kept free of subsystem headers so `BFW_TM_Game.c` doesn't pull in sound/texture headers).
- `BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_MacLayout.c` — owns ALL Mac record→engine-struct translation. Includes the subsystem headers it needs (sound now; texture/binary later). One `switch (tag)` dispatch; one static `iTranslate<TAG>` per template. This file grows one case per stage; the bridge call site never changes after Stage 1.

**Modified:**
- `BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_Game.c` — add `isMac` to `TMtInstanceFile`; set it from the header checksum at load; add the one translate-site branch.
- `OniProj/OniCMakeProjs/OniProj/CMakeLists.txt` — add the two new files to the explicit source list (sources are listed, not globbed).

**Per-stage additions** to `BFW_TM_MacLayout.c` (Stages 2–4) and possibly a `.sep` read helper (Stage 2) — detailed in their tasks.

---

## STAGE 1 — SNDD (stops the crash)

**Outcome:** Mac level 1 loads and plays `mus_asian` (the first stereo SNDD) with no SIGSEGV. This is the forced-first stage — the crash blocks reaching anything else. Build + verify before Stage 2.

### Task 1.1: Detector — `isMac` on `TMtInstanceFile`

**Files:**
- Create: `BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_MacLayout.h`
- Modify: `BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_Game.c` (struct ~line 186; alloc ~1638; header load ~1696)

- [ ] **Step 1: Create the MacLayout header with the checksum constants.**

`BFW_TM_MacLayout.h`:
```c
#ifndef BFW_TM_MACLAYOUT_H
#define BFW_TM_MACLAYOUT_H

#include "BFW.h"

/* Per-file template-checksum family discriminators (first 8 bytes of a
   TMtInstanceFile_Header). Mac and PC retail ship different layout families
   for SNDD/OSBD/BINA/TXMP; everything else shares one layout. */
#define TMcMacTemplateChecksum  0x0003bcdf23c13061ULL
#define TMcPCTemplateChecksum   0x0003bcdf33dc271fULL

/*
 * Translate one Mac retail on-disk instance record into its 64-bit engine
 * struct, selected by template tag. inSrcRecord / outDstRecord both point at
 * the 8-byte container preamble; the template body follows at +TMcPreDataSize.
 * Returns UUcTrue if inTag is a Mac-divergent template this function handled
 * (caller skips the normal PC bridge translate), UUcFalse to fall through to
 * the PC path unchanged.
 */
UUtBool
TMrMacLayout_Translate(
    UUtUns32     inTag,
    const void  *inSrcRecord,
    void        *outDstRecord,
    UUtBool      inNeedsSwapping);

#endif /* BFW_TM_MACLAYOUT_H */
```

> `totalTemplateChecksum` is a `UUtUns64` (verified at `BFW_TM_Private.h:114`); no 64-bit constructor macro exists in the tree, so these are plain `ULL` literals, compared host-endian (after `LoadHeaderFromMemory` applies any swap on big-endian hosts — a no-op here).

- [ ] **Step 2: Add the `isMac` field to `TMtInstanceFile`.**

In `BFW_TM_Game.c`, in `struct TMtInstanceFile` (the `final` / `preparedForMemory` block, ~line 186–187), add a sibling flag:
```c
		UUtBool						final;
		UUtBool						preparedForMemory;
		UUtBool						isMac;					/* Mac retail data: select Mac on-disk layout for SNDD/OSBD/BINA/TXMP */
```

- [ ] **Step 3: Initialize `isMac` false at allocation, set true on Mac checksum.**

In `TMiGame_InstanceFile_New_FromFileRef`, right after the struct is allocated (`newInstanceFile = ... UUrMemory_Block_New(...)`, ~line 1637–1638):
```c
		newInstanceFile = (TMtInstanceFile *)UUrMemory_Block_New(sizeof(TMtInstanceFile));
		UUmError_ReturnOnNull(newInstanceFile);
		newInstanceFile->isMac = UUcFalse;
```
Then, immediately after the header is loaded (`error = TMiGame_InstanceFile_LoadHeaderFromMemory(fileHeader, &needsSwapping);`, ~line 1696):
```c
		/* Mac retail data detector: retain whether this file carries the Mac
		   template-checksum family so the bridge translate site can select
		   the Mac on-disk layout for the templates that differ. The checksum
		   is host-endian here (LoadHeaderFromMemory already swapped it). */
		newInstanceFile->isMac =
			(fileHeader->totalTemplateChecksum == TMcMacTemplateChecksum) ? UUcTrue : UUcFalse;
```
Add the include near the other TM includes at the top of `BFW_TM_Game.c`:
```c
#include "BFW_TM_MacLayout.h"
```

> If there is a second `TMtInstanceFile` creation path (dynamic instance files — grep `UUrMemory_Block_New(sizeof(TMtInstanceFile))`), add `->isMac = UUcFalse;` there too. (Belt-and-suspenders; `isMac` only matters on the load path, but an uninitialized bool is a latent bug.)

- [ ] **Step 4 (B): Build to confirm the struct + header compile.** At this point `TMrMacLayout_Translate` is declared but undefined — Step 1.2 defines it before any call site exists, so build *after* 1.2. Skip the build here; proceed to Task 1.2.

### Task 1.2: `BFW_TM_MacLayout.c` — SNDD translation + CMake

**Files:**
- Create: `BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_MacLayout.c`
- Modify: `OniProj/OniCMakeProjs/OniProj/CMakeLists.txt:112`

- [ ] **Step 1: Write the SNDD translator.**

`BFW_TM_MacLayout.c`:
```c
#include "BFW.h"
#include "BFW_TemplateManager.h"
#include "BFW_TM_Private.h"      /* TMcPreDataSize */
#include "BFW_TM_MacLayout.h"
#include "BFW_SoundSystem2.h"    /* SStSoundData, SScWaveFormat_PCM, SScSoundDataFlag_* */

#include <stdint.h>              /* uintptr_t */
#include <string.h>             /* memcpy */

/*
 * Mac SNDD on-disk body (16 bytes, little-endian), per OniSplit
 * OniMacMetadata.cs `sndd` + Sound/SoundData.cs `if (sndd.IsMacFile)`:
 *   @0  Flags     (int32)  channel count = (Flags >> 1) + 1; bit0 = compressed
 *   @4  Duration  (int32)  game-ticks
 *   @8  DataSize  (int32)  sample byte count   -> SStSoundData.num_bytes
 *   @12 DataOffset(int32)  offset into .raw    -> SStSoundData.data (resolved later)
 * Samples are Apple IMA4 in the level .raw (NOT .sep) — so LoadPostProcess
 * (data = rawPtr + offset) and the IMA4 decode path are unchanged; the only
 * defect was a garbage num_bytes read through the 72-byte PC SNDD layout.
 */
static void
iTranslateSNDD(const UUtUns8 *inSrcBody, SStSoundData *outSnd, UUtBool inSwap)
{
	UUtInt32 mac_flags, mac_duration, mac_datasize, mac_dataoffset;
	UUtUns32 channels;

	memcpy(&mac_flags,      inSrcBody +  0, 4);
	memcpy(&mac_duration,   inSrcBody +  4, 4);
	memcpy(&mac_datasize,   inSrcBody +  8, 4);
	memcpy(&mac_dataoffset, inSrcBody + 12, 4);

	if (inSwap) {
		UUrSwap_4Byte(&mac_flags);
		UUrSwap_4Byte(&mac_duration);
		UUrSwap_4Byte(&mac_datasize);
		UUrSwap_4Byte(&mac_dataoffset);
	}

	channels = ((UUtUns32)mac_flags >> 1) + 1;
	if (channels < 1) channels = 1;
	else if (channels > 2) channels = 2;

	/* Populate the shared 72-byte engine struct. wFormatTag must be != 2 so
	   SS2rPlatform_SoundChannel_SetSoundData takes the Apple-IMA4 branch, not
	   ffmpeg MS-ADPCM. nChannels drives both the channel count and (via the
	   Stereo flag) SSrSound_IsStereo. The IMA path uses the SScSamplesPerSecond
	   constant for the AL buffer, so no sample rate is synthesized here. */
	outSnd->flags          = (channels == 2) ? SScSoundDataFlag_Stereo : SScSoundDataFlag_None;
	outSnd->f.wFormatTag   = SScWaveFormat_PCM;            /* 0x0001, != 2 */
	outSnd->f.nChannels    = (UUtUns16)channels;
	outSnd->duration_ticks = (UUtUns16)mac_duration;
	outSnd->num_bytes      = (UUtUns32)mac_datasize;
	outSnd->data           = (void *)(uintptr_t)(UUtUns32)mac_dataoffset; /* .raw offset; LoadPostProcess adds rawPtr */
}

UUtBool
TMrMacLayout_Translate(
	UUtUns32     inTag,
	const void  *inSrcRecord,
	void        *outDstRecord,
	UUtBool      inNeedsSwapping)
{
	const UUtUns8 *src = (const UUtUns8 *)inSrcRecord;
	UUtUns8       *dst = (UUtUns8 *)outDstRecord;

	switch (inTag) {
	case UUm4CharToUns32('S','N','D','D'):
	{
		/* Copy the 8-byte container preamble verbatim (placeholder + fileID),
		   mirroring the PC walker's first two 4-byte fields. Downstream
		   TMrInstance_GetRawOffset reads this preamble to resolve the owning
		   instance file — if it is left zero, the sample pointer resolves
		   wrong and the fix is undone. */
		UUtUns32 pre0, pre1;
		memcpy(&pre0, src + 0, 4);
		memcpy(&pre1, src + 4, 4);
		if (inNeedsSwapping) { UUrSwap_4Byte(&pre0); UUrSwap_4Byte(&pre1); }
		memcpy(dst + 0, &pre0, 4);
		memcpy(dst + 4, &pre1, 4);

		iTranslateSNDD(src + TMcPreDataSize,
		               (SStSoundData *)(dst + TMcPreDataSize),
		               inNeedsSwapping);
		return UUcTrue;
	}
	default:
		return UUcFalse;
	}
}
```

> Confirm `UUtInt32` / `UUtUns32` / `UUtUns16` / `UUrSwap_4Byte` / `UUm4CharToUns32` names against `BFW.h` (all used elsewhere in this dir, so they exist). If `memcpy` is discouraged here, the codebase also uses it directly in `BFW_TM_Game.c` (e.g. line 1857), so it is fine.

- [ ] **Step 2: Add both new files to the game build.**

In `OniProj/OniCMakeProjs/OniProj/CMakeLists.txt`, after the `BFW_TM_Bridge.c` / `BFW_TM_Bridge.h` lines (~112–113):
```cmake
        ../../../BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_Bridge.c
        ../../../BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_Bridge.h
        ../../../BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_MacLayout.c
        ../../../BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_MacLayout.h
```

- [ ] **Step 3 (B): Re-run CMake (new source file) and build.**
```sh
cd /Users/andiyar/Developer/oni/OniARM64/build
cmake .. -DPlatform_SDL=ON && make -j8
```
Expected: clean compile of `BFW_TM_MacLayout.c` + link. If `BFW_SoundSystem2.h` triggers an include error from this dir, fall back to including `Oni_Sound2.h`'s chain or move the SNDD struct knowledge behind raw-offset writes (offsets: preamble 8; within struct body wFormatTag@4, nChannels@6, duration@54, num_bytes@56, data@64 — all + `TMcPreDataSize` from `dst`). Prefer the struct-field version; only fall back if includes genuinely cycle.

### Task 1.3: Wire the translate-site branch

**Files:**
- Modify: `BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_Game.c` (~line 1917)

- [ ] **Step 1: Gate the PC translate behind the Mac path.**

At the `TMrBridge_TranslateInstance(...)` call (~line 1917, immediately after the `[TM-SNDD]` diagnostic block), wrap it:
```c
			if (!(newInstanceFile->isMac &&
			      TMrMacLayout_Translate(tag,
			                             src_data - TMcPreDataSize,
			                             dst_preamble,
			                             needsSwapping)))
			{
				TMrBridge_TranslateInstance(lyt,
					src_data - TMcPreDataSize,
					dst_preamble,
					needsSwapping,
					var_count);
			}
```
Everything before this (lazy `lyt` build, `var_count` extraction, `per_inst_dst` sizing, the `memset`, the `[TM-SNDD]` diag) is unchanged — `lyt->dst_size` (= 8 + 72 for SNDD) still governs allocation/zeroing, which is correct because the dst is the shared engine struct either way. PC files have `isMac == UUcFalse`, so the `&&` short-circuits and the PC path is byte-for-byte unchanged.

- [ ] **Step 2 (B): Build.**
```sh
cd /Users/andiyar/Developer/oni/OniARM64/build && make -j8
```
Expected: clean compile + link, no warnings about `TMrMacLayout_Translate`.

### Task 1.4 (O): Offline oracle — validate parsing without launching

**Files:** none (read-only oracle).

- [ ] **Step 1: Locate OniSplit + export a Mac level's SNDDs.**
```sh
find /Users/andiyar/Developer/oni -iname 'OniSplit.exe' 2>/dev/null | head
# Export level1 (first level with a stereo SNDD, mus_asian):
mono <path>/OniSplit.exe -export /tmp/mac_l1 "/Volumes/Oni/Oni ƒ/GameDataFolder/level1_Final.dat"
ls /tmp/mac_l1 | grep -i sndd | head
```
- [ ] **Step 2: Record ground truth for `mus_asian`.** From the exported SNDD (OniSplit prints/encodes channel count, duration, DataSize, DataOffset), note the expected `num_bytes` (DataSize) and channel count. This is the oracle the engine's `[TM-SNDD]`/`SNDD LoadPost` diagnostics get diffed against. Expected (per root cause §1): with the fix, the engine's logged `num_bytes` for each SNDD should match OniSplit's DataSize (no longer the bogus `0xDEAD`-striding values), and `mus_asian` should report 2 channels.

> This step needs no game launch and is not gated. If `mono`/OniSplit is unavailable, skip and rely on the (V) step's `startup.txt` diagnostics, but note the gap.

### Task 1.5 (V): Behavioural verify (GATED) + commit

**Files:** commit of Tasks 1.1–1.3.

- [ ] **Step 1: Point data at the Mac disc** (see Data switching). Confirm the symlink resolves.
- [ ] **Step 2: ASK THE USER to launch** the `.app` with Mac data and report whether (a) it reaches the main menu / level 1 without SIGSEGV and (b) level-1 music (`mus_asian`) plays. Do **not** launch it yourself unless the user has set up a driving task. While waiting, read any `startup.txt`/`debugger.txt` they produce: confirm `[TM-SNDD]` shows real 16-byte Mac records and `SNDD LoadPost` shows sane `num_bytes` (matching Task 1.4), and that the previous `SSiIMA_DecompressSoundData_Stereo` SIGSEGV is gone.
- [ ] **Step 3: Regression guard.** Point the symlink back at PC/CX data; ASK the user (or reuse a driving task) to confirm PC data still loads + plays normally. The `isMac=false` short-circuit must leave it unchanged.
- [ ] **Step 4: Commit (stage only the 4 files).**
```sh
cd /Users/andiyar/Developer/oni/OniARM64
git add BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_MacLayout.c \
        BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_MacLayout.h \
        BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_Game.c \
        OniProj/OniCMakeProjs/OniProj/CMakeLists.txt
git commit  # message: "feat(mac-data): Mac SNDD layout — engine-native IMA4 load (addresses #37)"
```
Use `addresses #37` (not `fixes`) until the user confirms. Update [`HISTORY.md`](HISTORY.md) (new session bullet) and tick the relevant README milestone in the **same commit**. Comment on #37 with the result + commit SHA. Push to `origin main` after the user's positive verdict.

---

## STAGE 2 — OSBD + BINA (`.sep` binary data)

**Outcome:** ambient/group audio (OSBD) + particles/impact-fx/materials (BINA) present, no misparse warnings. OSBD and BINA share the same Mac shape (`DataSize` int32 + `SepOffset` int32 → samples in `.sep`), so they are one stage.

> **Investigation-first:** unlike SNDD, the sample source is `.sep`, and `TMtInstanceFile.separateFile` is *opened but not mmapped*. The exact engine consumption of OSBD/BINA (their in-memory structs + where they read their data pointer) is **not yet traced**. Task 2.1 produces that ground truth before any translate code is written — do not skip it.

### Task 2.1: Trace OSBD/BINA in-memory structs + the `.sep` read path

**Files:** none (read-only).

- [ ] **Step 1: Get the Mac on-disk layout (already known, confirm).** `OniMacMetadata.cs`: both are `Field(Int32,"DataSize")` + `BinaryPartField(SepOffset,"DataOffset","DataSize")` → 8-byte body, sample bytes in `.sep` at `DataOffset` for `DataSize` bytes.
- [ ] **Step 2: Find the in-memory structs.** `grep -rn "BINA\|'B','I','N','A'\|OSBD\|'O','S','B','D'" BungieFrameWork OniProj --include=*.h`. Record each struct's fields + which field holds the data pointer and which the length, and which subsystem's LoadPostProcess resolves them.
- [ ] **Step 3: Trace how `.sep` bytes are read today.** `TMtInstanceFile.separateFile` is a `BFtFile*` (opened, not mapped). Find any existing reader (`TMrInstance_GetSeparateFile`, `BFrFile_Read`, `tm_separate`/`TMtSeparateFileOffset` consumers in `BFW_BinaryData.c:58`, `Motoko_Texture.c:166`). Decide: read-on-demand via `BFrFile_Read(separateFile, offset, size, buf)` vs mmap the `.sep` once (parallel to `rawMapping`). Record the decision + the exact API.
- [ ] **Step 4: Oracle.** `OniSplit -export` an OSBD and a BINA from a Mac level; record expected DataSize/offset for one known instance to diff against.

**Output of Task 2.1:** the dst struct field offsets, the `.sep` read mechanism, and one oracle value per template. The Stage-2 implementation tasks below are written against that output.

### Task 2.2: OSBD + BINA translators + `.sep` resolution

**Files:**
- Modify: `BFW_TM_MacLayout.c` (add `iTranslateOSBD`, `iTranslateBINA`, and `case` entries — same preamble-copy + body-fill pattern as `iTranslateSNDD` in Task 1.2)
- Modify: the `.sep` read path per Task 2.1 Step 3 (likely a `.sep` mmap field on `TMtInstanceFile` + setup in `TMiGame_InstanceFile_New_FromFileRef`, paralleling `rawMapping`/`rawPtr`; or a LoadPostProcess `.sep` resolve in the owning subsystem).

- [ ] **Step 1:** Add `case UUm4CharToUns32('O','S','B','D'):` and `case ...('B','I','N','A'):` to `TMrMacLayout_Translate`, each copying the preamble then filling the dst struct fields identified in Task 2.1 (DataSize→length field, SepOffset→data field). Write the actual field assignments using the offsets from Task 2.1 (mirror the `iTranslateSNDD` structure exactly — do not abbreviate).
- [ ] **Step 2:** Implement `.sep` sample resolution for Mac OSBD/BINA per Task 2.1's decision, so the data pointer resolves into `.sep` (not `.raw`). Guard it behind `isMac` so PC binary-data is untouched.
- [ ] **Step 3 (B):** `make -j8` — clean build.
- [ ] **Step 4 (O):** diff engine-logged DataSize/offset vs the Task 2.1 oracle values.
- [ ] **Step 5 (V, GATED):** ASK the user to run Mac level 1; confirm ambient/group audio + particle/impact effects present with no misparse warnings in `debugger.txt`. Regression-guard PC data. Commit (stage only the touched files; `addresses #37`); HISTORY + README in the same commit; comment #37.

---

## STAGE 3 — TXMP (`.sep` textures)

**Outcome:** Mac textures render correctly on screen (not just parse). Requires **visual** verification.

> **Investigation-first + most complex layout.** Mac TXMP (`OniMacMetadata.cs`): `Padding(128)`, `Flags`, `Width`(i16), `Height`(i16), `Format`, `Pointer(TXAN)`, `Pointer(TXMP)`, `Padding(4)`, `SepOffset DataOffset`, `Padding(8)`. The two `Pointer` fields make this the one Mac template with template-pointer fields — they need the same placeholder→pointer treatment the PC `TemplatePtr` path gives (confirm whether `TMrBridge_PreparePointers` runs over the Mac-translated TXMP, or whether the translator must emit the same placeholder encoding the PreparePointers pass expects).

### Task 3.1: Trace TXMP in-memory struct + texture `.sep` consumption + pointer handling

**Files:** none (read-only).

- [ ] **Step 1:** Confirm the Mac TXMP on-disk layout above; compute each field's byte offset (Padding(128) means the real fields start at +128).
- [ ] **Step 2:** Find the in-memory `M3tTextureMap` (or equivalent) struct; record offsets for width/height/format/animation ptr/envmap ptr/pixelStorage (`tm_separate`) data pointer. Note `Motoko_Texture.c:166` already consumes `.sep` for `pixelStorage` — see how PC TXMP resolves its texels and whether Mac differs only in header.
- [ ] **Step 3:** Determine the `TemplatePtr` handling: does `TMrBridge_PreparePointers` walk Mac-translated instances? If the Mac translator writes the raw 4-byte placeholder into the 8-byte dst pointer slots (zero-extended, as the PC pre-PreparePointers state expects), PreparePointers resolves them uniformly. Confirm by reading `TMrBridge_PreparePointers` + the post-translate call at `BFW_TM_Game.c:2448`.
- [ ] **Step 4 (O):** `OniSplit -extract` a Mac TXMP to a known image; record width/height/format to diff.

### Task 3.2: TXMP translator + render verify

**Files:**
- Modify: `BFW_TM_MacLayout.c` (`iTranslateTXMP` + `case`)
- Modify: texture `.sep` resolution if Mac differs from the existing `pixelStorage` path (per Task 3.1 Step 2)

- [ ] **Step 1:** Add `case ...('T','X','M','P'):` — copy preamble, fill width/height/format/data-offset from the +128-based Mac offsets, and write the two template-pointer slots in the placeholder encoding PreparePointers expects (per Task 3.1 Step 3). Full field code, mirroring `iTranslateSNDD`.
- [ ] **Step 2 (B):** `make -j8`.
- [ ] **Step 3 (O):** diff engine-logged TXMP dims/format vs the Task 3.1 oracle.
- [ ] **Step 4 (V, GATED — VISUAL):** ASK the user to run Mac level 1 and **screenshot**; confirm textures render correctly (per the project's visual-verification rule, a green log line ≠ a visible win — require the screenshot before commit). Also audit whether TXMP was previously misparsing silently (spec §3 open question). Regression-guard PC textures. Commit (`addresses #37`); HISTORY + README same commit; comment #37.

---

## STAGE 4 — IMA4 byte-order fidelity (by ear)

**Outcome:** Mac IMA4 audio is confirmed bit-correct by ear. The IMA decoder's per-packet state word may need a big-endian swap for Mac data that PC data did not (the swap was removed for PC in commit `5ca88b2`).

### Task 4.1: Recover the swap + A/B by ear

**Files:**
- Read-only: `git show 5ca88b2 -- BungieFrameWork/BFW_Source/BFW_SoundSystem2/BFW_SS2_IMA.c` to recover the exact removed lines (the state-word swap).
- Modify (conditional): `BFW_SS2_IMA.c` and/or `BFW_TM_MacLayout.c`.

- [ ] **Step 1:** `git show 5ca88b2` to see exactly which state-word byte-order handling was removed and where (`SSiIMA_DecompressSoundData_Mono`/`_Stereo`).
- [ ] **Step 2:** Form the hypothesis: Mac IMA4 stores the predictor/step-index state word big-endian. If so, the swap must be re-applied **only for Mac-sourced SNDD** (gate on `isMac`-derived state, e.g. a flag threaded onto `SStSoundData`, or a per-decode parameter) — never for PC data (which `5ca88b2` proved must not swap).
- [ ] **Step 3 (V, GATED — BY EAR):** This is a fidelity call that **cannot** be made from logs (memory: no fabricated evidence; "inconclusive" is valid). Decode a known Mac SNDD both ways and ASK the user to listen — clean music vs distorted/static. Only assert the byte-order after the ear test. If indistinguishable/inconclusive, document that and leave PC behaviour untouched.
- [ ] **Step 4:** If the swap is needed, implement it gated on Mac; `make -j8`; user ear-confirm; commit (`addresses #37` → `fixes #37` once the whole drop-and-play arc is user-confirmed). Final HISTORY + README; close #37 on positive verdict.

---

## Self-Review

**Spec coverage** (each spec section → task):
- §1 root cause (SNDD crash) → Stage 1 (Tasks 1.1–1.5). ✓
- §2 Mac SNDD 16-byte layout → Task 1.2 `iTranslateSNDD`. ✓
- §3 blast radius (4 templates) → SNDD=Stage 1, OSBD/BINA=Stage 2, TXMP=Stage 3. ✓
- §4 detector (checksum) → Task 1.1. ✓
- §5 insertion point (A + hand-written fn) → Task 1.3 branch + `BFW_TM_MacLayout.c`. ✓
- §5a verified map (struct offsets, `.sep` open, `.raw` for SNDD) → Tasks 1.1–1.3 use it directly. ✓
- §6 staged order (SNDD first, verify-then-continue) → stage gating + (V) steps. ✓
- §7 test strategy (build / observe / oracle / behavioural) → Verification Model + (B)/(O)/(V) tags. ✓
- §8 data switching → "Data switching" block. ✓
- §9 ship posture (Stage 0 graceful-fail) → **superseded** by Stage 1 for SNDD; the optional graceful-fail for other non-PC data is out of scope for this plan (note left here intentionally). 
- §10 honesty ledger (TXMP render, OSBD/BINA semantics, IMA byte-order unverified) → Stages 2/3/4 each lead with an investigation task; (V) gates prevent premature "done". ✓

**Placeholder scan:** Stage 1 has full, runnable code in every code step. Stages 2–4 deliberately front-load a read-only investigation task because their dst structs / `.sep` read path / IMA byte-order are spec-flagged unverified (§10) — writing fabricated field code there would violate the no-fabricated-evidence rule. Each implementation step names the exact files, the exact pattern to mirror (`iTranslateSNDD`), and the oracle to diff against — no "TODO/implement later".

**Type consistency:** `TMrMacLayout_Translate(UUtUns32, const void*, void*, UUtBool) -> UUtBool` is declared in 1.1 and defined in 1.2 with matching signature; the call site in 1.3 matches. `isMac` (`UUtBool`) is declared in 1.1 and read in 1.3. `iTranslate<TAG>` static helpers are consistent across stages.

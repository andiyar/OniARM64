# Mac Retail Data Support — Implementation Spec

> **Status:** design spec — §5 design decision **RESOLVED** via `superpowers:brainstorming` (2026-05-31, code-verified against the live tree). Scope locked: plan all 4 stages, implement + behaviourally verify **Stage 1 (SNDD)** first, then 2 → 3 → 4. Next step: `writing-plans` → `executing-plans`.
>
> **This doc supersedes the GitHub #37 comment thread.** That thread contains my earlier wrong framings and one fabricated-evidence episode (retracted). Trust THIS doc; use #37 only for history.

**Goal:** Let the original 2001 Mac retail `GameDataFolder` load and play on the ARM64 port without requiring PC/Windows data ("ported the Mac version but you need the PC version to play" is not shippable).

**Tech context:** C, ARM64 macOS, little-endian. Bungie template-manager + 32→64 bridge. OniSplit (community tool, C#, runs under mono here) is the authoritative format oracle.

---

## 1. Verified root cause

The crash is a **template-layout mismatch**, not endianness, and not an audio-codec bug.

Chain (every link verified this session):

1. Each `.dat` header's first 8 bytes are a **template checksum** identifying the layout family. Verified byte-exact against OniSplit constants:
   - Mac `level0_Final.dat` = `0x0003bcdf23c13061` (`OniMacTemplateChecksum`)
   - PC  `level0_Final.dat` = `0x0003bcdf33dc271f` (`OniPCTemplateChecksum`)
2. The ARM64 port **deliberately bypasses** the checksum-mismatch rejection — [`BFW_TM_Game.c:360-371`](../BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_Game.c#L360), comment *"bypass checksum validation for Windows game data compatibility"* (the `level_exists = UUcFalse; goto done;` is commented out). This was added so rebuilt/modded PC data loads; as a side effect Mac data also loads.
3. The 32→64 bridge then walks Mac instances with the **single compiled PC layout** per template (one `tdef->layoutDescriptor` per template tag, read at [`BFW_TM_Game.c:1809`](../BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_Game.c#L1809), lazily built from `tdef->swapCodes` by `TMrBridge_BuildDescriptor`, applied per-instance by `TMrBridge_TranslateInstance` at [`:1917`](../BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_Game.c#L1917)). *(Earlier drafts cited line 2598 — that is the unrelated instance-allocation path; the real translate site is 1809–1917.)*
4. For SNDD, the PC layout reads `num_bytes` at offset 56 / `data` at 64, but the Mac SNDD record is only **16 bytes** → reads land in `0xDEADDEAD` block-fill ([`TemplateManager.c:5658`](../BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TemplateManager.c#L5658)) and adjacent instances. **Log proof:** crash-session SNDD LoadPost shows `dur=57005` (=`0xDEAD`) constant across all 1174 sounds, `num_bytes` striding +256, `rawData=0x1`.
5. Garbage `num_bytes` → oversized `num_packets` → `SSiIMA_DecompressSoundData_Stereo` reads ~56 MB OOB → SIGSEGV. Fires on the **first stereo** SNDD (`mus_asian`, level 1). 96% of level SNDDs are mono and take the `_Mono` path, which is why load reached gameplay before dying.

**Why everything else loads:** OniSplit overrides only **4** templates Mac-vs-PC (§3). All others share one layout — so Mac geometry/AKEV/M3GM/characters loaded and the level-load sequence completed on our LE ARM build.

## 2. Verified Mac SNDD layout (16 bytes, little-endian)

Two OniSplit sources agree (`Metadata/OniMacMetadata.cs` `sndd`, and `Sound/SoundData.cs` `if (sndd.IsMacFile)`):

| Offset | Field | Type | Notes |
|---|---|---|---|
| 0 | Flags | int32 LE | channel count = `(flags >> 1) + 1`; bit0 = compression |
| 4 | Duration / game-ticks | int32 LE | |
| 8 | DataSize | int32 LE | sample byte count |
| 12 | DataOffset | int32 LE | offset into **`.raw`** |

Samples are **IMA4** in **`.raw`** (NOT `.sep`). PC SNDD by contrast is a ~96-byte WAVEFORMATEX header (`wFormatTag`, coef table) decoded as MS-ADPCM via ffmpeg.

## 3. Blast radius — the 4 templates that differ (from `OniMacMetadata.cs`)

| Tag | What | Mac difference | Sample source | Symptom if walked as PC |
|---|---|---|---|---|
| **SNDD** | Sound Data | 16-byte header | `.raw` | **the verified crash** |
| **OSBD** | Oni Sound Binary Data | DataSize + SepOffset | `.sep` | audio binary-data misparse |
| **BINA** | Binary Data | DataSize + SepOffset | `.sep` | particles / impact-fx / materials misparse |
| **TXMP** | Texture Map | 128-byte padding + flags + SepOffset | `.sep` | textures misparse (may be silent) |

Everything else shares the PC layout. **(Unverified:** whether TXMP currently misparses silently or renders OK — it loaded without crashing on level 1 but render-correctness was never checked. Audit during Stage 3.)

## 4. Detector

Per-file, authoritative: the instance-file header checksum. `file->...checksum == 0x0003bcdf23c13061` ⇒ Mac. (`.sep` presence is a coarser heuristic — 15 Mac / 0 PC — but the checksum is exact and already read at load time.)

## 5. Design decision — RESOLVED (2026-05-31, code-verified)

The bridge caches **one** layout per template (`tdef->layoutDescriptor`, [`BFW_TM_Game.c:1809`](../BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_Game.c#L1809)), lazily built from `tdef->swapCodes` by `TMrBridge_BuildDescriptor`, applied per instance by `TMrBridge_TranslateInstance` ([`:1917`](../BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_Game.c#L1917)). Mac support needs an **alternate translation for the 4 templates, selected when the source file is Mac.**

**Decision: A (select per-file at the translate site) + a hand-written Mac translate function per template** — NOT the swap-code-array approach (B) the earlier draft recommended.

**Why B was rejected (verified against the builder + `gSwapCodes_SNDD`):** the Mac record is not a *byte-swap* of the engine struct — it is a *remap with derived fields*. The Mac 16-byte SNDD must populate the **shared 72-byte `SStSoundData`** dst struct: 3 direct copies (`duration_ticks`←mac@4, `num_bytes`←mac@8, `data`-offset←mac@12) **plus** `f.nChannels = (flags>>1)+1` (derived — no swap code computes that) **plus** constants (`f.wFormatTag` forced ≠ 2 so decode routes to Apple-IMA4, not ffmpeg MS-ADPCM; sane `nSamplesPerSec`/`nBlockAlign`/`wBitsPerSample`). Swap codes emit a *packed* src→dst correspondence with no "skip dst to offset 56" primitive and no derive/const primitive — so B would need a built descriptor **plus** a post-fixup function anyway. A ~20-line hand-written translate fn is strictly simpler and clearer. (`TMtFieldDescriptor` does carry independent `src_offset`/`dst_offset`, so a pure descriptor *could* express the 3 copies — but the derived/const fields rule it out regardless. The original A/B/C framing is preserved in this file's git history.)

**Insertion point (A):** [`BFW_TM_Game.c:1809–1917`](../BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_Game.c#L1809) — `newInstanceFile`, `tag`, and `tdef` are all in scope. Branch: `if (isMac && tag ∈ {SNDD,OSBD,BINA,TXMP}) ONiMacXXXX_Translate(src, dst, needsSwapping); else TMrBridge_TranslateInstance(...)`. The PC `dst_size` (72 for SNDD) still governs allocation + zeroing — correct, since dst is the shared engine struct either way. **There is already `[TM-SNDD]` diagnostic instrumentation at this exact site** from prior session work — a confirmation the branch belongs here.

**Detector plumbing:** the file checksum is read transiently at load and **not retained** (`TMtInstanceFile` has no checksum field today). Add `UUtBool isMac` to `TMtInstanceFile` and set it in `TMiGame_InstanceFile_New_FromFileRef` from the header checksum (`== 0x0003bcdf23c13061`). Cheap, queryable at the translate site **and** in LoadPostProcess.

### 5a. Verified code map (ground truth for the plan)

Confirmed this session by reading the live tree (offsets corroborated by the existing `OniNative/startup.txt` capture):

- **In-memory SNDD** = `SStSoundData`, sizeof **72** (ARM64): `flags`@0, `SStFormat f`@4 (50-byte *packed* WAVEFORMATEX-ish: `wFormatTag`@4, `nChannels`@6, `nSamplesPerSec`@8, `nBlockAlign`@16), `duration_ticks`@54, `num_bytes`@56, `data`(void*)@64. ([`BFW_SoundSystem2.h:167`](../BungieFrameWork/BFW_Headers/BFW_SoundSystem2.h#L167)).
- **PC swap codes** = `gSwapCodes_SNDD[22]` ([`templatechecksum.c:615`](../OniProj/OniCMakeProjs/TEVCProj/templatechecksum.c#L615)); `data` = code `0x0a` (RawPtr → `.raw` offset). Code `0x0b` (SeparateIndex) is the `.sep` mechanism — used by textures/binary-data, **not** SNDD.
- **LoadPostProcess** = `SSiSoundData_ProcHandler` ([`BFW_SoundSystem2.c:894`](../BungieFrameWork/BFW_Source/BFW_SoundSystem2/BFW_SoundSystem2.c#L894)): `data = TMrInstance_GetRawOffset(.raw base) + on-disk-offset`. **Mac SNDD samples live in `.raw` too** → this path is unchanged for Stage 1.
- **`.sep` already plumbed**: `TMtInstanceFile.separateFile` is opened (with `.raw` fallback) in `TMiGame_InstanceFile_New_FromFileRef` ([`BFW_TM_Game.c:1673`](../BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_Game.c#L1673)), reachable via `TMrInstance_GetSeparateFile`. *Opened, not mmapped.* Needed for OSBD/BINA/TXMP (Stages 2–3), not SNDD.
- **Decode** ([`BFW_SS2_Platform_OpenAL.c:418`](../BungieFrameWork/BFW_Source/BFW_SoundSystem2/Platform_OpenAL/BFW_SS2_Platform_OpenAL.c#L418)): `wFormatTag==2` → ffmpeg MS-ADPCM; else → Apple IMA4. IMA4 packet math `num_packets = num_bytes / (channels·34)` is **unbounded** ([`BFW_SS2_IMA.c:533`](../BungieFrameWork/BFW_Source/BFW_SoundSystem2/BFW_SS2_IMA.c#L533)) — that unbounded read **is** the crash. Correct `num_bytes` fixes it; `SStIMA_SampleData` = 34 bytes (state u16 + 32 sample bytes).

## 6. Staged implementation (each stage independently verifiable)

Order matters: SNDD first because the crash blocks reaching anything else, and behavioural verification of later stages is impossible until the game gets past level load.

**Scope (locked 2026-05-31):** the implementation plan covers all 4 stages; **Stage 1 is built and user-verified before Stages 2–4 proceed.** Each stage is its own verify-then-continue checkpoint.

- **Stage 1 — SNDD (stops the crash).** Detector + Mac SNDD layout (16-byte) + route to the already-compiled IMA4 decoder (`BFW_SS2_IMA.c`). **Done = Mac level 1 loads and plays its music without SIGSEGV.**
- **Stage 2 — OSBD + BINA.** Mac binary-data layouts, samples from `.sep`. **Done = ambient/group audio + particles/impact-fx present, no misparse warnings.**
- **Stage 3 — TXMP.** Mac texture layout, samples from `.sep`; **visually verify** textures render (not just parse). **Done = Mac textures render correctly on screen.**
- **Stage 4 — IMA4 fidelity (downstream).** Confirm **by ear** whether the IMA state-word byte-order swap (removed for PC in `5ca88b2`, exact lines recoverable from that commit) must be re-added for Mac data. Strong format-logic case it's big-endian, but **NOT behaviourally confirmed** — decode a `.sep`/`.raw` SNDD both ways and listen. Do not assert without the ear test.

## 7. Test strategy (read before planning)

- **No unit-test framework exists** in this codebase (no ctest/gtest; only `Oni_Testbed.c`). "Tests" here are: (a) **build** clean, (b) **load Mac data + observe** — `startup.txt`/`debugger.txt` for parse correctness, no SIGSEGV; (c) **user behavioural verify** (audio by ear, textures by eye). Per project rules, a stage is not "done" until user-confirmed; use `addresses #37` until then.
- **Cheap offline oracle:** `OniSplit -export`/`-extract:aif`/`-extract:wav` on the Mac `.dat` gives ground-truth field values + decoded audio to diff engine output against — no game launch needed. Use it to validate parsing before any playtest.
- **Regression guard:** every stage must **not** break PC/CX data (the shipping path). Re-load PC data after each stage. The detector must leave the PC path byte-for-byte unchanged.

## 8. Repro / data switching

```sh
# Point at Mac disc data:
rm ~/Library/Application\ Support/OniARM64/gamedata
ln -s /Volumes/Oni/Oni\ ƒ/GameDataFolder ~/Library/Application\ Support/OniARM64/gamedata
# Restore PC/CX data (the working shipping path):
rm ~/Library/Application\ Support/OniARM64/gamedata
ln -s "/Users/andiyar/Developer/oni/CXOni/Oni/drive_c/Program Files (x86)/Oni/GameDataFolder" ~/Library/Application\ Support/OniARM64/gamedata
```
Mac disc must be mounted (`Oni [Mac].ISO` → `/Volumes/Oni`).

## 9. Ship posture

r1 stays **BYO PC/AE data** (LE, community-standard, zero-swap). Independent cheap win available now as Stage 0 if desired: at folder scan, if the header checksum is Mac and Mac mode isn't implemented yet, **fail gracefully** ("Mac retail disc data not yet supported — use PC/AE data") instead of the SIGSEGV.

## 10. What is verified vs not (honesty ledger)

- **Verified:** root cause chain (§1), Mac SNDD 16-byte layout (§2), 4-template blast radius (§3), detector checksum (§4), insertion point + translate-site line refs 1809–1917 (§5), in-memory `SStSoundData` offsets, `.sep` already opened on `TMtInstanceFile`, Mac SNDD samples resolve from `.raw` not `.sep` (§5a).
- **NOT verified (must confirm during implementation):** TXMP render-correctness; OSBD/BINA exact field semantics beyond the OniSplit declarations; IMA4 byte-order (format-logic only, needs ear test); that no *other* template misparses silently (OniSplit says only these 4 differ — trusted, not independently re-derived).

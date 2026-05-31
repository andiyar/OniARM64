# Mac Retail Data Support — Implementation Spec

> **Status:** design spec (verified ground truth + architecture + staged plan). NOT a line-by-line TDD plan — one design decision (the layout-injection insertion point, §5) should be resolved via `superpowers:brainstorming` at the start of the implementation session, then `writing-plans` → `executing-plans`.
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
3. The 32→64 bridge then walks Mac instances with the **single compiled PC layout** per template (one `layoutDescriptor` per template tag — [`BFW_TM_Game.c:2598`](../BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_Game.c#L2598)).
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

## 5. THE design decision to resolve first (brainstorm at session start)

The bridge caches **one** `layoutDescriptor` per template definition and applies it to every instance ([`BFW_TM_Game.c:2598-2620`](../BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_Game.c#L2598)). Mac support needs an **alternate layout for the 4 templates, selected when the source file is Mac.** Candidate insertion points (pick one in brainstorming):

- **A — alternate layout at the translate call site.** At line ~2598, if `inInstanceFile` is Mac and tag ∈ {SNDD,OSBD,BINA,TXMP}, use a Mac layout descriptor instead of `templateDef->layoutDescriptor`. Most localized; needs a place to cache the 4 Mac layouts (can't reuse the per-template slot — it holds the PC one).
- **B — static Mac swap-code arrays** feeding the existing `TMrBridge_BuildDescriptor`, matching the established `gSwapCodes_*` pattern (`templatechecksum.c`). Reuses the proven builder; the Mac record is simple enough (3-4 scalars) that the arrays are short.
- **C — hand-built `TMtLayoutDescriptor`** for the 4 Mac templates (skip swap codes). Most direct but bypasses the builder's alignment logic.

**Recommendation:** B (static Mac swap-code arrays) + A (select per-file at the call site). Reuses the existing builder and selection is one branch at one site. Confirm in brainstorming before coding.

## 6. Staged implementation (each stage independently verifiable)

Order matters: SNDD first because the crash blocks reaching anything else, and behavioural verification of later stages is impossible until the game gets past level load.

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

- **Verified:** root cause chain (§1), Mac SNDD 16-byte layout (§2), 4-template blast radius (§3), detector checksum (§4), insertion point (§5).
- **NOT verified (must confirm during implementation):** TXMP render-correctness; OSBD/BINA exact field semantics beyond the OniSplit declarations; IMA4 byte-order (format-logic only, needs ear test); that no *other* template misparses silently (OniSplit says only these 4 differ — trusted, not independently re-derived).

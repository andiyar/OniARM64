# Triage: tester reports from the chapter 10–14 playthrough (2026-09-22)

Static analysis of issues #111–#115 (filed by ekt1701 and simX against
1.3.0r5 / `main@6053051`), done from a clean clone without running the game.
Four of the five have code-level root causes; #111 and #112 collapse into one
bug. Each section gives the mechanism, the evidence, the fix shape and how to
verify, so the work can be picked up cold.

Suggested order: #111/#112 first (installer headline feature, most packs
affected), then #114 and #113 (tiny, minutes to verify), then #115 (sound
layer, needs a listening test).

---

## #111 + #112 — installer packs crash or vanish: the 31-character leaf-name limit

**Reports.** #111: `level0_CharacterRetexturePt4Synd1.dat` logs
"unable to get level info" and is ignored; `BetterWarehouseTraining` registers
but Oni "fails to run, no crash report" (HEAD build). #112: r5 binary SIGSEGVs
on launch with `BetterWarehouseTraining` or `HQTrainingRoomTextures` installed
via the OniMod Installer; 13 other depot packs "work". Same mods installed by
hand from Terminal run fine.

**Root cause.** The engine's file layer still carries the OS 9 leaf-name cap:
`BFcMaxFileNameLength` is 32 *including* the terminator
(`BungieFrameWork/BFW_Headers/BFW_FileManager.h:24`), so a leaf may be at most
31 characters. The installer allows a sanitised pack name of up to 32
characters (`tools/OniModInstaller/Installer.swift:208`) and writes
`level<N>_<Name>.dat`, so the leaf can reach 43. `onipack` accepts suffixes up
to 63 (`tools/onipack/onipack_writer.c:58`). Nothing between the installer and
the engine checks the length. Two different failures follow, by length:

1. **Leaf ≥ 35 characters → silently ignored.** The overlay scan hands the file
   to `TMrUtility_LevelInfo_Get`
   (`BungieFrameWork/BFW_Source/BFW_TemplateManager/BFW_TM_Common.c:624`),
   which copies the leaf into a `char nameBuffer[BFcMaxFileNameLength]`
   (`:631`, `:641`). `UUrString_Copy` truncates silently in release (its
   length assert compiles out, `BFW_String.c:179`). With 31 characters kept,
   the `.dat` dot is gone, `strchr(cp, '.')` fails (`:655`) and the function
   returns `UUcError_Generic`, which the scanner logs as
   "unable to get level info … skipping"
   (`BFW_TM_Game.c:3282`). The pack is never registered. That is #111's first
   complaint (37 characters).

2. **Leaf 32–34 characters → registers, then fails at level-0 load.** The dot
   survives truncation, the pack registers and wins name resolution. When
   level 0 loads, `TMiGame_InstanceFile_New_FromFileRef` derives the `.raw`
   and `.sep` companion names (`BFW_TM_Game.c:1703`, `:1728`) through
   `TMrUtility_DataRef_To_BinaryRef` → `BFrFileRef_DuplicateAndReplaceName` →
   `BFrFileRef_Set`, and the Linux/SDL file manager rejects any leaf of 32+
   characters (`BFW_FileManager_Linux.c:145`). What happens next depends on
   the build:
   - **r5 binary (2026-07-17):** the rejection message passed the name
     pointer through a `(UUtUns32)` cast, so formatting the error itself
     crashed. simX's `.ips` confirms this exactly: `_platform_strlen` on
     `0x5084cc90` (a 64-bit heap pointer with its top half cut off) inside
     `UUrError_ReportP_Internal` ← `BFrFileRef_Set` ← `BFrFileRef_MakeFromName`
     ← `BFrFileRef_Duplicate` ← `BFrFileRef_DuplicateAndReplaceName` ←
     `TMrUtility_DataRef_To_BinaryRef` ← `TMiGame_InstanceFile_New_FromFileRef`
     ← `TMiGame_LoadedInstanceFiles_Add` ← `TMiGame_InstanceFileRef_LoadLevel`
     ← `TMrLevel_Load` ← `ONrLevel_LoadZero`. The truncating cast was fixed
     in `2bcbdb3` (#99) on 2026-07-24, one week after r5 shipped, so every
     r5 user still has the crash.
   - **HEAD (`6053051`):** the cast is `uintptr_t`, the error is reported
     cleanly, `DataRef_To_BinaryRef` returns it, the level-0 load fails and
     Oni exits without a crash report. That is #111's second complaint.

   The log stopping at `[TMrLevel_Load] before InstanceFileRef_LoadLevel`
   in #112 matches: the fault is inside that call.

**Every data point in #112 fits the length rule** (leaf = `level<N>_` + name +
`.dat`):

| Installer-generated leaf                        | Length | Predicted            | Reported        |
|-------------------------------------------------|--------|----------------------|-----------------|
| `level1_BetterWarehouseTraining.dat`            | 34     | registers → fails    | crash           |
| `level0_HQTrainingRoomTextures.dat`             | 33     | registers → fails    | crash           |
| `level0_GriffinHD.dat`                          | 20     | works                | works           |
| `level0_HDScreens.dat`                          | 20     | works                | works (#113)    |
| `level0_MuroHQ.dat`                             | 17     | works                | works           |
| `level0_BetterConsolesDoors.dat`                | 30     | works                | works           |
| `level0_HQAirportTextures.dat`                  | 28     | works                | works           |
| `level0_KonokoHDAnimeFace.dat`                  | 28     | works                | works           |
| `level0_RealisticSkies.dat`                     | 25     | works                | works           |
| `level0_CharacterRetexturePt3TCTF.dat`          | 36     | silently skipped     | "works"         |
| `level0_CharacterRetexturePt4Synd1.dat`         | 37     | silently skipped     | skipped (#111)  |
| `level0_CharacterRetexturePart2Mains.dat`       | 39     | silently skipped     | "works"         |
| `level0_CharacterRetexturePt6MiscGuys.dat`      | 40     | silently skipped     | "works"         |
| `level0_CharacterRetexturePt7Civilians.dat`     | 41     | silently skipped     | "works"         |
| `level0_CharacterRetexturePt1KonokoCops.dat`    | 42     | silently skipped     | "works"         |

(Names as simX listed them; the actual sanitised names may differ by a few
characters but stay in the same band.) The six character packs marked "works"
are almost certainly not loaded at all: vanilla characters, no crash. simX's
startup.txt should show one `[overlay] unable to get level info` line per
pack. **Ask them to confirm** — it turns the prediction into a second data
set. "Terminal install works" in #111 simply means ekt1701 chose a short
suffix by hand.

**Fix shape** (three small pieces, independent of each other):

1. **Installer** — cap the sanitised name so the longest leaf fits.
   `level10_` is 8 characters, `.dat` is 4, so the name must be ≤ 19:
   change the `prefix(32)` in `Installer.swift:208` to `prefix(19)` and
   adjust the doc comment at `:203`. The fixture test in `main.swift` should
   gain a long-name case (a 32-character `NameOfMod` → 19-character pack).
   Optionally, also derive the cap from a shared constant rather than a
   literal.
2. **onipack** — refuse an output leaf of ≥ 32 characters where it already
   validates level and suffix (`onipack_main.c:85-91`); print a message that
   names the limit. `build-hd-overlays.sh` uses the short `HD1` suffix so it
   is unaffected, but the check protects hand-built packs too.
3. **Engine** — in `TMiGame_OverlayDir_Scan` (`BFW_TM_Game.c:3250-3340`)
   check `strlen(leaf) >= BFcMaxFileNameLength` *before* calling
   `TMrUtility_LevelInfo_Get`, and log
   `"[overlay] %s: file name too long (max 31 characters incl. .dat); rename the pack; skipping."`
   then `continue`. This turns both failure modes (silent skip and level-0
   fault) into one clear line and refuses the file before it can register.
   Raising `BFcMaxFileNameLength` itself is *not* recommended here: the
   constant sizes buffers across the template manager (`fileName`, suffix
   buffers, `BFcMaxFileNameLength * 2` name scratch) and the file-ref
   `leafName` field is separately 64 — it is a wider audit for another day.
4. **Release notes** — the r5 crash path (`(UUtUns32)` cast) is already fixed
   on main by `2bcbdb3`; the next cut should mention that overlong pack
   names no longer crash on launch.

**Workaround to post on both issues now:** rename the pack's `.dat`, `.raw`
and `.sep` files to a short suffix (all three consistently, e.g.
`level1_BWT.dat` / `.raw` / `.sep`) so each leaf is ≤ 31 characters, or
rename the mod folder to ≤ 19 characters before dropping it on the installer.
The pack *folder* name does not matter, only the file leaves.

**Verify:** install `BetterWarehouseTraining` with the fixed installer, check
startup.txt shows `registered level1_BetterWarehous….dat` with a ≤ 31-char
leaf and the level loads. Then drop a pack with a deliberately long name into
`TexturePacks/` and confirm the new `[overlay] … too long` line appears and
the game starts.

---

## #113 — HD Screens backgrounds drawn as four magnified corners

**Report.** With the HD Screens pack installed, the Load Game background (and
every other "screen") is split into pieces with the corners near the centre.

**Mechanism.** Menu backgrounds are `TXMB` big textures: a 640×480 image cut
into 256-pixel tiles (`M3cTextureMap_MaxWidth`/`Height` = 256,
`BFW_Motoko.h:777`), stored as `num_x × num_y` separate `TXMP` tiles, with
edge tiles exactly `min(256, width - left)` wide (importer:
`BFW_ToolSource/Common/Imp/Imp_Texture_Big.c:390`, the loop at the
`texture_width = UUmMin(M3cTextureMap_MaxWidth, width - left)` line).
`M3rDraw_BigBitmap` (`BFW_Motoko/Manager/Motoko_Utility.c:629`) places each
tile at `x * 256, y * 256` and calls `M3rDraw_Bitmap` with the *cell* size;
`M3rDraw_Bitmap` (`:498`) computes UVs as `cell / texture->width` (`:520`,
`uv[3].u = inWidth / inBitmap->width`).

HD Screens ships HD versions of those tiles plus its own `TXMB`. The installer
keeps `TXMP*.oni` only (#62 rule), so the vanilla `TXMB` with 256-pixel cells
now points at, e.g., 1024-pixel tiles: each cell shows only the top-left
quarter of its tile, magnified 4×. That is the reported look.
(`VUrDrawTextureRef` in `BFW_DialogManager/BFW_ViewUtilities.c:206`, big-texture case at `:255`, is the
dialog path that reaches `M3rDraw_BigBitmap`.)

**Fix shape** (engine, ~15 lines, no data change). Make `M3rDraw_BigBitmap`
resolution-independent: compute the *nominal* tile size from the TXMB
geometry exactly as the importer did —
`nominal_w = min(256, big->width - x*256)`,
`nominal_h = min(256, big->height - y*256)` — and draw each tile with
`M3rDraw_BitmapUV` using `uv = (drawn_w / nominal_w, drawn_h / nominal_h)`.
For a whole tile that is `1.0`, whatever the replacement's pixel size. Keep
the existing clamping of `drawn_w/h` to the requested `inWidth/inHeight`.
Consider setting interpolation to linear when
`texture->width != nominal_w` (the bitmap path uses
`M3cDrawState_Interpolation_None`, which will alias when minifying a 4× tile).

**Verify:** Load Game menu with HD Screens installed shows the full HD art;
vanilla menus (pack removed) are byte-identical in behaviour since UVs stay
`cell/256` there.

---

## #114 — aiming reticle stretched horizontally at 2560×1080

**Report.** The weapon reticle is horizontally stretched on a 21:9 display;
nothing else looks stretched.

**Mechanism.** The reticle is the laser-sight dot: `ONiDrawLaserSight`
(`Oni_Character.c:6325`) → `FXrDrawLaser` / `FXrDrawLaserDot` (`Oni_FX.c:79`)
→ `M3rSimpleSprite_Draw` → the software geometry engine's
`MSiSprite_Draw_Unoriented`
(`BFW_Motoko/Engines/GeomEngine/Software/MS_GC_Method_Geometry.c:1761`).
(`AMrRenderCrosshair` in `Oni_Aiming.c` is a no-op in shipping builds.) That
function sizes the sprite in screen space as

```c
float xScale = drawWidth  * 0.78125f;     // 500/640
float yScale = drawHeight * 1.041666667f; // 500/480
```

(`:1792-1793`), and the same pair appears in
`MSrGeomContext_Method_SpriteArray_Draw` (`:2298-2299`, used for sky stars).
The two scales agree only at 4:3. The port keeps the vertical FOV and widens
the frustum horizontally (`Oni_GameState.c:5103` comment;
`ONcMotoko_AspectRatio` = width/height), so pixels are square and the
projection's world→pixel scale is `(H/2)/tan(fovy/2)` on *both* axes. Bungie's
500 is exactly `(480/2)/tan(fovy/2)` for the default FOV. Result: every world
sprite is `(W/H)/(4/3)` too wide — 1.33× at 16:9, 1.78× at 21:9. The reporter
only noticed the dot because it is persistent and round; muzzle flashes,
weapon glows, powerup glows and stars are stretched the same way.

**Fix shape** (two lines, both sites): derive both scales from height —
`xScale = yScale = drawHeight * (500.f / 480.f)`. This is the same rule the
cinematics fix already applies to portraits
(`Oni_Cinematics.c:311`, "Sprite dimensions scale uniformly (height-based)").
No change at 4:3.

**Verify:** any widescreen resolution, equip a weapon, the dot is round;
a muzzle flash / phase-stream glow no longer looks squashed. Applies to GL and
Metal alike (shared geometry engine).

---

## #115 — music with multiple parts gets stuck repeating one loop

**Report.** Sometimes the first 3–4 seconds of a level's music repeat forever
(TCTF Science Prison save point 4 is the clearest case); everything else
keeps working. Not every load.

**Mechanism** (OpenAL-specific port regression, confirmed in source). Oni
music is an ambient (`OSrMusic_Start`, `Oni_Sound2.c:3172` →
`SSrAmbient_Start_Simple`) with an optional `in_sound`, then a *looping*
`base_track1` whose group holds several permutations — the "parts". In the
original platform layers a looping channel never looped a buffer. On buffer
completion the platform called `SSrGroup_Play(channel->group, channel, …)`
to pick the next permutation:

- Mac Sound Manager callback:
  `BFW_SoundSystem2/Platform_MacOS/BFW_SS2_Platform_MacOS.c:81`
- Win32 DirectSound stream fill:
  `Platform_Win32/BFW_SS2_Platform_Win32.c:584` (fills the remaining bytes
  with another permutation, gapless)

The OpenAL layer implements "looping" as `AL_LOOPING` on the source
(`Platform_OpenAL/BFW_SS2_Platform_OpenAL.c:291` at play, `:307` on every
`SetLooping`, the latter added for #2). So whichever permutation the weighted
random pick (`SSiGroup_SelectPermutation`) chose first loops at the hardware
level forever. `SSiSoundChannel_IsPlaying` (OpenAL: `AL_SOURCE_STATE`,
`BFW_SoundSystem2.c:1956`) never drops, so `SSiPAUpdate_BodyPlaying`
(`:4257`, replay at `:4299`) never gets to re-pick. It only *seems*
intermittent: it is obvious when the first pick is the short repetitive part
and easy to miss when it is a longer one, but the music is stuck on a single
part every time.

**Fix shape** (sound layer, medium):

1. Stop using `AL_LOOPING` for group channels: `SS2rPlatform_SoundChannel_Play`
   and `_SetLooping` set `AL_FALSE` (the shadow `SScSCStatus_Looping` bit
   stays as the engine's intent flag).
2. Emulate the Mac callback with a poll. In `SS2rUpdate`
   (`BFW_SoundSystem2.c:6774`), *before* `SSiPlayingAmbient_UpdateList()`,
   sweep `SSgSoundChannels_Mono` and `_Stereo`: for any channel with the
   Looping bit set, a non-NULL `group`, and a stopped source, call
   `SSrGroup_Play(channel->group, channel, "sound channel", NULL)`.
   Ordering matters: `SSiPAUpdate_BodyPlaying` captures `channel1_playing`
   before its own replay and then checks the stale value at `:4334`, so if
   the ambient update saw the stopped source first it would fall into
   `BodyStopping` (out-sound, then silence). The sweep must run first, as the
   platform callback did.
3. The graceful-stop path (`SSrAmbient_Stop`, `:5473`) keeps working:
   it clears the Looping bit, the current permutation plays out, the sweep
   does not replay, `BodyStopping` proceeds to the out sound.

Loop points get a sub-frame gap (≤ 16 ms at 60 Hz), the same as the Mac
original's callback latency; `alSourceQueueBuffers` could make it gapless
later if anyone notices. The OpenAL buffer cache is unaffected (it is flushed
on level unload, `:1032`, and keyed per `SStSoundData*`).

**Verify:** load TCTF Science Prison save point 4 several times; the opening
part must give way to the rest of the track every time. Also check a
scripted music change mid-level and a cutscene fade-out (graceful stop path).

---

## Side notes from the same batch

- simX commented on #49 that the sluggishness is transient within a level and
  clears on its own with no obvious trigger. New datum for the profiling
  session; still renderer-independent by their account.
- simX played chapters 10–14 end to end with only these four reports. That is
  effectively the #90 march done by a community tester; worth recording on
  #90 and ticking the chapter boxes on their word, with the staged tests
  (#28/#66/#74/#47/#55) still owed.
- mods.oni2.net is not reachable from the analysis sandbox, so the depot
  pages for the two crashing mods were not inspected; the length rule does
  not depend on their contents.

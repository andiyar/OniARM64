# Changelog

Plain-English log of what's changed in OniARM64, newest first. Entries here
are written for players; the per-commit developer detail lives in
[HISTORY.md](HISTORY.md).

**How it works:** user-visible changes get a line under **Unreleased** as they
land. At each release cut, the Unreleased section becomes the body of the
GitHub release notes and gets stamped with the version + date.

## Unreleased (since 1.3.0r5, 2026-07-17)

- The "Metal renderer" checkbox in Options now sits inside the third box under Invert Mouse, styled like the other checkboxes with a one-line label, instead of straddling the panel borders (#89).
- Only a real Escape key press (or whatever you bound to escape) can raise the in-game menu now. A stale Escape in the polled keyboard state is ignored and, with `ONI_INPUT_TRACE=1`, logged with its scancode. This is hardening, not a fix for a seen bug; the stuck-strafe report on #78 is a separate, still-open question.
- Fixed a memory leak on level load: every death reload, save-point load and level change used to keep about 15 to 26 MB of the previous level in memory for good. Long sessions with many reloads should stay flat now (#30).
- New app icon for macOS 26: the Oni "O" is now a proper layered glass icon, so it follows your system icon style (default, dark, clear, tinted) instead of sitting on the white placeholder tile. On older toolchains the build falls back to a refreshed static icon with a dark background.

### Mods
- Installing an HD Screens mod no longer leaves the Load Game, main menu and
  Options dialogs with red frames, highlights and button plates. When the
  installer skips a mod's re-laid-out screens it now also skips that mod's
  restyled menu chrome (`buttons` and `navi`) and says so in its report, so the
  vanilla blue dialogs stay blue. Same-grid retextures keep their chrome (#113,
  reported by simX).
- New **OniMod Installer** app in the DMG. Drop a texture mod downloaded from
  the Oni Mod Depot (the zip, or its unzipped folder) onto it and it builds the
  pack and installs it into `TexturePacks/` for you. No Terminal needed. It
  keeps only texture files (models, levels and scripts aren't loadable by this
  port), screens out replacements that would wash out shiny surfaces (faces,
  hair, glass) when your game data is installed, and offers to replace a pack
  you've already installed (#20).
- Reinstalling a mod that an earlier OniMod Installer put under a long (20 to 32
  character) folder name now migrates it: the installer spots the old folder,
  asks to replace, and removes it once the new short-named pack is in place,
  so the engine stops logging "file name too long" every launch and the old
  copy no longer takes a pack slot (#120).
- HD Screens packs no longer break the Load Game and Options screens. Those
  mods re-lay-out the menu backgrounds on a bigger grid than the game draws,
  which is why the picture came out as four corners in the middle. The
  installer (and the HD overlay build script) now skip the tiles of any screen
  the mod re-lays-out and say so in the report; same-grid retextures still
  install. Proper support for re-laid-out screens is tracked as #121 (#113,
  reported by simX).
- A texture pack whose `.dat` file is cut short (a bad download or copy) is
  now skipped with a `[tm] … header rejected (truncated …)` line in the log
  instead of crashing the game at level load (#119).
- Texture packs with over-long file names no longer stop Oni from starting. A
  pack file whose name runs past the engine's 31-character limit (`level10_` +
  name + `.dat`) used to make the game quit silently on launch (or crash, on
  r5); it is now skipped, with a line in `startup.txt` that says what to do. A
  pack that can't be opened for another reason (a missing `.raw`, say) is
  skipped the same way instead of taking the level down with it (#111, #112).
- OniMod Installer keeps pack names short enough for the engine: a name longer
  than 19 characters becomes its first 13 characters plus a short code. It also
  refuses a mod whose name would make Oni confuse it with a pack you already
  have, and tells you which one (#111, #112).
- `onipack` refuses to write a pack file the engine could never load.
- Heads-up: six of the depot's Character Retexture packs never actually loaded
  before (their file names were too long), so after a reinstall through the
  fixed installer you will see them applied for the first time.

### Mod safety
- Running out of engine object or physics slots no longer crashes the game.
  Mass-kill scripts and big `obj_create` ranges could exhaust the fixed pools
  Oni allocates from; the engine now logs a warning and degrades (a dropped
  item lies still instead of arcing away, extra objects are skipped) instead
  of dereferencing NULL (#107, #108). Stock play doesn't hit these limits.
- Hardened the script interpreter and pause screen against out-of-spec
  community content: scripts nested deeper than the engine's limits, functions
  with too many parameters, and data sets with extra help pages or no diary
  pages no longer corrupt memory or crash. They now degrade gracefully with a
  log warning (#85, #86). Stock game data was never affected.
- Fixed a crash in the error path for missing furniture geometry: the log
  message itself would crash instead of reporting the problem (#95). The same
  fault existed in the "filename too long" report, reachable with long
  HD-pack filenames (#99).
- A scripted film playback that aborts because the character is too far away
  no longer snaps the character's facing or swaps the film mid-play, and a
  new film no longer inherits the previous one's leftover position drift
  (#96).
- More out-of-spec content hardening: character classes missing their Stand
  animation are refused instead of crashing on spawn (#97), and the dev
  console, ambient-sound IDs, melee profiles and turret templates all got
  bounds checks where asserts used to compile away (#98).
- The `ai2_panic` script command no longer crashes the game, and its cancel
  form (timer 0) no longer puts the character straight back into panic
  (#104). No stock script uses the command, so this matters for the dev
  console and mod scripts.
- Dropping an unbuilt mod folder into `TexturePacks/` now tells you what to
  do. Mod-depot downloads are usually raw `.oni` files, which the engine
  can't load until they're packed, and the startup log used to just say
  nothing was registered. It now names the pack-building step (#110). The
  README covers it too.

### Saved games
- Your progress file is now backed up before the game ever resets it. If
  `persist.dat` can't be read, or was written by a build using a different
  save version, the old file is copied beside it as `persist.dat.v15.bak`
  (or `persist.dat.unreadable.bak`) before anything gets cleared, so save
  points, unlocked levels and diary pages are still recoverable. An existing
  backup is never overwritten, so the earliest copy is the one you keep (#91).

### In-game
- Music with several parts no longer gets stuck repeating one short part (most
  obvious at TCTF Science Prison, Save Point 4). The OpenAL sound layer had
  been looping the first part at the hardware level; it now moves on to the
  next part like the original Mac and PC builds did. There can be a tiny gap
  between parts (#115, reported by simX).
- Controls respond one frame sooner: the game now reads the keyboard and mouse
  before building each frame's input instead of after, which removed a constant
  16 ms of input lag. Part of the #49 "feels sluggish" investigation; a
  diagnostic (ONI_TICK_TRACE=1) is in for the rest.
- The aiming reticle and muzzle flashes are round again on widescreen
  displays (16:9, 21:9). Sprites were scaled with a 4:3 assumption, so they
  came out stretched sideways at anything wider (#114, reported by simX).

### Menus
- Grabbing the scrollbar thumb in a list (save/load, options) no longer makes
  the list jump to a random position. The click was sending an uninitialised
  scroll position to the list (#102).

### Input
- Keys are now read by their physical position rather than by the character
  your keyboard layout produces, so the default WASD movement works on AZERTY,
  QWERTZ and other non-QWERTY layouts without rebinding anything (#93). Bind
  names in `key_config.txt` refer to the key's QWERTY position, so `w` means
  the key above `s` wherever you are. The dev console reads raw keys the same
  way, so typing in it on a non-QWERTY layout gives you QWERTY letters; set
  `ONI_KEY_LAYOUT=1` if you'd rather have the old layout-based mapping back.

## 1.3.0r5 — 2026-07-17

### Campaign progress
- Chapters 1–9 (through *Truth and Consequences*) now verified playable
  end-to-end: combat, AI, cutscenes, save/load. Chapters 10–14 load and
  render but await a full playthrough.

### Metal renderer
- The game now remembers which renderer you picked. There's a new "Metal
  renderer" toggle on the Options screen; switching takes effect next launch.
  Holding Option at launch still works as a one-off try-it override, and
  OpenGL stays the default until you choose otherwise (#89).
- The Metal renderer is now feature-complete with OpenGL and carried the
  entire chapter 1–9 march. (Hold Option at launch to select it; OpenGL
  remains the default while it soaks.)
- Fixed HD-pack textures rendering with red/blue swapped under Metal (#67).
- Fixed glow effects (energy rings, light halos) washing out to hard white in
  fogged areas under Metal. Additive effects are now drawn fog-free, matching
  OpenGL (#82).
- Still being chased: a sporadic mid-play freeze where a phantom Escape opens
  the menu invisibly and seizes input (#78). Seen only under the opt-in Metal
  renderer on development builds; tracing is in place.

### HD texture packs
- Texture-pack support landed: drop a pack into
  `~/Library/Application Support/OniARM64/TexturePacks` and its textures
  override the originals, with no changes to your game data (#16).
- A chain of engine fixes to make packs safe: HD-sized textures no longer
  overflow load buffers or vanish (#44, #45, #60), packs can no longer hijack
  level selection (#62), and the modern 32-bit texture format now converts
  correctly, fixing the white-face, blue-face and olive-glass bugs (#63).
- Curation rules learned the hard way: sky textures are excluded (they're the
  shared reflection source for faces and vehicles), as are retextures that
  drop the original shininess masks.

### AI
- Enemies now dodge gunfire. The dodge system shipped broken in 2001: the
  code measured a distance from the world origin instead of from the
  character, so NPCs charged in a straight line for 25 years. Feral fixed the
  behaviour in their 2014 Intel port; ours is a source-level fix (#21).
- Enemies no longer forget their target after the briefest line-of-sight
  break (#22).
- Fixed two AI crashes from playtesting: patrol guards shooting at a waypoint,
  and disarmed guards running for an alarm console (#79, #80).
- Fixed a crash waiting in the final boss fight — the boss's melee code
  squeezed a 64-bit pointer through a 32-bit slot (#50).

### Stability
- Roughly twenty crash-class fixes from playtests and code audits: 64-bit
  pointer truncation, buffer overruns in colliders/costumes/spawning, a sort
  routine corrupting memory, undersized render tables (#11, #51, #53–#58,
  #66, #68, #69, #71).
- Combat sounds no longer risk lagging or playing wrong after several level
  changes — the audio cache now clears between levels (#59).
- Corrupt or incomplete game data now fails with a clear message instead of
  crashing mid-load (#28, #66).

### Input & macOS
- If Oni crashes or is force-quit, the next launch offers to file a bug:
  one button opens a pre-filled GitHub issue (build, renderer, macOS version,
  the tail of the log) and reveals the macOS crash report for drag-and-drop.
  You see the whole report before submitting, and there's a "don't ask again"
  checkbox (#74).
- The macOS press-and-hold accent picker no longer pops up over gameplay when
  holding a movement key (#77).
- Oni now behaves like a Mac app when you quit or switch away: Quit from the
  Dock icon works, Cmd-Q works everywhere, and switching apps (Cmd-Tab) pauses
  the game by opening the menu instead of letting the fight carry on without
  you (#83). Set `ONI_AUTOPAUSE=0` if you preferred the old behaviour.
- Logs rotate at 10 MB instead of quietly growing to 90 MB, and diagnostic
  spam is off by default (#70).

### Engine limits (Feral parity)
- Collision and object-sort limits raised and the pathfinding cache enlarged
  to match Feral's 1.1/1.2 Intel-port values, so busy scenes hit fewer
  limits (#42).

### Housekeeping
- The .app now carries SDL3 next to SDL2. Builds since September use the
  sdl2-compat layer, which needs SDL3 at runtime; without it bundled, a Mac
  with no Homebrew could not start the game (#118).
- Deployment target pinned to macOS 15; app category set; version + build
  stamped into the session log banner; release process written down (#72).
- Developer access can be enabled at launch via `ONI_DEV_ACCESS=1` (#47).
- Tried and reverted: anisotropic filtering made no visible difference in
  play (#65). Neural texture upscaling is parked with its pipeline
  preserved (#64).

## 1.3.0r4 — 2026-06-19

- OniARM64 now checks GitHub for a newer release on launch and offers it when
  one exists.
- Initial work on an experimental native **Metal renderer** (hold Option
  while launching to enable it). OpenGL remains the default.
- Preview status: levels 1–5 play end-to-end; later levels untested.

## 1.3.0r3 — 2026-06-05

- The Options → Resolution menu now lists your display's real modes (up to
  4K/5K) instead of a fixed table that capped at 1920×1080.

## 1.3.0r2 — 2026-06-03

- The game now prompts you to add your `GameDataFolder` on first launch
  instead of requiring a manual copy.

## 1.3.0r1 — 2026-06-02

- First public preview. Levels 1–4 playable end-to-end: combat, AI, weapons,
  doors, particles, cutscenes, save/load.
- Loads both the original 2001 Mac retail and PC game data, auto-detected.
- Distributed as a signed `.dmg` with drag-to-Applications install.

## 1.3.0a1 — 2026-05-24

- First alpha build: native ARM64 binary boots, HiDPI fullscreen rendering,
  levels 1–3 playable, audio/music/dialogue/cutscenes working.

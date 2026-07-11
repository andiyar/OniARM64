# Changelog

Plain-English log of what's changed in OniARM64, newest first. Entries here
are written for players; the per-commit developer detail lives in
[HISTORY.md](HISTORY.md).

**How it works:** user-visible changes get a line under **Unreleased** as they
land. At each release cut, the Unreleased section becomes the body of the
GitHub release notes and gets stamped with the version + date.

## Unreleased (since 1.3.0r4, 2026-06-19)

### Campaign progress
- Chapters 1–9 (through *Truth and Consequences*) now verified playable
  end-to-end: combat, AI, cutscenes, save/load. Chapters 10–14 load and
  render but await a full playthrough.

### Metal renderer
- The Metal renderer is now feature-complete with OpenGL and carried the
  entire chapter 1–9 march. (Hold Option at launch to select it; OpenGL
  remains the default while it soaks.)
- Fixed HD-pack textures rendering with red/blue swapped under Metal (#67).
- Fixed glow effects (energy rings, light halos) washing out to hard white in
  fogged areas under Metal. Additive effects are now drawn fog-free, matching
  OpenGL (#82).

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

### Stability
- Roughly twenty crash-class fixes from playtests and code audits: 64-bit
  pointer truncation, buffer overruns in colliders/costumes/spawning, a sort
  routine corrupting memory, undersized render tables (#11, #51, #53–#58,
  #66, #68, #69, #71).
- Corrupt or incomplete game data now fails with a clear message instead of
  crashing mid-load (#28, #66).

### Input & macOS
- The macOS press-and-hold accent picker no longer pops up over gameplay when
  holding a movement key (#77).
- Logs rotate at 10 MB instead of quietly growing to 90 MB, and diagnostic
  spam is off by default (#70).

### Engine limits (Feral parity)
- Collision and object-sort limits raised and the pathfinding cache enlarged
  to match Feral's 1.1/1.2 Intel-port values, so busy scenes hit fewer
  limits (#42).

### Housekeeping
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

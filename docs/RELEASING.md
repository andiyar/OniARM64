# Cutting a release

Maintainer-only doc for producing a signed + notarized + stapled `OniARM64.dmg` and publishing it as a GitHub Release.

The README is intentionally light on this — most contributors will never need to do it. This doc is the full recipe.

## What the pipeline does

`make oni_app_release` chains three scripts under [`macos/`](../macos/):

1. **[`build-bundle.sh`](../macos/build-bundle.sh)** — assembles `build/bin/OniARM64.app`. Copies `Info.plist` + `PkgInfo` + icns + cinematics + `THIRD_PARTY_LICENSES.txt`. BFS-walks the binary's dylib graph, copies every bundlable dylib into `Contents/Frameworks/`, rewrites `LC_LOAD_DYLIB` paths to `@executable_path/`, signs everything **inside-out** (dylibs → main binary → bundle) with `--options runtime --timestamp` and `entitlements.plist` (the main binary only), then runs `codesign --verify --strict` to catch sign-order bugs locally before notarization.
2. **[`notarize-bundle.sh`](../macos/notarize-bundle.sh)** — `ditto`-zips the `.app` (plain `zip` strips macOS xattrs and notary rejects), submits to Apple via `xcrun notarytool submit --wait`, staples the ticket onto the `.app`, runs `spctl --assess` as the Gatekeeper acceptance check.
3. **[`package-dmg.sh`](../macos/package-dmg.sh)** — wraps the stapled `.app` in a DMG via `create-dmg` (drag-to-Applications layout, default white background, Oni.icns volume icon), signs the DMG with Developer ID + timestamp, submits the DMG to Apple's notary as a separate round-trip, staples the ticket onto the DMG, runs `spctl --assess` on the DMG.

End result: `build/OniARM64.dmg` with the same `source=Notarized Developer ID` Gatekeeper verdict at both layers.

Total wall clock: ~7 min on a clean run. Apple's notary service dominates; both round-trips are typically 1–5 min each.

## One-time setup

These three pieces stay set across sessions; only do them once per machine (or whenever rotating credentials).

### 1. Homebrew tool

```sh
brew install create-dmg
```

`create-dmg` (Andrey Tarantsov's wrapper around `hdiutil` + AppleScript) is the only extra build dep beyond what the regular build needs (`cmake`, `sdl2`).

### 2. Apple Developer ID

You need a `Developer ID Application` certificate, which requires an active Apple Developer Program enrollment ($99/yr). Create the cert via Xcode:

> Xcode → Settings → Apple Accounts → your team → Manage Certificates → `+` → Developer ID Application

Verify it's in your keychain:

```sh
security find-identity -v -p codesigning
```

You should see a line like:

```
1) 6EF1DE311FACF1C8D50EDA64EB6AE18BA3ECA8B0 "Developer ID Application: Your Name (TEAMID)"
```

(`Apple Development` certs are a separate type — they sign for local dev/testing on your own devices, but Gatekeeper rejects them on other Macs and Apple's notary refuses them. You specifically need `Developer ID Application`.)

### 3. Notarization credentials in the keychain

Generate an app-specific password at <https://appleid.apple.com/account/manage> → Sign-In and Security → App-Specific Passwords. Stash it in the keychain under a profile name (`oniarm64-notarize` is the convention the scripts default to):

```sh
xcrun notarytool store-credentials oniarm64-notarize \
    --apple-id "<your-apple-id-email>" \
    --team-id "<your-developer-id-team>" \
    --password "<app-specific-password>"
```

Expected output ends with `Success. Credentials validated.`

After this, every `notarytool submit` in the scripts references `--keychain-profile oniarm64-notarize` — no secrets in env vars, repo, or shell history.

### 4. CMake configure

```sh
cd build
cmake .. -DPlatform_SDL=ON \
    -DONI_SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
```

The `ONI_SIGN_IDENTITY` CMake cache var is what `make oni_app_release` reads. Without it, the target prints an error pointing back at this doc and exits non-zero. The dev `make oni_app` target ignores this var and always signs ad-hoc.

A subtle gotcha: CMake's cache var can get cleared if you re-run `cmake ..` without `-DONI_SIGN_IDENTITY=...` after, say, a `make clean` or a CMake regeneration. If `make oni_app_release` ever errors with "ONI_SIGN_IDENTITY not configured", re-run the `cmake ..` line above to restore the cache.

## Per-release build

```sh
cd build
make oni_app_release
```

Produces `build/OniARM64.dmg` — signed, notarized, stapled, drag-to-Applications. Takes ~7 min.

### Recovery: notary returns "Invalid"

Fetch the rejection log:

```sh
xcrun notarytool log <submission-id> --keychain-profile oniarm64-notarize
```

Common rejection causes:
- A bundled dylib has a stale signature from `install_name_tool` and wasn't re-signed in `build-bundle.sh`. Check the script's signing loop.
- A bundled dylib has hardened-runtime-incompatible flags (rare; would mean Homebrew shipped something unusual).
- The main binary uses `com.apple.security.cs.get-task-allow=true` (forbidden for notarization). Our `entitlements.plist` deliberately doesn't set it.

### Known flake: `create-dmg` hangs

`create-dmg` wraps `hdiutil` + AppleScript and the AppleScript step talks to Finder. If Finder is in an unusual state (sandboxed by another tool, accessibility permission revoked, just in a weird mood), the script hangs at `Running AppleScript to make Finder stuff pretty...`. Symptom: the build never progresses past that line.

Fix: kill the hung `make`, clean up any lingering `/Volumes/dmg.*` mounts (`hdiutil detach /Volumes/dmg.* -force`), delete temp `build/rw.*.OniARM64.dmg` files, re-run `make oni_app_release`. Usually works the second time.

If you tire of the flake, the AppleScript-free alternative is a 6-line plain `hdiutil` script that produces a default-Finder-view DMG with the Applications symlink but no precise icon positioning. Trades the drag-arrow chrome for reliability.

## Publishing to GitHub Releases

After `make oni_app_release` produces the DMG, use the notes template:

```sh
# Copy the template, edit it
cp macos/RELEASE_NOTES_TEMPLATE.md /tmp/oni-release-notes.md
$EDITOR /tmp/oni-release-notes.md
# Fill placeholders (<NEW-VERSION>, <BUILD-STAGE>, <ONE-LINE-WHAT-CHANGED>, etc.)
# Append one new line to ## Version history — don't rewrite the existing entries
# Refresh ## What works for anything new this release

# Tag the commit being released
git tag -a v<VERSION> -m "Oni <VERSION>"
git push origin v<VERSION>

# Cut the release
gh release create v<VERSION> \
    --repo andiyar/OniARM64 \
    --title "Oni <VERSION> — <SHORT-TAGLINE>" \
    --notes-file /tmp/oni-release-notes.md \
    --prerelease \
    build/OniARM64.dmg
```

For non-alpha builds, drop `--prerelease`.

## Bumping the version

The `.app`'s visible version comes from [`macos/Info.plist`](../macos/Info.plist):

- `CFBundleShortVersionString` — user-visible (e.g., `1.3.0a1`, `1.3.0b2`, `1.3.0`)
- `CFBundleVersion` — build number (currently mirrors `CFBundleShortVersionString` for simplicity; switch to a monotonic build number if/when we wire Sparkle for auto-updates)

Bump both before running `make oni_app_release` so the version baked into the `.app` matches the git tag and the release.

## Where signing/notarization knowledge lives

Layered, source-of-truth-first:

| Layer | Location |
| --- | --- |
| This doc | [`docs/RELEASING.md`](RELEASING.md) — full recipe, maintainer reference |
| Script source | [`macos/build-bundle.sh`](../macos/build-bundle.sh), [`macos/notarize-bundle.sh`](../macos/notarize-bundle.sh), [`macos/package-dmg.sh`](../macos/package-dmg.sh) — block-comment headers explain each script's contract |
| Entitlements | [`macos/entitlements.plist`](../macos/entitlements.plist) — single key, deliberately minimal |
| CMake wiring | [`macos/bundle.cmake`](../macos/bundle.cmake) — `oni_app_release` target + `ONI_SIGN_IDENTITY` cache var |
| Release notes template | [`macos/RELEASE_NOTES_TEMPLATE.md`](../macos/RELEASE_NOTES_TEMPLATE.md) — placeholder skeleton |
| Per-session narrative | [`HISTORY.md`](../HISTORY.md) — search for "Developer ID" or "session 35" |
| Original design rationale | Parent repo, `docs/superpowers/specs/2026-05-24-developer-id-signing-design.md` — alternatives considered, decisions table, risks |
| Implementation plan | Parent repo, `docs/superpowers/plans/2026-05-24-developer-id-signing.md` — the 16-task breakdown that produced the pipeline |

The README does NOT carry this content (intentionally — it's a landing page, not a maintainer manual).

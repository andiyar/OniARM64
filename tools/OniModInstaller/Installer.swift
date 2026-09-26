// Installer.swift — OniMod Installer core (#20). No AppKit; drives the
// bundled onipack + txmp-format-index tools. Both the CLI and the droplet
// call `ModInstaller.install`.
//
// What a depot mod looks like (surveyed 2026-09-10 in HDTextureMods/):
//   <Name>/[Mod_Info.cfg] + oni/[common/]level<N>_Final/[subdir/...]/TXMP*.oni
// Casing of the level dir varies, nesting varies (0–2 dirs deep, one mod
// doubles the level dir), the wrapper dir may be absent. Only TXMP*.oni is
// safe to pack (#62: TRBS/ONSK/TXMB/WMDD hijack the engine's template scan).
import Foundation

enum InstallError: Error, CustomStringConvertible {
    case notFound(String)
    case unzipFailed(String)
    case noTextures
    case onlyScreenTiles(count: Int)  // every TXMP was a tile of a re-laid-out screen (#113)
    case alreadyInstalled(String)     // pack folder path
    case toolMissing(String)
    case packFailed(String)
    case idCollision(pack: String, level: Int)   // engine file id equal to an installed pack's
    case idCollisionWithRetail(level: Int)       // engine file id equal to the game's own level<N>_Final.dat

    var description: String {
        switch self {
        case .notFound(let p): return "Can't find \(p)."
        case .unzipFailed(let m): return "Couldn't unpack the zip: \(m)"
        case .noTextures: return "No texture files (TXMP*.oni) found in this mod. OniMod Installer only handles texture mods; character models, levels and scripts can't be installed."
        case .onlyScreenTiles(let n): return "This mod only re-lays-out screens (\(n) tiles of screens with a different grid from the game's). Screen mods aren't supported yet, see https://github.com/andiyar/OniARM64/issues/121"
        case .alreadyInstalled(let p): return "A pack with this name is already installed at \(p)."
        case .toolMissing(let t): return "The bundled helper '\(t)' is missing. Reinstall OniMod Installer."
        case .packFailed(let m): return "Packing failed: \(m)"
        case .idCollision(let p, let l): return "Oni would confuse this mod with the installed pack '\(p)': both get the same file id for level \(l) (Oni tells packs apart by a small checksum of the name, and these two check out equal, so it would silently drop one). Change NameOfMod in Mod_Info.cfg (or, if the mod has no Mod_Info.cfg, rename the zip or folder) and try again."
        case .idCollisionWithRetail(let l): return "Oni would confuse this mod with its own level\(l)_Final.dat: the name's checksum is zero, the same as 'Final', so the game would drop its own level data. Change NameOfMod in Mod_Info.cfg (or, if the mod has no Mod_Info.cfg, rename the zip or folder) and try again."
        }
    }
    /// CLI exit code. 4 = already installed (retry with --replace), 5 = engine file-id collision (with an installed pack or the game's own level data), 1 = no textures (or only re-laid-out screen tiles), 2 = anything else.
    var exitCode: Int32 {
        switch self {
        case .alreadyInstalled: return 4
        case .noTextures, .onlyScreenTiles: return 1
        case .idCollision, .idCollisionWithRetail: return 5
        default: return 2
        }
    }
}

struct InstallReport {
    var modName = ""                       // display name (from Mod_Info or folder)
    var packName = ""                      // sanitised suffix + folder name
    var packFolder = ""                    // final on-disk path
    var levels: [(level: Int, textures: Int, skipped: Int, screenSkipped: Int)] = []
    var ignoredNonTexture = 0
    var duplicateNames = 0
    var alphaGuard = ""                    // one line: what happened
    var screenCheck = ""                   // one line: did the #113 screen check run
    var warnings: [String] = []

    var text: String {
        var s = "Installed \"\(modName)\" as \(packName)\n"
        for l in levels.sorted(by: { $0.level < $1.level }) where l.textures > 0 || l.skipped > 0 {   // screen-skip-only levels have no pack
            s += "  level \(l.level): \(l.textures) textures packed"
            if l.skipped > 0 { s += ", \(l.skipped) skipped" }
            s += "\n"
        }
        let screenSkipped = levels.reduce(0) { $0 + $1.screenSkipped }
        if screenSkipped > 0 { s += "  screen tiles skipped: \(screenSkipped) (this mod re-lays-out the screen; not supported yet, see #121)\n" }
        if ignoredNonTexture > 0 { s += "  \(ignoredNonTexture) non-texture file(s) ignored\n" }
        if duplicateNames > 0 { s += "  \(duplicateNames) duplicate texture name(s) dropped (first copy kept)\n" }
        if !alphaGuard.isEmpty { s += "  alpha guard: \(alphaGuard)\n" }
        if !screenCheck.isEmpty { s += "  screen check: \(screenCheck)\n" }
        for w in warnings { s += "  note: \(w)\n" }
        s += "Pack folder: \(packFolder)"
        return s
    }
}

struct ModInstaller {
    var onipack: URL
    var indexTool: URL
    var texturePacksDir: URL
    var gameDataDir: URL?          // nil = don't try the alpha guard
    var replace = false

    static func defaultTexturePacksDir() -> URL {
        FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Application Support/OniARM64/TexturePacks")
    }

    /// Mirrors ONiBundlePath_ResolveGameDataFolder: GameDataFolder, then gamedata.
    static func defaultGameDataDir() -> URL? {
        let base = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Application Support/OniARM64")
        for name in ["GameDataFolder", "gamedata"] {
            let u = base.appendingPathComponent(name)
            var isDir: ObjCBool = false
            if FileManager.default.fileExists(atPath: u.path, isDirectory: &isDir), isDir.boolValue { return u }
        }
        return nil
    }

    // MARK: - entry

    func install(_ input: URL) throws -> InstallReport {
        let fm = FileManager.default
        guard fm.fileExists(atPath: input.path) else { throw InstallError.notFound(input.path) }
        guard fm.isExecutableFile(atPath: onipack.path) else { throw InstallError.toolMissing("onipack") }

        let work = URL(fileURLWithPath: NSTemporaryDirectory())
            .appendingPathComponent("onimod-\(ProcessInfo.processInfo.processIdentifier)-\(UUID().uuidString.prefix(8))")
        try fm.createDirectory(at: work, withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: work) }

        // 1. Source tree: unzip, or use the folder as-is.
        var isDir: ObjCBool = false
        fm.fileExists(atPath: input.path, isDirectory: &isDir)
        let tree: URL
        if isDir.boolValue {
            tree = input
        } else {
            tree = work.appendingPathComponent("unzipped")
            try fm.createDirectory(at: tree, withIntermediateDirectories: true)
            let r = run("/usr/bin/ditto", ["-x", "-k", input.path, tree.path])
            if r.status != 0 { throw InstallError.unzipFailed(r.stderr.trimmingCharacters(in: .whitespacesAndNewlines)) }
        }

        // 2. Name.
        var report = InstallReport()
        let info = findModInfo(in: tree)
        let baseName = info?["NameOfMod"] ?? stripDepotID(input.deletingPathExtension().lastPathComponent)
        report.modName = baseName
        report.packName = Self.sanitise(baseName)
        let alnumCount = baseName.unicodeScalars.filter { $0.isASCII && CharacterSet.alphanumerics.contains($0) }.count
        if alnumCount > Self.maxPackNameLength {
            report.warnings.append("name shortened to \(report.packName) so the pack's file names fit Oni's \(Self.maxLeafLength)-character limit")
        }

        // 3. Collect TXMP*.oni by level.
        var allByLevel: [Int: [URL]] = [:]
        var seen: Set<String> = []          // "level/basename"
        if let e = fm.enumerator(at: tree, includingPropertiesForKeys: [.isRegularFileKey]) {
            for case let f as URL in e {
                guard (try? f.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true else { continue }
                let name = f.lastPathComponent
                guard name.lowercased().hasSuffix(".oni") else { continue }
                guard name.hasPrefix("TXMP") else { report.ignoredNonTexture += 1; continue }
                let level = Self.levelOf(f, root: tree)
                let key = "\(level)/\(name)"
                if seen.contains(key) { report.duplicateNames += 1; continue }
                seen.insert(key)
                allByLevel[level, default: []].append(f)
            }
        }
        guard !allByLevel.isEmpty else { throw InstallError.noTextures }

        // 4. Destination check (before doing any expensive work).
        let finalDir = texturePacksDir.appendingPathComponent(report.packName)
        if fm.fileExists(atPath: finalDir.path) && !replace { throw InstallError.alreadyInstalled(finalDir.path) }

        // 4a. HD Screens tiles (#113): a mod TXMB whose grid differs from retail
        // would have its tiles drawn into the retail layout (we never pack TXMB,
        // #62), so those tiles are left out. Full support is #121. Runs after
        // the destination check so a refused reinstall never reads the retail dats.
        let (screenSkip, screenCheck) = screenTilesToSkip(tree: tree)
        report.screenCheck = screenCheck
        var byLevel: [Int: [URL]] = [:]
        var screenSkipped: [Int: Int] = [:]
        for (level, files) in allByLevel {
            for f in files {
                if screenSkip.contains(String(f.lastPathComponent.dropFirst(4).dropLast(4)).lowercased()) {
                    screenSkipped[level, default: 0] += 1
                } else {
                    byLevel[level, default: []].append(f)
                }
            }
        }
        guard !byLevel.isEmpty else { throw InstallError.onlyScreenTiles(count: screenSkipped.values.reduce(0, +)) }
        // 4b. Engine file-id collision with an installed pack (#111, #112).
        try checkFileIDCollisions(levels: Array(byLevel.keys), packName: report.packName,
                                  excluding: replace ? report.packName : nil)

        // 5. Alpha guard index from retail data (#63). Optional: warn and go on.
        var guardArgs: [String] = []
        if let gd = gameDataDir {
            switch buildAlphaGuardIndex(gameData: gd, into: work) {
            case .success(let tsv): guardArgs = ["--alpha-guard", tsv.path]; report.alphaGuard = "on (retail formats indexed)"
            case .failure(let why): report.alphaGuard = "off — \(why.message)"
            }
        } else {
            report.alphaGuard = "off — game data folder not found, so alpha-less replacements of shiny retail textures can't be screened (#63)"
        }

        // 6. Stage + pack each level into a temp pack folder.
        let stagedPack = work.appendingPathComponent("pack")
        try fm.createDirectory(at: stagedPack, withIntermediateDirectories: true)
        for (level, files) in byLevel.sorted(by: { $0.key < $1.key }) {
            let stage = work.appendingPathComponent("stage\(level)")
            try fm.createDirectory(at: stage, withIntermediateDirectories: true)
            for f in files { try fm.copyItem(at: f, to: stage.appendingPathComponent(f.lastPathComponent)) }
            let out = stagedPack.appendingPathComponent("level\(level)_\(report.packName).dat")
            let r = run(onipack.path, ["import-sep"] + guardArgs + [stage.path, out.path])
            // 0 clean, 3 packed with skips, else failure
            guard r.status == 0 || r.status == 3 else {
                throw InstallError.packFailed(r.stderr.split(separator: "\n").last.map(String.init) ?? "exit \(r.status)")
            }
            let (packed, skipped) = Self.parseOnipackSummary(r.stderr)
            report.levels.append((level, packed, skipped, screenSkipped[level] ?? 0))
        }
        for (level, n) in screenSkipped where byLevel[level] == nil {
            report.levels.append((level, 0, 0, n))   // every TXMP of this level was a skipped tile
        }

        // 7. Credits / provenance file, then move into place.
        var meta = "Installed by OniMod Installer from \(input.lastPathComponent)\n"
        if let info = info { for (k, v) in info.sorted(by: { $0.key < $1.key }) { meta += "\(k): \(v)\n" } }
        try meta.write(to: stagedPack.appendingPathComponent("Mod_Info.txt"), atomically: true, encoding: .utf8)

        try fm.createDirectory(at: texturePacksDir, withIntermediateDirectories: true)
        if fm.fileExists(atPath: finalDir.path) { try fm.removeItem(at: finalDir) }
        try fm.moveItem(at: stagedPack, to: finalDir)
        report.packFolder = finalDir.path

        let lower = baseName.lowercased()
        if lower.contains("sky") || lower.contains("skies") {
            report.warnings.append("sky textures are the reflection source for every shiny surface (faces, hair, vehicles); if characters look washed out, delete this pack folder")
        }
        return report
    }

    // MARK: - helpers

    /// `level<N>_final` component nearest the file (case-insensitive), else 0.
    static func levelOf(_ file: URL, root: URL) -> Int {
        let rel = file.pathComponents.dropFirst(root.pathComponents.count)
        for comp in rel.reversed() {
            let l = comp.lowercased()
            guard l.hasPrefix("level"), l.hasSuffix("_final") else { continue }
            let digits = l.dropFirst(5).dropLast(6)
            if let n = Int(digits), n >= 0, n < 128 { return n }
        }
        return 0
    }

    /// onipack suffix rules: [A-Za-z0-9]+, not "Final". The engine caps a file
    /// leaf at 31 characters (BFcMaxFileNameLength is 32 including the NUL) and
    /// the leaf is `level<N>_<name>.dat`, so with `level10_` (8) and `.dat` (4)
    /// the name may be at most 19 = 31 - 8 - 4 (#111, #112). Longer names keep their first
    /// 13 characters plus a 6-character base-36 FNV-1a digest of the full
    /// sanitised name: deterministic, readable, and distinct for the depot's
    /// seven `CharacterRetexture*` packs, which a plain prefix would fold into
    /// one folder and one engine id. Never change this: the leaf is what an
    /// installed pack is found by.
    static let maxPackNameLength = 19
    static let maxLeafLength = 31          // BFcMaxFileNameLength (32) less the NUL
    static let digestLength = 6

    static func sanitise(_ name: String) -> String {
        var out = String(name.unicodeScalars
            .filter { $0.isASCII && CharacterSet.alphanumerics.contains($0) }
            .map { Character($0) })
        if out.isEmpty { out = "Mod" }
        if out.lowercased() == "final" { out += "Mod" }
        if out.count > maxPackNameLength {
            out = String(out.prefix(maxPackNameLength - digestLength)) + shortDigest(out)
        }
        return out
    }

    /// 32-bit FNV-1a of the UTF-8 bytes, reduced mod 36^6 and written as six
    /// base-36 digits (0-9a-z), zero-padded.
    static func shortDigest(_ s: String) -> String {
        var h: UInt32 = 2166136261
        for b in s.utf8 { h ^= UInt32(b); h = h &* 16777619 }
        var v = h % 2176782336            // 36^6
        let digits = Array("0123456789abcdefghijklmnopqrstuvwxyz")
        var out = ""
        for _ in 0..<digestLength { out = String(digits[Int(v % 36)]) + out; v /= 36 }
        return out
    }

    /// Port of onipack's opk_file_id / the engine's TMrUtility_LevelInfo_Get:
    /// level<<25 | (checksum & 0xFFFFFF)<<1 | 1, checksum the case-insensitive
    /// weighted letter sum (toupper(c) - 'A' + 1) * position, "Final" = 0.
    /// Digits give negative terms that wrap mod 2^32 exactly as the C does.
    static func fileID(level: Int, suffix: String) -> UInt32 {
        var hash: UInt32 = 0
        if suffix != "Final" {
            var factor: UInt32 = 1
            for b in suffix.utf8 {
                let up = (b >= 0x61 && b <= 0x7a) ? b - 0x20 : b          // ASCII toupper
                let term = Int32(up) - Int32(UInt8(ascii: "A")) + 1
                hash = hash &+ (UInt32(bitPattern: term) &* factor)
                factor &+= 1
            }
        }
        return (UInt32(level) << 25) | ((hash & 0xFFFFFF) << 1) | 1
    }

    /// Leaves already under TexturePacks/<Pack>/level<N>_<Suffix>.dat, as
    /// (pack, level, suffix). Mirrors the engine's scan: only regular files count
    /// (its POSIX iterator takes DT_REG only); leaves longer than maxLeafLength
    /// bytes are never registered, so they cannot collide with anything; and the
    /// suffix runs from after `level<N>_` to the first '.', as
    /// TMrUtility_LevelInfo_Get parses it, so `level0_AB.bar.dat` has suffix `AB`.
    /// Prefix and extension match case-insensitively, like the engine's scan and
    /// the macOS file system.
    func installedPackLeaves(excluding: String?) -> [(pack: String, level: Int, suffix: String)] {
        let fm = FileManager.default
        var out: [(pack: String, level: Int, suffix: String)] = []
        guard let packs = try? fm.contentsOfDirectory(at: texturePacksDir, includingPropertiesForKeys: nil) else { return out }
        for p in packs {
            let pack = p.lastPathComponent
            if pack.hasPrefix(".") || (excluding.map { pack.caseInsensitiveCompare($0) == .orderedSame } ?? false) { continue }
            // the URL API will not list through a symlinked folder; the engine stat()s through it, so resolve first
            guard let files = try? fm.contentsOfDirectory(at: p.resolvingSymlinksInPath(), includingPropertiesForKeys: [.isRegularFileKey]) else { continue }
            for f in files {
                guard (try? f.resourceValues(forKeys: [.isRegularFileKey]))?.isRegularFile == true else { continue }
                let leaf = f.lastPathComponent
                // the engine measures the leaf with strlen, so the cap is in UTF-8 bytes
                guard leaf.lowercased().hasPrefix("level"), leaf.lowercased().hasSuffix(".dat"),
                      leaf.utf8.count <= Self.maxLeafLength else { continue }
                let stem = leaf.dropFirst(5)                             // "<N>_<Suffix>[.x].dat"
                guard let us = stem.firstIndex(of: "_"), let level = Int(stem[..<us]) else { continue }
                let rest = stem[stem.index(after: us)...]
                let suffix = String(rest[..<(rest.firstIndex(of: ".") ?? rest.endIndex)])
                if !suffix.isEmpty { out.append((pack, level, suffix)) }
            }
        }
        return out
    }

    /// Refuse a pack whose engine file id equals an installed pack's for any
    /// level it carries: the engine registers pack folders in strcmp order
    /// (ONi_TexturePacks.c sorts them before registration), so the folder that
    /// sorts first wins and the other is dropped silently (BFW_TM_Game.c overlay
    /// scan, "file index already registered"); readdir order only matters
    /// between leaves inside one folder. `excluding` is the pack being replaced.
    func checkFileIDCollisions(levels: [Int], packName: String, excluding: String?) throws {
        let installed = installedPackLeaves(excluding: excluding)
        for level in levels.sorted() {
            let mine = Self.fileID(level: level, suffix: packName)
            if (mine >> 1) & 0xFFFFFF == 0 {
                // hash 0 is what "Final" hashes to: this name would take the game's
                // own level<N>_Final.dat index and the scan would drop the retail file.
                throw InstallError.idCollisionWithRetail(level: level)
            }
            if let other = installed.first(where: { $0.level == level && Self.fileID(level: level, suffix: $0.suffix) == mine }) {
                throw InstallError.idCollision(pack: other.pack, level: level)
            }
        }
    }

    /// "23951-CharacterRetexture-Pt1" → "CharacterRetexture-Pt1"; a bare number stays.
    func stripDepotID(_ s: String) -> String {
        let parts = s.split(separator: "-", maxSplits: 1, omittingEmptySubsequences: false)
        if parts.count == 2, parts[0].count >= 4, parts[0].allSatisfy(\.isNumber), !parts[1].isEmpty {
            return String(parts[1])
        }
        return s
    }

    /// Nearest Mod_Info.cfg within three levels of the tree root. `Key -> Value` lines.
    func findModInfo(in tree: URL) -> [String: String]? {
        let fm = FileManager.default
        var queue = [tree]
        var depth = 0
        while !queue.isEmpty && depth <= 3 {
            var next: [URL] = []
            for dir in queue {
                let cfg = dir.appendingPathComponent("Mod_Info.cfg")
                if let text = (try? String(contentsOf: cfg, encoding: .utf8)) ?? (try? String(contentsOf: cfg, encoding: .isoLatin1)) {
                    var d: [String: String] = [:]
                    for line in text.split(whereSeparator: \.isNewline) {
                        guard let r = line.range(of: "->") else { continue }
                        let k = line[..<r.lowerBound].trimmingCharacters(in: .whitespaces)
                        let v = line[r.upperBound...].trimmingCharacters(in: .whitespaces)
                        if !k.isEmpty { d[k] = v }
                    }
                    return d
                }
                if let kids = try? fm.contentsOfDirectory(at: dir, includingPropertiesForKeys: [.isDirectoryKey]) {
                    next += kids.filter { (try? $0.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true }
                }
            }
            queue = next; depth += 1
        }
        return nil
    }

    /// One TXMB grid as `txmp-format-index --txmb` prints it.
    struct ScreenGrid { var width: Int; var height: Int; var count: Int; var tiles: [String] }

    /// Runs the index tool in --txmb mode; returns (lowercased name, grid) per record in output order.
    func screenGrids(_ files: [URL]) -> [(name: String, grid: ScreenGrid)] {
        guard !files.isEmpty, FileManager.default.isExecutableFile(atPath: indexTool.path) else { return [] }
        let r = run(indexTool.path, ["--txmb"] + files.map(\.path))
        var out: [(name: String, grid: ScreenGrid)] = []
        for line in r.stdout.split(whereSeparator: \.isNewline) {
            let f = line.split(separator: "\t", omittingEmptySubsequences: false)
            guard f.count >= 6, f[0] != "-", let w = Int(f[1]), let h = Int(f[2]), let n = Int(f[3]) else { continue }
            let tiles = f[4].split(separator: ",").map { $0.lowercased() }.filter { $0 != "-" }
            out.append((f[0].lowercased(), ScreenGrid(width: w, height: h, count: n, tiles: tiles)))
        }
        return out
    }

    /// Lowercased tile names (TXMP prefix stripped) of every mod screen whose
    /// TXMB grid (width, height or tile count) differs from the retail TXMB of
    /// the same name: the union of the mod's and retail's tile names (#113). A
    /// mod TXMB with no retail twin, a same-grid one, or no retail data at all
    /// skips nothing. Retail: level*_Final.dat, first hit per name wins.
    /// Also returns a one-line state for the report ("on ..." or "off — why").
    func screenTilesToSkip(tree: URL) -> (skip: Set<String>, state: String) {
        let fm = FileManager.default
        guard let gd = gameDataDir else {
            return ([], "off — game data folder not found, so screen mods with a different grid (#113) can't be detected")
        }
        guard fm.isExecutableFile(atPath: indexTool.path) else {
            return ([], "off — the bundled helper 'txmp-format-index' is missing, so screen mods with a different grid (#113) can't be detected")
        }
        let dats = ((try? fm.contentsOfDirectory(at: gd, includingPropertiesForKeys: nil)) ?? [])
            .filter { $0.lastPathComponent.hasPrefix("level") && $0.lastPathComponent.hasSuffix("_Final.dat") }
            .sorted { $0.lastPathComponent < $1.lastPathComponent }
        var retail: [String: ScreenGrid] = [:]
        for r in screenGrids(dats) where retail[r.name] == nil { retail[r.name] = r.grid }
        guard !retail.isEmpty else {
            return ([], "off — no screens found in the game data folder, so screen mods with a different grid (#113) can't be detected")
        }
        var modFiles: [URL] = []
        if let e = fm.enumerator(at: tree, includingPropertiesForKeys: [.isRegularFileKey]) {
            for case let f as URL in e {
                let n = f.lastPathComponent
                guard n.hasPrefix("TXMB"), n.lowercased().hasSuffix(".oni"),
                      (try? f.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true else { continue }
                modFiles.append(f)
            }
        }
        var skip: Set<String> = []
        for m in screenGrids(modFiles) {
            guard let r = retail[m.name] else { continue }
            if m.grid.width != r.width || m.grid.height != r.height || m.grid.count != r.count {
                skip.formUnion(m.grid.tiles); skip.formUnion(r.tiles)
            }
        }
        return (skip, "on (retail screens indexed)")
    }

    struct GuardFailure: Error { var message: String }

    /// Retail name→format index for onipack --alpha-guard, same merge rule as
    /// scripts/build-hd-overlays.sh: skip unnamed ("-"), first format wins unless a
    /// later level shows an alpha-carrying format for the same name.
    func buildAlphaGuardIndex(gameData: URL, into work: URL) -> Result<URL, GuardFailure> {
        let fm = FileManager.default
        guard fm.isExecutableFile(atPath: indexTool.path) else { return .failure(.init(message: "bundled txmp-format-index missing")) }
        let dats = ((try? fm.contentsOfDirectory(at: gameData, includingPropertiesForKeys: nil)) ?? [])
            .filter { $0.lastPathComponent.hasPrefix("level") && $0.lastPathComponent.hasSuffix("_Final.dat") }
            .sorted { $0.lastPathComponent < $1.lastPathComponent }
        guard !dats.isEmpty else { return .failure(.init(message: "no level*_Final.dat in \(gameData.path)")) }
        let r = run(indexTool.path, dats.map(\.path))
        guard r.status == 0 else { return .failure(.init(message: "index tool failed: \(r.stderr.prefix(200))")) }
        let alpha: Set<String> = ["BGRA4444", "BGRA5551", "RGBA", "A8", "A4I4"]
        var fmt: [String: String] = [:]
        var order: [String] = []
        for line in r.stdout.split(whereSeparator: \.isNewline) {
            let f = line.split(separator: "\t", omittingEmptySubsequences: false)
            guard f.count >= 2, f[0] != "-" else { continue }
            let name = String(f[0]), format = String(f[1])
            if let old = fmt[name] {
                if old != format && alpha.contains(format) && !alpha.contains(old) { fmt[name] = format }
            } else { fmt[name] = format; order.append(name) }
        }
        guard !order.isEmpty else { return .failure(.init(message: "retail index came back empty")) }
        let tsv = work.appendingPathComponent("retail-txmp-formats.tsv")
        let body = order.map { "\($0)\t\(fmt[$0]!)\tretail" }.joined(separator: "\n") + "\n"
        do { try body.write(to: tsv, atomically: true, encoding: .utf8) } catch { return .failure(.init(message: "\(error)")) }
        return .success(tsv)
    }

    /// "onipack: level0_X.dat: 12 textures packed (14 instances ...), 1 skipped"
    static func parseOnipackSummary(_ stderr: String) -> (Int, Int) {
        var packed = 0, skipped = 0
        for line in stderr.split(whereSeparator: \.isNewline) where line.contains("textures packed") {
            let words = line.split(separator: " ")
            for (i, w) in words.enumerated() {
                if w == "textures" && i > 0 { packed = Int(words[i - 1]) ?? 0 }
                if w.hasPrefix("skipped") && i > 0 { skipped = Int(words[i - 1]) ?? 0 }
            }
        }
        return (packed, skipped)
    }

    struct RunResult { var status: Int32; var stdout: String; var stderr: String }

    func run(_ tool: String, _ args: [String]) -> RunResult {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: tool)
        p.arguments = args
        let out = Pipe(), err = Pipe()
        p.standardOutput = out; p.standardError = err
        do { try p.run() } catch { return RunResult(status: 127, stdout: "", stderr: "\(error)") }
        // Drain both pipes before waiting so a chatty tool can't fill one and block.
        var o = Data(), e = Data()
        let group = DispatchGroup()
        group.enter(); DispatchQueue.global().async { o = out.fileHandleForReading.readDataToEndOfFile(); group.leave() }
        group.enter(); DispatchQueue.global().async { e = err.fileHandleForReading.readDataToEndOfFile(); group.leave() }
        group.wait()
        p.waitUntilExit()
        return RunResult(status: p.terminationStatus,
                         stdout: String(decoding: o, as: UTF8.self),
                         stderr: String(decoding: e, as: UTF8.self))
    }
}

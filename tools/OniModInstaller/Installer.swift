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
    case alreadyInstalled(String)     // pack folder path
    case toolMissing(String)
    case packFailed(String)

    var description: String {
        switch self {
        case .notFound(let p): return "Can't find \(p)."
        case .unzipFailed(let m): return "Couldn't unpack the zip: \(m)"
        case .noTextures: return "No texture files (TXMP*.oni) found in this mod. OniMod Installer only handles texture mods; character models, levels and scripts can't be installed."
        case .alreadyInstalled(let p): return "A pack with this name is already installed at \(p)."
        case .toolMissing(let t): return "The bundled helper '\(t)' is missing. Reinstall OniMod Installer."
        case .packFailed(let m): return "Packing failed: \(m)"
        }
    }
    /// CLI exit code. 4 is reserved for "already installed" so a caller can retry with --replace.
    var exitCode: Int32 {
        switch self {
        case .alreadyInstalled: return 4
        case .noTextures: return 1
        default: return 2
        }
    }
}

struct InstallReport {
    var modName = ""                       // display name (from Mod_Info or folder)
    var packName = ""                      // sanitised suffix + folder name
    var packFolder = ""                    // final on-disk path
    var levels: [(level: Int, textures: Int, skipped: Int)] = []
    var ignoredNonTexture = 0
    var duplicateNames = 0
    var alphaGuard = ""                    // one line: what happened
    var warnings: [String] = []

    var text: String {
        var s = "Installed \"\(modName)\" as \(packName)\n"
        for l in levels.sorted(by: { $0.level < $1.level }) {
            s += "  level \(l.level): \(l.textures) textures packed"
            if l.skipped > 0 { s += ", \(l.skipped) skipped" }
            s += "\n"
        }
        if ignoredNonTexture > 0 { s += "  \(ignoredNonTexture) non-texture file(s) ignored\n" }
        if duplicateNames > 0 { s += "  \(duplicateNames) duplicate texture name(s) dropped (first copy kept)\n" }
        if !alphaGuard.isEmpty { s += "  alpha guard: \(alphaGuard)\n" }
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

        // 3. Collect TXMP*.oni by level.
        var byLevel: [Int: [URL]] = [:]
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
                byLevel[level, default: []].append(f)
            }
        }
        guard !byLevel.isEmpty else { throw InstallError.noTextures }

        // 4. Destination check (before doing any expensive work).
        let finalDir = texturePacksDir.appendingPathComponent(report.packName)
        if fm.fileExists(atPath: finalDir.path) && !replace { throw InstallError.alreadyInstalled(finalDir.path) }

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
            report.levels.append((level, packed, skipped))
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

    /// onipack suffix rules: [A-Za-z0-9]+, not "Final"; keep it short.
    static func sanitise(_ name: String) -> String {
        var s = name.unicodeScalars
            .filter { $0.isASCII && CharacterSet.alphanumerics.contains($0) }
            .map { Character($0) }
        if s.count > 32 { s = Array(s.prefix(32)) }
        var out = String(s)
        if out.isEmpty { out = "Mod" }
        if out.lowercased() == "final" { out += "Mod" }
        return out
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

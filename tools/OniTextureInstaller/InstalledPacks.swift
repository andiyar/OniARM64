// InstalledPacks.swift — what is in TexturePacks now (#124).
import AppKit

struct InstalledPack: Equatable {
    var name: String            // folder name
    var folder: URL
    var levels: Int             // files named level<N>_*.dat
    var bytes: Int
    var info: [String: String]  // Mod_Info.txt "Key: Value" lines (empty if none)
    var depotPackage: Int?      // from "DepotPackage: N"
    /// "Depot 70000", "file:<name>", or "by hand"
    var sourceText: String {
        if let n = depotPackage { return "Depot \(n)" }
        if let s = info["Source"], !s.isEmpty { return "file:\(s)" }
        if let first = info["_first"], let r = first.range(of: " from ") { return "file:" + first[r.upperBound...] }
        return "by hand"
    }
}

enum InstalledPacks {
    static func scan(dir: URL) -> [InstalledPack] {
        let fm = FileManager.default
        guard let entries = try? fm.contentsOfDirectory(at: dir, includingPropertiesForKeys: [.isDirectoryKey]) else { return [] }
        var out: [InstalledPack] = []
        for e in entries where !e.lastPathComponent.hasPrefix(".") {
            // resolve first: the URL API will not see through a symlinked folder, the engine stat()s through it
            let resolved = e.resolvingSymlinksInPath()
            guard (try? resolved.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true else { continue }
            let files = (try? fm.contentsOfDirectory(at: resolved, includingPropertiesForKeys: [.fileSizeKey, .isRegularFileKey])) ?? []
            var levels = 0, bytes = 0
            for f in files {
                let rv = try? f.resourceValues(forKeys: [.fileSizeKey, .isRegularFileKey])
                guard rv?.isRegularFile == true else { continue }
                bytes += rv?.fileSize ?? 0
                let leaf = f.lastPathComponent.lowercased()
                if leaf.hasPrefix("level"), leaf.hasSuffix(".dat"), leaf.dropFirst(5).first?.isNumber == true, leaf.contains("_") { levels += 1 }
            }
            let info = readInfo(resolved.appendingPathComponent("Mod_Info.txt"))
            out.append(InstalledPack(name: e.lastPathComponent, folder: e, levels: levels, bytes: bytes, info: info,
                                     depotPackage: info["DepotPackage"].flatMap { Int($0) }))
        }
        return out.sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
    }

    /// "Key: Value" lines; the first line is kept under "_first" (it carries the source in older packs).
    static func readInfo(_ url: URL) -> [String: String] {
        guard let text = (try? String(contentsOf: url, encoding: .utf8)) ?? (try? String(contentsOf: url, encoding: .isoLatin1)) else { return [:] }
        var d: [String: String] = [:]
        for (i, raw) in text.split(whereSeparator: \.isNewline).enumerated() {
            let line = String(raw)
            if i == 0 { d["_first"] = line }
            guard let c = line.firstIndex(of: ":") else { continue }
            let k = line[..<c].trimmingCharacters(in: .whitespaces), v = line[line.index(after: c)...].trimmingCharacters(in: .whitespaces)
            if !k.isEmpty, d[k] == nil { d[k] = v }
        }
        return d
    }

    /// Moves the pack folder to the Trash. Throws on failure. Never deletes.
    static func trash(_ pack: InstalledPack) throws {
        var trashed: NSURL?
        try FileManager.default.trashItem(at: pack.folder, resultingItemURL: &trashed)
    }

    static func tsvLine(_ p: InstalledPack) -> String { "\(p.name)\t\(p.levels)\t\(p.bytes)\t\(p.sourceText)" }
}

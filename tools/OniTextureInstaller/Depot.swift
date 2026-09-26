// Depot.swift — the Oni Mod Depot's package index (#124).
// The AE Installer reads http://mods.oni2.net/jsoncache/jsoncache.zip: four Drupal
// dumps (vocabulary.json, terms.json, nodes.json, files.json). We keep packages whose
// "Mod type" includes Texture and whose "Install method" is Package.
// Hand-test hook: OTI_INDEX_URL overrides the index URL (e.g. http://127.0.0.1:9/x to simulate offline).
import Foundation

struct DepotPackage: Equatable {
    var nid: Int
    var packageNumber: Int
    var title: String
    var creator: String
    var version: String
    var description: String      // HTML stripped, whitespace collapsed
    var fileName: String
    var fileSize: Int
    var downloadURL: URL
}

enum DepotError: Error, CustomStringConvertible {
    case unzipFailed(String), missingFile(String), badJSON(String), missingTerm(String)
    var description: String {
        switch self {
        case .unzipFailed(let m): return "Couldn't unpack the Depot index: \(m)"
        case .missingFile(let f): return "The Depot index has no \(f)"
        case .badJSON(let f): return "The Depot index's \(f) is not the JSON shape we expect"
        case .missingTerm(let t): return "The Depot index has no '\(t)' term"
        }
    }
}

enum DepotIndex {
    static let indexURL = ProcessInfo.processInfo.environment["OTI_INDEX_URL"].flatMap { $0.isEmpty ? nil : URL(string: $0) } ?? URL(string: "http://mods.oni2.net/jsoncache/jsoncache.zip")!
    static let downloadBase = "http://mods.oni2.net/system/files/"

    /// Unzips with ditto into a temp folder, parses, filters, sorts by title (case-insensitive).
    static func parse(zipURL: URL) throws -> [DepotPackage] {
        let fm = FileManager.default
        let work = URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent("depot-\(UUID().uuidString.prefix(8))")
        try fm.createDirectory(at: work, withIntermediateDirectories: true)
        defer { try? fm.removeItem(at: work) }
        let p = Process(); p.executableURL = URL(fileURLWithPath: "/usr/bin/ditto")
        p.arguments = ["-x", "-k", zipURL.path, work.path]
        let err = Pipe(); p.standardError = err
        try p.run(); p.waitUntilExit()
        guard p.terminationStatus == 0 else {
            let msg = (String(data: err.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
            throw DepotError.unzipFailed(msg.isEmpty ? "exit \(p.terminationStatus)" : msg)
        }
        func load(_ name: String) throws -> [[String: Any]] {
            // entries sit at the zip root; also look one level down in case of a wrapper folder
            let candidates = [work.appendingPathComponent(name)] + ((try? fm.contentsOfDirectory(at: work, includingPropertiesForKeys: nil)) ?? []).map { $0.appendingPathComponent(name) }
            guard let url = candidates.first(where: { fm.fileExists(atPath: $0.path) }) else { throw DepotError.missingFile(name) }
            // not valid JSON, or valid JSON that is not an array of objects: both are the shape error
            guard let data = try? Data(contentsOf: url), let obj = try? JSONSerialization.jsonObject(with: data),
                  let arr = obj as? [[String: Any]] else { throw DepotError.badJSON(name) }
            return arr
        }
        let vocab = try load("vocabulary.json"), terms = try load("terms.json")
        let nodes = try load("nodes.json"), files = try load("files.json")
        return try packages(vocab: vocab, terms: terms, nodes: nodes, files: files)
    }

    /// Pure: the filter and field mapping, separated from the unzip so it can be fed arrays directly.
    static func packages(vocab: [[String: Any]], terms: [[String: Any]], nodes: [[String: Any]], files: [[String: Any]]) throws -> [DepotPackage] {
        func int(_ v: Any?) -> Int? { if let i = v as? Int { return i }; if let s = v as? String { return Int(s) }; return nil }
        func vid(named n: String) throws -> Int {
            guard let v = vocab.first(where: { ($0["name"] as? String) == n }), let id = int(v["vid"]) else { throw DepotError.missingTerm(n) }
            return id
        }
        let typeVid = try vid(named: "Mod type"), methodVid = try vid(named: "Install method")
        func tid(named n: String, in v: Int) throws -> Int {
            guard let t = terms.first(where: { ($0["name"] as? String) == n && int($0["vid"]) == v }), let id = int(t["tid"]) else { throw DepotError.missingTerm(n) }
            return id
        }
        let textureTid = try tid(named: "Texture", in: typeVid), packageTid = try tid(named: "Package", in: methodVid)
        var fileByFid: [Int: [String: Any]] = [:]
        for f in files { if let id = int(f["fid"]) { fileByFid[id] = f } }
        func und(_ node: [String: Any], _ key: String) -> [[String: Any]] { ((node[key] as? [String: Any])?["und"] as? [[String: Any]]) ?? [] }
        func firstValue(_ node: [String: Any], _ key: String) -> String { (und(node, key).first?["value"] as? String) ?? "" }
        var out: [DepotPackage] = []
        for n in nodes where (n["type"] as? String) == "mod" {
            let types = und(n, "taxonomy_vocabulary_\(typeVid)").compactMap { int($0["tid"]) }
            let methods = und(n, "taxonomy_vocabulary_\(methodVid)").compactMap { int($0["tid"]) }
            guard types.contains(textureTid), methods.contains(packageTid) else { continue }
            guard let up = und(n, "upload").first(where: { int($0["display"]) != 0 }), let fid = int(up["fid"]) else { continue }
            let f = fileByFid[fid]
            let fileName = (f?["filename"] as? String) ?? (up["filename"] as? String) ?? ""
            let urlString = (f?["uri_full"] as? String) ?? (downloadBase + fileName)
            guard !fileName.isEmpty, let url = URL(string: urlString), let nid = int(n["nid"]) else { continue }
            out.append(DepotPackage(nid: nid,
                                    packageNumber: Int(firstValue(n, "field_package_number")) ?? 0,
                                    title: ((n["title"] as? String) ?? "").trimmingCharacters(in: .whitespacesAndNewlines),
                                    creator: firstValue(n, "field_creator").trimmingCharacters(in: .whitespacesAndNewlines),
                                    version: firstValue(n, "field_version").trimmingCharacters(in: .whitespacesAndNewlines),
                                    description: stripHTML(firstValue(n, "body")),
                                    fileName: fileName,
                                    fileSize: int(f?["filesize"]) ?? int(up["filesize"]) ?? 0,
                                    downloadURL: url))
        }
        return out.sorted { $0.title.localizedCaseInsensitiveCompare($1.title) == .orderedAscending }
    }

    /// Drops tags, unescapes the few entities the Depot uses, collapses whitespace.
    static func stripHTML(_ s: String) -> String {
        var t = s.replacingOccurrences(of: "<br\\s*/?>", with: "\n", options: .regularExpression)
        t = t.replacingOccurrences(of: "</p>", with: "\n")
        t = t.replacingOccurrences(of: "<[^>]+>", with: "", options: .regularExpression)
        for (e, c) in [("&amp;", "&"), ("&lt;", "<"), ("&gt;", ">"), ("&quot;", "\""), ("&#039;", "'"), ("&nbsp;", " ")] { t = t.replacingOccurrences(of: e, with: c) }
        t = t.replacingOccurrences(of: "\r", with: "")
        t = t.replacingOccurrences(of: "[ \\t]+", with: " ", options: .regularExpression)
        t = t.replacingOccurrences(of: "\\n{3,}", with: "\n\n", options: .regularExpression)
        return t.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    /// One tab-separated line per package: number, title, creator, version, size, file name, URL.
    static func tsvLine(_ p: DepotPackage) -> String {
        [String(p.packageNumber), p.title, p.creator, p.version, String(p.fileSize), p.fileName, p.downloadURL.absoluteString]
            .map { $0.replacingOccurrences(of: "\t", with: " ").replacingOccurrences(of: "\n", with: " ") }
            .joined(separator: "\t")
    }
}

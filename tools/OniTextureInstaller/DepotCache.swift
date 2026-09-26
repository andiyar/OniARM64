// DepotCache.swift — the last-fetched Depot index, so the catalogue shows at once and offline (#124).
// Hand-test hook: OTI_CACHE_DIR points the cache somewhere other than Application Support.
import Foundation

struct DepotCache {
    var dir: URL
    static func defaultDir() -> URL {
        if let p = ProcessInfo.processInfo.environment["OTI_CACHE_DIR"], !p.isEmpty { return URL(fileURLWithPath: p) }   // seat's hand checks
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        return base.appendingPathComponent("OniARM64/TextureInstaller")
    }
    var zipURL: URL { dir.appendingPathComponent("jsoncache.zip") }
    var dateURL: URL { dir.appendingPathComponent("index-date.txt") }

    /// (packages, date) from the cached zip, or nil if there is none or it does not parse.
    func load() -> (packages: [DepotPackage], date: String)? {
        guard FileManager.default.fileExists(atPath: zipURL.path), let pk = try? DepotIndex.parse(zipURL: zipURL) else { return nil }
        let date = (try? String(contentsOf: dateURL, encoding: .utf8))?.trimmingCharacters(in: .whitespacesAndNewlines) ?? "unknown date"
        return (pk, date)
    }

    /// Downloads the index, parses it (so a bad fetch never replaces a good cache), then stores it.
    /// Synchronous; call from a work queue, never main.
    func refresh() throws -> (packages: [DepotPackage], date: String) {
        let tmp = URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent("oti-index-\(UUID().uuidString.prefix(8)).zip")
        defer { try? FileManager.default.removeItem(at: tmp) }
        var comps = URLComponents(url: DepotIndex.indexURL, resolvingAgainstBaseURL: false)!
        comps.queryItems = (comps.queryItems ?? []) + [URLQueryItem(name: "ts", value: String(Int(Date().timeIntervalSince1970)))]
        _ = try Downloader.download(comps.url!, to: tmp) { _ in }
        let pk = try DepotIndex.parse(zipURL: tmp)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        try? FileManager.default.removeItem(at: zipURL)
        try FileManager.default.copyItem(at: tmp, to: zipURL)
        let date = ISO8601DateFormatter().string(from: Date())
        try date.write(to: dateURL, atomically: true, encoding: .utf8)
        return (pk, date)
    }
}

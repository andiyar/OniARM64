// ReportLog.swift — every run's report goes to ~/Library/Logs/OniARM64/installer.txt (#123).
import Foundation

enum ReportLog {
    static func logURL() -> URL {
        let home = ProcessInfo.processInfo.environment["HOME"].map { URL(fileURLWithPath: $0) } ?? FileManager.default.homeDirectoryForCurrentUser
        return home.appendingPathComponent("Library/Logs/OniARM64/installer.txt")
    }

    /// Appends one entry: "=== <ISO-8601 time>  source: <source>" then the text, then a blank line.
    /// Never throws; a log failure must not fail an install.
    static func append(source: String, text: String) {
        let url = logURL()
        let fm = FileManager.default
        try? fm.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        let stamp = ISO8601DateFormatter().string(from: Date())
        let entry = "=== \(stamp)  source: \(source)\n\(text)\n\n"
        guard let data = entry.data(using: .utf8) else { return }
        if let h = try? FileHandle(forWritingTo: url) {
            defer { try? h.close() }
            _ = try? h.seekToEnd()
            try? h.write(contentsOf: data)
        } else {
            try? data.write(to: url)
        }
    }
}

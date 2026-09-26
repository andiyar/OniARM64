// Batch.swift — run several installs in sequence and produce one combined report (#124).
// Shared by the window (dropped files, ticked Depot packages) and the CLI (--install a b c).
// Runs on the caller's thread, which must not be main (downloads and installs block).
import Foundation

enum BatchItem {
    case file(URL)
    case depot(DepotPackage)
    var displayName: String { switch self { case .file(let u): return u.lastPathComponent; case .depot(let p): return p.title } }
    var sourceLabel: String { switch self { case .file(let u): return u.path; case .depot(let p): return "depot \(p.packageNumber) \(p.downloadURL.absoluteString)" } }
    var depotNid: Int? { if case .depot(let p) = self { return p.nid }; return nil }
}

struct BatchOutcome { var text: String; var installed: Int; var skipped: Int; var failed: Int; var firstFailureCode: Int32?
    /// nids of .depot items that failed (download or install), so the window can keep them ticked.
    var failedNids: [Int] = [] }

struct BatchRunner {
    var makeInstaller: () -> ModInstaller
    /// Applied after the item's own source (the CLI's --source-depot uses it). nil = keep the item's.
    var sourceOverride: InstallSource? = nil
    /// Called on the work thread; must block until the user answers. CLI: returns the --replace flag.
    var askReplace: (String) -> Bool
    /// (item index, fraction 0…1 or nil for indeterminate, label). Called on the work thread, except
    /// download fractions, which arrive on URLSession's delegate queue (throttled to 1% steps).
    var progress: (Int, Double?, String) -> Void = { _, _, _ in }
    var log: (String, String) -> Void = ReportLog.append(source:text:)

    func run(_ items: [BatchItem]) -> BatchOutcome {
        var sections: [String] = []; var installed = 0, skipped = 0, failed = 0; var firstCode: Int32?; var failedNids: [Int] = []
        // Downloads land here and go when the batch ends, installed or not.
        let work = URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent("oti-batch-\(UUID().uuidString.prefix(8))")
        try? FileManager.default.createDirectory(at: work, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: work) }
        for (i, item) in items.enumerated() {
            var inst = makeInstaller()
            let input: URL
            switch item {
            case .file(let u): input = u; inst.source = .file(u.lastPathComponent)
            case .depot(let p):
                inst.source = .depot(packageNumber: p.packageNumber, title: p.title)
                progress(i, 0, "Downloading \(p.title)…")
                do {
                    input = try Downloader.download(p.downloadURL, to: work.appendingPathComponent(Self.safeFileName(p))) { f in
                        progress(i, f, "Downloading \(p.title)… \(Int(f * 100))%")
                    }
                } catch {
                    let m = "download failed: \(error.localizedDescription)"
                    sections.append("\(p.title): Nothing installed: \(m)"); failed += 1; firstCode = firstCode ?? 6; failedNids.append(p.nid)
                    log(item.sourceLabel, "Nothing installed: \(m)")
                    continue
                }
            }
            if let o = sourceOverride { inst.source = o }
            progress(i, nil, "Installing \(item.displayName)…")
            do {
                var report: InstallReport
                do { inst.replace = false; report = try inst.install(input) }
                catch InstallError.alreadyInstalled(let path) {
                    guard askReplace(path) else {
                        sections.append("\(item.displayName): skipped, already installed at \(path).")
                        log(item.sourceLabel, "Skipped: already installed at \(path) (use Replace, or --replace on the command line, to re-pack it).")
                        skipped += 1; continue
                    }
                    inst.replace = true; report = try inst.install(input)
                }
                sections.append(report.text); installed += 1; log(item.sourceLabel, report.text)
            } catch {
                let e = error as? InstallError
                let msg = e?.description ?? "\(error)"
                sections.append("\(item.displayName): Nothing installed: \(msg)"); failed += 1; firstCode = firstCode ?? (e?.exitCode ?? 2)
                if let n = item.depotNid { failedNids.append(n) }
                log(item.sourceLabel, "Nothing installed: \(msg)")
            }
        }
        var text = sections.joined(separator: "\n\n") + "\n\n\(installed) installed, \(skipped) skipped, \(failed) failed."
        if installed == 1 { text += "\nThe pack loads next time Oni starts." }
        else if installed > 1 { text += "\nThe packs load next time Oni starts." }
        return BatchOutcome(text: text, installed: installed, skipped: skipped, failed: failed, firstFailureCode: firstCode, failedNids: failedNids)
    }

    /// The Depot's file name comes off the network. Downloader removes whatever sits at the
    /// destination before moving the download in, so a name with "../" or a leading dot could
    /// delete or clobber a file outside the batch folder. Keep only the last path component, and
    /// fall back to a fixed name when that is empty, ".", ".." or hidden.
    static func safeFileName(_ p: DepotPackage) -> String {
        let leaf = (p.fileName as NSString).lastPathComponent
        if leaf.isEmpty || leaf.hasPrefix(".") || leaf.contains("/") { return "package-\(p.packageNumber).zip" }
        return leaf
    }
}

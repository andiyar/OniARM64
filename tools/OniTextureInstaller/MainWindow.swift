// MainWindow.swift — the Oni Texture Installer window (#124).
// Layout (top to bottom): catalogue box (later tasks fill it), installed-packs table
// (InstalledPacks.swift scan, Reveal / Remove to the Trash), report box. Dropping a zip or folder anywhere on the window installs it.
import AppKit
import UniformTypeIdentifiers

final class DropView: NSView {
    var onDrop: (([URL]) -> Void)?
    override init(frame: NSRect) { super.init(frame: frame); registerForDraggedTypes([.fileURL]) }
    required init?(coder: NSCoder) { fatalError() }
    override func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation { urls(sender).isEmpty ? [] : .copy }
    override func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        let u = urls(sender); guard !u.isEmpty else { return false }; onDrop?(u); return true
    }
    private func urls(_ s: NSDraggingInfo) -> [URL] {
        let items = s.draggingPasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL] ?? []
        return items.filter { $0.pathExtension.lowercased() == "zip" || (try? $0.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true }
    }
}

final class MainWindowController: NSWindowController {
    let reportView = NSTextView()
    let progress = NSProgressIndicator()
    let progressLabel = NSTextField(labelWithString: "")
    let catalogueBox = NSBox()
    let installedBox = NSBox()
    let reportBox = NSBox()
    private let queue = DispatchQueue(label: "installer.work")
    // Installed-list scans get their own serial queue so they never wait behind an install batch,
    // and stay in order (a later scan can not be overwritten by an earlier one).
    private let scanQueue = DispatchQueue(label: "installer.scan", qos: .userInitiated)
    // Main-thread only. Batches can overlap (a second drop while one runs); the bar stays up
    // until the last finishes, and later reports in the series append rather than replace.
    private var runningBatches = 0
    private var seriesHasReport = false
    // Installed-packs list (main-thread only).
    let installedTable = NSTableView()
    var installed: [InstalledPack] = []
    private let revealButton = NSButton(title: "Reveal in Finder", target: nil, action: nil)
    private let removeButton = NSButton(title: "Remove…", target: nil, action: nil)
    private let sizeFormatter: ByteCountFormatter = { let f = ByteCountFormatter(); f.countStyle = .file; return f }()

    init() {
        let w = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 900, height: 700),
                         styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
        w.title = "Oni Texture Installer"
        w.minSize = NSSize(width: 820, height: 600)
        super.init(window: w)
        let root = DropView(frame: w.contentView!.bounds)
        root.autoresizingMask = [.width, .height]
        root.onDrop = { [weak self] urls in self?.install(files: urls) }
        w.contentView = root
        buildLayout(in: root)
        w.center()
        reloadInstalled()
    }
    required init?(coder: NSCoder) { fatalError() }

    /// Puts a single centred, wrapping grey label inside a box (placeholder until a later task fills it).
    private func fill(_ box: NSBox, title: String, note: String) {
        box.title = title
        box.titlePosition = .atTop
        box.translatesAutoresizingMaskIntoConstraints = false
        let label = NSTextField(wrappingLabelWithString: note)
        label.alignment = .center
        label.textColor = .secondaryLabelColor
        label.translatesAutoresizingMaskIntoConstraints = false
        let content = box.contentView!
        content.addSubview(label)
        NSLayoutConstraint.activate([
            label.centerXAnchor.constraint(equalTo: content.centerXAnchor),
            label.centerYAnchor.constraint(equalTo: content.centerYAnchor),
            label.leadingAnchor.constraint(greaterThanOrEqualTo: content.leadingAnchor, constant: 12),
            label.trailingAnchor.constraint(lessThanOrEqualTo: content.trailingAnchor, constant: -12),
        ])
    }

    private func buildLayout(in root: NSView) {
        fill(catalogueBox, title: "Mod Depot texture packages", note: "The Depot catalogue arrives in a later step.")
        buildInstalledBox()
        catalogueBox.setContentHuggingPriority(.defaultLow, for: .vertical)
        catalogueBox.setContentCompressionResistancePriority(.defaultLow, for: .vertical)

        // Report box: scrolling read-only text over a row of progress + buttons.
        reportBox.title = "Report"
        reportBox.titlePosition = .atTop
        reportBox.translatesAutoresizingMaskIntoConstraints = false

        let scroll = NSScrollView()
        scroll.translatesAutoresizingMaskIntoConstraints = false
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        scroll.autohidesScrollers = true
        reportView.isEditable = false
        reportView.isSelectable = true
        reportView.drawsBackground = true
        reportView.font = NSFont.monospacedSystemFont(ofSize: 12, weight: .regular)
        reportView.textContainerInset = NSSize(width: 4, height: 4)
        reportView.isVerticallyResizable = true
        reportView.isHorizontallyResizable = false
        reportView.autoresizingMask = [.width]
        reportView.minSize = NSSize(width: 0, height: 0)
        reportView.maxSize = NSSize(width: CGFloat.greatestFiniteMagnitude, height: CGFloat.greatestFiniteMagnitude)
        reportView.textContainer?.widthTracksTextView = true
        reportView.string = "Drop a texture mod (.zip or its folder) here, or choose one."
        scroll.documentView = reportView

        progress.style = .bar
        progress.isIndeterminate = true
        progress.isHidden = true
        progress.controlSize = .small
        progress.translatesAutoresizingMaskIntoConstraints = false
        progress.widthAnchor.constraint(equalToConstant: 140).isActive = true
        progressLabel.textColor = .secondaryLabelColor
        progressLabel.lineBreakMode = .byTruncatingTail
        progressLabel.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

        let spacer = NSView()
        spacer.setContentHuggingPriority(.init(1), for: .horizontal)
        let showLogButton = NSButton(title: "Show log", target: self, action: #selector(showLog))
        let chooseButton = NSButton(title: "Choose file…", target: self, action: #selector(chooseFile))

        let row = NSStackView(views: [progress, progressLabel, spacer, showLogButton, chooseButton])
        row.orientation = .horizontal
        row.spacing = 8
        row.alignment = .centerY
        row.translatesAutoresizingMaskIntoConstraints = false

        let rc = reportBox.contentView!
        rc.addSubview(scroll)
        rc.addSubview(row)
        NSLayoutConstraint.activate([
            scroll.topAnchor.constraint(equalTo: rc.topAnchor, constant: 6),
            scroll.leadingAnchor.constraint(equalTo: rc.leadingAnchor, constant: 6),
            scroll.trailingAnchor.constraint(equalTo: rc.trailingAnchor, constant: -6),
            row.topAnchor.constraint(equalTo: scroll.bottomAnchor, constant: 8),
            row.leadingAnchor.constraint(equalTo: rc.leadingAnchor, constant: 6),
            row.trailingAnchor.constraint(equalTo: rc.trailingAnchor, constant: -6),
            row.bottomAnchor.constraint(equalTo: rc.bottomAnchor, constant: -6),
        ])

        let stack = NSStackView(views: [catalogueBox, installedBox, reportBox])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.distribution = .fill
        stack.spacing = 10
        stack.translatesAutoresizingMaskIntoConstraints = false
        root.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: root.topAnchor, constant: 12),
            stack.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 12),
            stack.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -12),
            stack.bottomAnchor.constraint(equalTo: root.bottomAnchor, constant: -12),
            catalogueBox.widthAnchor.constraint(equalTo: stack.widthAnchor),
            installedBox.widthAnchor.constraint(equalTo: stack.widthAnchor),
            reportBox.widthAnchor.constraint(equalTo: stack.widthAnchor),
            installedBox.heightAnchor.constraint(equalToConstant: 200),
            reportBox.heightAnchor.constraint(equalToConstant: 170),
            catalogueBox.heightAnchor.constraint(greaterThanOrEqualToConstant: 120),
        ])
    }

    @objc func showLog() {
        let url = ReportLog.logURL()
        if !FileManager.default.fileExists(atPath: url.path) { ReportLog.append(source: "(none)", text: "Log created.") }
        NSWorkspace.shared.activateFileViewerSelecting([url])
    }

    @objc func chooseFile() {
        let panel = NSOpenPanel()
        panel.title = "Choose a mod to install"
        panel.message = "Pick a texture mod downloaded from the Oni Mod Depot (a .zip, or its unzipped folder)."
        panel.canChooseFiles = true; panel.canChooseDirectories = true; panel.allowsMultipleSelection = true
        panel.allowedContentTypes = [.zip, .folder]; panel.prompt = "Install"
        panel.beginSheetModal(for: window!) { [weak self] r in if r == .OK { self?.install(files: panel.urls) } }
    }

    /// Runs the pipeline for dropped or chosen files, one after another, and shows one combined report.
    func install(files: [URL]) {
        runningBatches += 1
        setBusy(true, label: "Installing \(files.count) item(s)…")
        queue.async { [self] in
            var sections: [String] = []
            var installed = 0, skipped = 0, failed = 0
            for url in files {
                var inst = makeInstaller()
                do {
                    var report: InstallReport
                    do { inst.replace = false; report = try inst.install(url) }
                    catch InstallError.alreadyInstalled(let path) {
                        guard askReplace(path) else { sections.append("\(url.lastPathComponent): skipped (already installed)."); skipped += 1; continue }
                        inst.replace = true; report = try inst.install(url)
                    }
                    sections.append(report.text); installed += 1
                    ReportLog.append(source: url.path, text: report.text)
                } catch {
                    let msg = (error as? InstallError)?.description ?? "\(error)"
                    sections.append("\(url.lastPathComponent): Nothing installed: \(msg)"); failed += 1
                    ReportLog.append(source: url.path, text: "Nothing installed: \(msg)")
                }
            }
            var text = sections.joined(separator: "\n\n") + "\n\n\(installed) installed, \(skipped) skipped, \(failed) failed."
            if installed > 0 { text += "\nThe pack loads next time Oni starts." }
            DispatchQueue.main.async { self.finishBatch(text) }
        }
    }

    /// Replace/Skip prompt, run on main and waited for from the work queue.
    private func askReplace(_ path: String) -> Bool {
        var answer = false
        DispatchQueue.main.sync {
            let a = NSAlert()
            a.messageText = "Replace the installed pack?"
            a.informativeText = "A pack with this name is already at:\n\(path)\n\nReplacing it re-packs from the new file."
            a.addButton(withTitle: "Replace"); a.addButton(withTitle: "Skip")
            answer = a.runModal() == .alertFirstButtonReturn
        }
        return answer
    }

    private func finishBatch(_ text: String) {
        runningBatches -= 1
        showReport(text, append: seriesHasReport)
        seriesHasReport = runningBatches > 0
        if runningBatches == 0 { setBusy(false, label: "") }
        didFinishInstall()
    }

    func showReport(_ text: String, append: Bool = false) {
        reportView.string = append ? reportView.string + "\n\n----\n\n" + text : text
        reportView.scrollToEndOfDocument(nil)
    }
    func setBusy(_ busy: Bool, label: String) {
        progress.isHidden = !busy; progressLabel.stringValue = label
        if busy { progress.isIndeterminate = true; progress.startAnimation(nil) } else { progress.stopAnimation(nil) }
    }
    /// Refreshes the installed list (and, in later tasks, the catalogue marks) after a batch.
    func didFinishInstall() { reloadInstalled() }
}

// MARK: - installed packs

extension MainWindowController: NSTableViewDataSource, NSTableViewDelegate {
    private static let columns: [(id: String, title: String, width: CGFloat, right: Bool)] = [
        ("name", "Name", 240, false), ("levels", "Levels", 60, true), ("size", "Size", 90, true), ("source", "Source", 300, false),
    ]

    func buildInstalledBox() {
        installedBox.title = "Installed packs"
        installedBox.titlePosition = .atTop
        installedBox.translatesAutoresizingMaskIntoConstraints = false

        for c in Self.columns {
            let col = NSTableColumn(identifier: NSUserInterfaceItemIdentifier(c.id))
            col.title = c.title
            col.width = c.width
            if c.right { col.headerCell.alignment = .right }
            col.resizingMask = c.id == "source" ? .autoresizingMask : .userResizingMask
            installedTable.addTableColumn(col)
        }
        installedTable.columnAutoresizingStyle = .lastColumnOnlyAutoresizingStyle
        installedTable.usesAlternatingRowBackgroundColors = true
        installedTable.allowsMultipleSelection = false
        installedTable.allowsEmptySelection = true
        installedTable.dataSource = self
        installedTable.delegate = self
        installedTable.target = self
        installedTable.doubleAction = #selector(tableDoubleClicked)

        let scroll = NSScrollView()
        scroll.translatesAutoresizingMaskIntoConstraints = false
        scroll.hasVerticalScroller = true
        scroll.autohidesScrollers = true
        scroll.borderType = .bezelBorder
        scroll.documentView = installedTable

        revealButton.target = self; revealButton.action = #selector(revealSelected)
        removeButton.target = self; removeButton.action = #selector(removeSelected)
        revealButton.isEnabled = false; removeButton.isEnabled = false
        let refreshButton = NSButton(title: "Refresh", target: self, action: #selector(refreshInstalled))
        let spacer = NSView()
        spacer.setContentHuggingPriority(.init(1), for: .horizontal)
        let row = NSStackView(views: [revealButton, removeButton, spacer, refreshButton])
        row.orientation = .horizontal
        row.spacing = 8
        row.alignment = .centerY
        row.translatesAutoresizingMaskIntoConstraints = false

        let c = installedBox.contentView!
        c.addSubview(scroll)
        c.addSubview(row)
        NSLayoutConstraint.activate([
            scroll.topAnchor.constraint(equalTo: c.topAnchor, constant: 6),
            scroll.leadingAnchor.constraint(equalTo: c.leadingAnchor, constant: 6),
            scroll.trailingAnchor.constraint(equalTo: c.trailingAnchor, constant: -6),
            row.topAnchor.constraint(equalTo: scroll.bottomAnchor, constant: 8),
            row.leadingAnchor.constraint(equalTo: c.leadingAnchor, constant: 6),
            row.trailingAnchor.constraint(equalTo: c.trailingAnchor, constant: -6),
            row.bottomAnchor.constraint(equalTo: c.bottomAnchor, constant: -6),
        ])
    }

    /// Scans TexturePacks on the scan queue (not behind installs), then updates the table on main, keeping the selection by name.
    func reloadInstalled() {
        let dir = ModInstaller.defaultTexturePacksDir()
        scanQueue.async { [weak self] in
            let packs = InstalledPacks.scan(dir: dir)
            DispatchQueue.main.async {
                guard let self = self else { return }
                let keep = self.selectedPack?.name
                self.installed = packs
                self.installedTable.reloadData()
                if let keep = keep, let i = packs.firstIndex(where: { $0.name == keep }) {
                    self.installedTable.selectRowIndexes(IndexSet(integer: i), byExtendingSelection: false)
                } else {
                    self.installedTable.deselectAll(nil)
                }
                self.updateInstalledButtons()
            }
        }
    }

    private var selectedPack: InstalledPack? {
        let r = installedTable.selectedRow
        return r >= 0 && r < installed.count ? installed[r] : nil
    }

    private func updateInstalledButtons() {
        let has = selectedPack != nil
        revealButton.isEnabled = has
        removeButton.isEnabled = has
    }

    func numberOfRows(in tableView: NSTableView) -> Int { installed.count }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard let col = tableColumn, row < installed.count else { return nil }
        let p = installed[row]
        let text: String
        switch col.identifier.rawValue {
        case "name": text = p.name
        case "levels": text = "\(p.levels)"
        case "size": text = sizeFormatter.string(fromByteCount: Int64(p.bytes))
        default: text = p.sourceText
        }
        let id = NSUserInterfaceItemIdentifier("cell." + col.identifier.rawValue)
        let label = (tableView.makeView(withIdentifier: id, owner: self) as? NSTextField) ?? {
            let l = NSTextField(labelWithString: "")
            l.identifier = id
            l.lineBreakMode = .byTruncatingTail
            l.alignment = (col.identifier.rawValue == "levels" || col.identifier.rawValue == "size") ? .right : .left
            return l
        }()
        label.stringValue = text
        label.toolTip = col.identifier.rawValue == "name" ? p.folder.path : nil
        return label
    }

    func tableViewSelectionDidChange(_ notification: Notification) { updateInstalledButtons() }

    @objc func tableDoubleClicked() {
        let r = installedTable.clickedRow
        guard r >= 0, r < installed.count else { return }
        NSWorkspace.shared.activateFileViewerSelecting([installed[r].folder])
    }

    @objc func revealSelected() {
        guard let p = selectedPack else { return }
        NSWorkspace.shared.activateFileViewerSelecting([p.folder])
    }

    @objc func refreshInstalled() { reloadInstalled() }

    @objc func removeSelected() {
        guard let p = selectedPack, let window = window else { return }
        let a = NSAlert()
        a.messageText = "Move \(p.name) to the Trash?"
        a.informativeText = "The pack stops loading next time Oni starts. You can put it back from the Trash."
        a.addButton(withTitle: "Move to Trash")
        a.addButton(withTitle: "Cancel")
        a.beginSheetModal(for: window) { [weak self] response in
            guard response == .alertFirstButtonReturn, let self = self else { return }
            do {
                try InstalledPacks.trash(p)
            } catch {
                let e = NSAlert()
                e.messageText = "Could not move \(p.name) to the Trash."
                e.informativeText = error.localizedDescription
                e.addButton(withTitle: "OK")
                // the confirm sheet has closed by now; show the error as a sheet too
                DispatchQueue.main.async { e.beginSheetModal(for: window, completionHandler: nil) }
                return
            }
            // During a batch the note joins the running series instead of wiping it.
            if self.runningBatches > 0 {
                self.showReport("Moved \(p.name) to the Trash.", append: self.seriesHasReport)
                self.seriesHasReport = true
            } else {
                self.showReport("Moved \(p.name) to the Trash.")
            }
            ReportLog.append(source: p.folder.path, text: "Moved to the Trash.")
            self.reloadInstalled()
        }
    }
}

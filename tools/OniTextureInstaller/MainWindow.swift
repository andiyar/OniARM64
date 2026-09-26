// MainWindow.swift — the Oni Texture Installer window (#124).
// Layout (top to bottom): Depot catalogue (tick packages, Install Selected), installed-packs table
// (InstalledPacks.swift scan, Reveal / Remove to the Trash), report box. The catalogue comes from the cached index (DepotCache.swift)
// at once, then a background refresh; Refresh re-fetches. Dropping a zip or folder anywhere on the window installs it.
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
    // Depot catalogue (main-thread only).
    let catalogueTable = NSTableView()
    let searchField = NSSearchField()
    let refreshButton = NSButton(title: "Refresh", target: nil, action: nil)
    let installButton = NSButton(title: "Install Selected", target: nil, action: nil)
    let statusLabel = NSTextField(labelWithString: "")
    let descriptionField = NSTextField(wrappingLabelWithString: "")
    var packages: [DepotPackage] = []
    var visible: [DepotPackage] = []       // packages after the search and Show installed only filters
    var ticked: Set<Int> = []              // nids
    let installedOnlyBox = NSButton(checkboxWithTitle: "Show installed only", target: nil, action: nil)
    let cache = DepotCache(dir: DepotCache.defaultDir())
    var indexDate: String?                 // ISO 8601, from the cache or the last refresh
    var refreshing = false
    // Index fetch and cache reads: serial (the cached load lands before the first refresh), and
    // apart from scanQueue so a slow or failing fetch never holds up the installed list.
    private let catalogueQueue = DispatchQueue(label: "installer.catalogue", qos: .userInitiated)

    init() {
        let w = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 900, height: 820),
                         styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
        w.title = "Oni Texture Installer"
        w.minSize = NSSize(width: 820, height: 680)
        super.init(window: w)
        let root = DropView(frame: w.contentView!.bounds)
        root.autoresizingMask = [.width, .height]
        root.onDrop = { [weak self] urls in self?.install(files: urls) }
        w.contentView = root
        buildLayout(in: root)
        w.center()
        reloadInstalled()
        loadCatalogue()
    }
    required init?(coder: NSCoder) { fatalError() }

    private func buildLayout(in root: NSView) {
        buildCatalogueBox()
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
        runBatch(files.map { .file($0) }, label: "Installing \(files.count) item(s)…")
    }

    /// One path for dropped files and ticked Depot packages: BatchRunner on the work queue,
    /// Replace prompts and progress bridged to main. `then` runs on main after the report shows.
    func runBatch(_ items: [BatchItem], label: String, then: ((BatchOutcome) -> Void)? = nil) {
        runningBatches += 1
        updateInstallButton()
        setBusy(true, label: label)
        let runner = BatchRunner(
            makeInstaller: { makeInstaller() },
            askReplace: { [weak self] path in self?.askReplace(path) ?? false },
            progress: { [weak self] _, fraction, text in
                DispatchQueue.main.async { self?.showProgress(fraction, text) }
            })
        queue.async { [self] in
            let outcome = runner.run(items)
            DispatchQueue.main.async { self.finishBatch(outcome.text); then?(outcome) }
        }
    }

    /// Determinate bar when a fraction is known (downloads), indeterminate otherwise.
    private func showProgress(_ fraction: Double?, _ text: String) {
        guard runningBatches > 0 else { return }
        progressLabel.stringValue = text
        if let f = fraction {
            progress.stopAnimation(nil)
            progress.isIndeterminate = false
            progress.minValue = 0; progress.maxValue = 1
            progress.doubleValue = f
        } else if !progress.isIndeterminate {
            progress.isIndeterminate = true
            progress.startAnimation(nil)
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
        updateInstallButton()
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
    /// Refreshes the installed list and, through it, the catalogue's Installed marks after a batch.
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
        let rescanButton = NSButton(title: "Rescan", target: self, action: #selector(refreshInstalled))
        let spacer = NSView()
        spacer.setContentHuggingPriority(.init(1), for: .horizontal)
        let row = NSStackView(views: [revealButton, removeButton, spacer, rescanButton])
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
                // Installed marks (and the Show installed only filter) follow the new list
                if self.installedOnlyBox.state == .on { self.applyFilter() } else { self.catalogueTable.reloadData() }
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

    func numberOfRows(in tableView: NSTableView) -> Int { tableView === catalogueTable ? visible.count : installed.count }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        if tableView === catalogueTable { return catalogueCell(tableColumn, row: row) }
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

    func tableViewSelectionDidChange(_ notification: Notification) {
        if (notification.object as? NSTableView) === catalogueTable { updateDescription() } else { updateInstalledButtons() }
    }

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

// MARK: - Depot catalogue

extension MainWindowController {
    private static let catalogueColumns: [(id: String, title: String, width: CGFloat, right: Bool)] = [
        ("tick", "", 28, false), ("name", "Name", 260, false), ("creator", "Creator", 140, false),
        ("version", "Version", 70, false), ("size", "Size", 90, true), ("installed", "Installed", 70, false),
    ]

    func buildCatalogueBox() {
        catalogueBox.title = "Mod Depot texture packages"
        catalogueBox.titlePosition = .atTop
        catalogueBox.translatesAutoresizingMaskIntoConstraints = false

        searchField.placeholderString = "Filter by name or creator"
        searchField.sendsSearchStringImmediately = true
        searchField.target = self
        searchField.action = #selector(filterChanged)
        searchField.widthAnchor.constraint(equalToConstant: 240).isActive = true
        refreshButton.target = self
        refreshButton.action = #selector(refreshCatalogue)
        installedOnlyBox.target = self
        installedOnlyBox.action = #selector(installedOnlyChanged)
        installButton.target = self
        installButton.action = #selector(installSelected)
        installButton.isEnabled = false
        statusLabel.textColor = .secondaryLabelColor
        statusLabel.lineBreakMode = .byTruncatingTail
        statusLabel.alignment = .right
        statusLabel.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        statusLabel.setContentHuggingPriority(.init(1), for: .horizontal)
        let top = NSStackView(views: [searchField, installedOnlyBox, refreshButton, installButton, statusLabel])
        top.orientation = .horizontal
        top.spacing = 8
        top.alignment = .centerY
        top.translatesAutoresizingMaskIntoConstraints = false

        for c in Self.catalogueColumns {
            let col = NSTableColumn(identifier: NSUserInterfaceItemIdentifier(c.id))
            col.title = c.title
            col.width = c.width
            if c.id == "tick" { col.minWidth = c.width; col.maxWidth = c.width }
            if c.right { col.headerCell.alignment = .right }
            col.resizingMask = c.id == "name" ? [.autoresizingMask, .userResizingMask] : (c.id == "tick" ? [] : .userResizingMask)
            catalogueTable.addTableColumn(col)
        }
        catalogueTable.columnAutoresizingStyle = .uniformColumnAutoresizingStyle
        catalogueTable.usesAlternatingRowBackgroundColors = true
        catalogueTable.allowsMultipleSelection = false
        catalogueTable.allowsEmptySelection = true
        catalogueTable.dataSource = self
        catalogueTable.delegate = self

        let scroll = NSScrollView()
        scroll.translatesAutoresizingMaskIntoConstraints = false
        scroll.hasVerticalScroller = true
        scroll.autohidesScrollers = true
        scroll.borderType = .bezelBorder
        scroll.documentView = catalogueTable

        descriptionField.maximumNumberOfLines = 4
        descriptionField.lineBreakMode = .byWordWrapping
        descriptionField.cell?.truncatesLastVisibleLine = true
        descriptionField.textColor = .secondaryLabelColor
        descriptionField.font = NSFont.systemFont(ofSize: NSFont.smallSystemFontSize)
        descriptionField.translatesAutoresizingMaskIntoConstraints = false
        descriptionField.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

        let c = catalogueBox.contentView!
        c.addSubview(top)
        c.addSubview(scroll)
        c.addSubview(descriptionField)
        NSLayoutConstraint.activate([
            top.topAnchor.constraint(equalTo: c.topAnchor, constant: 6),
            top.leadingAnchor.constraint(equalTo: c.leadingAnchor, constant: 6),
            top.trailingAnchor.constraint(equalTo: c.trailingAnchor, constant: -6),
            scroll.topAnchor.constraint(equalTo: top.bottomAnchor, constant: 8),
            scroll.leadingAnchor.constraint(equalTo: c.leadingAnchor, constant: 6),
            scroll.trailingAnchor.constraint(equalTo: c.trailingAnchor, constant: -6),
            scroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 80),
            descriptionField.topAnchor.constraint(equalTo: scroll.bottomAnchor, constant: 6),
            descriptionField.leadingAnchor.constraint(equalTo: c.leadingAnchor, constant: 8),
            descriptionField.trailingAnchor.constraint(equalTo: c.trailingAnchor, constant: -8),
            descriptionField.heightAnchor.constraint(equalToConstant: 58),   // four small lines
            descriptionField.bottomAnchor.constraint(equalTo: c.bottomAnchor, constant: -6),
        ])
    }

    /// Shows the cached index at once (if any), then fetches a fresh one in the background.
    func loadCatalogue() {
        statusLabel.stringValue = "Reading the Depot catalogue…"
        let cache = self.cache
        catalogueQueue.async { [weak self] in
            let cached = cache.load()
            DispatchQueue.main.async {
                guard let self = self else { return }
                if let c = cached { self.showCatalogue(c.packages, date: c.date) }
                self.refreshCatalogue()
            }
        }
    }

    /// The Refresh button's action: download the index, and on success replace the catalogue.
    /// A failed fetch leaves both the cache and the shown catalogue alone.
    @objc func refreshCatalogue() {
        guard !refreshing else { return }
        refreshing = true
        refreshButton.isEnabled = false
        statusLabel.stringValue = packages.isEmpty ? "Checking the Mod Depot…" : catalogueStatus() + ". Checking the Mod Depot…"
        statusLabel.toolTip = statusLabel.stringValue
        let cache = self.cache
        catalogueQueue.async { [weak self] in
            let result = Result { try cache.refresh() }
            DispatchQueue.main.async {
                guard let self = self else { return }
                self.refreshing = false
                self.refreshButton.isEnabled = true
                switch result {
                case .success(let r):
                    self.showCatalogue(r.packages, date: r.date)
                case .failure(let e):
                    let depot = e as? DepotError
                    var why = depot?.description ?? e.localizedDescription
                    while why.hasSuffix(".") { why.removeLast() }
                    if self.packages.isEmpty {
                        self.statusLabel.stringValue = (depot != nil ? "Couldn't read the Mod Depot index: " : "Couldn't reach the Mod Depot: ") + "\(why)."
                        self.refreshButton.title = "Retry"
                    } else {
                        self.statusLabel.stringValue = self.catalogueStatus() + (depot != nil ? ". Couldn't read the Mod Depot index just now: " : ". Couldn't reach the Mod Depot just now: ") + "\(why)."
                    }
                }
                self.statusLabel.toolTip = self.statusLabel.stringValue
            }
        }
    }

    /// Main only: replaces the catalogue, keeping ticks for packages still listed.
    private func showCatalogue(_ p: [DepotPackage], date: String) {
        packages = p
        indexDate = date
        ticked.formIntersection(Set(p.map { $0.nid }))
        refreshButton.title = "Refresh"
        statusLabel.stringValue = catalogueStatus()
        statusLabel.toolTip = statusLabel.stringValue
        applyFilter()
    }

    /// "N texture packages, index from <local short date and time>" (raw string if it is not ISO 8601).
    private func catalogueStatus() -> String {
        var when = indexDate ?? "unknown date"
        if let d = ISO8601DateFormatter().date(from: when) {
            let f = DateFormatter(); f.dateStyle = .short; f.timeStyle = .short
            when = f.string(from: d)
        }
        return "\(packages.count) texture package\(packages.count == 1 ? "" : "s"), index from \(when)"
    }

    @objc func installedOnlyChanged() { applyFilter() }

    private func applyFilter() {
        let q = searchField.stringValue.trimmingCharacters(in: .whitespaces)
        var v = q.isEmpty ? packages : packages.filter {
            $0.title.localizedCaseInsensitiveContains(q) || $0.creator.localizedCaseInsensitiveContains(q)
        }
        if installedOnlyBox.state == .on { v = v.filter { isInstalled($0) } }
        visible = v
        catalogueTable.reloadData()
        catalogueTable.deselectAll(nil)
        updateDescription()
        updateInstallButton()
    }

    @objc func filterChanged() { applyFilter() }

    /// Ticked rows the filter still shows: what Install Selected acts on.
    private var tickedVisible: [DepotPackage] { visible.filter { ticked.contains($0.nid) } }

    func updateInstallButton() {
        installButton.isEnabled = !tickedVisible.isEmpty && runningBatches == 0
    }

    func updateDescription() {
        let r = catalogueTable.selectedRow
        descriptionField.stringValue = r >= 0 && r < visible.count ? visible[r].description : ""
    }

    /// Installed when a pack records this Depot number, or (an older install) has the sanitised title as its name.
    func isInstalled(_ p: DepotPackage) -> Bool {
        let name = ModInstaller.sanitise(p.title)
        return installed.contains { $0.depotPackage == p.packageNumber || $0.name == name }
    }

    func catalogueCell(_ tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard let col = tableColumn, row < visible.count else { return nil }
        let p = visible[row]
        let key = col.identifier.rawValue
        if key == "tick" {
            let id = NSUserInterfaceItemIdentifier("cat.tick")
            let b = (catalogueTable.makeView(withIdentifier: id, owner: self) as? NSButton) ?? {
                let b = NSButton(checkboxWithTitle: "", target: self, action: #selector(toggleTick(_:)))
                b.identifier = id
                return b
            }()
            b.state = ticked.contains(p.nid) ? .on : .off
            return b
        }
        let text: String
        switch key {
        case "name": text = p.title
        case "creator": text = p.creator
        case "version": text = p.version
        case "size": text = p.fileSize > 0 ? sizeFormatter.string(fromByteCount: Int64(p.fileSize)) : ""
        default: text = isInstalled(p) ? "✓" : ""
        }
        let id = NSUserInterfaceItemIdentifier("cat." + key)
        let label = (catalogueTable.makeView(withIdentifier: id, owner: self) as? NSTextField) ?? {
            let l = NSTextField(labelWithString: "")
            l.identifier = id
            l.lineBreakMode = .byTruncatingTail
            l.alignment = key == "size" ? .right : (key == "installed" ? .center : .left)
            return l
        }()
        label.stringValue = text
        label.toolTip = key == "name" ? p.fileName : nil
        return label
    }

    @objc func toggleTick(_ sender: NSButton) {
        let r = catalogueTable.row(for: sender)
        guard r >= 0, r < visible.count else { return }
        let nid = visible[r].nid
        if sender.state == .on { ticked.insert(nid) } else { ticked.remove(nid) }
        updateInstallButton()
    }

    @objc func installSelected() {
        let chosen = tickedVisible
        guard !chosen.isEmpty else { return }
        let nids = Set(chosen.map { $0.nid })
        runBatch(chosen.map { .depot($0) }, label: "Installing \(chosen.count) package(s)…") { [weak self] outcome in
            guard let self = self else { return }
            // installed and skipped rows lose their tick; failed ones keep it so a retry is one click
            self.ticked.subtract(nids.subtracting(outcome.failedNids))
            self.catalogueTable.reloadData()
            self.updateInstallButton()
        }
    }
}

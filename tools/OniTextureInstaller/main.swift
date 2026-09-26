// main.swift — Oni Texture Installer entry (#20).
//   CLI:  OniTextureInstaller --install <zip-or-folder>... [--dest <TexturePacks>]
//                         [--gamedata <GameDataFolder>|none] [--replace] [--source-depot <N>]
//         (several inputs install in turn, one combined report; Batch.swift)
//         OniTextureInstaller --file-id <level> <suffix>   (test hook: prints the
//         engine file id the installer computes, for the Swift/C parity check)
//         OniTextureInstaller --parse-index <jsoncache.zip>   (prints the Depot
//         texture-package catalogue as TSV, Depot.swift)
//         OniTextureInstaller --list-installed <TexturePacks>   (prints the installed
//         packs as TSV: name, levels, bytes, source; InstalledPacks.swift)
//         OniTextureInstaller --cache-info <cache dir>   (prints the cached Depot
//         index as "date\tpackage count", or "none"; DepotCache.swift)
//   GUI:  no --install → the Oni Texture Installer window (MainWindow.swift).
//         Drop a mod on the window, use Choose file, or Finder Open With.
// Helper tools: bundled beside the executable, or ONIMOD_ONIPACK /
// ONIMOD_INDEX env overrides (used by tests/test_oni_texture_installer.sh).
import AppKit
import Foundation
import UniformTypeIdentifiers

func helperURL(_ name: String, env: String) -> URL {
    if let p = ProcessInfo.processInfo.environment[env], !p.isEmpty { return URL(fileURLWithPath: p) }
    return Bundle.main.executableURL!.deletingLastPathComponent().appendingPathComponent(name)
}

func makeInstaller() -> ModInstaller {
    ModInstaller(onipack: helperURL("onipack", env: "ONIMOD_ONIPACK"),
                 indexTool: helperURL("txmp-format-index", env: "ONIMOD_INDEX"),
                 texturePacksDir: ModInstaller.defaultTexturePacksDir(),
                 gameDataDir: ModInstaller.defaultGameDataDir())
}

func stderrLine(_ s: String) {
    FileHandle.standardError.write((s + "\n").data(using: .utf8)!)
}

func runCLI(_ args: [String]) -> Never {
    var inst = makeInstaller()
    var inputs: [URL] = []
    var sourceDepot: Int?
    var i = 0
    while i < args.count {
        switch args[i] {
        case "--install":
            while i + 1 < args.count, !args[i + 1].hasPrefix("--") { i += 1; inputs.append(URL(fileURLWithPath: args[i])) }
        case "--source-depot":
            i += 1
            guard i < args.count, let n = Int(args[i]) else { stderrLine("usage: --source-depot <Depot package number>"); exit(2) }
            sourceDepot = n
        case "--dest":    i += 1; if i < args.count { inst.texturePacksDir = URL(fileURLWithPath: args[i]) }
        case "--gamedata": i += 1; if i < args.count { inst.gameDataDir = args[i] == "none" ? nil : URL(fileURLWithPath: args[i]) }
        case "--replace": inst.replace = true
        case "--file-id":
            // Debug/test hook: print the engine file id the installer computes
            // for <level> <suffix>, so tests can compare it with onipack's C.
            guard i + 2 < args.count, let level = Int(args[i + 1]), (0..<128).contains(level) else {
                stderrLine("usage: OniTextureInstaller --file-id <level> <suffix>"); exit(2)
            }
            print(String(format: "0x%08x", ModInstaller.fileID(level: level, suffix: args[i + 2])))
            exit(0)
        case "--parse-index":
            guard i + 1 < args.count else { stderrLine("usage: OniTextureInstaller --parse-index <jsoncache.zip>"); exit(2) }
            do {
                for p in try DepotIndex.parse(zipURL: URL(fileURLWithPath: args[i + 1])) { print(DepotIndex.tsvLine(p)) }
                exit(0)
            } catch let e as DepotError { stderrLine("Oni Texture Installer: \(e.description)"); exit(3) }
            catch { stderrLine("Oni Texture Installer: \(error)"); exit(3) }
        case "--list-installed":
            guard i + 1 < args.count else { stderrLine("usage: OniTextureInstaller --list-installed <TexturePacks>"); exit(2) }
            for p in InstalledPacks.scan(dir: URL(fileURLWithPath: args[i + 1])) { print(InstalledPacks.tsvLine(p)) }
            exit(0)
        case "--cache-info":
            guard i + 1 < args.count else { stderrLine("usage: OniTextureInstaller --cache-info <cache dir>"); exit(2) }
            let c = DepotCache(dir: URL(fileURLWithPath: args[i + 1]))
            if let r = c.load() { print("\(r.date)\t\(r.packages.count)") } else { print("none") }
            exit(0)
        case "--help": inputs = []; i = args.count
        default:
            stderrLine("unknown argument \(args[i])")
            exit(2)
        }
        i += 1
    }
    guard let first = inputs.first else {
        stderrLine("usage: OniTextureInstaller --install <zip-or-folder>... [--dest dir] [--gamedata dir|none] [--replace] [--source-depot N]\n       OniTextureInstaller --parse-index <jsoncache.zip>\n       OniTextureInstaller --list-installed <TexturePacks>\n       OniTextureInstaller --cache-info <cache dir>\nexit: 0 installed (at least one), 1 no textures, 2 other failure, 4 already installed (use --replace),\n      5 file-id collision, 6 download failed")
        exit(2)
    }
    let base = inst
    let override = sourceDepot.map { InstallSource.depot(packageNumber: $0, title: first.deletingPathExtension().lastPathComponent) }
    let runner = BatchRunner(makeInstaller: { base }, sourceOverride: override, askReplace: { _ in base.replace })
    let outcome = runner.run(inputs.map { .file($0) })
    print(outcome.text)
    // One install that worked is a success; otherwise "already installed" (4) if that is all
    // that happened, else the first failure's code (matches the old single-input exits).
    if outcome.installed > 0 { exit(0) }
    if outcome.skipped > 0 && outcome.failed == 0 { exit(4) }
    exit(outcome.firstFailureCode ?? 0)
}

let argv = Array(CommandLine.arguments.dropFirst())
if argv.contains("--install") || argv.contains("--help") || argv.contains("--file-id") || argv.contains("--parse-index") || argv.contains("--list-installed") || argv.contains("--cache-info") {
    runCLI(argv)
}

// MARK: - window app

final class AppDelegate: NSObject, NSApplicationDelegate {
    private var controller: MainWindowController?

    private func ensureController() -> MainWindowController {
        if let c = controller { return c }
        let c = MainWindowController()
        controller = c
        return c
    }

    func applicationDidFinishLaunching(_ note: Notification) {
        NSApp.setActivationPolicy(.regular)
        NSApp.mainMenu = buildMainMenu(controller: ensureController())
        NSApp.activate(ignoringOtherApps: true)
        ensureController().showWindow(nil)
    }

    /// The standard menu bar, built in code (no nib): app, File, Edit, Window.
    private func buildMainMenu(controller c: MainWindowController) -> NSMenu {
        let name = "Oni Texture Installer"
        let main = NSMenu()
        func submenu(_ title: String) -> NSMenu {
            let item = NSMenuItem(title: title, action: nil, keyEquivalent: "")
            let m = NSMenu(title: title)
            item.submenu = m
            main.addItem(item)
            return m
        }

        let appMenu = submenu(name)
        appMenu.addItem(withTitle: "About \(name)", action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)), keyEquivalent: "")
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "Hide \(name)", action: #selector(NSApplication.hide(_:)), keyEquivalent: "h")
        appMenu.addItem(withTitle: "Quit \(name)", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")

        let file = submenu("File")
        let choose = file.addItem(withTitle: "Choose file…", action: #selector(MainWindowController.chooseFile), keyEquivalent: "o")
        choose.target = c
        file.addItem(withTitle: "Close", action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")

        let edit = submenu("Edit")
        edit.addItem(withTitle: "Cut", action: #selector(NSText.cut(_:)), keyEquivalent: "x")
        edit.addItem(withTitle: "Copy", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
        edit.addItem(withTitle: "Paste", action: #selector(NSText.paste(_:)), keyEquivalent: "v")
        edit.addItem(withTitle: "Select All", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a")

        let window = submenu("Window")
        window.addItem(withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m")
        NSApp.windowsMenu = window
        return main
    }


    // Finder can deliver files before didFinishLaunching, so make the window on demand.
    func application(_ app: NSApplication, open urls: [URL]) {
        let c = ensureController()
        c.showWindow(nil)
        c.install(files: urls)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.run()

// main.swift — OniMod Installer entry (#20).
//   CLI:  OniModInstaller --install <zip-or-folder> [--dest <TexturePacks>]
//                         [--gamedata <GameDataFolder>|none] [--replace]
//   GUI:  no --install → AppKit droplet: Finder drop / Open With, or an open
//         panel when double-clicked with nothing dropped.
// Helper tools: bundled beside the executable, or ONIMOD_ONIPACK /
// ONIMOD_INDEX env overrides (used by tests/test_onimod_installer.sh).
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
    var input: URL?
    var i = 0
    while i < args.count {
        switch args[i] {
        case "--install": i += 1; if i < args.count { input = URL(fileURLWithPath: args[i]) }
        case "--dest":    i += 1; if i < args.count { inst.texturePacksDir = URL(fileURLWithPath: args[i]) }
        case "--gamedata": i += 1; if i < args.count { inst.gameDataDir = args[i] == "none" ? nil : URL(fileURLWithPath: args[i]) }
        case "--replace": inst.replace = true
        case "--help": input = nil; i = args.count
        default:
            stderrLine("unknown argument \(args[i])")
            exit(2)
        }
        i += 1
    }
    guard let input = input else {
        stderrLine("usage: OniModInstaller --install <zip-or-folder> [--dest dir] [--gamedata dir|none] [--replace]")
        exit(2)
    }
    do {
        let r = try inst.install(input)
        print(r.text)
        exit(0)
    } catch let e as InstallError {
        stderrLine("OniMod Installer: \(e.description)")
        exit(e.exitCode)
    } catch {
        stderrLine("OniMod Installer: \(error)")
        exit(2)
    }
}

let argv = Array(CommandLine.arguments.dropFirst())
if argv.contains("--install") || argv.contains("--help") {
    runCLI(argv)
}

// MARK: - droplet

final class DropletDelegate: NSObject, NSApplicationDelegate {
    private var openedViaFinder = false

    func application(_ app: NSApplication, open urls: [URL]) {
        openedViaFinder = true
        installAll(urls)
    }

    func applicationDidFinishLaunching(_ note: Notification) {
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
        // Finder delivers dropped files before this fires; if nothing came, ask.
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [self] in
            if !openedViaFinder { pickAndInstall() }
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    private func pickAndInstall() {
        let panel = NSOpenPanel()
        panel.title = "Choose a mod to install"
        panel.message = "Pick a texture mod downloaded from the Oni Mod Depot (a .zip, or its unzipped folder)."
        panel.canChooseFiles = true
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = true
        panel.allowedContentTypes = [.zip, .folder]
        panel.prompt = "Install"
        if panel.runModal() == .OK, !panel.urls.isEmpty { installAll(panel.urls) } else { NSApp.terminate(nil) }
    }

    private func installAll(_ urls: [URL]) {
        var inst = makeInstaller()
        var summaries: [String] = []
        var lastFolder: String?
        for url in urls {
            do {
                var report: InstallReport
                do {
                    inst.replace = false
                    report = try inst.install(url)
                } catch InstallError.alreadyInstalled(let path) {
                    guard confirmReplace(path) else { summaries.append("Skipped \(url.lastPathComponent) (already installed)."); continue }
                    inst.replace = true
                    report = try inst.install(url)
                }
                summaries.append(report.text)
                lastFolder = report.packFolder
            } catch {
                summaries.append("\(url.lastPathComponent): \(error)")
            }
        }
        let ok = lastFolder != nil
        let alert = NSAlert()
        alert.messageText = ok ? "Mod installed" : "Nothing installed"
        alert.informativeText = summaries.joined(separator: "\n\n") + (ok ? "\n\nThe pack loads next time Oni starts." : "")
        alert.alertStyle = ok ? .informational : .warning
        if ok { alert.addButton(withTitle: "Show in Finder") }
        alert.addButton(withTitle: "Done")
        let choice = alert.runModal()
        if ok, choice == .alertFirstButtonReturn, let f = lastFolder {
            NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: f)])
        }
        NSApp.terminate(nil)
    }

    private func confirmReplace(_ path: String) -> Bool {
        let a = NSAlert()
        a.messageText = "Replace the installed pack?"
        a.informativeText = "A pack with this name is already at:\n\(path)\n\nReplacing it re-packs from the file you dropped."
        a.addButton(withTitle: "Replace")
        a.addButton(withTitle: "Skip")
        return a.runModal() == .alertFirstButtonReturn
    }
}

let app = NSApplication.shared
let delegate = DropletDelegate()
app.delegate = delegate
app.run()

// Downloader.swift — fetch one Depot zip with progress (#124). Synchronous on the caller's
// (work) thread; URLSession does the I/O on its own queue. Never call it on the main thread.
import Foundation

enum DownloadError: Error, LocalizedError {
    case http(Int), transport(String)
    var errorDescription: String? { switch self { case .http(let c): return "the Depot answered HTTP \(c)"; case .transport(let m): return m } }
}

final class Downloader: NSObject, URLSessionDownloadDelegate {
    private var onProgress: (Double) -> Void = { _ in }
    private var result: Result<URL, Error>?
    private let done = DispatchSemaphore(value: 0)
    private var destination: URL!

    static func download(_ url: URL, to dest: URL, progress: @escaping (Double) -> Void) throws -> URL {
        precondition(!Thread.isMainThread, "Downloader.download blocks; call it from a work queue")
        let d = Downloader(); d.onProgress = progress; d.destination = dest
        let cfg = URLSessionConfiguration.ephemeral; cfg.timeoutIntervalForRequest = 60; cfg.timeoutIntervalForResource = 3600
        let session = URLSession(configuration: cfg, delegate: d, delegateQueue: nil)
        session.downloadTask(with: url).resume()
        d.done.wait(); session.finishTasksAndInvalidate()
        return try d.result!.get()
    }
    func urlSession(_ s: URLSession, downloadTask t: URLSessionDownloadTask, didWriteData b: Int64, totalBytesWritten w: Int64, totalBytesExpectedToWrite e: Int64) {
        if e > 0 { onProgress(Double(w) / Double(e)) }
    }
    func urlSession(_ s: URLSession, downloadTask t: URLSessionDownloadTask, didFinishDownloadingTo location: URL) {
        // the temp file is deleted when this returns, so move it now
        if let code = (t.response as? HTTPURLResponse)?.statusCode, code >= 400 { result = .failure(DownloadError.http(code)); return }
        do { try? FileManager.default.removeItem(at: destination); try FileManager.default.moveItem(at: location, to: destination); result = .success(destination) }
        catch { result = .failure(error) }
    }
    func urlSession(_ s: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        if let e = error, result == nil { result = .failure(DownloadError.transport(e.localizedDescription)) }
        if result == nil { result = .failure(DownloadError.transport("no data")) }
        done.signal()
    }
}

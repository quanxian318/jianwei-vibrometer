import UIKit
import WebKit

/// The web app in a WKWebView, with the native bridge window.VibmonNative the
/// pages expect (same names as the Android and Windows shells).
final class WebViewController: UIViewController, WKScriptMessageHandler, WKNavigationDelegate {
    private var web: WKWebView!
    private var dark = false

    override var preferredStatusBarStyle: UIStatusBarStyle { dark ? .lightContent : .darkContent }

    override func loadView() {
        let cfg = WKWebViewConfiguration()
        let ucc = WKUserContentController()
        let version = Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "1.0"
        let device = "\(UIDevice.current.model) · \(UIDevice.current.systemName) \(UIDevice.current.systemVersion)"
        let js = """
        window.VibmonNative = {
          discover: function (ms, cb) { webkit.messageHandlers.jw.postMessage({ op: 'discover', ms: ms, cb: cb }); },
          saveFile: function (name, mime, text) { webkit.messageHandlers.jw.postMessage({ op: 'save', name: name, mime: mime, text: text }); return true; },
          shareText: function (title, text) { webkit.messageHandlers.jw.postMessage({ op: 'share', title: title, text: text }); },
          setTheme: function (dark) { webkit.messageHandlers.jw.postMessage({ op: 'theme', dark: !!dark }); },
          keepScreenOn: function (on) { webkit.messageHandlers.jw.postMessage({ op: 'awake', on: !!on }); },
          vibrate: function (ms) { webkit.messageHandlers.jw.postMessage({ op: 'vibrate', ms: ms || 12 }); },
          appVersion: function () { return \(jsString(version)); },
          deviceName: function () { return \(jsString(device)); }
        };
        """
        ucc.addUserScript(WKUserScript(source: js, injectionTime: .atDocumentStart, forMainFrameOnly: true))
        ucc.add(self, name: "jw")
        cfg.userContentController = ucc
        // The page talks to the instrument over plain HTTP on the LAN.
        cfg.preferences.setValue(true, forKey: "allowFileAccessFromFileURLs")
        cfg.setValue(true, forKey: "allowUniversalAccessFromFileURLs")
        web = WKWebView(frame: .zero, configuration: cfg)
        web.navigationDelegate = self
        web.scrollView.contentInsetAdjustmentBehavior = .never
        web.scrollView.bounces = false
        web.isOpaque = false
        web.backgroundColor = UIColor(red: 0.953, green: 0.957, blue: 0.969, alpha: 1)
        view = web
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        guard let dir = Bundle.main.url(forResource: "web", withExtension: nil) else { return }
        var page = dir.appendingPathComponent("index.html")
        // `-shot <tab>` (simulator screenshots on the build machine) opens the demo there.
        let args = ProcessInfo.processInfo.arguments
        if let i = args.firstIndex(of: "-shot"), i + 1 < args.count,
           let u = URL(string: page.absoluteString + "#shot=" + args[i + 1]) { page = u }
        web.loadFileURL(page, allowingReadAccessTo: dir)
    }

    func userContentController(_ ucc: WKUserContentController, didReceive message: WKScriptMessage) {
        guard let m = message.body as? [String: Any], let op = m["op"] as? String else { return }
        switch op {
        case "discover":
            let ms = m["ms"] as? Int ?? 2500, cb = m["cb"] as? String ?? ""
            DispatchQueue.global(qos: .userInitiated).async {
                let found = Discovery.run(timeoutMs: ms)
                let json = String(data: (try? JSONSerialization.data(withJSONObject: found)) ?? Data("[]".utf8), encoding: .utf8) ?? "[]"
                DispatchQueue.main.async {
                    self.web.evaluateJavaScript("window.__jw && window.__jw[\(self.jsString(cb))] && window.__jw[\(self.jsString(cb))](\(self.jsString(json)))")
                }
            }
        case "save":
            let name = (m["name"] as? String ?? "export.txt").replacingOccurrences(of: "/", with: "_")
            let url = FileManager.default.temporaryDirectory.appendingPathComponent(name)
            do {
                try (m["text"] as? String ?? "").write(to: url, atomically: true, encoding: .utf8)
                present(items: [url])
            } catch {
                web.evaluateJavaScript("window.U && U.toast('保存失败', 'err')")
            }
        case "share":
            present(items: [m["text"] as? String ?? ""])
        case "theme":
            dark = m["dark"] as? Bool ?? false
            overrideUserInterfaceStyle = dark ? .dark : .light
            web.backgroundColor = dark ? UIColor(red: 0.04, green: 0.05, blue: 0.07, alpha: 1) : UIColor(red: 0.953, green: 0.957, blue: 0.969, alpha: 1)
            setNeedsStatusBarAppearanceUpdate()
        case "awake":
            UIApplication.shared.isIdleTimerDisabled = m["on"] as? Bool ?? false
        case "vibrate":
            UIImpactFeedbackGenerator(style: .light).impactOccurred()
        default:
            break
        }
    }

    /// Share sheet (on iPad anchored to the bottom centre).
    private func present(items: [Any]) {
        let vc = UIActivityViewController(activityItems: items, applicationActivities: nil)
        if let pop = vc.popoverPresentationController {
            pop.sourceView = view
            pop.sourceRect = CGRect(x: view.bounds.midX, y: view.bounds.maxY - 40, width: 1, height: 1)
        }
        present(vc, animated: true)
    }

    /// Links leave the app for Safari.
    func webView(_ webView: WKWebView, decidePolicyFor action: WKNavigationAction, decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
        if let u = action.request.url, u.scheme == "http" || u.scheme == "https", action.navigationType == .linkActivated {
            UIApplication.shared.open(u)
            decisionHandler(.cancel)
        } else {
            decisionHandler(.allow)
        }
    }

    private func jsString(_ s: String) -> String {
        let data = (try? JSONSerialization.data(withJSONObject: [s])) ?? Data("[\"\"]".utf8)
        let arr = String(data: data, encoding: .utf8) ?? "[\"\"]"
        return String(arr.dropFirst().dropLast())
    }
}

#include "common.hpp"
#include "observer.hpp"
#include "classifier.hpp"
#include "ole/superposition_dataobj.hpp"
#include "ui/hud_tray.hpp"
#include "ui/http_server.hpp"

namespace {

HWND g_staHwnd = nullptr;

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT || ctrlType == CTRL_CLOSE_EVENT) {
        if (g_staHwnd) {
            PostMessageW(g_staHwnd, sclip::WM_SCLIP_SHUTDOWN, 0, 0);
            return TRUE;
        }
    }
    return FALSE;
}

class SchrodingerDaemon {
public:
    SchrodingerDaemon(uint16_t port, bool headless, bool showHud, bool autoOpenBrowser)
        : port_(port),
          headless_(headless),
          showHud_(showHud),
          autoOpenBrowser_(autoOpenBrowser) {}

    int Run(HINSTANCE hInst) {
        // Initialize RegisteredFormats early
        (void)sclip::RegisteredFormats::Instance();

        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = StaticWndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"SchrodingerClipStaDaemonClass";
        RegisterClassExW(&wc);

        hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"SchrodingerClip STA Guardian",
                                WS_OVERLAPPEDWINDOW, 0, 0, 0, 0,
                                nullptr, nullptr, hInst, this);
        if (!hwnd_) {
            std::fprintf(stderr, "[FATAL] Failed to create STA message window.\n");
            return 1;
        }
        g_staHwnd = hwnd_;

        AddClipboardFormatListener(hwnd_);

        if (!headless_) {
            RegisterHotKey(hwnd_, sclip::HOTKEY_TOGGLE_BYPASS, MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'V');
            RegisterHotKey(hwnd_, sclip::HOTKEY_OPEN_CONSOLE, MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'O');
            hud_.Create(hInst, showHud_);
            tray_.Add(hwnd_, L"SchrodingerClip // Quantum Superposition Clipboard");
            SetTimer(hwnd_, 1, 250, nullptr);
        }

        httpServer_ = std::make_unique<sclip::ObservatoryHttpServer>(port_, hwnd_, &observerMgr_, &telemetry_);
        if (!httpServer_->Start()) {
            std::fprintf(stderr, "[FATAL] Failed to bind local Observatory HTTP port %u.\n", port_);
            CleanupBeforeQuit();
            return 2;
        }

        std::printf("================================================================\n");
        std::printf("  SchrodingerClip Quantum Guardian Active (STA COM Ready)\n");
        std::printf("  Observatory Console : http://127.0.0.1:%u\n", port_);
        std::printf("  Mode                : %s\n", headless_ ? "Headless / Automated" : "Desktop Interactive (HUD + Tray)");
        std::printf("  Bypass Hotkey       : Alt + Shift + V\n");
        std::printf("================================================================\n");
        std::fflush(stdout);

        if (!headless_ && autoOpenBrowser_) {
            std::wstring url = L"http://127.0.0.1:" + std::to_wstring(port_);
            ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }

        // Core STA Message Pump (Required for OLE Clipboard & Cross-Process IStream Marshaling!)
        MSG msg = {};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        CleanupBeforeQuit();
        return 0;
    }

private:
    uint16_t port_ = 17890;
    bool headless_ = false;
    bool showHud_ = true;
    bool autoOpenBrowser_ = false;

    HWND hwnd_ = nullptr;
    bool cleanedUp_ = false;
    bool isSelfArming_ = false;
    DWORD lastOwnSeq_ = 0;
    uint64_t envelopeCounter_ = 0;

    sclip::ObserverContextManager observerMgr_;
    sclip::TelemetryHub telemetry_;
    sclip::SuperpositionDataObject* currentDataObj_ = nullptr;
    sclip::DesktopMicroHud hud_;
    sclip::SystemTrayController tray_;
    std::unique_ptr<sclip::ObservatoryHttpServer> httpServer_;

    void OpenObservatoryInBrowser() {
        std::wstring url = L"http://127.0.0.1:" + std::to_wstring(port_);
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    bool MountSuperpositionSeed(sclip::QuantumSeed&& seed) {
        isSelfArming_ = true;
        auto* nextObj = new sclip::SuperpositionDataObject(seed, &observerMgr_, &telemetry_);

        HRESULT hr = OleSetClipboard(nextObj);
        if (FAILED(hr)) {
            nextObj->Release();
            isSelfArming_ = false;
            return false;
        }

        if (currentDataObj_) {
            currentDataObj_->Release();
        }
        currentDataObj_ = nextObj;
        lastOwnSeq_ = GetClipboardSequenceNumber();
        telemetry_.SetActiveSeed(seed);
        isSelfArming_ = false;
        RefreshHudOverlay();
        return true;
    }

    void DisarmClipboard() {
        isSelfArming_ = true;
        if (currentDataObj_) {
            if (OleIsCurrentClipboard(currentDataObj_) == S_OK) {
                OleSetClipboard(nullptr);
            }
            currentDataObj_->Release();
            currentDataObj_ = nullptr;
        }
        lastOwnSeq_ = GetClipboardSequenceNumber();
        telemetry_.ClearSeed();
        isSelfArming_ = false;
        RefreshHudOverlay();
    }

    void OnClipboardUpdated() {
        if (isSelfArming_) return;
        DWORD seq = GetClipboardSequenceNumber();
        if (seq == lastOwnSeq_) return;

        // Guard 1: Check OLE current clipboard identity
        if (currentDataObj_ && OleIsCurrentClipboard(currentDataObj_) == S_OK) {
            lastOwnSeq_ = seq;
            return;
        }
        // Guard 2: Check private SuperpositionEnvelope marker format
        if (IsClipboardFormatAvailable(sclip::RegisteredFormats::Instance().cfEnvelope)) {
            lastOwnSeq_ = seq;
            return;
        }

        // Open clipboard briefly (retry up to 5 times if source app hasn't released lock yet)
        bool opened = false;
        for (int attempt = 0; attempt < 5; ++attempt) {
            if (OpenClipboard(hwnd_)) {
                opened = true;
                break;
            }
            Sleep(12);
        }
        if (!opened) return;

        bool capturedFiles = false;
        std::vector<std::wstring> droppedPaths;
        std::string rawUtf8Text;
        bool capturedText = false;

        if (IsClipboardFormatAvailable(CF_HDROP)) {
            HANDLE hDropData = GetClipboardData(CF_HDROP);
            if (hDropData) {
                HDROP hDrop = static_cast<HDROP>(GlobalLock(hDropData));
                if (hDrop) {
                    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
                    for (UINT i = 0; i < count; ++i) {
                        UINT chars = DragQueryFileW(hDrop, i, nullptr, 0);
                        if (chars > 0) {
                            std::wstring p(chars + 1, L'\0');
                            DragQueryFileW(hDrop, i, &p[0], chars + 1);
                            p.resize(chars);
                            droppedPaths.push_back(std::move(p));
                        }
                    }
                    GlobalUnlock(hDropData);
                    if (!droppedPaths.empty()) capturedFiles = true;
                }
            }
        } else if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            HANDLE hTxt = GetClipboardData(CF_UNICODETEXT);
            if (hTxt) {
                const wchar_t* wptr = static_cast<const wchar_t*>(GlobalLock(hTxt));
                if (wptr) {
                    rawUtf8Text = sclip::WideToUtf8(std::wstring(wptr));
                    capturedText = !rawUtf8Text.empty();
                    GlobalUnlock(hTxt);
                }
            }
        }

        CloseClipboard();

        if (capturedFiles) {
            sclip::QuantumSeed seed = sclip::ClassifyFileDropSeed(droppedPaths, ++envelopeCounter_);
            MountSuperpositionSeed(std::move(seed));
        } else if (capturedText) {
            // Only mount superposition when seed matches one of the 5 polymorphic sources
            // (or mount universal superposition so any text can also inspect telemetry!)
            sclip::QuantumSeed seed = sclip::ClassifyTextSeed(rawUtf8Text, ++envelopeCounter_);
            if (seed.kind != sclip::SuperpositionKind::PlainText) {
                MountSuperpositionSeed(std::move(seed));
            } else {
                // Keep plain text untouched on OS clipboard while updating telemetry display
                if (currentDataObj_) {
                    currentDataObj_->Release();
                    currentDataObj_ = nullptr;
                }
                telemetry_.SetActiveSeed(seed);
                RefreshHudOverlay();
            }
        }
    }

    void HandleSyncArm(sclip::SyncArmRequest* req) {
        if (!req) return;
        sclip::QuantumSeed seed = req->isFileDrop
                                      ? sclip::ClassifyFileDropSeed(req->filePaths, ++envelopeCounter_)
                                      : sclip::ClassifyTextSeed(req->utf8Text, ++envelopeCounter_);
        req->ok = MountSuperpositionSeed(std::move(seed));
        req->outEnvelopeId = envelopeCounter_;
        req->outClipboardSeq = lastOwnSeq_;
    }

    void HandleSyncPreview(sclip::SyncPreviewRequest* preq) {
        if (!preq) return;
        observerMgr_.SetSimulatedObserver(true, preq->processName, preq->windowTitle);

        IDataObject* pDataObj = nullptr;
        HRESULT hr = OleGetClipboard(&pDataObj);
        if (FAILED(hr) || !pDataObj) {
            observerMgr_.SetSimulatedObserver(false);
            preq->ok = false;
            preq->errorMsg = "OleGetClipboard failed";
            return;
        }

        const auto& rf = sclip::RegisteredFormats::Instance();
        FORMATETC fmt = {};
        fmt.dwAspect = DVASPECT_CONTENT;
        fmt.lindex = -1;
        fmt.tymed = TYMED_HGLOBAL;

        if (preq->requestedKind == "png") {
            fmt.cfFormat = static_cast<CLIPFORMAT>(rf.cfPng);
        } else if (preq->requestedKind == "html") {
            fmt.cfFormat = static_cast<CLIPFORMAT>(rf.cfHtml);
        } else {
            fmt.cfFormat = CF_UNICODETEXT;
        }

        STGMEDIUM stg = {};
        hr = pDataObj->GetData(&fmt, &stg);
        if (SUCCEEDED(hr) && stg.tymed == TYMED_HGLOBAL && stg.hGlobal) {
            size_t sz = GlobalSize(stg.hGlobal);
            const uint8_t* ptr = static_cast<const uint8_t*>(GlobalLock(stg.hGlobal));
            if (ptr && sz > 0) {
                preq->ok = true;
                preq->bytesOut = sz;
                if (preq->requestedKind == "png") {
                    preq->resultKind = "png";
                    preq->base64Out = sclip::Base64Encode(ptr, sz);
                } else if (preq->requestedKind == "html") {
                    preq->resultKind = "html";
                    std::string fullHtml(reinterpret_cast<const char*>(ptr));
                    size_t sf = fullHtml.find("<!--StartFragment-->");
                    size_t ef = fullHtml.find("<!--EndFragment-->");
                    if (sf != std::string::npos && ef != std::string::npos && ef > sf + 20) {
                        preq->htmlFragOut = fullHtml.substr(sf + 20, ef - (sf + 20));
                    } else {
                        preq->htmlFragOut = fullHtml;
                    }
                } else {
                    preq->resultKind = "text";
                    const wchar_t* wptr = reinterpret_cast<const wchar_t*>(ptr);
                    preq->textOut = sclip::WideToUtf8(std::wstring(wptr));
                }
                GlobalUnlock(stg.hGlobal);
            }
            ReleaseStgMedium(&stg);
        } else {
            preq->ok = false;
            preq->errorMsg = "Format not applicable for current quantum seed";
        }

        pDataObj->Release();
        observerMgr_.SetSimulatedObserver(false);
    }

    void RefreshHudOverlay() {
        if (headless_ || !hud_.IsVisible()) return;
        bool hasSeed = false;
        sclip::QuantumSeed seed = telemetry_.GetActiveSeed(&hasSeed);
        sclip::ObserverSnapshot obs = observerMgr_.Capture(false);
        bool isBypass = (obs.lockMode == sclip::ManualLockMode::RawBypass);

        std::wstring top;
        if (isBypass) {
            top = L"⚡ RAW BYPASS ACTIVE (Alt+Shift+V to resume)";
        } else if (hasSeed) {
            top = sclip::Utf8ToWide(std::string(sclip::SuperpositionKindBadgeZh(seed)) + " [0B Eval]");
        } else {
            top = L"SCHRÖDINGER·CLIP // STANDBY (0 Bytes)";
        }

        std::string targetDesc = obs.processName;
        if (!obs.detectedFile.empty()) targetDesc += " [" + obs.detectedFile + "]";
        std::string outcome = hasSeed ? sclip::PredictCollapseTargetLabel(seed, obs) : "Waiting for Ctrl+C";
        std::wstring bot = sclip::Utf8ToWide("🎯 " + targetDesc + " ──▶ " + outcome);

        hud_.UpdateLines(top, bot, isBypass);
    }

    void CleanupBeforeQuit() {
        if (cleanedUp_) return;
        cleanedUp_ = true;

        if (httpServer_) {
            httpServer_->Stop();
            httpServer_.reset();
        }
        if (hwnd_) {
            KillTimer(hwnd_, 1);
            if (!headless_) {
                UnregisterHotKey(hwnd_, sclip::HOTKEY_TOGGLE_BYPASS);
                UnregisterHotKey(hwnd_, sclip::HOTKEY_OPEN_CONSOLE);
            }
            RemoveClipboardFormatListener(hwnd_);
        }
        tray_.Remove();
        hud_.Destroy();

        // Cleanly release OLE clipboard ownership without forcing eager evaluation
        if (currentDataObj_) {
            if (OleIsCurrentClipboard(currentDataObj_) == S_OK) {
                OleSetClipboard(nullptr);
            }
            currentDataObj_->Release();
            currentDataObj_ = nullptr;
        }

        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
            g_staHwnd = nullptr;
        }
    }

    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        SchrodingerDaemon* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<SchrodingerDaemon*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<SchrodingerDaemon*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
        return self->WndProc(hwnd, msg, wParam, lParam);
    }

    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
            case WM_CLIPBOARDUPDATE:
                OnClipboardUpdated();
                return 0;

            case WM_TIMER:
                RefreshHudOverlay();
                return 0;

            case WM_HOTKEY:
                if (wParam == sclip::HOTKEY_TOGGLE_BYPASS) {
                    observerMgr_.ToggleRawBypass();
                    RefreshHudOverlay();
                } else if (wParam == sclip::HOTKEY_OPEN_CONSOLE) {
                    OpenObservatoryInBrowser();
                }
                return 0;

            case sclip::WM_SCLIP_TRAY:
                if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                    bool isBypass = (observerMgr_.GetManualLockMode() == sclip::ManualLockMode::RawBypass);
                    tray_.ShowContextMenu(hwnd, isBypass, hud_.IsVisible());
                } else if (lParam == WM_LBUTTONDBLCLK) {
                    OpenObservatoryInBrowser();
                }
                return 0;

            case WM_COMMAND:
                switch (LOWORD(wParam)) {
                    case sclip::IDM_TRAY_OPEN_UI: OpenObservatoryInBrowser(); break;
                    case sclip::IDM_TRAY_TOGGLE_RAW:
                        observerMgr_.ToggleRawBypass();
                        RefreshHudOverlay();
                        break;
                    case sclip::IDM_TRAY_TOGGLE_HUD:
                        hud_.ToggleVisible();
                        RefreshHudOverlay();
                        break;
                    case sclip::IDM_TRAY_DISARM:
                        DisarmClipboard();
                        break;
                    case sclip::IDM_TRAY_EXIT:
                        PostMessageW(hwnd, sclip::WM_SCLIP_SHUTDOWN, 0, 0);
                        break;
                }
                return 0;

            case sclip::WM_SCLIP_ARM_SYNC:
                HandleSyncArm(reinterpret_cast<sclip::SyncArmRequest*>(wParam));
                return 0;

            case sclip::WM_SCLIP_PREVIEW_SYNC:
                HandleSyncPreview(reinterpret_cast<sclip::SyncPreviewRequest*>(wParam));
                return 0;

            case sclip::WM_SCLIP_DISARM_SYNC:
                DisarmClipboard();
                return 0;

            case sclip::WM_SCLIP_SHUTDOWN:
                CleanupBeforeQuit();
                PostQuitMessage(0);
                return 0;

            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;

            default:
                return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }
};

} // namespace

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);

    uint16_t port = 17890;
    bool headless = false;
    bool showHud = true;
    bool autoOpenBrowser = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--headless") {
            headless = true;
        } else if (arg == "--no-hud") {
            showHud = false;
        } else if (arg == "--open-ui") {
            autoOpenBrowser = true;
        } else if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        }
    }

    // Must initialize single-threaded OLE apartment for OleSetClipboard & IStream marshaling
    HRESULT hr = OleInitialize(nullptr);
    if (FAILED(hr)) {
        std::fprintf(stderr, "[FATAL] OleInitialize failed: 0x%08lx\n", static_cast<unsigned long>(hr));
        return 1;
    }

    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&gdiToken, &gsi, nullptr);

    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    int exitCode = 0;
    {
        SchrodingerDaemon daemon(port, headless, showHud, autoOpenBrowser);
        exitCode = daemon.Run(GetModuleHandleW(nullptr));
    }

    Gdiplus::GdiplusShutdown(gdiToken);
    OleUninitialize();
    return exitCode;
}

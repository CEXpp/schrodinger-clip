#pragma once
#include "common.hpp"

namespace sclip {

enum class HostCategory {
    PlainGeneric = 0,
    CodeEditor,
    Terminal,
    Spreadsheet,
    DatabaseClient,
    RichDocument,
    VisualCanvasOrChat,
    FileExplorer,
    Browser
};

enum class MicroLangContext {
    None = 0,
    TypeScript,
    JavaScript,
    Python,
    Go,
    Rust,
    SQL,
    Markdown,
    HtmlCss,
    HttpRest
};

enum class ManualLockMode {
    Auto = 0,
    RawBypass,
    LockTypeScript,
    LockPython,
    LockGo,
    LockRust,
    LockSQL,
    LockExcel,
    LockMarkdown,
    LockPngCard,
    LockFetchJs
};

struct ObserverSnapshot {
    DWORD foregroundPid = 0;
    DWORD rpcCallerPid = 0;
    std::string processName;      // Effective target exe name, e.g. "Code.exe"
    std::string rpcCallerProcess; // Actual COM RPC caller exe if cross-process
    std::string windowTitle;      // UTF-8 window title
    std::string detectedFile;     // e.g. "schema.ts" or "Google Sheets"
    HostCategory category = HostCategory::PlainGeneric;
    MicroLangContext lang = MicroLangContext::None;
    ManualLockMode lockMode = ManualLockMode::Auto;
    bool isSimulated = false;
    bool isBlockedSniffer = false;
    double detectTimeMs = 0.0;
};

inline const char* HostCategoryName(HostCategory c) {
    switch (c) {
        case HostCategory::CodeEditor: return "Code Editor";
        case HostCategory::Terminal: return "Terminal / Shell";
        case HostCategory::Spreadsheet: return "Spreadsheet Grid";
        case HostCategory::DatabaseClient: return "Database Client";
        case HostCategory::RichDocument: return "Rich Document / Mail";
        case HostCategory::VisualCanvasOrChat: return "IM Chat / Canvas / Slides";
        case HostCategory::FileExplorer: return "File Explorer Shell";
        case HostCategory::Browser: return "Web Browser";
        default: return "Generic Host";
    }
}

inline const char* MicroLangName(MicroLangContext l) {
    switch (l) {
        case MicroLangContext::TypeScript: return ".ts/.tsx (TypeScript)";
        case MicroLangContext::JavaScript: return ".js/.jsx (JavaScript)";
        case MicroLangContext::Python: return ".py (Python)";
        case MicroLangContext::Go: return ".go (Go)";
        case MicroLangContext::Rust: return ".rs (Rust)";
        case MicroLangContext::SQL: return ".sql (SQL)";
        case MicroLangContext::Markdown: return ".md (Markdown)";
        case MicroLangContext::HtmlCss: return ".html/.css (Web)";
        case MicroLangContext::HttpRest: return ".http (REST Client)";
        default: return "None";
    }
}

inline std::string QueryProcessBaseName(DWORD pid) {
    if (pid == 0) return "";
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return "";
    wchar_t pathBuf[MAX_PATH * 2] = {};
    DWORD sz = static_cast<DWORD>(sizeof(pathBuf) / sizeof(wchar_t));
    std::string result;
    if (QueryFullProcessImageNameW(hProc, 0, pathBuf, &sz) && sz > 0) {
        const wchar_t* base = PathFindFileNameW(pathBuf);
        result = WideToUtf8(base ? base : pathBuf);
    }
    CloseHandle(hProc);
    return result;
}

struct EnumPidWindowCtx {
    DWORD targetPid = 0;
    std::wstring bestTitle;
};

inline BOOL CALLBACK EnumWindowForPidProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumPidWindowCtx*>(lParam);
    DWORD wndPid = 0;
    GetWindowThreadProcessId(hwnd, &wndPid);
    if (wndPid == ctx->targetPid && IsWindowVisible(hwnd)) {
        wchar_t buf[512] = {};
        int len = GetWindowTextW(hwnd, buf, 511);
        if (len > 0) {
            ctx->bestTitle.assign(buf, static_cast<size_t>(len));
            return FALSE;
        }
    }
    return TRUE;
}

inline bool IsBackgroundSnifferProcess(const std::string& exeName) {
    if (exeName.empty()) return false;
    std::string low = ToLowerAscii(exeName);
    static const std::set<std::string> kSnifferBlacklist = {
        "svchost.exe",                 // Hosts ClipboardUserSvc (Win+V background service)
        "textinputhost.exe",           // Win10/11 Win+V / Emoji panel background sniffer
        "searchhost.exe",
        "searchapp.exe",
        "shellexperiencehost.exe",
        "startmenuexperiencehost.exe",
        "sihost.exe",
        "ctfmon.exe",
        "widgets.exe",
        "widgetservice.exe",
        "lockapp.exe",
        "rdpclip.exe",
        "smartscreen.exe",
        "systemsettings.exe"
    };
    return kSnifferBlacklist.count(low) > 0;
}

inline MicroLangContext MatchExtensionToLang(const std::string& extLower) {
    if (extLower == ".ts" || extLower == ".tsx" || extLower == ".mts" || extLower == ".cts")
        return MicroLangContext::TypeScript;
    if (extLower == ".js" || extLower == ".jsx" || extLower == ".mjs" || extLower == ".cjs")
        return MicroLangContext::JavaScript;
    if (extLower == ".py" || extLower == ".pyi")
        return MicroLangContext::Python;
    if (extLower == ".go")
        return MicroLangContext::Go;
    if (extLower == ".rs")
        return MicroLangContext::Rust;
    if (extLower == ".sql" || extLower == ".pgsql" || extLower == ".mysql" || extLower == ".ddl")
        return MicroLangContext::SQL;
    if (extLower == ".md" || extLower == ".mdx" || extLower == ".markdown")
        return MicroLangContext::Markdown;
    if (extLower == ".html" || extLower == ".htm" || extLower == ".css" ||
        extLower == ".scss" || extLower == ".less" || extLower == ".vue" || extLower == ".svelte")
        return MicroLangContext::HtmlCss;
    if (extLower == ".http" || extLower == ".rest")
        return MicroLangContext::HttpRest;
    return MicroLangContext::None;
}

// Extract active file name and language extension from an editor window title
inline void ExtractFileMicroContext(const std::string& title, std::string& outFile, MicroLangContext& outLang) {
    outFile.clear();
    outLang = MicroLangContext::None;
    if (title.empty()) return;

    auto cleanSegment = [](std::string t) -> std::string {
        t = Trim(t);
        // Strip leading dirty/punctuation markers (*, [, (, ", ', ●, •)
        while (!t.empty()) {
            if (StartsWith(t, "\xE2\x97\x8F") || StartsWith(t, "\xE2\x80\xA2")) {
                t = Trim(t.substr(3));
                continue;
            }
            char f = t.front();
            if (f == '*' || f == '[' || f == '(' || f == '"' || f == '\'') {
                t = Trim(t.substr(1));
                continue;
            }
            break;
        }
        // Strip trailing dirty/punctuation markers (*, ], ), ", ', ,, :, ●, •)
        while (!t.empty()) {
            if (EndsWith(t, "\xE2\x97\x8F") || EndsWith(t, "\xE2\x80\xA2")) {
                t = Trim(t.substr(0, t.size() - 3));
                continue;
            }
            char b = t.back();
            if (b == '*' || b == ']' || b == ')' || b == '"' || b == '\'' || b == ',' || b == ':') {
                t = Trim(t.substr(0, t.size() - 1));
                continue;
            }
            break;
        }
        size_t slashPos = t.find_last_of("/\\");
        if (slashPos != std::string::npos) {
            t = Trim(t.substr(slashPos + 1));
        }
        return t;
    };

    auto matchCandidate = [&](const std::string& rawCand) -> bool {
        std::string t = cleanSegment(rawCand);
        size_t dotPos = t.rfind('.');
        if (dotPos != std::string::npos && dotPos > 0 && dotPos + 1 < t.size()) {
            std::string ext = ToLowerAscii(t.substr(dotPos));
            MicroLangContext matched = MatchExtensionToLang(ext);
            if (matched != MicroLangContext::None) {
                outFile = t;
                outLang = matched;
                return true;
            }
        }
        return false;
    };

    // Pass 1: Split on standard IDE/editor title separators (" - ", " — ", " – ", " | ")
    // This preserves filenames with spaces such as "my schema.ts - project - Visual Studio Code"
    std::string normSep = ReplaceAll(title, " \xE2\x80\x94 ", "\n");
    normSep = ReplaceAll(normSep, " \xE2\x80\x93 ", "\n");
    normSep = ReplaceAll(normSep, " - ", "\n");
    normSep = ReplaceAll(normSep, " | ", "\n");
    for (const auto& seg : SplitLines(normSep)) {
        if (matchCandidate(seg)) return;
    }

    // Pass 2: Fallback token scan (handles "schema.ts (Working Tree)" etc.)
    std::string token;
    for (size_t i = 0; i <= title.size(); ++i) {
        char c = (i < title.size()) ? title[i] : ' ';
        if (c == ' ' || c == '\t' || c == '|' || c == ';') {
            if (!token.empty()) {
                if (matchCandidate(token)) return;
                token.clear();
            }
        } else {
            token.push_back(c);
        }
    }
}

inline void ClassifyObserverContext(ObserverSnapshot& snap) {
    std::string lowExe = ToLowerAscii(snap.processName);
    std::string lowTitle = ToLowerAscii(snap.windowTitle);

    // Layer 1: Host Process Identification
    if (lowExe == "explorer.exe" || lowExe == "files.exe" || lowExe == "totalcmd64.exe" || lowExe == "onecommander.exe") {
        snap.category = HostCategory::FileExplorer;
    } else if (lowExe == "windowsterminal.exe" || lowExe == "wt.exe" || lowExe == "powershell.exe" ||
               lowExe == "pwsh.exe" || lowExe == "cmd.exe" || lowExe == "mintty.exe" ||
               lowExe == "alacritty.exe" || lowExe == "wezterm-gui.exe" || lowExe == "conhost.exe" ||
               lowExe == "xshell.exe" || lowExe == "tabby.exe") {
        snap.category = HostCategory::Terminal;
    } else if (lowExe == "excel.exe" || lowExe == "et.exe" || lowExe == "scalc.exe") {
        snap.category = HostCategory::Spreadsheet;
    } else if (lowExe == "datagrip64.exe" || lowExe == "dbeaver.exe" || lowExe == "navicat.exe" ||
               lowExe == "ssms.exe" || lowExe == "heidisql.exe" || lowExe == "pgadmin4.exe") {
        snap.category = HostCategory::DatabaseClient;
        snap.lang = MicroLangContext::SQL;
    } else if (lowExe == "code.exe" || lowExe == "code - insiders.exe" || lowExe == "cursor.exe" ||
               lowExe == "windsurf.exe" || lowExe == "kiro.exe" || lowExe == "qoder.exe" ||
               lowExe == "zed.exe" || lowExe == "devenv.exe" || lowExe == "idea64.exe" ||
               lowExe == "pycharm64.exe" || lowExe == "goland64.exe" || lowExe == "rustrover64.exe" ||
               lowExe == "webstorm64.exe" || lowExe == "clion64.exe" || lowExe == "sublime_text.exe" ||
               lowExe == "notepad++.exe" || lowExe == "nvim-qt.exe" || lowExe == "gvim.exe") {
        snap.category = HostCategory::CodeEditor;
        if (lowExe == "pycharm64.exe") snap.lang = MicroLangContext::Python;
        else if (lowExe == "goland64.exe") snap.lang = MicroLangContext::Go;
        else if (lowExe == "rustrover64.exe") snap.lang = MicroLangContext::Rust;
    } else if (lowExe == "typora.exe" || lowExe == "obsidian.exe" || lowExe == "marktext.exe") {
        snap.category = HostCategory::CodeEditor;
        snap.lang = MicroLangContext::Markdown;
    } else if (lowExe == "winword.exe" || lowExe == "wps.exe" || lowExe == "outlook.exe" ||
               lowExe == "thunderbird.exe" || lowExe == "wordpad.exe") {
        snap.category = HostCategory::RichDocument;
    } else if (lowExe == "wechat.exe" || lowExe == "weixin.exe" || lowExe == "qq.exe" ||
               lowExe == "tim.exe" || lowExe == "telegram.exe" || lowExe == "slack.exe" ||
               lowExe == "discord.exe" || lowExe == "dingtalk.exe" || lowExe == "feishu.exe" ||
               lowExe == "lark.exe" || lowExe == "teams.exe" || lowExe == "ms-teams.exe" ||
               lowExe == "powerpnt.exe" || lowExe == "wpp.exe" || lowExe == "mspaint.exe" ||
               lowExe == "paintdotnet.exe" || lowExe == "figma.exe") {
        snap.category = HostCategory::VisualCanvasOrChat;
    } else if (lowExe == "chrome.exe" || lowExe == "msedge.exe" || lowExe == "firefox.exe" ||
               lowExe == "brave.exe" || lowExe == "arc.exe" || lowExe == "opera.exe" || lowExe == "vivaldi.exe") {
        snap.category = HostCategory::Browser;
    } else {
        snap.category = HostCategory::PlainGeneric;
    }

    // Layer 2: Window Title Micro-Context Extraction
    std::string detectedFile;
    MicroLangContext detectedLang = MicroLangContext::None;
    ExtractFileMicroContext(snap.windowTitle, detectedFile, detectedLang);
    if (detectedLang != MicroLangContext::None) {
        snap.detectedFile = detectedFile;
        snap.lang = detectedLang;
        if (snap.category == HostCategory::PlainGeneric) {
            snap.category = HostCategory::CodeEditor;
        }
    }

    // Browser Web-App Micro-Context Detection
    if (snap.category == HostCategory::Browser) {
        if (lowTitle.find("google sheets") != std::string::npos ||
            snap.windowTitle.find("\xE9\xA3\x9E\xE4\xB9\xA6") != std::string::npos || // 飞书
            snap.windowTitle.find("\xE8\x85\xBE\xE8\xAE\xAF\xE6\x96\x87\xE6\xA1\xA3") != std::string::npos || // 腾讯文档
            snap.windowTitle.find("\xE7\x9F\xB3\xE5\xA2\xA8\xE6\x96\x87\xE6\xA1\xA3") != std::string::npos || // 石墨文档
            lowTitle.find("airtable") != std::string::npos ||
            lowTitle.find("feishu") != std::string::npos) {
            snap.category = HostCategory::Spreadsheet;
            snap.detectedFile = "Web Spreadsheet";
        } else if (lowTitle.find("excalidraw") != std::string::npos ||
                   lowTitle.find("figma") != std::string::npos ||
                   lowTitle.find("canva") != std::string::npos ||
                   lowTitle.find("miro") != std::string::npos ||
                   lowTitle.find("photopea") != std::string::npos) {
            snap.category = HostCategory::VisualCanvasOrChat;
            snap.detectedFile = "Web Visual Canvas";
        } else if (detectedLang == MicroLangContext::None &&
                   (lowTitle.find("notion") != std::string::npos ||
                    lowTitle.find("github") != std::string::npos ||
                    snap.windowTitle.find("\xE8\xAF\xAD\xE9\x9B\x80") != std::string::npos)) { // 语雀
            snap.lang = MicroLangContext::Markdown;
            snap.detectedFile = "Web Markdown Doc";
        }
    }

    // Apply Manual Lock Overrides if active
    switch (snap.lockMode) {
        case ManualLockMode::LockTypeScript:
            snap.category = HostCategory::CodeEditor;
            snap.lang = MicroLangContext::TypeScript;
            snap.detectedFile = "[Locked: .ts]";
            break;
        case ManualLockMode::LockPython:
            snap.category = HostCategory::CodeEditor;
            snap.lang = MicroLangContext::Python;
            snap.detectedFile = "[Locked: .py]";
            break;
        case ManualLockMode::LockGo:
            snap.category = HostCategory::CodeEditor;
            snap.lang = MicroLangContext::Go;
            snap.detectedFile = "[Locked: .go]";
            break;
        case ManualLockMode::LockRust:
            snap.category = HostCategory::CodeEditor;
            snap.lang = MicroLangContext::Rust;
            snap.detectedFile = "[Locked: .rs]";
            break;
        case ManualLockMode::LockSQL:
            snap.category = HostCategory::DatabaseClient;
            snap.lang = MicroLangContext::SQL;
            snap.detectedFile = "[Locked: .sql]";
            break;
        case ManualLockMode::LockExcel:
            snap.category = HostCategory::Spreadsheet;
            snap.detectedFile = "[Locked: Grid]";
            break;
        case ManualLockMode::LockMarkdown:
            snap.category = HostCategory::CodeEditor;
            snap.lang = MicroLangContext::Markdown;
            snap.detectedFile = "[Locked: .md]";
            break;
        case ManualLockMode::LockPngCard:
            snap.category = HostCategory::VisualCanvasOrChat;
            snap.detectedFile = "[Locked: 2x PNG]";
            break;
        case ManualLockMode::LockFetchJs:
            snap.category = HostCategory::CodeEditor;
            snap.lang = MicroLangContext::TypeScript;
            snap.detectedFile = "[Locked: fetch()]";
            break;
        default:
            break;
    }
}

class ObserverContextManager {
public:
    void SetManualLockMode(ManualLockMode mode) {
        std::lock_guard<std::mutex> lk(mu_);
        lockMode_ = mode;
    }

    ManualLockMode GetManualLockMode() const {
        std::lock_guard<std::mutex> lk(mu_);
        return lockMode_;
    }

    ManualLockMode ToggleRawBypass() {
        std::lock_guard<std::mutex> lk(mu_);
        lockMode_ = (lockMode_ == ManualLockMode::RawBypass) ? ManualLockMode::Auto : ManualLockMode::RawBypass;
        return lockMode_;
    }

    void SetSimulatedObserver(bool enabled, const std::string& processName = "", const std::string& windowTitle = "") {
        std::lock_guard<std::mutex> lk(mu_);
        simEnabled_ = enabled;
        simProcess_ = processName;
        simTitle_ = windowTitle;
    }

    ObserverSnapshot Capture(bool checkRpcCaller = true) const {
        auto t0 = std::chrono::steady_clock::now();
        ObserverSnapshot snap;

        ManualLockMode curLock;
        bool useSim = false;
        std::string simProc, simWinTitle;
        {
            std::lock_guard<std::mutex> lk(mu_);
            curLock = lockMode_;
            useSim = simEnabled_;
            simProc = simProcess_;
            simWinTitle = simTitle_;
        }
        snap.lockMode = curLock;

        // 1. Inspect actual COM RPC caller PID if invoked inside IDataObject::GetData
        if (checkRpcCaller) {
            unsigned long callerPid = 0;
            if (I_RpcBindingInqLocalClientPID(nullptr, &callerPid) == RPC_S_OK && callerPid != 0) {
                snap.rpcCallerPid = static_cast<DWORD>(callerPid);
                snap.rpcCallerProcess = QueryProcessBaseName(snap.rpcCallerPid);
                if (snap.rpcCallerPid != GetCurrentProcessId() && IsBackgroundSnifferProcess(snap.rpcCallerProcess)) {
                    snap.isBlockedSniffer = true;
                    snap.processName = snap.rpcCallerProcess;
                    snap.detectTimeMs = ElapsedMs(t0);
                    return snap;
                }
            }
        }

        // 2. If Sandbox/Verification Simulated Observer Override is active, use it cleanly
        if (useSim && !simProc.empty()) {
            snap.isSimulated = true;
            snap.processName = simProc;
            snap.windowTitle = simWinTitle;
            ClassifyObserverContext(snap);
            snap.detectTimeMs = ElapsedMs(t0);
            return snap;
        }

        // 3. Real Win32 Foreground Window + RPC Caller Reconciliation
        HWND fgHwnd = GetForegroundWindow();
        if (fgHwnd) {
            GetWindowThreadProcessId(fgHwnd, &snap.foregroundPid);
            wchar_t titleBuf[512] = {};
            int len = GetWindowTextW(fgHwnd, titleBuf, 511);
            if (len > 0) {
                snap.windowTitle = WideToUtf8(std::wstring(titleBuf, static_cast<size_t>(len)));
            }
            snap.processName = QueryProcessBaseName(snap.foregroundPid);
        }

        // Reconcile when COM RPC caller PID differs from foreground window PID
        // (e.g. Explorer background copy worker thread, pasting into an unfocused window,
        // or an automated COM client calling OleGetClipboard()->GetData while another app is focused)
        if (snap.rpcCallerPid != 0 && snap.rpcCallerPid != GetCurrentProcessId() &&
            snap.rpcCallerPid != snap.foregroundPid && !snap.rpcCallerProcess.empty()) {
            std::string lowCaller = ToLowerAscii(snap.rpcCallerProcess);
            std::string lowFg = ToLowerAscii(snap.processName);
            if (lowCaller != "dllhost.exe" && lowCaller != "backgroundtaskhost.exe" &&
                lowFg != "applicationframehost.exe") {
                snap.processName = snap.rpcCallerProcess;
                snap.windowTitle.clear();
                EnumPidWindowCtx enumCtx{snap.rpcCallerPid, L""};
                EnumWindows(EnumWindowForPidProc, reinterpret_cast<LPARAM>(&enumCtx));
                if (!enumCtx.bestTitle.empty()) {
                    snap.windowTitle = WideToUtf8(enumCtx.bestTitle);
                }
            }
        }

        if (snap.processName.empty()) {
            snap.processName = "UnknownHost.exe";
        }

        ClassifyObserverContext(snap);
        snap.detectTimeMs = ElapsedMs(t0);
        return snap;
    }

private:
    mutable std::mutex mu_;
    ManualLockMode lockMode_ = ManualLockMode::Auto;
    bool simEnabled_ = false;
    std::string simProcess_;
    std::string simTitle_;
};

} // namespace sclip

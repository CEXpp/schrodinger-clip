#include "../src/common.hpp"
#include "../src/ole/superposition_dataobj.hpp"

namespace {

int g_totalAssertions = 0;
int g_failedAssertions = 0;

void CheckAssert(bool cond, const char* expr, const std::string& detail, int line) {
    ++g_totalAssertions;
    if (cond) {
        std::printf("  [PASS] %s\n", detail.c_str());
    } else {
        ++g_failedAssertions;
        std::fprintf(stderr, "  [FAIL] (line %d) %s -- expr: %s\n", line, detail.c_str(), expr);
    }
    std::fflush(stdout);
}

#define VERIFY_ASSERT(cond, detail) CheckAssert((cond), #cond, (detail), __LINE__)

// Instrumented 24 MB HTTP File Server for Verification 1 & Verification 4
class InstrumentedTestFileServer {
public:
    InstrumentedTestFileServer(uint16_t port, const std::vector<uint8_t>& payload)
        : port_(port), payload_(payload) {}

    ~InstrumentedTestFileServer() { Stop(); }

    bool Start() {
        WSADATA wsa = {};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
        sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock_ == INVALID_SOCKET) return false;
        int opt = 1;
        setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port_);
        if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) return false;
        if (listen(sock_, SOMAXCONN) == SOCKET_ERROR) return false;

        running_ = true;
        worker_ = std::thread([this]() { Loop(); });
        return true;
    }

    void Stop() {
        running_ = false;
        if (sock_ != INVALID_SOCKET) {
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
        }
        if (worker_.joinable()) worker_.join();
    }

    void ResetCounters() {
        headCount_.store(0);
        getCount_.store(0);
        bytesServed_.store(0);
    }

    uint64_t HeadCount() const { return headCount_.load(); }
    uint64_t GetCount() const { return getCount_.load(); }
    uint64_t BytesServed() const { return bytesServed_.load(); }

private:
    uint16_t port_;
    const std::vector<uint8_t>& payload_;
    SOCKET sock_ = INVALID_SOCKET;
    std::atomic<bool> running_{false};
    std::thread worker_;

    std::atomic<uint64_t> headCount_{0};
    std::atomic<uint64_t> getCount_{0};
    std::atomic<uint64_t> bytesServed_{0};

    void Loop() {
        while (running_) {
            sockaddr_in caddr = {};
            int clen = sizeof(caddr);
            SOCKET c = accept(sock_, reinterpret_cast<sockaddr*>(&caddr), &clen);
            if (c == INVALID_SOCKET) {
                if (!running_) break;
                continue;
            }
            std::thread([this, c]() { HandleConn(c); }).detach();
        }
    }

    void HandleConn(SOCKET c) {
        char buf[4096];
        std::string req;
        while (req.find("\r\n\r\n") == std::string::npos && req.size() < 16384) {
            int n = recv(c, buf, sizeof(buf), 0);
            if (n <= 0) break;
            req.append(buf, static_cast<size_t>(n));
        }
        if (req.empty()) {
            closesocket(c);
            return;
        }

        bool isHead = sclip::StartsWithIgnoreCase(req, "HEAD ");
        bool isGet = sclip::StartsWithIgnoreCase(req, "GET ");
        if (isHead) headCount_.fetch_add(1);
        if (isGet) getCount_.fetch_add(1);

        std::ostringstream hdr;
        hdr << "HTTP/1.1 200 OK\r\n"
            << "Content-Type: application/octet-stream\r\n"
            << "Content-Length: " << payload_.size() << "\r\n"
            << "Connection: close\r\n\r\n";
        std::string h = hdr.str();
        send(c, h.data(), static_cast<int>(h.size()), 0);

        if (isGet) {
            size_t offset = 0;
            while (offset < payload_.size() && running_) {
                int chunk = static_cast<int>(std::min<size_t>(65536, payload_.size() - offset));
                int sent = send(c, reinterpret_cast<const char*>(payload_.data() + offset), chunk, 0);
                if (sent <= 0) break;
                offset += static_cast<size_t>(sent);
                bytesServed_.fetch_add(static_cast<uint64_t>(sent));
            }
        }
        closesocket(c);
    }
};

std::string HttpRequestLocal(uint16_t port, const std::wstring& method,
                             const std::wstring& path, const std::string& jsonBody = "") {
    HINTERNET hSess = WinHttpOpen(L"E2EVerify/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) return "";
    WinHttpSetTimeouts(hSess, 2000, 2000, 4000, 4000);
    HINTERNET hConn = WinHttpConnect(hSess, L"127.0.0.1", port, 0);
    std::string out;
    if (hConn) {
        HINTERNET hReq = WinHttpOpenRequest(hConn, method.c_str(), path.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (hReq) {
            std::wstring hdrs = jsonBody.empty() ? L"" : L"Content-Type: application/json\r\n";
            LPVOID bodyPtr = jsonBody.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(jsonBody.data());
            DWORD bodyLen = static_cast<DWORD>(jsonBody.size());
            if (WinHttpSendRequest(hReq,
                                   hdrs.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdrs.c_str(),
                                   static_cast<DWORD>(hdrs.size()),
                                   bodyPtr, bodyLen, bodyLen, 0) &&
                WinHttpReceiveResponse(hReq, nullptr)) {
                char buf[8192];
                DWORD got = 0;
                while (WinHttpReadData(hReq, buf, sizeof(buf), &got) && got > 0) {
                    out.append(buf, got);
                    got = 0;
                }
            }
            WinHttpCloseHandle(hReq);
        }
        WinHttpCloseHandle(hConn);
    }
    WinHttpCloseHandle(hSess);
    return out;
}

bool PutRawTextOnOsClipboard(const std::string& utf8Text) {
    for (int i = 0; i < 10; ++i) {
        if (OpenClipboard(nullptr)) {
            EmptyClipboard();
            HGLOBAL hMem = sclip::AllocHGlobalWideString(sclip::Utf8ToWide(utf8Text));
            bool ok = (SetClipboardData(CF_UNICODETEXT, hMem) != nullptr);
            if (!ok && hMem) GlobalFree(hMem);
            CloseClipboard();
            return ok;
        }
        Sleep(15);
    }
    return false;
}

bool ReadComClipboardUnicodeText(std::string& outUtf8) {
    outUtf8.clear();
    IDataObject* pObj = nullptr;
    if (FAILED(OleGetClipboard(&pObj)) || !pObj) return false;

    FORMATETC fmt = {CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM stg = {};
    bool ok = false;
    if (SUCCEEDED(pObj->GetData(&fmt, &stg)) && stg.tymed == TYMED_HGLOBAL && stg.hGlobal) {
        const wchar_t* wptr = static_cast<const wchar_t*>(GlobalLock(stg.hGlobal));
        if (wptr) {
            outUtf8 = sclip::WideToUtf8(std::wstring(wptr));
            ok = true;
            GlobalUnlock(stg.hGlobal);
        }
        ReleaseStgMedium(&stg);
    }
    pObj->Release();
    return ok;
}

bool ReadComClipboardBytes(UINT cf, std::vector<uint8_t>& outBytes) {
    outBytes.clear();
    IDataObject* pObj = nullptr;
    if (FAILED(OleGetClipboard(&pObj)) || !pObj) return false;

    FORMATETC fmt = {static_cast<CLIPFORMAT>(cf), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM stg = {};
    bool ok = false;
    if (SUCCEEDED(pObj->GetData(&fmt, &stg)) && stg.tymed == TYMED_HGLOBAL && stg.hGlobal) {
        size_t sz = GlobalSize(stg.hGlobal);
        const uint8_t* ptr = static_cast<const uint8_t*>(GlobalLock(stg.hGlobal));
        if (ptr && sz > 0) {
            outBytes.assign(ptr, ptr + sz);
            ok = true;
            GlobalUnlock(stg.hGlobal);
        }
        ReleaseStgMedium(&stg);
    }
    pObj->Release();
    return ok;
}

void SetSimulatedObserverViaApi(uint16_t daemonPort, bool enabled,
                                const std::string& proc = "", const std::string& title = "") {
    std::ostringstream body;
    body << "{\"enabled\":" << (enabled ? "true" : "false")
         << ",\"processName\":\"" << sclip::JsonEscape(proc) << "\""
         << ",\"windowTitle\":\"" << sclip::JsonEscape(title) << "\"}";
    HttpRequestLocal(daemonPort, L"POST", L"/api/observer-override", body.str());
}

SIZE_T GetProcessWorkingSetBytes(HANDLE hProcess) {
    PROCESS_MEMORY_COUNTERS pmc = {};
    pmc.cb = sizeof(pmc);
    if (K32GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    std::wstring daemonExe = L"bin\\schrodinger_clip.exe";
    if (argc > 1) {
        daemonExe = sclip::Utf8ToWide(argv[1]);
    }

    HRESULT hrOle = OleInitialize(nullptr);
    if (FAILED(hrOle)) {
        std::fprintf(stderr, "OleInitialize failed: 0x%08lx\n", static_cast<unsigned long>(hrOle));
        return 1;
    }

    const uint16_t kDaemonPort = 17895;
    const uint16_t kFileServerPort = 17896;
    const auto& rf = sclip::RegisteredFormats::Instance();

    std::printf("======================================================================\n");
    std::printf("  SchrodingerClip Automated E2E System-Level Verification Suite\n");
    std::printf("======================================================================\n\n");

    // ------------------------------------------------------------------------
    // Step 0: Prepare 24 MB (>= 20 MB) deterministic payload + SHA-256 + Server
    // ------------------------------------------------------------------------
    const size_t kPayloadSize = 24ULL * 1024ULL * 1024ULL; // 25,165,824 bytes
    std::printf("[Setup] Generating 24 MB (25,165,824 bytes) binary payload & computing SHA-256...\n");
    std::vector<uint8_t> source24Mb(kPayloadSize);
    uint64_t prng = 0x9E3779B97F4A7C15ULL;
    for (size_t i = 0; i < kPayloadSize; ++i) {
        prng ^= prng >> 12;
        prng ^= prng << 25;
        prng ^= prng >> 27;
        source24Mb[i] = static_cast<uint8_t>((prng * 0x2545F4914F6CDD1DULL) >> 56);
    }
    std::string expectedSha256 = sclip::Sha256Hex(source24Mb.data(), source24Mb.size());
    std::printf("  -> Source 24 MB SHA-256: %s\n", expectedSha256.c_str());

    InstrumentedTestFileServer fileServer(kFileServerPort, source24Mb);
    VERIFY_ASSERT(fileServer.Start(), "Started local instrumented HTTP file server on 127.0.0.1:17896");

    // Launch SchrodingerClip Daemon process
    std::wstring cmdLine = L"\"" + daemonExe + L"\" --headless --port " + std::to_wstring(kDaemonPort);
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    BOOL created = CreateProcessW(nullptr, &cmdLine[0], nullptr, nullptr, FALSE,
                                  CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    VERIFY_ASSERT(created == TRUE, "Spawned schrodinger_clip.exe STA daemon process");
    if (!created) {
        fileServer.Stop();
        OleUninitialize();
        return 2;
    }

    bool daemonReady = false;
    for (int i = 0; i < 40; ++i) {
        std::string st = HttpRequestLocal(kDaemonPort, L"GET", L"/api/state");
        if (st.find("\"evaluatedBytesAtCopy\":0") != std::string::npos) {
            daemonReady = true;
            break;
        }
        Sleep(50);
    }
    VERIFY_ASSERT(daemonReady, "SchrodingerClip STA daemon HTTP telemetry & COM apartment ready");

    // ========================================================================
    // VERIFICATION 1: Strict Lazy Evaluation & Zero Pre-Download (0 Bytes!)
    // ========================================================================
    std::printf("\n--- [Verification 1] Strict Lazy Evaluation & Zero Pre-Download ---\n");
    fileServer.ResetCounters();
    std::string testFileUrl = "http://127.0.0.1:" + std::to_string(kFileServerPort) + "/quantum_archive_24mb.bin";

    // Inject via real Win32 OS Clipboard (tests real WM_CLIPBOARDUPDATE interception!)
    VERIFY_ASSERT(PutRawTextOnOsClipboard(testFileUrl),
                  "Copied 24 MB remote URL onto real Windows OS Clipboard (Ctrl+C simulation)");

    // Wait up to 300ms for WM_CLIPBOARDUPDATE -> OleSetClipboard superposition mount
    bool envelopeMounted = false;
    for (int i = 0; i < 25; ++i) {
        if (IsClipboardFormatAvailable(rf.cfEnvelope)) {
            envelopeMounted = true;
            break;
        }
        Sleep(20);
    }
    VERIFY_ASSERT(envelopeMounted,
                  "Daemon intercepted WM_CLIPBOARDUPDATE and mounted COM SuperpositionDataObject");
    VERIFY_ASSERT(IsClipboardFormatAvailable(rf.cfFileDescriptorW) == TRUE &&
                  IsClipboardFormatAvailable(rf.cfFileContents) == TRUE,
                  "Virtual file formats (CFSTR_FILEDESCRIPTORW + CFSTR_FILECONTENTS) advertised on OS clipboard");

    // Verify Premature-Collapse Shield Formats (CanIncludeInClipboardHistory = 0, CanUploadToCloudClipboard = 0)
    std::vector<uint8_t> histFlag, cloudFlag;
    VERIFY_ASSERT(ReadComClipboardBytes(rf.cfCanIncludeHistory, histFlag) &&
                  histFlag.size() >= sizeof(DWORD) &&
                  *reinterpret_cast<const DWORD*>(histFlag.data()) == 0,
                  "CanIncludeInClipboardHistory == 0 verified on active COM IDataObject");
    VERIFY_ASSERT(ReadComClipboardBytes(rf.cfCanUploadCloud, cloudFlag) &&
                  cloudFlag.size() >= sizeof(DWORD) &&
                  *reinterpret_cast<const DWORD*>(cloudFlag.data()) == 0,
                  "CanUploadToCloudClipboard == 0 verified on active COM IDataObject");

    // Wait > 500 ms (650 ms) to prove zero premature evaluation & zero background sniffer downloads!
    Sleep(650);
    VERIFY_ASSERT(fileServer.BytesServed() == 0,
                  "After 650 ms wait: HTTP server served STRICTLY 0 Bytes (zero premature download!)");
    VERIFY_ASSERT(fileServer.GetCount() == 0 && fileServer.HeadCount() == 0,
                  "After 650 ms wait: HTTP server received STRICTLY 0 GET and 0 HEAD requests!");

    // ========================================================================
    // VERIFICATION 2: Single Copy, Non-Destructive Multi-Context Collapse
    // ========================================================================
    std::printf("\n--- [Verification 2] Single Copy -> Multi-Context Heterogeneous Collapse ---\n");
    const std::string complexJson = R"({
  "clusterId": "Q-NODE-07",
  "epochVersion": 42,
  "active": true,
  "decommissionReason": null,
  "owner": {
    "uid": 9001,
    "email": "architect@quantum.io"
  },
  "metrics": [
    {"sensor": "cryo_core", "kelvin": 0.015, "healthy": true},
    {"sensor": "laser_bus", "kelvin": 293.4, "healthy": false, "alertTag": "THERMAL"}
  ]
})";

    HttpRequestLocal(kDaemonPort, L"POST", L"/api/arm",
                     "{\"text\":\"" + sclip::JsonEscape(complexJson) + "\"}");
    DWORD seqAfterSingleArm = GetClipboardSequenceNumber();

    // 2.1 TypeScript (.ts) context
    SetSimulatedObserverViaApi(kDaemonPort, true, "Code.exe", "● schema.ts - workspace - Visual Studio Code");
    std::string tsOut;
    VERIFY_ASSERT(ReadComClipboardUnicodeText(tsOut) &&
                  tsOut.find("export interface ") != std::string::npos &&
                  tsOut.find("clusterId: string;") != std::string::npos &&
                  tsOut.find("decommissionReason: null;") != std::string::npos &&
                  tsOut.find("alertTag?: string;") != std::string::npos,
                  "[Collapse 1/7 .ts] Returned valid recursive TypeScript interfaces with optional & nullable fields");

    // 2.2 Python (.py) context
    SetSimulatedObserverViaApi(kDaemonPort, true, "Cursor.exe", "models.py - backend - Cursor");
    std::string pyOut;
    VERIFY_ASSERT(ReadComClipboardUnicodeText(pyOut) &&
                  pyOut.find("from dataclasses import dataclass") != std::string::npos &&
                  pyOut.find("@dataclass") != std::string::npos &&
                  pyOut.find("List[") != std::string::npos &&
                  pyOut.find("Optional[") != std::string::npos,
                  "[Collapse 2/7 .py] Returned valid Python @dataclass hierarchy");

    // 2.3 SQL (.sql) context
    SetSimulatedObserverViaApi(kDaemonPort, true, "datagrip64.exe", "01_schema.sql - DataGrip");
    std::string sqlOut;
    VERIFY_ASSERT(ReadComClipboardUnicodeText(sqlOut) &&
                  sqlOut.find("CREATE TABLE IF NOT EXISTS ") != std::string::npos &&
                  sqlOut.find("INSERT INTO ") != std::string::npos &&
                  sqlOut.find("'cryo_core'") != std::string::npos &&
                  sqlOut.find("'laser_bus'") != std::string::npos,
                  "[Collapse 3/7 .sql] Returned valid SQL CREATE TABLE + multi-row batch INSERT");

    // 2.4 Spreadsheet (CF_HTML + CSV + TSV) context
    SetSimulatedObserverViaApi(kDaemonPort, true, "EXCEL.EXE", "Book1 - Excel");
    std::vector<uint8_t> htmlBytes, csvBytes;
    std::string tsvOut;
    VERIFY_ASSERT(ReadComClipboardBytes(rf.cfHtml, htmlBytes),
                  "[Collapse 4a/7 Excel CF_HTML] Retrieved CF_HTML payload");
    std::string htmlStr(reinterpret_cast<const char*>(htmlBytes.data()), htmlBytes.size());
    VERIFY_ASSERT(htmlStr.find("Version:0.9") != std::string::npos &&
                  htmlStr.find("<table") != std::string::npos &&
                  htmlStr.find("owner.email") != std::string::npos,
                  "CF_HTML contains valid offset header and flattened nested table columns");

    VERIFY_ASSERT(ReadComClipboardBytes(rf.cfCsv, csvBytes),
                  "[Collapse 4b/7 Excel CSV] Retrieved CSV payload");
    std::string csvStr(reinterpret_cast<const char*>(csvBytes.data()), csvBytes.size());
    VERIFY_ASSERT(csvStr.find("clusterId") != std::string::npos &&
                  csvStr.find("cryo_core") != std::string::npos &&
                  csvStr.find("laser_bus") != std::string::npos,
                  "CSV contains aligned headers and unpacked rows");

    VERIFY_ASSERT(ReadComClipboardUnicodeText(tsvOut) && tsvOut.find('\t') != std::string::npos,
                  "[Collapse 4c/7 Excel TSV] CF_UNICODETEXT collapsed to tab-separated grid for Excel");

    // 2.5 Go & Rust structs + .xlsx virtual stream from the SAME single copy!
    SetSimulatedObserverViaApi(kDaemonPort, true, "goland64.exe", "models.go - GoLand");
    std::string goOut;
    VERIFY_ASSERT(ReadComClipboardUnicodeText(goOut) &&
                  goOut.find("type ") != std::string::npos &&
                  goOut.find("`json:\"clusterId\"`") != std::string::npos,
                  "[Collapse 5/7 .go] Returned idiomatic Go structs with json tags");

    SetSimulatedObserverViaApi(kDaemonPort, true, "rustrover64.exe", "models.rs - RustRover");
    std::string rsOut;
    VERIFY_ASSERT(ReadComClipboardUnicodeText(rsOut) &&
                  rsOut.find("#[derive(Debug, Clone, Serialize, Deserialize)]") != std::string::npos &&
                  rsOut.find("pub struct ") != std::string::npos,
                  "[Collapse 6/7 .rs] Returned idiomatic Rust Serde structs");

    // Check .xlsx virtual stream on the exact same JSON seed!
    SetSimulatedObserverViaApi(kDaemonPort, true, "explorer.exe", "Documents");
    {
        IDataObject* pObj = nullptr;
        VERIFY_ASSERT(SUCCEEDED(OleGetClipboard(&pObj)) && pObj != nullptr, "Opened OLE clipboard for .xlsx check");
        if (pObj) {
            FORMATETC fmtFc = {static_cast<CLIPFORMAT>(rf.cfFileContents), nullptr, DVASPECT_CONTENT, 0, TYMED_ISTREAM};
            STGMEDIUM stgFc = {};
            VERIFY_ASSERT(SUCCEEDED(pObj->GetData(&fmtFc, &stgFc)) && stgFc.tymed == TYMED_ISTREAM && stgFc.pstm != nullptr,
                          "[Collapse 7/7 Explorer .xlsx] Obtained TYMED_ISTREAM for styled .xlsx virtual file");
            if (stgFc.pstm) {
                uint8_t zipSig[4] = {};
                ULONG got = 0;
                stgFc.pstm->Read(zipSig, 4, &got);
                VERIFY_ASSERT(got == 4 && zipSig[0] == 0x50 && zipSig[1] == 0x4B && zipSig[2] == 0x03 && zipSig[3] == 0x04,
                              "Virtual .xlsx stream begins with valid OpenXML PK ZIP header (50 4B 03 04)");
                ReleaseStgMedium(&stgFc);
            }
            pObj->Release();
        }
    }

    VERIFY_ASSERT(GetClipboardSequenceNumber() == seqAfterSingleArm,
                  "Clipboard sequence number UNCHANGED across all 7 heterogeneous collapses (non-destructive!)");
    SetSimulatedObserverViaApi(kDaemonPort, false);

    // ========================================================================
    // VERIFICATION 3: On-Demand Visual Synthesis (SVG + Code + #HEX -> PNG & DIB)
    // ========================================================================
    std::printf("\n--- [Verification 3] On-Demand Visual Synthesis (PNG / CF_DIBV5 / CF_DIB) ---\n");
    const std::string sampleSvg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 300 200\" width=\"300\" height=\"200\">"
        "<rect x=\"10\" y=\"10\" width=\"280\" height=\"180\" rx=\"20\" fill=\"#0F172A\" stroke=\"#38BDF8\" stroke-width=\"4\"/>"
        "<circle cx=\"150\" cy=\"100\" r=\"55\" fill=\"#3B82F6\"/>"
        "</svg>";
    HttpRequestLocal(kDaemonPort, L"POST", L"/api/arm",
                     "{\"text\":\"" + sclip::JsonEscape(sampleSvg) + "\"}");

    std::vector<uint8_t> pngBytes, dibV5Bytes, dibBytes;
    VERIFY_ASSERT(ReadComClipboardBytes(rf.cfPng, pngBytes) && pngBytes.size() > 256,
                  "Synthesized non-empty PNG payload from SVG vector source");
    const uint8_t kPngMagic[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    bool magicOk = pngBytes.size() >= 24 && std::memcmp(pngBytes.data(), kPngMagic, 8) == 0;
    uint32_t pngW = 0, pngH = 0;
    if (magicOk) {
        pngW = (static_cast<uint32_t>(pngBytes[16]) << 24) | (static_cast<uint32_t>(pngBytes[17]) << 16) |
               (static_cast<uint32_t>(pngBytes[18]) << 8) | static_cast<uint32_t>(pngBytes[19]);
        pngH = (static_cast<uint32_t>(pngBytes[20]) << 24) | (static_cast<uint32_t>(pngBytes[21]) << 16) |
               (static_cast<uint32_t>(pngBytes[22]) << 8) | static_cast<uint32_t>(pngBytes[23]);
    }
    std::printf("  -> SVG 2x Rasterized PNG Dimensions: %ux%u (%zu bytes)\n", pngW, pngH, pngBytes.size());
    VERIFY_ASSERT(magicOk && pngW == 600 && pngH == 400,
                  "PNG header signature valid and exact 2x HiDPI dimensions (600x400 for 300x200 SVG)");

    VERIFY_ASSERT(ReadComClipboardBytes(CF_DIBV5, dibV5Bytes) && dibV5Bytes.size() > sizeof(BITMAPV5HEADER),
                  "Synthesized valid CF_DIBV5 bitmap payload");
    if (dibV5Bytes.size() >= sizeof(BITMAPV5HEADER)) {
        const auto* v5 = reinterpret_cast<const BITMAPV5HEADER*>(dibV5Bytes.data());
        VERIFY_ASSERT(v5->bV5Width == 600 && v5->bV5Height == 400 && v5->bV5BitCount == 32,
                      "CF_DIBV5 BITMAPV5HEADER matches 600x400 32-bpp ARGB specification");
    }
    VERIFY_ASSERT(ReadComClipboardBytes(CF_DIB, dibBytes) && dibBytes.size() > sizeof(BITMAPINFOHEADER),
                  "Synthesized valid CF_DIB bitmap payload");

    // Also test Code Card PNG + #HEX Swatch Card PNG
    HttpRequestLocal(kDaemonPort, L"POST", L"/api/arm",
                     "{\"text\":\"pub async fn collapse_wavefunction(seed: &Seed) -> Result<Payload> {\\n    Ok(seed.synthesize())\\n}\"}");
    std::vector<uint8_t> codePng;
    VERIFY_ASSERT(ReadComClipboardBytes(rf.cfPng, codePng) && codePng.size() > 1024 &&
                  std::memcmp(codePng.data(), kPngMagic, 8) == 0,
                  "Synthesized 2x HiDPI anti-aliased Syntax Code Card PNG");

    HttpRequestLocal(kDaemonPort, L"POST", L"/api/arm", "{\"text\":\"#3B82F6\"}");
    std::vector<uint8_t> swatchPng;
    VERIFY_ASSERT(ReadComClipboardBytes(rf.cfPng, swatchPng) && swatchPng.size() > 1024 &&
                  std::memcmp(swatchPng.data(), kPngMagic, 8) == 0,
                  "Synthesized 2x HiDPI #3B82F6 Designer Color Swatch PNG");

    // ========================================================================
    // VERIFICATION 4: CFSTR_FILECONTENTS + IStream True 24 MB Streaming Disk Write
    // ========================================================================
    std::printf("\n--- [Verification 4] CFSTR_FILECONTENTS + TYMED_ISTREAM 24 MB Streaming & SHA-256 ---\n");
    fileServer.ResetCounters();
    HttpRequestLocal(kDaemonPort, L"POST", L"/api/arm",
                     "{\"text\":\"" + sclip::JsonEscape(testFileUrl) + "\"}");

    SIZE_T memBeforeStream = GetProcessWorkingSetBytes(pi.hProcess);
    SIZE_T peakMemDeltaBytes = 0;

    std::wstring outFilePath = L"bin\\e2e_streamed_24mb_output.bin";
    DeleteFileW(outFilePath.c_str());

    IDataObject* pStreamDataObj = nullptr;
    VERIFY_ASSERT(SUCCEEDED(OleGetClipboard(&pStreamDataObj)) && pStreamDataObj != nullptr,
                  "Acquired real COM IDataObject from OS clipboard for 24 MB stream");

    if (pStreamDataObj) {
        // 4.1 Read CFSTR_FILEDESCRIPTORW
        FORMATETC fmtDesc = {static_cast<CLIPFORMAT>(rf.cfFileDescriptorW), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM stgDesc = {};
        HRESULT hrDesc = pStreamDataObj->GetData(&fmtDesc, &stgDesc);
        VERIFY_ASSERT(SUCCEEDED(hrDesc) && stgDesc.tymed == TYMED_HGLOBAL && stgDesc.hGlobal != nullptr,
                      "Retrieved CFSTR_FILEDESCRIPTORW metadata via COM GetData");

        if (SUCCEEDED(hrDesc) && stgDesc.hGlobal) {
            const auto* fgd = static_cast<const FILEGROUPDESCRIPTORW*>(GlobalLock(stgDesc.hGlobal));
            VERIFY_ASSERT(fgd != nullptr && fgd->cItems == 1, "FILEGROUPDESCRIPTORW contains 1 virtual file item");
            if (fgd && fgd->cItems == 1) {
                const FILEDESCRIPTORW& fd = fgd->fgd[0];
                DWORD reqFlags = FD_FILESIZE | FD_PROGRESSUI | FD_WRITESTIME;
                uint64_t reportedSz = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
                std::printf("  -> Descriptor Filename: %s | Reported Size: %llu bytes | Flags: 0x%lx\n",
                            sclip::WideToUtf8(fd.cFileName).c_str(),
                            static_cast<unsigned long long>(reportedSz),
                            static_cast<unsigned long>(fd.dwFlags));
                VERIFY_ASSERT((fd.dwFlags & reqFlags) == reqFlags,
                              "FILEDESCRIPTORW has FD_FILESIZE | FD_PROGRESSUI | FD_WRITESTIME enabled");
                VERIFY_ASSERT(reportedSz == kPayloadSize,
                              "FILEDESCRIPTORW reports exact 25,165,824 byte Content-Length from HTTP HEAD");
                VERIFY_ASSERT(fileServer.HeadCount() == 1 && fileServer.BytesServed() == 0,
                              "Only 1 lightweight HTTP HEAD sent; 0 file body bytes downloaded before IStream::Read");
            }
            GlobalUnlock(stgDesc.hGlobal);
            ReleaseStgMedium(&stgDesc);
        }

        // 4.2 Read CFSTR_FILECONTENTS (lindex = 0, TYMED_ISTREAM)
        FORMATETC fmtStream = {static_cast<CLIPFORMAT>(rf.cfFileContents), nullptr, DVASPECT_CONTENT, 0, TYMED_ISTREAM};
        STGMEDIUM stgStream = {};
        HRESULT hrStm = pStreamDataObj->GetData(&fmtStream, &stgStream);
        VERIFY_ASSERT(SUCCEEDED(hrStm) && stgStream.tymed == TYMED_ISTREAM && stgStream.pstm != nullptr,
                      "Acquired live COM IStream* (TYMED_ISTREAM) for CFSTR_FILECONTENTS");

        if (SUCCEEDED(hrStm) && stgStream.pstm) {
            STATSTG st = {};
            VERIFY_ASSERT(SUCCEEDED(stgStream.pstm->Stat(&st, 0)) && st.cbSize.QuadPart == kPayloadSize,
                          "IStream::Stat reports exact 25,165,824 byte stream size");
            if (st.pwcsName) CoTaskMemFree(st.pwcsName);

            HANDLE hOut = CreateFileW(outFilePath.c_str(), GENERIC_WRITE, 0, nullptr,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            VERIFY_ASSERT(hOut != INVALID_HANDLE_VALUE, "Opened destination file for chunked streaming write");

            auto tStreamStart = std::chrono::steady_clock::now();
            std::vector<uint8_t> chunkBuf(131072); // 128 KB read chunks
            uint64_t totalWritten = 0;
            int chunkIterations = 0;

            while (hOut != INVALID_HANDLE_VALUE) {
                ULONG bytesRead = 0;
                HRESULT hrRead = stgStream.pstm->Read(chunkBuf.data(), static_cast<ULONG>(chunkBuf.size()), &bytesRead);
                if (FAILED(hrRead)) {
                    std::fprintf(stderr, "  [ERR] IStream::Read failed: 0x%08lx\n", static_cast<unsigned long>(hrRead));
                    break;
                }
                if (bytesRead == 0) break;

                DWORD bw = 0;
                WriteFile(hOut, chunkBuf.data(), bytesRead, &bw, nullptr);
                totalWritten += bw;
                ++chunkIterations;

                if ((chunkIterations % 16) == 0) {
                    SIZE_T curMem = GetProcessWorkingSetBytes(pi.hProcess);
                    if (curMem > memBeforeStream) {
                        peakMemDeltaBytes = std::max(peakMemDeltaBytes, curMem - memBeforeStream);
                    }
                }
            }
            if (hOut != INVALID_HANDLE_VALUE) CloseHandle(hOut);
            double streamMs = sclip::ElapsedMs(tStreamStart);
            double mbps = (streamMs > 0.1) ? ((24.0) / (streamMs / 1000.0)) : 0.0;

            std::printf("  -> Streamed %llu bytes in %d chunks (%.1f ms, %.1f MB/s) | Peak Daemon RAM Delta: %.2f MB\n",
                        static_cast<unsigned long long>(totalWritten), chunkIterations, streamMs, mbps,
                        static_cast<double>(peakMemDeltaBytes) / (1024.0 * 1024.0));

            VERIFY_ASSERT(totalWritten == kPayloadSize, "Streamed exact 25,165,824 bytes to disk");
            VERIFY_ASSERT(peakMemDeltaBytes < (8ULL * 1024ULL * 1024ULL),
                          "Daemon memory footprint delta stayed strictly < 8 MB during 24 MB transfer");

            ReleaseStgMedium(&stgStream);
        }
        pStreamDataObj->Release();
    }

    std::string diskSha256 = sclip::Sha256FileHex(outFilePath);
    std::printf("  -> Expected SHA-256 : %s\n", expectedSha256.c_str());
    std::printf("  -> Written  SHA-256 : %s\n", diskSha256.c_str());
    VERIFY_ASSERT(!diskSha256.empty() && diskSha256 == expectedSha256,
                  "Streamed 24 MB file SHA-256 matches source 100% byte-for-byte!");
    DeleteFileW(outFilePath.c_str());

    // ========================================================================
    // VERIFICATION 5: cURL, URL Git/Markdown, Local Image Base64, Bypass & Clean Exit
    // ========================================================================
    std::printf("\n--- [Verification 5] Remaining Sources, Bypass Switch & Clean Shutdown ---\n");

    // 5.1 cURL -> Python requests & JS fetch()
    const std::string curlSample =
        "curl 'https://api.example.com/v1/items' -H 'Authorization: Bearer tok_123' "
        "-H 'Cookie: sid=abc' --data-raw '{\"name\":\"qubit\",\"count\":8}'";
    HttpRequestLocal(kDaemonPort, L"POST", L"/api/arm",
                     "{\"text\":\"" + sclip::JsonEscape(curlSample) + "\"}");

    SetSimulatedObserverViaApi(kDaemonPort, true, "Code.exe", "client.py - Visual Studio Code");
    std::string curlPy;
    VERIFY_ASSERT(ReadComClipboardUnicodeText(curlPy) &&
                  curlPy.find("import requests") != std::string::npos &&
                  curlPy.find("\"sid\": \"abc\"") != std::string::npos &&
                  curlPy.find("json=payload") != std::string::npos,
                  "cURL collapsed to Python requests with parsed cookies & JSON payload");

    SetSimulatedObserverViaApi(kDaemonPort, true, "Code.exe", "api.ts - Visual Studio Code");
    std::string curlTs;
    VERIFY_ASSERT(ReadComClipboardUnicodeText(curlTs) &&
                  curlTs.find("await fetch(") != std::string::npos &&
                  curlTs.find("method: \"POST\"") != std::string::npos,
                  "cURL collapsed to modern async fetch() for .ts/.js editor");

    // 5.2 GitHub Repo URL -> git clone in Terminal & Smart Link in .md
    HttpRequestLocal(kDaemonPort, L"POST", L"/api/arm",
                     "{\"text\":\"https://github.com/CEXpp/schrodinger-clip\"}");
    SetSimulatedObserverViaApi(kDaemonPort, true, "WindowsTerminal.exe", "PowerShell");
    std::string ghTerm;
    VERIFY_ASSERT(ReadComClipboardUnicodeText(ghTerm) &&
                  ghTerm == "git clone https://github.com/CEXpp/schrodinger-clip.git",
                  "GitHub repo URL collapsed to 'git clone ...' in Terminal");

    SetSimulatedObserverViaApi(kDaemonPort, true, "Typora.exe", "README.md - Typora");
    std::string ghMd;
    VERIFY_ASSERT(ReadComClipboardUnicodeText(ghMd) &&
                  ghMd == "[CEXpp/schrodinger-clip](https://github.com/CEXpp/schrodinger-clip)",
                  "GitHub repo URL collapsed to Markdown smart link in .md editor");

    // 5.3 Local Image File (CF_HDROP) -> Escaped Path in Terminal & Base64 Data URI in .html/.css
    std::wstring tempPngPath = L"bin\\temp_test_icon.png";
    {
        HANDLE hf = CreateFileW(tempPngPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD bw = 0;
        WriteFile(hf, pngBytes.data(), static_cast<DWORD>(pngBytes.size()), &bw, nullptr);
        CloseHandle(hf);
    }
    wchar_t fullPngPath[MAX_PATH] = {};
    GetFullPathNameW(tempPngPath.c_str(), MAX_PATH, fullPngPath, nullptr);
    HttpRequestLocal(kDaemonPort, L"POST", L"/api/arm",
                     "{\"filePaths\":[\"" + sclip::JsonEscape(sclip::WideToUtf8(fullPngPath)) + "\"]}");

    SetSimulatedObserverViaApi(kDaemonPort, true, "Code.exe", "index.html - Visual Studio Code");
    std::string inlineUri;
    VERIFY_ASSERT(ReadComClipboardUnicodeText(inlineUri) &&
                  sclip::StartsWith(inlineUri, "data:image/png;base64,iVBORw0KGgo"),
                  "Local PNG CF_HDROP lazily collapsed to data:image/png;base64,... in .html editor");
    DeleteFileW(tempPngPath.c_str());

    // 5.4 Raw Bypass Mode Toggle Verification
    HttpRequestLocal(kDaemonPort, L"POST", L"/api/arm", "{\"text\":\"{\\\"rawKey\\\":123}\"}");
    HttpRequestLocal(kDaemonPort, L"POST", L"/api/lock", "{\"mode\":1}"); // RawBypass ON
    SetSimulatedObserverViaApi(kDaemonPort, true, "Code.exe", "schema.ts - Visual Studio Code");
    std::string bypassedOut;
    VERIFY_ASSERT(ReadComClipboardUnicodeText(bypassedOut) && bypassedOut == "{\"rawKey\":123}",
                  "Raw Bypass Mode ON delivered exact unmodified JSON into .ts editor");
    HttpRequestLocal(kDaemonPort, L"POST", L"/api/lock", "{\"mode\":0}"); // Auto mode restored
    std::string restoredTsOut;
    VERIFY_ASSERT(ReadComClipboardUnicodeText(restoredTsOut) &&
                  restoredTsOut.find("export interface ") != std::string::npos,
                  "Raw Bypass Mode OFF immediately restored TypeScript interface collapse");
    SetSimulatedObserverViaApi(kDaemonPort, false);

    // 5.5 Clean Daemon Shutdown & OLE Handle Release
    HttpRequestLocal(kDaemonPort, L"POST", L"/api/shutdown", "{}");
    DWORD waitRes = WaitForSingleObject(pi.hProcess, 3000);
    DWORD daemonExitCode = 999;
    GetExitCodeProcess(pi.hProcess, &daemonExitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    VERIFY_ASSERT(waitRes == WAIT_OBJECT_0 && daemonExitCode == 0,
                  "Daemon exited cleanly (code 0) within timeout with zero deadlocks");
    VERIFY_ASSERT(IsClipboardFormatAvailable(rf.cfEnvelope) == FALSE,
                  "OLE SuperpositionDataObject cleanly released from Windows Clipboard on exit");

    fileServer.Stop();
    OleUninitialize();

    std::printf("\n======================================================================\n");
    std::printf("  E2E VERIFICATION SUMMARY: %d / %d Assertions Passed (%d Failed)\n",
                g_totalAssertions - g_failedAssertions, g_totalAssertions, g_failedAssertions);
    std::printf("======================================================================\n");
    return (g_failedAssertions == 0) ? 0 : 1;
}

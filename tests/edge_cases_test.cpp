#include "../src/common.hpp"
#include "../src/observer.hpp"
#include "../src/classifier.hpp"
#include "../src/ole/superposition_dataobj.hpp"

namespace {

int g_passed = 0;
int g_failed = 0;

void Expect(bool cond, const char* expr, const char* msg, int line) {
    if (cond) {
        ++g_passed;
    } else {
        ++g_failed;
        std::fprintf(stderr, "  [EDGE FAIL] line %d: %s (%s)\n", line, msg, expr);
    }
}

#define EDGE_ASSERT(cond, msg) Expect((cond), #cond, (msg), __LINE__)

} // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    OleInitialize(nullptr);
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&gdiToken, &gsi, nullptr);

    std::printf("======================================================================\n");
    std::printf("  SchrodingerClip Adversarial Edge-Case & Boundary Unit Suite\n");
    std::printf("======================================================================\n");

    // 1. Empty & Whitespace Seeds
    {
        auto sEmpty = sclip::ClassifyTextSeed("", 1);
        EDGE_ASSERT(sEmpty.kind == sclip::SuperpositionKind::PlainText, "Empty string -> PlainText");
        EDGE_ASSERT(sEmpty.evaluatedBytesAtCopy == 0, "Empty string -> 0 evaluated bytes");

        auto sWs = sclip::ClassifyTextSeed("   \r\n\t  ", 2);
        EDGE_ASSERT(sWs.kind == sclip::SuperpositionKind::PlainText, "Whitespace-only -> PlainText");

        sclip::ValNode emptyObj, emptyArr;
        EDGE_ASSERT(sclip::JsonParser("{}").Parse(emptyObj) && emptyObj.kind == sclip::ValNode::Object, "Empty {} parses");
        EDGE_ASSERT(sclip::JsonParser("[]").Parse(emptyArr) && emptyArr.kind == sclip::ValNode::Array, "Empty [] parses");

        std::string tsEmpty = sclip::CollapseToTypeScript(emptyObj, "EmptyRoot");
        EDGE_ASSERT(tsEmpty.find("export interface EmptyRoot") != std::string::npos, "Empty {} -> valid TS interface");

        std::string pyEmpty = sclip::CollapseToPythonDataclass(emptyObj, "EmptyRoot");
        EDGE_ASSERT(pyEmpty.find("pass") != std::string::npos, "Empty {} -> Python dataclass with 'pass'");

        std::string sqlEmpty = sclip::CollapseToSql(emptyObj);
        EDGE_ASSERT(sqlEmpty == "-- Empty dataset", "Empty {} -> safe SQL comment");
    }

    // 2. Malformed JSON Graceful Fallback + UTF-8/Surrogate Pair + Language Keywords
    {
        auto badJsonSeed = sclip::ClassifyTextSeed("{\"broken\": [1, 2, }", 3);
        sclip::ValNode ast;
        EDGE_ASSERT(!sclip::ParseStructuredSeed(badJsonSeed, ast), "Truncated JSON cleanly rejected by AST parser");

        // Verify SuperpositionDataObject falls back to raw text instead of crashing on malformed JSON
        sclip::ObserverContextManager obsMgr;
        obsMgr.SetSimulatedObserver(true, "Code.exe", "broken.ts - VS Code");
        auto hub = std::make_shared<sclip::TelemetryHub>();
        auto* dobj = new sclip::SuperpositionDataObject(badJsonSeed, &obsMgr, hub);
        FORMATETC fmt = {CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM stg = {};
        EDGE_ASSERT(SUCCEEDED(dobj->GetData(&fmt, &stg)), "Malformed JSON gracefully falls back in GetData");
        if (stg.hGlobal) ReleaseStgMedium(&stg);
        dobj->Release();

        // Reserved keyword field names + UTF-16 surrogate pair (\uD83D\uDE80 = 🚀)
        const std::string trickyJson =
            "{\"class\":\"A\",\"type\":42,\"123num\":true,\"emoji\":\"\\uD83D\\uDE80\",\"用户名\":\"薛定谔\"}";
        sclip::ValNode trickyAst;
        EDGE_ASSERT(sclip::JsonParser(trickyJson).Parse(trickyAst), "UTF-8 + surrogate pair JSON parsed");
        std::string pyTricky = sclip::CollapseToPythonDataclass(trickyAst, "Tricky");
        EDGE_ASSERT(pyTricky.find("class_: str") != std::string::npos, "Python escapes reserved keyword 'class' -> 'class_'");
        std::string rsTricky = sclip::CollapseToRustStruct(trickyAst, "Tricky");
        EDGE_ASSERT(rsTricky.find("r#type") != std::string::npos, "Rust escapes reserved keyword 'type' -> 'r#type'");
        std::string tsTricky = sclip::CollapseToTypeScript(trickyAst, "Tricky");
        EDGE_ASSERT(tsTricky.find("\"123num\": boolean;") != std::string::npos, "TS quotes numeric-leading property key");
    }

    // 3. CSV Quoted Embedded Commas/Quotes + YAML Nested Structures
    {
        const std::string csvText = "id,title,notes\r\n1,\"Quantum, Core\",\"Says \"\"Hello\"\"\"\r\n2,Photon,Clean\r\n";
        auto csvSeed = sclip::ClassifyTextSeed(csvText, 4);
        EDGE_ASSERT(csvSeed.kind == sclip::SuperpositionKind::StructuredData &&
                    csvSeed.structuredSyntax == sclip::StructuredSyntax::Csv, "RFC4180 CSV classified");
        sclip::ValNode csvAst;
        EDGE_ASSERT(sclip::ParseStructuredSeed(csvSeed, csvAst) && csvAst.arrItems.size() == 2, "CSV rows parsed");
        std::string csvSql = sclip::CollapseToSql(csvAst);
        EDGE_ASSERT(csvSql.find("'Quantum, Core'") != std::string::npos, "CSV embedded comma preserved in SQL");

        const std::string yamlText =
            "service: schrodinger-clip\n"
            "port: 17890\n"
            "lazy_eval: true\n"
            "features:\n"
            "  - name: istream\n"
            "    zero_copy: true\n"
            "  - name: observer\n"
            "    zero_copy: true\n";
        auto yamlSeed = sclip::ClassifyTextSeed(yamlText, 5);
        EDGE_ASSERT(yamlSeed.kind == sclip::SuperpositionKind::StructuredData &&
                    yamlSeed.structuredSyntax == sclip::StructuredSyntax::Yaml, "YAML classified");
        sclip::ValNode yamlAst;
        EDGE_ASSERT(sclip::ParseStructuredSeed(yamlSeed, yamlAst), "YAML nested mapping + sequence parsed");
        std::string yamlTs = sclip::CollapseToTypeScript(yamlAst, "YamlConfig");
        EDGE_ASSERT(yamlTs.find("port: number;") != std::string::npos &&
                    yamlTs.find("features:") != std::string::npos, "YAML collapsed to TypeScript interface");
    }

    // 4. Dual-Layer Observer Window Title Micro-Context & Browser Web-Apps & Sniffer Blacklist
    {
        EDGE_ASSERT(sclip::IsBackgroundSnifferProcess("TextInputHost.exe"), "TextInputHost.exe blacklisted");
        EDGE_ASSERT(sclip::IsBackgroundSnifferProcess("SVCHOST.EXE"), "SVCHOST.EXE blacklisted (case-insensitive)");
        EDGE_ASSERT(!sclip::IsBackgroundSnifferProcess("Code.exe"), "Code.exe allowed");
        EDGE_ASSERT(!sclip::IsBackgroundSnifferProcess("explorer.exe"), "explorer.exe allowed");

        sclip::ObserverSnapshot s1;
        s1.processName = "chrome.exe";
        s1.windowTitle = "2026 Q4 预算表 - 飞书云文档 - Google Chrome";
        sclip::ClassifyObserverContext(s1);
        EDGE_ASSERT(s1.category == sclip::HostCategory::Spreadsheet, "Feishu Cloud Sheet in Chrome -> Spreadsheet");

        sclip::ObserverSnapshot s2;
        s2.processName = "msedge.exe";
        s2.windowTitle = "System Diagram - Excalidraw - Microsoft Edge";
        sclip::ClassifyObserverContext(s2);
        EDGE_ASSERT(s2.category == sclip::HostCategory::VisualCanvasOrChat, "Excalidraw in Edge -> VisualCanvas");

        sclip::ObserverSnapshot s3;
        s3.processName = "Cursor.exe";
        s3.windowTitle = "• api_handler.go — backend - Cursor";
        sclip::ClassifyObserverContext(s3);
        EDGE_ASSERT(s3.lang == sclip::MicroLangContext::Go && s3.detectedFile == "api_handler.go",
                    "Bullet dirty marker stripped & .go extracted");
    }

    // 5. COM IDataObject, IEnumFORMATETC & IStream Boundary Error Paths
    {
        auto urlSeed = sclip::ClassifyTextSeed("http://127.0.0.1:59999/unreachable_file.bin", 6);
        sclip::ObserverContextManager obsMgr;
        // Pin File Explorer deterministically so headless CI terminals (pwsh.exe) never mask COM contract checks
        obsMgr.SetSimulatedObserver(true, "explorer.exe", "Downloads");
        auto hub = std::make_shared<sclip::TelemetryHub>();
        auto* dobj = new sclip::SuperpositionDataObject(urlSeed, &obsMgr, hub);

        EDGE_ASSERT(dobj->GetData(nullptr, nullptr) == E_INVALIDARG, "GetData(nullptr) -> E_INVALIDARG");

        FORMATETC badAspect = {CF_UNICODETEXT, nullptr, DVASPECT_ICON, -1, TYMED_HGLOBAL};
        STGMEDIUM stg = {};
        EDGE_ASSERT(dobj->GetData(&badAspect, &stg) == DV_E_DVASPECT, "Wrong dwAspect -> DV_E_DVASPECT");

        FORMATETC badLindexText = {CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, 0, TYMED_HGLOBAL};
        EDGE_ASSERT(dobj->QueryGetData(&badLindexText) == DV_E_LINDEX, "lindex=0 on CF_UNICODETEXT -> DV_E_LINDEX");

        const auto& rf = sclip::RegisteredFormats::Instance();
        FORMATETC goodLindexFc = {static_cast<CLIPFORMAT>(rf.cfFileContents), nullptr, DVASPECT_CONTENT, 0, TYMED_ISTREAM};
        EDGE_ASSERT(dobj->QueryGetData(&goodLindexFc) == S_OK, "lindex=0 on CFSTR_FILECONTENTS (Explorer) -> S_OK");

        FORMATETC badLindexFc = {static_cast<CLIPFORMAT>(rf.cfFileContents), nullptr, DVASPECT_CONTENT, 5, TYMED_ISTREAM};
        EDGE_ASSERT(dobj->QueryGetData(&badLindexFc) == DV_E_LINDEX, "lindex=5 on CFSTR_FILECONTENTS -> DV_E_LINDEX");

        // Verify observer gate blocks virtual file stream in Terminal hosts (pwsh.exe)
        obsMgr.SetSimulatedObserver(true, "pwsh.exe", "PowerShell");
        EDGE_ASSERT(dobj->QueryGetData(&goodLindexFc) == DV_E_FORMATETC,
                    "CFSTR_FILECONTENTS gated off for Terminal host (pwsh.exe)");

        // Verify QueryGetData NEVER performs a blocking network HEAD probe on binary URLs
        obsMgr.SetSimulatedObserver(true, "WeChat.exe", "Chat");
        FORMATETC pngFmt = {static_cast<CLIPFORMAT>(rf.cfPng), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        auto tProbeStart = std::chrono::steady_clock::now();
        HRESULT hrPngQuery = dobj->QueryGetData(&pngFmt);
        double probeMs = sclip::ElapsedMs(tProbeStart);
        EDGE_ASSERT(hrPngQuery == DV_E_FORMATETC && probeMs < 200.0,
                    "QueryGetData(PNG) rejects .bin URL immediately with zero network I/O");

        // Restore Explorer observer to verify unreachable HTTP stream returns STG_E_READFAULT cleanly
        obsMgr.SetSimulatedObserver(true, "explorer.exe", "Downloads");
        HRESULT hrStreamGet = dobj->GetData(&goodLindexFc, &stg);
        EDGE_ASSERT(SUCCEEDED(hrStreamGet) && stg.pstm != nullptr,
                    "GetData(CFSTR_FILECONTENTS) returns live IStream under Explorer observer");
        if (SUCCEEDED(hrStreamGet) && stg.pstm) {
            uint8_t buf[64];
            ULONG got = 99;
            EDGE_ASSERT(stg.pstm->Read(nullptr, 64, &got) == STG_E_INVALIDPOINTER, "Read(nullptr) -> STG_E_INVALIDPOINTER");
            HRESULT hrUnreach = stg.pstm->Read(buf, sizeof(buf), &got);
            EDGE_ASSERT(hrUnreach == STG_E_READFAULT && got == 0, "Unreachable HTTP server -> STG_E_READFAULT & 0 bytes");
            ReleaseStgMedium(&stg);
        }

        // IEnumFORMATETC contract checks
        IEnumFORMATETC* pEnum = nullptr;
        EDGE_ASSERT(SUCCEEDED(dobj->EnumFormatEtc(DATADIR_GET, &pEnum)) && pEnum != nullptr, "EnumFormatEtc(DATADIR_GET)");
        if (pEnum) {
            FORMATETC arr[2];
            EDGE_ASSERT(pEnum->Next(2, arr, nullptr) == E_INVALIDARG, "Next(celt>1, nullptr) -> E_INVALIDARG");
            EDGE_ASSERT(pEnum->Skip(999) == S_FALSE, "Skip past end -> S_FALSE");
            EDGE_ASSERT(pEnum->Reset() == S_OK, "Reset -> S_OK");
            IEnumFORMATETC* pClone = nullptr;
            EDGE_ASSERT(SUCCEEDED(pEnum->Clone(&pClone)) && pClone != nullptr, "Clone -> S_OK");
            if (pClone) pClone->Release();
            pEnum->Release();
        }
        dobj->Release();

        // SynthesizedMemoryStream Seek / Clone / Stat checks
        FILETIME ft = {};
        GetSystemTimeAsFileTime(&ft);
        auto* memStm = new sclip::SynthesizedMemoryStream({'A', 'B', 'C', 'D', 'E'}, L"test.txt", ft);
        LARGE_INTEGER neg = {};
        neg.QuadPart = -5;
        EDGE_ASSERT(memStm->Seek(neg, STREAM_SEEK_SET, nullptr) == STG_E_INVALIDFUNCTION, "Negative seek -> STG_E_INVALIDFUNCTION");
        LARGE_INTEGER off2 = {};
        off2.QuadPart = 2;
        ULARGE_INTEGER newPos = {};
        EDGE_ASSERT(SUCCEEDED(memStm->Seek(off2, STREAM_SEEK_SET, &newPos)) && newPos.QuadPart == 2, "Seek(2) -> pos 2");
        char ch = 0;
        ULONG r = 0;
        memStm->Read(&ch, 1, &r);
        EDGE_ASSERT(r == 1 && ch == 'C', "Read after Seek(2) -> 'C'");
        STATSTG stNoName = {};
        EDGE_ASSERT(SUCCEEDED(memStm->Stat(&stNoName, STATFLAG_NONAME)) && stNoName.pwcsName == nullptr,
                    "Stat(STATFLAG_NONAME) leaves pwcsName == nullptr");
        memStm->Release();
    }

    // 6. Windows CMD Copy-as-cURL (^ continuations) + Bash $'...' + Mermaid + #RGB + Unix Epoch
    {
        const std::string cmdCurl =
            "curl \"https://example.org/v1/login\" ^\r\n"
            "  -H \"Content-Type: application/json\" ^\r\n"
            "  -u \"alice:wonderland\" ^\r\n"
            "  --data-raw \"{\\\"otp\\\":9988}\"";
        auto parsedCmd = sclip::ParseCurlCommand(cmdCurl);
        EDGE_ASSERT(parsedCmd.method == "POST" && parsedCmd.basicUser == "alice" && parsedCmd.isJsonBody,
                    "Windows CMD caret-escaped Copy-as-cURL parsed accurately");

        const std::string mermaidCode = "graph TD\n  A[Start] --> B{Validate}\n  B -->|OK| C[Collapse]";
        auto mBundle = sclip::RenderMermaidBitmap2x(mermaidCode);
        EDGE_ASSERT(mBundle.valid && mBundle.width >= 720 && mBundle.height >= 420,
                    "Mermaid diagram rendered to valid 2x HiDPI PNG & DIB");

        const std::string latexCode = "$$i\\hbar \\frac{\\partial \\Psi}{\\partial t} = -\\frac{\\hbar^2}{2m}\\nabla^2 \\Psi + V(x)\\Psi$$";
        auto latexSeed = sclip::ClassifyTextSeed(latexCode, 7);
        EDGE_ASSERT(latexSeed.kind == sclip::SuperpositionKind::VisualSource &&
                    latexSeed.visualSyntax == sclip::VisualSyntax::MathLatex,
                    "Schrodinger equation LaTeX classified as MathLatex visual source");
        auto mathBundle = sclip::RenderMathFormulaBitmap2x(latexCode);
        EDGE_ASSERT(mathBundle.valid && mathBundle.width >= 780 && !mathBundle.pngBytes.empty(),
                    "LaTeX formula rendered to valid 2x HiDPI PNG & DIB");

        const std::string arcSvg =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 200 200\" width=\"200\" height=\"200\">"
            "<path d=\"M 20 100 A 80 50 0 1 1 180 100 Z\" fill=\"#38BDF8\" stroke=\"#A855F7\" stroke-width=\"3\"/>"
            "</svg>";
        auto arcBundle = sclip::RenderSvgBitmap2x(arcSvg);
        EDGE_ASSERT(arcBundle.valid && arcBundle.width == 512 && arcBundle.height == 512 && arcBundle.pngBytes.size() > 512,
                    "SVG elliptical arc (A command) rasterized accurately at HiDPI >= 512px");

        auto shortHex = sclip::ParseHexColorDetails("#0AF");
        EDGE_ASSERT(shortHex.normalizedHex == "#00AAFF", "3-digit #0AF expanded to #00AAFF");

        sclip::ObserverSnapshot pyObs;
        pyObs.lang = sclip::MicroLangContext::Python;
        std::string pyEpoch = sclip::CollapseUnixTimestamp("1731912000000", pyObs);
        EDGE_ASSERT(pyEpoch.find("datetime.fromtimestamp(1731912000") != std::string::npos,
                    "13-digit ms epoch normalized cleanly for Python datetime");
    }

    Gdiplus::GdiplusShutdown(gdiToken);
    OleUninitialize();

    std::printf("  Edge-Case Results: %d Passed, %d Failed\n", g_passed, g_failed);
    std::printf("======================================================================\n");
    return (g_failed == 0) ? 0 : 1;
}

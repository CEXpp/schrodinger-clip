#pragma once
#include "common.hpp"
#include "observer.hpp"

namespace sclip {

enum class SuperpositionKind {
    PlainText = 0,
    RemoteUrl,
    StructuredData,
    VisualSource,
    CurlCommand,
    LocalFiles,
    HexColor,
    UnixTimestamp
};

enum class StructuredSyntax {
    None = 0,
    Json,
    Csv,
    Yaml
};

enum class VisualSyntax {
    None = 0,
    Svg,
    Mermaid,
    MathLatex,
    CodeSnippet
};

struct OrbitDescriptor {
    std::string id;
    std::string label;
    std::string formatBadge;
    bool armed = false;
};

struct QuantumSeed {
    uint64_t envelopeId = 0;
    std::string armedAt;
    SuperpositionKind kind = SuperpositionKind::PlainText;
    StructuredSyntax structuredSyntax = StructuredSyntax::None;
    VisualSyntax visualSyntax = VisualSyntax::None;

    std::string rawText;                 // UTF-8 seed text (empty if pure CF_HDROP unless path)
    std::vector<std::wstring> filePaths; // Non-empty when armed from CF_HDROP
    bool isGitHubRepoUrl = false;
    bool looksLikeImageUrl = false;
    bool isLocalImageFile = false;
    size_t seedRawBytes = 0;
    size_t evaluatedBytesAtCopy = 0;     // Strictly 0 Bytes at Ctrl+C!
};

inline const char* SuperpositionKindName(SuperpositionKind k) {
    switch (k) {
        case SuperpositionKind::RemoteUrl: return "REMOTE_LINK_WAVEFUNCTION";
        case SuperpositionKind::StructuredData: return "STRUCTURED_DATA_TENSOR";
        case SuperpositionKind::VisualSource: return "VISUAL_SOURCE_MANIFOLD";
        case SuperpositionKind::CurlCommand: return "NETWORK_CURL_ENVELOPE";
        case SuperpositionKind::LocalFiles: return "LOCAL_FILE_SUPERPOSITION";
        case SuperpositionKind::HexColor: return "CHROMATIC_HEX_SEED";
        case SuperpositionKind::UnixTimestamp: return "TEMPORAL_EPOCH_SEED";
        default: return "RAW_TEXT_EIGENSTATE";
    }
}

inline const char* SuperpositionKindBadgeZh(const QuantumSeed& seed) {
    switch (seed.kind) {
        case SuperpositionKind::RemoteUrl:
            return seed.isGitHubRepoUrl ? "远程 GitHub 仓库叠加态" :
                   (seed.looksLikeImageUrl ? "远程图像直链叠加态" : "远程文件/URL 流式叠加态");
        case SuperpositionKind::StructuredData:
            if (seed.structuredSyntax == StructuredSyntax::Json) return "结构化 JSON 叠加态";
            if (seed.structuredSyntax == StructuredSyntax::Csv) return "结构化 CSV 叠加态";
            return "结构化 YAML 叠加态";
        case SuperpositionKind::VisualSource:
            if (seed.visualSyntax == VisualSyntax::Svg) return "SVG 矢量源码叠加态";
            if (seed.visualSyntax == VisualSyntax::Mermaid) return "Mermaid 图表源码叠加态";
            if (seed.visualSyntax == VisualSyntax::MathLatex) return "数学公式源码叠加态";
            return "代码片段视觉叠加态";
        case SuperpositionKind::CurlCommand:
            return "cURL 网络请求叠加态";
        case SuperpositionKind::LocalFiles:
            return seed.isLocalImageFile ? "本地图像文件叠加态 (CF_HDROP)" : "本地文件叠加态 (CF_HDROP)";
        case SuperpositionKind::HexColor:
            return "色彩 #HEX 微工具叠加态";
        case SuperpositionKind::UnixTimestamp:
            return "Unix 时间戳微工具叠加态";
        default:
            return "基础文本量子态";
    }
}

inline bool IsHexColorString(const std::string& t) {
    if (t.size() != 4 && t.size() != 7 && t.size() != 9) return false;
    if (t[0] != '#') return false;
    for (size_t i = 1; i < t.size(); ++i) {
        char c = t[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!ok) return false;
    }
    return true;
}

inline bool IsUnixTimestampString(const std::string& t) {
    if (t.size() != 10 && t.size() != 13) return false;
    for (char c : t) {
        if (c < '0' || c > '9') return false;
    }
    long long val = std::stoll(t);
    if (t.size() == 10) {
        return val >= 946684800LL && val <= 4102444800LL; // 2000-01-01 .. 2100-01-01
    }
    return val >= 946684800000LL && val <= 4102444800000LL;
}

inline bool IsSingleHttpUrl(const std::string& t, bool& isGitHubRepo, bool& looksLikeImage) {
    isGitHubRepo = false;
    looksLikeImage = false;
    if (!StartsWithIgnoreCase(t, "http://") && !StartsWithIgnoreCase(t, "https://")) return false;
    for (char c : t) {
        if (static_cast<unsigned char>(c) <= 0x20) return false;
    }
    std::string low = ToLowerAscii(t);
    // Strip query/fragment for extension check
    size_t qpos = low.find_first_of("?#");
    std::string pathPart = (qpos == std::string::npos) ? low : low.substr(0, qpos);

    if (EndsWith(pathPart, ".png") || EndsWith(pathPart, ".jpg") || EndsWith(pathPart, ".jpeg") ||
        EndsWith(pathPart, ".gif") || EndsWith(pathPart, ".webp") || EndsWith(pathPart, ".bmp") ||
        EndsWith(pathPart, ".ico") || EndsWith(pathPart, ".svg") ||
        low.find("format=png") != std::string::npos || low.find("format=jpg") != std::string::npos) {
        looksLikeImage = true;
    }

    // Check GitHub repository root URL: https://github.com/<owner>/<repo>[.git][/]
    if (StartsWith(pathPart, "https://github.com/") || StartsWith(pathPart, "http://github.com/")) {
        size_t schemeEnd = pathPart.find("github.com/") + 11;
        std::string rest = pathPart.substr(schemeEnd);
        while (!rest.empty() && rest.back() == '/') rest.pop_back();
        size_t firstSlash = rest.find('/');
        if (firstSlash != std::string::npos && firstSlash > 0 && firstSlash + 1 < rest.size()) {
            std::string afterOwner = rest.substr(firstSlash + 1);
            if (afterOwner.find('/') == std::string::npos) {
                isGitHubRepo = true;
            }
        }
    }
    return true;
}

inline bool LooksLikeCurlCommand(const std::string& t) {
    if (StartsWithIgnoreCase(t, "curl ") || StartsWithIgnoreCase(t, "curl\t") ||
        StartsWithIgnoreCase(t, "curl.exe ")) {
        return t.find("http://") != std::string::npos || t.find("https://") != std::string::npos;
    }
    return false;
}

inline bool LooksLikeJsonPayload(const std::string& t) {
    if (t.size() < 2) return false;
    if (!((t.front() == '{' && t.back() == '}') || (t.front() == '[' && t.back() == ']'))) return false;
    // Ensure balanced top-level quotes/colons/commas heuristic
    return t.find(':') != std::string::npos || (t.front() == '[' && t.find(',') != std::string::npos);
}

inline bool LooksLikeSvgPayload(const std::string& t) {
    std::string low = ToLowerAscii(t.substr(0, std::min<size_t>(t.size(), 512)));
    return (StartsWith(low, "<svg") || (StartsWith(low, "<?xml") && low.find("<svg") != std::string::npos)) &&
           ToLowerAscii(t).find("</svg>") != std::string::npos;
}

inline bool LooksLikeMermaidPayload(const std::string& t) {
    std::vector<std::string> lines = SplitLines(t);
    if (lines.empty()) return false;
    std::string first = Trim(lines[0]);
    return StartsWith(first, "graph ") || StartsWith(first, "flowchart ") ||
           StartsWith(first, "sequenceDiagram") || StartsWith(first, "classDiagram") ||
           StartsWith(first, "stateDiagram") || StartsWith(first, "erDiagram") ||
           StartsWith(first, "gantt") || StartsWith(first, "pie");
}

inline bool LooksLikeMathLatex(const std::string& t) {
    if ((StartsWith(t, "$$") && EndsWith(t, "$$") && t.size() > 4) ||
        StartsWith(t, "\\begin{equation") || StartsWith(t, "\\begin{align")) {
        return true;
    }
    return (t.find("\\frac{") != std::string::npos || t.find("\\sum_") != std::string::npos ||
            t.find("\\int_") != std::string::npos || t.find("\\sqrt{") != std::string::npos);
}

inline bool LooksLikeCsvPayload(const std::string& t) {
    auto lines = SplitLines(t);
    if (lines.size() < 2) return false;
    // Reject code blocks with braces or semicolons
    if (t.find('{') != std::string::npos || t.find(';') != std::string::npos) return false;
    auto countDelim = [](const std::string& line, char delim) {
        int c = 0;
        bool inQuotes = false;
        for (char ch : line) {
            if (ch == '"') inQuotes = !inQuotes;
            else if (ch == delim && !inQuotes) ++c;
        }
        return c;
    };
    for (char delim : {',', '\t'}) {
        int c0 = countDelim(lines[0], delim);
        if (c0 >= 1) {
            int matched = 0;
            int checked = 0;
            for (size_t i = 1; i < std::min<size_t>(lines.size(), 8); ++i) {
                if (Trim(lines[i]).empty()) continue;
                ++checked;
                if (countDelim(lines[i], delim) == c0) ++matched;
            }
            if (checked >= 1 && matched == checked) return true;
        }
    }
    return false;
}

inline bool LooksLikeYamlPayload(const std::string& t) {
    auto lines = SplitLines(t);
    if (lines.size() < 2) return false;
    if (t.find('{') != std::string::npos || t.find(';') != std::string::npos) return false;
    int kvLines = 0;
    for (size_t i = 0; i < std::min<size_t>(lines.size(), 12); ++i) {
        std::string ln = Trim(lines[i]);
        if (ln.empty() || ln[0] == '#' || ln == "---") continue;
        if (StartsWith(ln, "- ")) {
            ++kvLines;
            continue;
        }
        size_t colon = ln.find(':');
        if (colon != std::string::npos && colon > 0 &&
            (colon + 1 == ln.size() || ln[colon + 1] == ' ')) {
            // Key should be identifier-like
            bool keyOk = true;
            for (size_t k = 0; k < colon; ++k) {
                char ch = ln[k];
                if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                      (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) {
                    keyOk = false;
                    break;
                }
            }
            if (keyOk) ++kvLines;
        }
    }
    return kvLines >= 2;
}

inline bool LooksLikeCodeSnippet(const std::string& t) {
    static const char* kMarkers[] = {
        "function ", "const ", "let ", "export ", "import ", "interface ",
        "def ", "class ", "async ", "await ", "pub fn ", "fn ", "pub struct ",
        "#include", "std::", "package ", "func ", "SELECT ", "CREATE TABLE",
        "public class ", "private ", "return ", "=>"
    };
    int hits = 0;
    for (const char* m : kMarkers) {
        if (t.find(m) != std::string::npos) ++hits;
    }
    if (hits >= 2) return true;
    if (hits >= 1 && (t.find('\n') != std::string::npos || t.find('{') != std::string::npos)) return true;
    return false;
}

inline QuantumSeed ClassifyTextSeed(const std::string& rawUtf8, uint64_t envelopeId) {
    QuantumSeed seed;
    seed.envelopeId = envelopeId;
    seed.armedAt = NowTimestampString();
    seed.rawText = rawUtf8;
    seed.seedRawBytes = rawUtf8.size();
    seed.evaluatedBytesAtCopy = 0; // Zero evaluation at Ctrl+C!

    std::string trimmed = Trim(rawUtf8);
    if (trimmed.empty()) {
        seed.kind = SuperpositionKind::PlainText;
        return seed;
    }

    if (IsHexColorString(trimmed)) {
        seed.kind = SuperpositionKind::HexColor;
        return seed;
    }

    if (IsUnixTimestampString(trimmed)) {
        seed.kind = SuperpositionKind::UnixTimestamp;
        return seed;
    }

    bool isGh = false, isImg = false;
    if (IsSingleHttpUrl(trimmed, isGh, isImg)) {
        seed.kind = SuperpositionKind::RemoteUrl;
        seed.isGitHubRepoUrl = isGh;
        seed.looksLikeImageUrl = isImg;
        return seed;
    }

    if (LooksLikeCurlCommand(trimmed)) {
        seed.kind = SuperpositionKind::CurlCommand;
        return seed;
    }

    if (LooksLikeSvgPayload(trimmed)) {
        seed.kind = SuperpositionKind::VisualSource;
        seed.visualSyntax = VisualSyntax::Svg;
        return seed;
    }

    if (LooksLikeMermaidPayload(trimmed)) {
        seed.kind = SuperpositionKind::VisualSource;
        seed.visualSyntax = VisualSyntax::Mermaid;
        return seed;
    }

    if (LooksLikeMathLatex(trimmed)) {
        seed.kind = SuperpositionKind::VisualSource;
        seed.visualSyntax = VisualSyntax::MathLatex;
        return seed;
    }

    if (LooksLikeJsonPayload(trimmed)) {
        seed.kind = SuperpositionKind::StructuredData;
        seed.structuredSyntax = StructuredSyntax::Json;
        return seed;
    }

    if (LooksLikeCsvPayload(trimmed)) {
        seed.kind = SuperpositionKind::StructuredData;
        seed.structuredSyntax = StructuredSyntax::Csv;
        return seed;
    }

    if (LooksLikeYamlPayload(trimmed)) {
        seed.kind = SuperpositionKind::StructuredData;
        seed.structuredSyntax = StructuredSyntax::Yaml;
        return seed;
    }

    if (LooksLikeCodeSnippet(trimmed)) {
        seed.kind = SuperpositionKind::VisualSource;
        seed.visualSyntax = VisualSyntax::CodeSnippet;
        return seed;
    }

    seed.kind = SuperpositionKind::PlainText;
    return seed;
}

inline QuantumSeed ClassifyFileDropSeed(const std::vector<std::wstring>& paths, uint64_t envelopeId) {
    QuantumSeed seed;
    seed.envelopeId = envelopeId;
    seed.armedAt = NowTimestampString();
    seed.kind = SuperpositionKind::LocalFiles;
    seed.filePaths = paths;
    seed.evaluatedBytesAtCopy = 0; // Zero bytes read from image/file at Ctrl+C!

    if (!paths.empty()) {
        seed.rawText = WideToUtf8(paths[0]);
        seed.seedRawBytes = seed.rawText.size();
        std::wstring low = ToLowerWide(paths[0]);
        if (EndsWithIgnoreCase(WideToUtf8(low), ".png") || EndsWithIgnoreCase(WideToUtf8(low), ".jpg") ||
            EndsWithIgnoreCase(WideToUtf8(low), ".jpeg") || EndsWithIgnoreCase(WideToUtf8(low), ".gif") ||
            EndsWithIgnoreCase(WideToUtf8(low), ".webp") || EndsWithIgnoreCase(WideToUtf8(low), ".bmp") ||
            EndsWithIgnoreCase(WideToUtf8(low), ".svg") || EndsWithIgnoreCase(WideToUtf8(low), ".ico")) {
            seed.isLocalImageFile = true;
        }
    }
    return seed;
}

// Predict exact collapse outcome description for the Live Target Crosshair Radar
inline std::string PredictCollapseTargetLabel(const QuantumSeed& seed, const ObserverSnapshot& obs) {
    if (obs.lockMode == ManualLockMode::RawBypass) {
        return "Raw Passthrough (反坍缩原味直通)";
    }

    switch (seed.kind) {
        case SuperpositionKind::RemoteUrl:
            if (obs.category == HostCategory::FileExplorer) {
                return "IStream Virtual File Download (原生流式落盘)";
            }
            if (obs.category == HostCategory::Terminal) {
                return seed.isGitHubRepoUrl ? "git clone <repo>" : "curl -fLO -C - \"<url>\"";
            }
            if (obs.lang == MicroLangContext::Markdown) {
                return "Markdown Smart Link [Title](URL)";
            }
            if (obs.category == HostCategory::VisualCanvasOrChat && seed.looksLikeImageUrl) {
                return "Decoded High-DPI Bitmap (PNG / CF_DIBV5)";
            }
            return "Original URL Text";

        case SuperpositionKind::StructuredData:
            if (obs.category == HostCategory::FileExplorer) {
                return "Styled Excel Workbook (.xlsx IStream)";
            }
            if (obs.category == HostCategory::Spreadsheet) {
                return "Multi-Column Spreadsheet Grid (CF_HTML + TSV/CSV)";
            }
            if (obs.lang == MicroLangContext::TypeScript || obs.lang == MicroLangContext::JavaScript) {
                return "TypeScript Interface Hierarchy";
            }
            if (obs.lang == MicroLangContext::Python) {
                return "Python @dataclass Models";
            }
            if (obs.lang == MicroLangContext::Go) {
                return "Go Struct + JSON Tags";
            }
            if (obs.lang == MicroLangContext::Rust) {
                return "Rust Serde Struct";
            }
            if (obs.lang == MicroLangContext::SQL || obs.category == HostCategory::DatabaseClient) {
                return "SQL CREATE TABLE + Batch INSERT";
            }
            if (obs.lang == MicroLangContext::Markdown) {
                return "Markdown Pipe Table";
            }
            return "Raw Structured Text";

        case SuperpositionKind::VisualSource:
            if (obs.category == HostCategory::FileExplorer) {
                return (seed.visualSyntax == VisualSyntax::Svg)
                           ? "Vector Graphic File (.svg IStream)"
                           : "2x HiDPI Visual Card (.png IStream)";
            }
            if (obs.category == HostCategory::VisualCanvasOrChat) {
                return (seed.visualSyntax == VisualSyntax::Svg)
                           ? "2x Rasterized Transparent PNG (SVG->Bitmap)"
                           : "2x Anti-Aliased Syntax Card PNG";
            }
            if (obs.category == HostCategory::RichDocument) {
                return "Syntax-Highlighted CF_HTML Block";
            }
            return "Pure Source Code";

        case SuperpositionKind::CurlCommand:
            if (obs.category == HostCategory::FileExplorer || obs.lang == MicroLangContext::HttpRest) {
                return "REST Client Request File (.http IStream)";
            }
            if (obs.lang == MicroLangContext::Python) {
                return "Python requests Call";
            }
            if (obs.lang == MicroLangContext::TypeScript || obs.lang == MicroLangContext::JavaScript) {
                return "Modern Async fetch() Code";
            }
            if (obs.category == HostCategory::VisualCanvasOrChat) {
                return "2x API Request Card PNG";
            }
            return "Executable cURL Command";

        case SuperpositionKind::LocalFiles:
            if (obs.category == HostCategory::FileExplorer) {
                return "Native File Copy (CF_HDROP)";
            }
            if (obs.category == HostCategory::Terminal) {
                return "Shell-Escaped Absolute File Path";
            }
            if ((obs.lang == MicroLangContext::HtmlCss || obs.lang == MicroLangContext::Markdown) && seed.isLocalImageFile) {
                return "Inline Base64 Data URI (data:image/...;base64)";
            }
            return "File Path Text";

        case SuperpositionKind::HexColor:
            if (obs.category == HostCategory::FileExplorer) {
                return "Color Swatch Card (.png IStream)";
            }
            if (obs.category == HostCategory::VisualCanvasOrChat) {
                return "2x HEX/RGB/HSL Swatch Card PNG";
            }
            return "Original #HEX Literal";

        case SuperpositionKind::UnixTimestamp:
            if (obs.lang == MicroLangContext::TypeScript || obs.lang == MicroLangContext::JavaScript) {
                return "JS Date Construction + ISO Comment";
            }
            if (obs.lang == MicroLangContext::Python) {
                return "Python timezone-aware datetime";
            }
            if (obs.lang == MicroLangContext::SQL || obs.category == HostCategory::DatabaseClient) {
                return "SQL TIMESTAMP Literal";
            }
            return "ISO-8601 Local + UTC Human Timestamp";

        default:
            return "UTF-16 Native Text";
    }
}

// Enumerate the 10 quantum orbits displayed in the Superposition Wavefunction Graph
inline std::vector<OrbitDescriptor> BuildWavefunctionOrbits(const QuantumSeed& seed) {
    bool isUrl = (seed.kind == SuperpositionKind::RemoteUrl);
    bool isStruct = (seed.kind == SuperpositionKind::StructuredData);
    bool isVis = (seed.kind == SuperpositionKind::VisualSource);
    bool isCurl = (seed.kind == SuperpositionKind::CurlCommand);
    bool isLocal = (seed.kind == SuperpositionKind::LocalFiles);
    bool isHex = (seed.kind == SuperpositionKind::HexColor);
    bool isTs = (seed.kind == SuperpositionKind::UnixTimestamp);

    return {
        {"istream_file", "IStream Virtual File", "CFSTR_FILECONTENTS",
         isUrl || isStruct || isVis || isCurl || isHex},
        {"png_card", "2x HiDPI PNG / Bitmap", "PNG + CF_DIBV5",
         isVis || isHex || (isUrl && seed.looksLikeImageUrl) || isCurl},
        {"excel_grid", "Excel / Sheets Grid", "CF_HTML + CSV + .xlsx",
         isStruct},
        {"ts_interface", "TypeScript Interface / fetch()", "CF_UNICODETEXT [.ts]",
         isStruct || isCurl || isTs},
        {"py_model", "Python @dataclass / requests", "CF_UNICODETEXT [.py]",
         isStruct || isCurl || isTs},
        {"go_rust", "Go & Rust Serde Structs", "CF_UNICODETEXT [.go/.rs]",
         isStruct},
        {"sql_schema", "SQL DDL + Batch INSERT", "CF_UNICODETEXT [.sql]",
         isStruct || isTs},
        {"shell_cmd", "Shell Command / Escaped Path", "Terminal Context",
         isUrl || isCurl || isLocal},
        {"rich_doc", "Highlighted CF_HTML / Markdown", "CF_HTML + [.md]",
         isVis || isUrl || isStruct},
        {"base64_inline", "Base64 Data URI / Swatch", "[.html/.css] + #HEX",
         (isLocal && seed.isLocalImageFile) || isHex}
    };
}

} // namespace sclip

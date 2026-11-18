#pragma once
#include "../common.hpp"
#include "local_transform.hpp"

namespace sclip {

struct RenderedImageBundle {
    bool valid = false;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pngBytes;
    std::vector<uint8_t> dibV5Bytes;
    std::vector<uint8_t> dibBytes;
};

inline bool GetGdiplusEncoderClsid(const wchar_t* mimeType, CLSID* pClsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return false;
    std::vector<uint8_t> buf(size);
    auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    if (Gdiplus::GetImageEncoders(num, size, info) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < num; ++i) {
        if (info[i].MimeType && wcscmp(info[i].MimeType, mimeType) == 0) {
            *pClsid = info[i].Clsid;
            return true;
        }
    }
    return false;
}

inline RenderedImageBundle EncodeGdiplusBitmapBundle(Gdiplus::Bitmap& bmp) {
    RenderedImageBundle out;
    int w = static_cast<int>(bmp.GetWidth());
    int h = static_cast<int>(bmp.GetHeight());
    if (w <= 0 || h <= 0) return out;
    out.width = w;
    out.height = h;

    // 1. Encode PNG via in-memory IStream
    CLSID pngClsid = {};
    if (GetGdiplusEncoderClsid(L"image/png", &pngClsid)) {
        IStream* pMemStream = nullptr;
        if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &pMemStream)) && pMemStream) {
            if (bmp.Save(pMemStream, &pngClsid, nullptr) == Gdiplus::Ok) {
                HGLOBAL hGlob = nullptr;
                if (SUCCEEDED(GetHGlobalFromStream(pMemStream, &hGlob)) && hGlob) {
                    STATSTG st = {};
                    if (SUCCEEDED(pMemStream->Stat(&st, STATFLAG_NONAME)) && st.cbSize.QuadPart > 0) {
                        size_t sz = static_cast<size_t>(st.cbSize.QuadPart);
                        void* ptr = GlobalLock(hGlob);
                        if (ptr) {
                            out.pngBytes.assign(static_cast<const uint8_t*>(ptr),
                                                static_cast<const uint8_t*>(ptr) + sz);
                            GlobalUnlock(hGlob);
                        }
                    }
                }
            }
            pMemStream->Release();
        }
    }

    // 2. Encode CF_DIBV5 + CF_DIB (bottom-up 32bpp BGRA)
    Gdiplus::Rect rect(0, 0, w, h);
    Gdiplus::BitmapData bd = {};
    if (bmp.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) == Gdiplus::Ok) {
        size_t rowBytes = static_cast<size_t>(w) * 4;
        size_t pixelBytes = rowBytes * static_cast<size_t>(h);

        // CF_DIBV5
        BITMAPV5HEADER v5 = {};
        v5.bV5Size = sizeof(BITMAPV5HEADER);
        v5.bV5Width = w;
        v5.bV5Height = h; // positive = bottom-up
        v5.bV5Planes = 1;
        v5.bV5BitCount = 32;
        v5.bV5Compression = BI_BITFIELDS;
        v5.bV5SizeImage = static_cast<DWORD>(pixelBytes);
        v5.bV5RedMask = 0x00FF0000;
        v5.bV5GreenMask = 0x0000FF00;
        v5.bV5BlueMask = 0x000000FF;
        v5.bV5AlphaMask = 0xFF000000;
        v5.bV5CSType = 0x73524742U; // LCS_sRGB ('sRGB' without MinGW -Wmultichar)

        out.dibV5Bytes.resize(sizeof(BITMAPV5HEADER) + pixelBytes);
        std::memcpy(out.dibV5Bytes.data(), &v5, sizeof(BITMAPV5HEADER));
        uint8_t* dstV5 = out.dibV5Bytes.data() + sizeof(BITMAPV5HEADER);

        // CF_DIB (composited onto dark/white or straight BGRA for 32bpp BI_RGB)
        BITMAPINFOHEADER bi = {};
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = w;
        bi.biHeight = h;
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;
        bi.biSizeImage = static_cast<DWORD>(pixelBytes);

        out.dibBytes.resize(sizeof(BITMAPINFOHEADER) + pixelBytes);
        std::memcpy(out.dibBytes.data(), &bi, sizeof(BITMAPINFOHEADER));
        uint8_t* dstDib = out.dibBytes.data() + sizeof(BITMAPINFOHEADER);

        const uint8_t* srcBase = static_cast<const uint8_t*>(bd.Scan0);
        for (int y = 0; y < h; ++y) {
            const uint8_t* srcRow = srcBase + static_cast<ptrdiff_t>(h - 1 - y) * bd.Stride;
            std::memcpy(dstV5 + static_cast<size_t>(y) * rowBytes, srcRow, rowBytes);
            std::memcpy(dstDib + static_cast<size_t>(y) * rowBytes, srcRow, rowBytes);
        }
        bmp.UnlockBits(&bd);
    }

    out.valid = !out.pngBytes.empty() && !out.dibV5Bytes.empty();
    return out;
}

inline void AddRoundedRectPath(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float r) {
    float maxR = std::min(w, h) * 0.5f;
    if (r > maxR) r = maxR;
    if (r <= 0.5f) {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }
    float d = r * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

// ============================================================================
// Syntax Tokenizer (powers both CF_HTML Rich Code Block & 2x HiDPI Code Card)
// ============================================================================

enum class CodeTokKind { Plain, Keyword, TypeName, StringLit, NumberLit, Comment, Punct };

struct CodeToken {
    CodeTokKind kind;
    std::string text;
};

inline std::vector<CodeToken> TokenizeSourceLine(const std::string& line) {
    static const std::set<std::string> kKeywords = {
        "function", "const", "let", "var", "return", "if", "else", "for", "while", "async", "await",
        "import", "from", "export", "default", "interface", "type", "class", "extends", "implements",
        "new", "try", "catch", "throw", "def", "None", "True", "False", "true", "false", "null",
        "pub", "fn", "struct", "enum", "impl", "use", "mut", "match", "package", "func", "go", "defer",
        "SELECT", "FROM", "WHERE", "INSERT", "INTO", "VALUES", "CREATE", "TABLE", "JOIN", "AND", "OR",
        "#include", "#pragma", "namespace", "template", "typename", "inline", "static", "void", "auto"
    };
    static const std::set<std::string> kTypes = {
        "string", "number", "boolean", "any", "unknown", "Promise", "Record", "Array", "List", "Optional",
        "Dict", "int", "float", "str", "bool", "i32", "i64", "u64", "f64", "usize", "String", "Vec", "Option",
        "Result", "int64", "float64", "BIGINT", "TEXT", "VARCHAR", "TIMESTAMP", "HRESULT", "DWORD"
    };

    std::vector<CodeToken> out;
    size_t i = 0;
    while (i < line.size()) {
        // Check line comments
        if ((line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/') ||
            (line[i] == '-' && i + 1 < line.size() && line[i + 1] == '-') ||
            (line[i] == '#' && !StartsWith(line.substr(i), "#include") && !StartsWith(line.substr(i), "#pragma"))) {
            out.push_back({CodeTokKind::Comment, line.substr(i)});
            break;
        }
        // String literals
        if (line[i] == '"' || line[i] == '\'' || line[i] == '`') {
            char q = line[i];
            size_t start = i++;
            while (i < line.size()) {
                if (line[i] == '\\' && i + 1 < line.size()) {
                    i += 2;
                    continue;
                }
                if (line[i] == q) {
                    ++i;
                    break;
                }
                ++i;
            }
            out.push_back({CodeTokKind::StringLit, line.substr(start, i - start)});
            continue;
        }
        // Numbers
        if (line[i] >= '0' && line[i] <= '9') {
            size_t start = i++;
            while (i < line.size() &&
                   ((line[i] >= '0' && line[i] <= '9') || line[i] == '.' || line[i] == 'x' ||
                    (line[i] >= 'a' && line[i] <= 'f') || (line[i] >= 'A' && line[i] <= 'F'))) {
                ++i;
            }
            out.push_back({CodeTokKind::NumberLit, line.substr(start, i - start)});
            continue;
        }
        // Identifiers / #directives / @decorators
        auto isIdentStart = [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '@' || c == '#';
        };
        auto isIdentPart = [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        };
        if (isIdentStart(line[i])) {
            size_t start = i++;
            while (i < line.size() && isIdentPart(line[i])) ++i;
            std::string word = line.substr(start, i - start);
            if (word[0] == '@' || kKeywords.count(word)) {
                out.push_back({CodeTokKind::Keyword, word});
            } else if (kTypes.count(word) || (word[0] >= 'A' && word[0] <= 'Z' && word.size() > 1)) {
                out.push_back({CodeTokKind::TypeName, word});
            } else {
                out.push_back({CodeTokKind::Plain, word});
            }
            continue;
        }
        // Whitespace
        if (line[i] == ' ' || line[i] == '\t') {
            size_t start = i;
            std::string ws;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
                if (line[i] == '\t') ws += "    ";
                else ws.push_back(' ');
                ++i;
            }
            (void)start;
            out.push_back({CodeTokKind::Plain, ws});
            continue;
        }
        // Punctuation
        out.push_back({CodeTokKind::Punct, std::string(1, line[i++])});
    }
    return out;
}

inline std::string CollapseCodeToCfHtml(const std::string& rawCode) {
    auto lines = SplitLines(rawCode);
    std::ostringstream frag;
    frag << "<div style=\"background-color:#0d1117;color:#e6edf3;font-family:Consolas,'Cascadia Code',monospace;"
         << "font-size:10.5pt;line-height:1.55;padding:16px 20px;border-radius:10px;border:1px solid #30363d;\">";
    for (size_t idx = 0; idx < lines.size(); ++idx) {
        frag << "<div><span style=\"color:#6e7681;user-select:none;margin-right:14px;\">"
             << (idx + 1 < 10 ? " " : "") << (idx + 1) << "</span>";
        auto toks = TokenizeSourceLine(lines[idx]);
        if (toks.empty()) {
            frag << "&nbsp;";
        } else {
            for (const auto& tk : toks) {
                std::string esc = ReplaceAll(HtmlEscape(tk.text), " ", "&nbsp;");
                const char* col = "#e6edf3";
                switch (tk.kind) {
                    case CodeTokKind::Keyword: col = "#ff7b72"; break;
                    case CodeTokKind::TypeName: col = "#79c0ff"; break;
                    case CodeTokKind::StringLit: col = "#a5d6ff"; break;
                    case CodeTokKind::NumberLit: col = "#f2cc60"; break;
                    case CodeTokKind::Comment: col = "#8b949e"; break;
                    case CodeTokKind::Punct: col = "#d2a8ff"; break;
                    default: break;
                }
                frag << "<span style=\"color:" << col << ";\">" << esc << "</span>";
            }
        }
        frag << "</div>";
    }
    frag << "</div>";
    return BuildCfHtmlPayload(frag.str());
}

// ============================================================================
// 2x HiDPI Anti-Aliased Rounded Code / Math / cURL Card Renderer
// ============================================================================

inline RenderedImageBundle RenderCodeCardBitmap2x(const std::string& rawCode,
                                                  const std::string& badgeTitle = "QUANTUM SOURCE // 2X HIDPI") {
    std::vector<std::string> lines = SplitLines(rawCode);
    if (lines.empty()) lines.push_back("");
    if (lines.size() > 38) {
        lines.resize(38);
        lines.push_back("// ... [truncated in visual card preview]");
    }

    size_t maxCols = 36;
    for (const auto& ln : lines) {
        size_t expanded = 0;
        for (char c : ln) expanded += (c == '\t') ? 4 : 1;
        maxCols = std::max(maxCols, std::min<size_t>(expanded, 92));
    }

    // 2x HiDPI geometry
    const float margin = 36.0f;
    const float headerH = 76.0f;
    const float padX = 40.0f;
    const float padBottom = 38.0f;
    const float lineH = 38.0f;
    const float gutterW = 72.0f;
    const float charW = 16.2f;

    int cardW = static_cast<int>(padX * 2.0f + gutterW + static_cast<float>(maxCols) * charW + 30.0f);
    cardW = std::max(760, std::min(cardW, 1840));
    int cardH = static_cast<int>(headerH + static_cast<float>(lines.size()) * lineH + padBottom);
    int imgW = cardW + static_cast<int>(margin * 2.0f);
    int imgH = cardH + static_cast<int>(margin * 2.0f);

    Gdiplus::Bitmap bmp(imgW, imgH, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));

        // Soft layered ambient shadow
        for (int s = 5; s >= 1; --s) {
            float grow = static_cast<float>(s) * 4.0f;
            Gdiplus::GraphicsPath shPath;
            AddRoundedRectPath(shPath, margin - grow * 0.5f, margin + static_cast<float>(s) * 2.5f - grow * 0.3f,
                               static_cast<float>(cardW) + grow, static_cast<float>(cardH) + grow, 28.0f + grow * 0.4f);
            Gdiplus::SolidBrush shBrush(Gdiplus::Color(static_cast<BYTE>(18 * (6 - s)), 2, 6, 23));
            g.FillPath(&shBrush, &shPath);
        }

        // Card Body (#0D1117)
        Gdiplus::GraphicsPath cardPath;
        AddRoundedRectPath(cardPath, margin, margin, static_cast<float>(cardW), static_cast<float>(cardH), 26.0f);
        Gdiplus::LinearGradientBrush bgBrush(
            Gdiplus::PointF(margin, margin),
            Gdiplus::PointF(margin, margin + static_cast<float>(cardH)),
            Gdiplus::Color(255, 15, 23, 42),
            Gdiplus::Color(255, 10, 14, 24));
        g.FillPath(&bgBrush, &cardPath);

        Gdiplus::Pen borderPen(Gdiplus::Color(220, 56, 189, 248), 2.0f);
        g.DrawPath(&borderPen, &cardPath);

        // Header divider
        Gdiplus::Pen divPen(Gdiplus::Color(110, 51, 65, 85), 1.5f);
        g.DrawLine(&divPen, margin + 2.0f, margin + headerH, margin + static_cast<float>(cardW) - 2.0f, margin + headerH);

        // Window traffic lights
        Gdiplus::SolidBrush redB(Gdiplus::Color(255, 255, 95, 86));
        Gdiplus::SolidBrush yelB(Gdiplus::Color(255, 255, 189, 46));
        Gdiplus::SolidBrush grnB(Gdiplus::Color(255, 39, 201, 63));
        float dotY = margin + 26.0f;
        g.FillEllipse(&redB, margin + 32.0f, dotY, 22.0f, 22.0f);
        g.FillEllipse(&yelB, margin + 68.0f, dotY, 22.0f, 22.0f);
        g.FillEllipse(&grnB, margin + 104.0f, dotY, 22.0f, 22.0f);

        Gdiplus::Font badgeFont(L"Consolas", 18.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush badgeBrush(Gdiplus::Color(220, 125, 211, 252));
        std::wstring wBadge = Utf8ToWide(badgeTitle);
        Gdiplus::StringFormat rightFmt;
        rightFmt.SetAlignment(Gdiplus::StringAlignmentFar);
        g.DrawString(wBadge.c_str(), -1, &badgeFont,
                     Gdiplus::PointF(margin + static_cast<float>(cardW) - 32.0f, margin + 26.0f),
                     &rightFmt, &badgeBrush);

        // Code typography
        Gdiplus::Font codeFont(L"Consolas", 25.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::StringFormat typoFmt(Gdiplus::StringFormat::GenericTypographic());
        typoFmt.SetFormatFlags(Gdiplus::StringFormatFlagsMeasureTrailingSpaces | Gdiplus::StringFormatFlagsNoWrap);

        Gdiplus::SolidBrush brLineNo(Gdiplus::Color(160, 100, 116, 139));
        Gdiplus::SolidBrush brPlain(Gdiplus::Color(255, 230, 237, 243));
        Gdiplus::SolidBrush brKw(Gdiplus::Color(255, 255, 123, 114));
        Gdiplus::SolidBrush brType(Gdiplus::Color(255, 121, 192, 255));
        Gdiplus::SolidBrush brStr(Gdiplus::Color(255, 165, 214, 255));
        Gdiplus::SolidBrush brNum(Gdiplus::Color(255, 242, 204, 96));
        Gdiplus::SolidBrush brCom(Gdiplus::Color(255, 139, 148, 158));
        Gdiplus::SolidBrush brPct(Gdiplus::Color(255, 210, 168, 255));

        for (size_t idx = 0; idx < lines.size(); ++idx) {
            float y = margin + headerH + 18.0f + static_cast<float>(idx) * lineH;
            wchar_t lnBuf[16];
            std::swprintf(lnBuf, 16, L"%2zu", idx + 1);
            g.DrawString(lnBuf, -1, &codeFont, Gdiplus::PointF(margin + 28.0f, y), &typoFmt, &brLineNo);

            float x = margin + padX + gutterW;
            std::string displayLine = lines[idx];
            if (displayLine.size() > 96) displayLine = displayLine.substr(0, 93) + "...";

            auto toks = TokenizeSourceLine(displayLine);
            for (const auto& tk : toks) {
                std::wstring wt = Utf8ToWide(tk.text);
                if (wt.empty()) continue;
                Gdiplus::Brush* br = &brPlain;
                switch (tk.kind) {
                    case CodeTokKind::Keyword: br = &brKw; break;
                    case CodeTokKind::TypeName: br = &brType; break;
                    case CodeTokKind::StringLit: br = &brStr; break;
                    case CodeTokKind::NumberLit: br = &brNum; break;
                    case CodeTokKind::Comment: br = &brCom; break;
                    case CodeTokKind::Punct: br = &brPct; break;
                    default: break;
                }
                g.DrawString(wt.c_str(), -1, &codeFont, Gdiplus::PointF(x, y), &typoFmt, br);
                Gdiplus::RectF box;
                g.MeasureString(wt.c_str(), -1, &codeFont, Gdiplus::PointF(0, 0), &typoFmt, &box);
                x += box.Width;
            }
        }
    }
    return EncodeGdiplusBitmapBundle(bmp);
}

// ============================================================================
// 2x HiDPI Designer #HEX Color Swatch Card Renderer
// ============================================================================

inline RenderedImageBundle RenderColorSwatchBitmap2x(const std::string& rawHex) {
    ParsedColor pc = ParseHexColorDetails(rawHex);
    const int imgW = 840;
    const int imgH = 420;

    Gdiplus::Bitmap bmp(imgW, imgH, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));

        // Card container
        Gdiplus::GraphicsPath bgPath;
        AddRoundedRectPath(bgPath, 24.0f, 24.0f, 792.0f, 372.0f, 30.0f);
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 15, 23, 42));
        g.FillPath(&bgBrush, &bgPath);
        Gdiplus::Pen borderPen(Gdiplus::Color(220, pc.r, pc.g, pc.b), 3.0f);
        g.DrawPath(&borderPen, &bgPath);

        // Left Swatch Preview Block
        Gdiplus::GraphicsPath swPath;
        AddRoundedRectPath(swPath, 54.0f, 54.0f, 280.0f, 312.0f, 22.0f);
        Gdiplus::SolidBrush swBrush(Gdiplus::Color(pc.a, pc.r, pc.g, pc.b));
        g.FillPath(&swBrush, &swPath);

        // Right Metadata Typography
        Gdiplus::Font titleFont(L"Segoe UI", 20.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font hexFont(L"Consolas", 44.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font specFont(L"Consolas", 25.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

        Gdiplus::SolidBrush accentBr(Gdiplus::Color(255, 148, 163, 184));
        Gdiplus::SolidBrush whiteBr(Gdiplus::Color(255, 248, 250, 252));
        Gdiplus::SolidBrush subBr(Gdiplus::Color(255, 203, 213, 225));

        g.DrawString(L"CHROMATIC SUPERPOSITION // 2X SWATCH", -1, &titleFont,
                     Gdiplus::PointF(368.0f, 64.0f), &accentBr);

        std::wstring wHex = Utf8ToWide("HEX  " + pc.normalizedHex);
        g.DrawString(wHex.c_str(), -1, &hexFont, Gdiplus::PointF(368.0f, 114.0f), &whiteBr);

        char rgbBuf[96], hslBuf[96];
        std::snprintf(rgbBuf, sizeof(rgbBuf), "RGB  rgb(%u, %u, %u)", pc.r, pc.g, pc.b);
        std::snprintf(hslBuf, sizeof(hslBuf), "HSL  hsl(%d deg, %d%%, %d%%)", pc.h, pc.s, pc.l);

        std::wstring wRgb = Utf8ToWide(rgbBuf);
        std::wstring wHsl = Utf8ToWide(hslBuf);
        g.DrawString(wRgb.c_str(), -1, &specFont, Gdiplus::PointF(368.0f, 216.0f), &subBr);
        g.DrawString(wHsl.c_str(), -1, &specFont, Gdiplus::PointF(368.0f, 272.0f), &subBr);
    }
    return EncodeGdiplusBitmapBundle(bmp);
}

// ============================================================================
// Native 2x HiDPI Transparent SVG Vector Parser & GDI+ Rasterizer
// ============================================================================

inline std::string ExtractXmlAttr(const std::string& tag, const std::string& attrName) {
    std::string lowTag = ToLowerAscii(tag);
    std::string lowAttr = ToLowerAscii(attrName);
    size_t pos = 0;
    while ((pos = lowTag.find(lowAttr, pos)) != std::string::npos) {
        if (pos > 0) {
            char prev = lowTag[pos - 1];
            if ((prev >= 'a' && prev <= 'z') || prev == '-' || prev == ':') {
                pos += lowAttr.size();
                continue;
            }
        }
        size_t eq = pos + lowAttr.size();
        while (eq < lowTag.size() && lowTag[eq] == ' ') ++eq;
        if (eq < lowTag.size() && lowTag[eq] == '=') {
            ++eq;
            while (eq < lowTag.size() && lowTag[eq] == ' ') ++eq;
            if (eq < tag.size() && (tag[eq] == '"' || tag[eq] == '\'')) {
                char q = tag[eq++];
                size_t end = tag.find(q, eq);
                if (end != std::string::npos) return tag.substr(eq, end - eq);
            }
        }
        pos += lowAttr.size();
    }
    return "";
}

inline float ParseSvgFloat(const std::string& s, float defVal = 0.0f) {
    if (s.empty()) return defVal;
    try { return std::stof(s); } catch (...) { return defVal; }
}

inline bool ParseSvgColor(const std::string& raw, float opacityMul, Gdiplus::Color& outCol) {
    std::string s = ToLowerAscii(Trim(raw));
    if (s.empty() || s == "none" || s == "transparent") return false;
    BYTE alpha = static_cast<BYTE>(std::max(0.0f, std::min(1.0f, opacityMul)) * 255.0f);
    if (s[0] == '#') {
        ParsedColor pc = ParseHexColorDetails(s);
        BYTE a = static_cast<BYTE>((static_cast<int>(pc.a) * static_cast<int>(alpha)) / 255);
        outCol = Gdiplus::Color(a, pc.r, pc.g, pc.b);
        return true;
    }
    if (StartsWith(s, "rgb(")) {
        size_t l = s.find('('), r = s.find(')');
        if (l != std::string::npos && r != std::string::npos && r > l) {
            std::string inner = s.substr(l + 1, r - l - 1);
            int rv = 0, gv = 0, bv = 0;
            if (std::sscanf(inner.c_str(), "%d , %d , %d", &rv, &gv, &bv) == 3) {
                outCol = Gdiplus::Color(alpha,
                                        static_cast<BYTE>(std::max(0, std::min(255, rv))),
                                        static_cast<BYTE>(std::max(0, std::min(255, gv))),
                                        static_cast<BYTE>(std::max(0, std::min(255, bv))));
                return true;
            }
        }
    }
    static const std::map<std::string, uint32_t> kNamed = {
        {"white", 0xFFFFFF}, {"black", 0x000000}, {"red", 0xEF4444}, {"green", 0x22C55E},
        {"blue", 0x3B82F6}, {"cyan", 0x06B6D4}, {"aqua", 0x06B6D4}, {"yellow", 0xEAB308},
        {"orange", 0xF97316}, {"purple", 0xA855F7}, {"magenta", 0xEC4899}, {"pink", 0xEC4899},
        {"gray", 0x64748B}, {"grey", 0x64748B}, {"gold", 0xF59E0B}, {"skyblue", 0x38BDF8},
        {"currentcolor", 0x38BDF8}
    };
    auto it = kNamed.find(s);
    if (it != kNamed.end()) {
        uint32_t v = it->second;
        outCol = Gdiplus::Color(alpha, static_cast<BYTE>((v >> 16) & 0xFF),
                                static_cast<BYTE>((v >> 8) & 0xFF), static_cast<BYTE>(v & 0xFF));
        return true;
    }
    outCol = Gdiplus::Color(alpha, 56, 189, 248);
    return true;
}

inline void ParseSvgPathData(const std::string& d, Gdiplus::GraphicsPath& path) {
    std::vector<std::string> toks;
    size_t i = 0;
    while (i < d.size()) {
        char c = d[i];
        if (c == ' ' || c == ',' || c == '\t' || c == '\r' || c == '\n') {
            ++i;
        } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            toks.push_back(std::string(1, c));
            ++i;
        } else {
            size_t start = i;
            if (d[i] == '+' || d[i] == '-') ++i;
            bool seenDot = false;
            while (i < d.size()) {
                if (d[i] >= '0' && d[i] <= '9') {
                    ++i;
                } else if (d[i] == '.' && !seenDot) {
                    seenDot = true;
                    ++i;
                } else if ((d[i] == 'e' || d[i] == 'E') && i + 1 < d.size()) {
                    ++i;
                    if (d[i] == '+' || d[i] == '-') ++i;
                } else {
                    break;
                }
            }
            if (i > start) toks.push_back(d.substr(start, i - start));
            else ++i;
        }
    }

    float cx = 0.0f, cy = 0.0f, sx = 0.0f, sy = 0.0f, lastCtrlX = 0.0f, lastCtrlY = 0.0f;
    char cmd = 0;
    size_t idx = 0;
    auto hasNum = [&]() -> bool {
        if (idx >= toks.size()) return false;
        char c = toks[idx][0];
        return !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
    };
    auto nextNum = [&]() -> float {
        return hasNum() ? ParseSvgFloat(toks[idx++], 0.0f) : 0.0f;
    };

    while (idx < toks.size()) {
        char c0 = toks[idx][0];
        if ((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z')) {
            cmd = c0;
            ++idx;
        }
        if (cmd == 'Z' || cmd == 'z') {
            path.CloseFigure();
            cx = sx;
            cy = sy;
            cmd = 0;
            continue;
        }
        if (!hasNum()) continue;
        bool rel = (cmd >= 'a' && cmd <= 'z');
        char up = rel ? static_cast<char>(cmd - 'a' + 'A') : cmd;

        if (up == 'M') {
            float x = nextNum(), y = nextNum();
            if (rel) { x += cx; y += cy; }
            path.StartFigure();
            cx = sx = lastCtrlX = x;
            cy = sy = lastCtrlY = y;
            cmd = rel ? 'l' : 'L';
        } else if (up == 'L') {
            float x = nextNum(), y = nextNum();
            if (rel) { x += cx; y += cy; }
            path.AddLine(cx, cy, x, y);
            cx = lastCtrlX = x;
            cy = lastCtrlY = y;
        } else if (up == 'H') {
            float x = nextNum();
            if (rel) x += cx;
            path.AddLine(cx, cy, x, cy);
            cx = lastCtrlX = x;
        } else if (up == 'V') {
            float y = nextNum();
            if (rel) y += cy;
            path.AddLine(cx, cy, cx, y);
            cy = lastCtrlY = y;
        } else if (up == 'C') {
            float x1 = nextNum(), y1 = nextNum();
            float x2 = nextNum(), y2 = nextNum();
            float x = nextNum(), y = nextNum();
            if (rel) { x1 += cx; y1 += cy; x2 += cx; y2 += cy; x += cx; y += cy; }
            path.AddBezier(cx, cy, x1, y1, x2, y2, x, y);
            lastCtrlX = x2; lastCtrlY = y2;
            cx = x; cy = y;
        } else if (up == 'S') {
            float x1 = 2.0f * cx - lastCtrlX, y1 = 2.0f * cy - lastCtrlY;
            float x2 = nextNum(), y2 = nextNum();
            float x = nextNum(), y = nextNum();
            if (rel) { x2 += cx; y2 += cy; x += cx; y += cy; }
            path.AddBezier(cx, cy, x1, y1, x2, y2, x, y);
            lastCtrlX = x2; lastCtrlY = y2;
            cx = x; cy = y;
        } else if (up == 'Q') {
            float qx = nextNum(), qy = nextNum();
            float x = nextNum(), y = nextNum();
            if (rel) { qx += cx; qy += cy; x += cx; y += cy; }
            float c1x = cx + (2.0f / 3.0f) * (qx - cx);
            float c1y = cy + (2.0f / 3.0f) * (qy - cy);
            float c2x = x + (2.0f / 3.0f) * (qx - x);
            float c2y = y + (2.0f / 3.0f) * (qy - y);
            path.AddBezier(cx, cy, c1x, c1y, c2x, c2y, x, y);
            lastCtrlX = qx; lastCtrlY = qy;
            cx = x; cy = y;
        } else if (up == 'A') {
            // Consume SVG elliptical arc args (rx ry rot largeArc sweep x y) and approximate smoothly
            (void)nextNum(); (void)nextNum(); (void)nextNum(); (void)nextNum(); (void)nextNum();
            float x = nextNum(), y = nextNum();
            if (rel) { x += cx; y += cy; }
            path.AddLine(cx, cy, x, y);
            cx = x; cy = y;
        } else {
            (void)nextNum();
        }
    }
}

inline RenderedImageBundle RenderSvgBitmap2x(const std::string& svgSource) {
    float vbX = 0.0f, vbY = 0.0f, vbW = 256.0f, vbH = 256.0f;
    size_t svgTagPos = ToLowerAscii(svgSource).find("<svg");
    if (svgTagPos != std::string::npos) {
        size_t svgTagEnd = svgSource.find('>', svgTagPos);
        if (svgTagEnd != std::string::npos) {
            std::string rootTag = svgSource.substr(svgTagPos, svgTagEnd - svgTagPos + 1);
            std::string vb = ExtractXmlAttr(rootTag, "viewBox");
            if (!vb.empty()) {
                std::string normVb = ReplaceAll(vb, ",", " ");
                std::sscanf(normVb.c_str(), "%f %f %f %f", &vbX, &vbY, &vbW, &vbH);
            } else {
                float wAttr = ParseSvgFloat(ExtractXmlAttr(rootTag, "width"), 0.0f);
                float hAttr = ParseSvgFloat(ExtractXmlAttr(rootTag, "height"), 0.0f);
                if (wAttr > 1.0f) vbW = wAttr;
                if (hAttr > 1.0f) vbH = hAttr;
            }
        }
    }
    if (vbW <= 1.0f) vbW = 256.0f;
    if (vbH <= 1.0f) vbH = 256.0f;

    // 2x HiDPI output sizing (at least 512px on longest side, capped at 1600px)
    float scale = 2.0f;
    if (std::max(vbW, vbH) * scale < 512.0f) {
        scale = 512.0f / std::max(vbW, vbH);
    } else if (std::max(vbW, vbH) * scale > 1600.0f) {
        scale = 1600.0f / std::max(vbW, vbH);
    }
    int outW = std::max(64, static_cast<int>(std::round(vbW * scale)));
    int outH = std::max(64, static_cast<int>(std::round(vbH * scale)));

    // Pre-parse <linearGradient id="..."> first/last stop colors
    std::map<std::string, std::pair<Gdiplus::Color, Gdiplus::Color>> gradients;
    size_t gSearch = 0;
    std::string lowAll = ToLowerAscii(svgSource);
    while ((gSearch = lowAll.find("<lineargradient", gSearch)) != std::string::npos) {
        size_t gOpenEnd = svgSource.find('>', gSearch);
        size_t gClose = lowAll.find("</lineargradient>", gSearch);
        if (gOpenEnd == std::string::npos || gClose == std::string::npos) break;
        std::string gTag = svgSource.substr(gSearch, gOpenEnd - gSearch + 1);
        std::string gid = ExtractXmlAttr(gTag, "id");
        std::string gBody = svgSource.substr(gOpenEnd + 1, gClose - gOpenEnd - 1);
        std::vector<Gdiplus::Color> stops;
        size_t stPos = 0;
        std::string lowBody = ToLowerAscii(gBody);
        while ((stPos = lowBody.find("<stop", stPos)) != std::string::npos) {
            size_t stEnd = gBody.find('>', stPos);
            if (stEnd == std::string::npos) break;
            std::string stTag = gBody.substr(stPos, stEnd - stPos + 1);
            std::string sc = ExtractXmlAttr(stTag, "stop-color");
            float so = ParseSvgFloat(ExtractXmlAttr(stTag, "stop-opacity"), 1.0f);
            Gdiplus::Color col;
            if (ParseSvgColor(sc, so, col)) stops.push_back(col);
            stPos = stEnd + 1;
        }
        if (!gid.empty() && !stops.empty()) {
            gradients[gid] = {stops.front(), stops.back()};
        }
        gSearch = gClose + 17;
    }

    Gdiplus::Bitmap bmp(outW, outH, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        g.Clear(Gdiplus::Color(0, 0, 0, 0)); // True transparent background!

        g.ScaleTransform(scale, scale);
        g.TranslateTransform(-vbX, -vbY);

        auto resolveFillAndStroke = [&](const std::string& tag, Gdiplus::GraphicsPath& shapePath) {
            float op = ParseSvgFloat(ExtractXmlAttr(tag, "opacity"), 1.0f);
            float fillOp = op * ParseSvgFloat(ExtractXmlAttr(tag, "fill-opacity"), 1.0f);
            float strokeOp = op * ParseSvgFloat(ExtractXmlAttr(tag, "stroke-opacity"), 1.0f);

            std::string fillAttr = ExtractXmlAttr(tag, "fill");
            if (fillAttr.empty()) {
                // Default SVG fill is black unless stroke is explicitly set on line/polyline
                std::string lowT = ToLowerAscii(tag);
                if (!StartsWith(lowT, "<line") && !StartsWith(lowT, "<polyline")) {
                    fillAttr = "#38BDF8";
                } else {
                    fillAttr = "none";
                }
            }

            if (StartsWithIgnoreCase(Trim(fillAttr), "url(#")) {
                std::string ref = Trim(fillAttr).substr(5);
                if (!ref.empty() && ref.back() == ')') ref.pop_back();
                auto git = gradients.find(ref);
                Gdiplus::RectF bounds;
                shapePath.GetBounds(&bounds);
                if (bounds.Width < 1.0f) bounds.Width = vbW;
                if (bounds.Height < 1.0f) bounds.Height = vbH;
                Gdiplus::Color c1(255, 56, 189, 248), c2(255, 168, 85, 247);
                if (git != gradients.end()) {
                    c1 = git->second.first;
                    c2 = git->second.second;
                }
                Gdiplus::LinearGradientBrush gradBr(
                    Gdiplus::PointF(bounds.X, bounds.Y),
                    Gdiplus::PointF(bounds.X + bounds.Width, bounds.Y + bounds.Height), c1, c2);
                g.FillPath(&gradBr, &shapePath);
            } else {
                Gdiplus::Color fillCol;
                if (ParseSvgColor(fillAttr, fillOp, fillCol)) {
                    Gdiplus::SolidBrush br(fillCol);
                    g.FillPath(&br, &shapePath);
                }
            }

            std::string strokeAttr = ExtractXmlAttr(tag, "stroke");
            Gdiplus::Color strokeCol;
            if (!strokeAttr.empty() && ParseSvgColor(strokeAttr, strokeOp, strokeCol)) {
                float sw = ParseSvgFloat(ExtractXmlAttr(tag, "stroke-width"), 1.5f);
                Gdiplus::Pen pen(strokeCol, sw);
                pen.SetLineJoin(Gdiplus::LineJoinRound);
                pen.SetStartCap(Gdiplus::LineCapRound);
                pen.SetEndCap(Gdiplus::LineCapRound);
                g.DrawPath(&pen, &shapePath);
            }
        };

        // Scan SVG geometric elements in document order
        size_t pos = 0;
        while ((pos = svgSource.find('<', pos)) != std::string::npos) {
            if (pos + 1 >= svgSource.size()) break;
            if (svgSource[pos + 1] == '/' || svgSource[pos + 1] == '!' || svgSource[pos + 1] == '?') {
                size_t close = svgSource.find('>', pos + 1);
                if (close == std::string::npos) break;
                pos = close + 1;
                continue;
            }
            size_t tagEnd = svgSource.find('>', pos);
            if (tagEnd == std::string::npos) break;
            std::string tag = svgSource.substr(pos, tagEnd - pos + 1);
            std::string lowTag = ToLowerAscii(tag);

            // Skip <defs> block so definitions aren't rendered directly
            if (StartsWith(lowTag, "<defs")) {
                size_t defsEnd = lowAll.find("</defs>", tagEnd);
                pos = (defsEnd == std::string::npos) ? (tagEnd + 1) : (defsEnd + 7);
                continue;
            }

            if (StartsWith(lowTag, "<rect")) {
                float x = ParseSvgFloat(ExtractXmlAttr(tag, "x"), 0.0f);
                float y = ParseSvgFloat(ExtractXmlAttr(tag, "y"), 0.0f);
                float w = ParseSvgFloat(ExtractXmlAttr(tag, "width"), 0.0f);
                float h = ParseSvgFloat(ExtractXmlAttr(tag, "height"), 0.0f);
                float rx = ParseSvgFloat(ExtractXmlAttr(tag, "rx"), ParseSvgFloat(ExtractXmlAttr(tag, "ry"), 0.0f));
                if (w > 0.0f && h > 0.0f) {
                    Gdiplus::GraphicsPath p;
                    AddRoundedRectPath(p, x, y, w, h, rx);
                    resolveFillAndStroke(tag, p);
                }
            } else if (StartsWith(lowTag, "<circle")) {
                float cx = ParseSvgFloat(ExtractXmlAttr(tag, "cx"), 0.0f);
                float cy = ParseSvgFloat(ExtractXmlAttr(tag, "cy"), 0.0f);
                float r = ParseSvgFloat(ExtractXmlAttr(tag, "r"), 0.0f);
                if (r > 0.0f) {
                    Gdiplus::GraphicsPath p;
                    p.AddEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
                    resolveFillAndStroke(tag, p);
                }
            } else if (StartsWith(lowTag, "<ellipse")) {
                float cx = ParseSvgFloat(ExtractXmlAttr(tag, "cx"), 0.0f);
                float cy = ParseSvgFloat(ExtractXmlAttr(tag, "cy"), 0.0f);
                float rx = ParseSvgFloat(ExtractXmlAttr(tag, "rx"), 0.0f);
                float ry = ParseSvgFloat(ExtractXmlAttr(tag, "ry"), 0.0f);
                if (rx > 0.0f && ry > 0.0f) {
                    Gdiplus::GraphicsPath p;
                    p.AddEllipse(cx - rx, cy - ry, rx * 2.0f, ry * 2.0f);
                    resolveFillAndStroke(tag, p);
                }
            } else if (StartsWith(lowTag, "<line")) {
                float x1 = ParseSvgFloat(ExtractXmlAttr(tag, "x1"), 0.0f);
                float y1 = ParseSvgFloat(ExtractXmlAttr(tag, "y1"), 0.0f);
                float x2 = ParseSvgFloat(ExtractXmlAttr(tag, "x2"), 0.0f);
                float y2 = ParseSvgFloat(ExtractXmlAttr(tag, "y2"), 0.0f);
                Gdiplus::GraphicsPath p;
                p.AddLine(x1, y1, x2, y2);
                resolveFillAndStroke(tag, p);
            } else if (StartsWith(lowTag, "<polygon") || StartsWith(lowTag, "<polyline")) {
                std::string ptsStr = ReplaceAll(ExtractXmlAttr(tag, "points"), ",", " ");
                std::istringstream iss(ptsStr);
                std::vector<Gdiplus::PointF> pts;
                float px = 0.0f, py = 0.0f;
                while (iss >> px >> py) pts.emplace_back(px, py);
                if (pts.size() >= 2) {
                    Gdiplus::GraphicsPath p;
                    if (StartsWith(lowTag, "<polygon")) p.AddPolygon(pts.data(), static_cast<INT>(pts.size()));
                    else p.AddLines(pts.data(), static_cast<INT>(pts.size()));
                    resolveFillAndStroke(tag, p);
                }
            } else if (StartsWith(lowTag, "<path")) {
                std::string d = ExtractXmlAttr(tag, "d");
                if (!d.empty()) {
                    Gdiplus::GraphicsPath p;
                    ParseSvgPathData(d, p);
                    resolveFillAndStroke(tag, p);
                }
            } else if (StartsWith(lowTag, "<text") && tag[tag.size() - 2] != '/') {
                size_t closeText = lowAll.find("</text>", tagEnd);
                if (closeText != std::string::npos) {
                    std::string innerTxt = Trim(svgSource.substr(tagEnd + 1, closeText - tagEnd - 1));
                    float tx = ParseSvgFloat(ExtractXmlAttr(tag, "x"), 10.0f);
                    float ty = ParseSvgFloat(ExtractXmlAttr(tag, "y"), 24.0f);
                    float fsz = ParseSvgFloat(ExtractXmlAttr(tag, "font-size"), 16.0f);
                    std::string fillAttr = ExtractXmlAttr(tag, "fill");
                    Gdiplus::Color tCol(255, 248, 250, 252);
                    if (!fillAttr.empty()) ParseSvgColor(fillAttr, 1.0f, tCol);
                    Gdiplus::Font font(L"Segoe UI", fsz, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                    Gdiplus::SolidBrush br(tCol);
                    Gdiplus::StringFormat sf;
                    if (ToLowerAscii(ExtractXmlAttr(tag, "text-anchor")) == "middle") {
                        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
                    }
                    std::wstring wt = Utf8ToWide(innerTxt);
                    g.DrawString(wt.c_str(), -1, &font, Gdiplus::PointF(tx, ty - fsz * 0.85f), &sf, &br);
                    pos = closeText + 7;
                    continue;
                }
            }
            pos = tagEnd + 1;
        }
    }
    return EncodeGdiplusBitmapBundle(bmp);
}

// ============================================================================
// Native 2x HiDPI Mermaid Flowchart Diagram Renderer
// ============================================================================

inline RenderedImageBundle RenderMermaidBitmap2x(const std::string& mermaidSrc) {
    struct MNode {
        std::string id;
        std::string label;
        int rank = 0;
        float x = 0.0f, y = 0.0f;
    };
    struct MEdge {
        std::string from;
        std::string to;
        std::string label;
    };

    auto lines = SplitLines(mermaidSrc);
    bool horizontal = false;
    if (!lines.empty()) {
        std::string f = ToLowerAscii(Trim(lines[0]));
        if (f.find(" lr") != std::string::npos || f.find(" rl") != std::string::npos) horizontal = true;
    }

    std::vector<std::string> nodeOrder;
    std::map<std::string, MNode> nodes;
    std::vector<MEdge> edges;

    auto registerNodeToken = [&](const std::string& rawTok) -> std::string {
        std::string t = Trim(rawTok);
        if (t.empty()) return "";
        std::string id = t, label = t;
        size_t bPos = t.find_first_of("[({");
        if (bPos != std::string::npos && bPos > 0) {
            id = Trim(t.substr(0, bPos));
            label = t.substr(bPos + 1);
            if (!label.empty() && (label.back() == ']' || label.back() == ')' || label.back() == '}')) {
                label.pop_back();
            }
        }
        if (!nodes.count(id)) {
            nodeOrder.push_back(id);
            nodes[id] = {id, label, 0, 0.0f, 0.0f};
        } else if (label != id) {
            nodes[id].label = label;
        }
        return id;
    };

    for (size_t i = 1; i < lines.size(); ++i) {
        std::string ln = Trim(lines[i]);
        if (ln.empty() || StartsWith(ln, "%%")) continue;
        size_t arrow = ln.find("-->");
        if (arrow == std::string::npos) arrow = ln.find("---");
        if (arrow != std::string::npos) {
            std::string lhs = Trim(ln.substr(0, arrow));
            std::string rhs = Trim(ln.substr(arrow + 3));
            std::string edgeLbl;
            if (!rhs.empty() && rhs[0] == '|') {
                size_t pipe2 = rhs.find('|', 1);
                if (pipe2 != std::string::npos) {
                    edgeLbl = rhs.substr(1, pipe2 - 1);
                    rhs = Trim(rhs.substr(pipe2 + 1));
                }
            }
            std::string u = registerNodeToken(lhs);
            std::string v = registerNodeToken(rhs);
            if (!u.empty() && !v.empty()) {
                edges.push_back({u, v, edgeLbl});
            }
        }
    }

    if (nodes.empty()) {
        return RenderCodeCardBitmap2x(mermaidSrc, "MERMAID DIAGRAM // 2X HIDPI");
    }

    // Simple rank propagation
    for (size_t pass = 0; pass < nodes.size(); ++pass) {
        for (const auto& e : edges) {
            if (nodes[e.to].rank <= nodes[e.from].rank) {
                nodes[e.to].rank = std::min<int>(static_cast<int>(nodes.size()), nodes[e.from].rank + 1);
            }
        }
    }

    std::map<int, std::vector<std::string>> layers;
    int maxRank = 0;
    size_t maxPerLayer = 1;
    for (const auto& id : nodeOrder) {
        int r = nodes[id].rank;
        maxRank = std::max(maxRank, r);
        layers[r].push_back(id);
        maxPerLayer = std::max(maxPerLayer, layers[r].size());
    }

    const float boxW = 280.0f, boxH = 84.0f, gapMain = 150.0f, gapCross = 120.0f, pad = 80.0f;
    int imgW = horizontal
                   ? static_cast<int>(pad * 2 + (maxRank + 1) * boxW + maxRank * gapMain)
                   : static_cast<int>(pad * 2 + maxPerLayer * boxW + (maxPerLayer - 1) * gapCross);
    int imgH = horizontal
                   ? static_cast<int>(pad * 2 + maxPerLayer * boxH + (maxPerLayer - 1) * gapCross)
                   : static_cast<int>(pad * 2 + (maxRank + 1) * boxH + maxRank * gapMain);
    imgW = std::max(720, imgW);
    imgH = std::max(420, imgH);

    for (const auto& kv : layers) {
        int r = kv.first;
        const auto& ids = kv.second;
        for (size_t idx = 0; idx < ids.size(); ++idx) {
            if (horizontal) {
                float totalH = ids.size() * boxH + (ids.size() - 1) * gapCross;
                float startY = (imgH - totalH) * 0.5f;
                nodes[ids[idx]].x = pad + r * (boxW + gapMain);
                nodes[ids[idx]].y = startY + idx * (boxH + gapCross);
            } else {
                float totalW = ids.size() * boxW + (ids.size() - 1) * gapCross;
                float startX = (imgW - totalW) * 0.5f;
                nodes[ids[idx]].x = startX + idx * (boxW + gapCross);
                nodes[ids[idx]].y = pad + r * (boxH + gapMain);
            }
        }
    }

    Gdiplus::Bitmap bmp(imgW, imgH, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

        Gdiplus::GraphicsPath bg;
        AddRoundedRectPath(bg, 12.0f, 12.0f, static_cast<float>(imgW - 24), static_cast<float>(imgH - 24), 28.0f);
        Gdiplus::SolidBrush bgBr(Gdiplus::Color(255, 13, 17, 23));
        g.FillPath(&bgBr, &bg);
        Gdiplus::Pen borderPen(Gdiplus::Color(200, 56, 189, 248), 2.5f);
        g.DrawPath(&borderPen, &bg);

        // Draw edges with arrowheads
        Gdiplus::Pen edgePen(Gdiplus::Color(230, 125, 211, 252), 3.2f);
        Gdiplus::AdjustableArrowCap arrowCap(7.0f, 7.0f, TRUE);
        edgePen.SetCustomEndCap(&arrowCap);
        Gdiplus::Font edgeFont(L"Segoe UI", 18.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush edgeLblBr(Gdiplus::Color(255, 250, 204, 21));

        for (const auto& e : edges) {
            const auto& u = nodes[e.from];
            const auto& v = nodes[e.to];
            float x1 = horizontal ? (u.x + boxW) : (u.x + boxW * 0.5f);
            float y1 = horizontal ? (u.y + boxH * 0.5f) : (u.y + boxH);
            float x2 = horizontal ? v.x : (v.x + boxW * 0.5f);
            float y2 = horizontal ? (v.y + boxH * 0.5f) : v.y;
            g.DrawLine(&edgePen, x1, y1, x2, y2);
            if (!e.label.empty()) {
                std::wstring wl = Utf8ToWide(e.label);
                g.DrawString(wl.c_str(), -1, &edgeFont,
                             Gdiplus::PointF((x1 + x2) * 0.5f + 8.0f, (y1 + y2) * 0.5f - 14.0f), &edgeLblBr);
            }
        }

        // Draw nodes
        Gdiplus::Font nodeFont(L"Segoe UI", 22.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::StringFormat centerFmt;
        centerFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        centerFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::SolidBrush txtBr(Gdiplus::Color(255, 248, 250, 252));

        for (const auto& id : nodeOrder) {
            const auto& nd = nodes[id];
            Gdiplus::GraphicsPath np;
            AddRoundedRectPath(np, nd.x, nd.y, boxW, boxH, 18.0f);
            Gdiplus::LinearGradientBrush nbr(
                Gdiplus::PointF(nd.x, nd.y), Gdiplus::PointF(nd.x, nd.y + boxH),
                Gdiplus::Color(255, 30, 58, 138), Gdiplus::Color(255, 15, 23, 42));
            g.FillPath(&nbr, &np);
            Gdiplus::Pen npen(Gdiplus::Color(240, 56, 189, 248), 2.5f);
            g.DrawPath(&npen, &np);

            std::wstring wlbl = Utf8ToWide(nd.label);
            Gdiplus::RectF r(nd.x + 10.0f, nd.y + 6.0f, boxW - 20.0f, boxH - 12.0f);
            g.DrawString(wlbl.c_str(), -1, &nodeFont, r, &centerFmt, &txtBr);
        }
    }
    return EncodeGdiplusBitmapBundle(bmp);
}

inline RenderedImageBundle DecodeRawImageBytesToBundle(const std::vector<uint8_t>& rawBytes) {
    RenderedImageBundle out;
    if (rawBytes.empty()) return out;

    // If rawBytes is SVG XML text, route to our vector SVG rasterizer
    std::string headSample(reinterpret_cast<const char*>(rawBytes.data()),
                           std::min<size_t>(rawBytes.size(), 256));
    if (LooksLikeSvgPayload(std::string(reinterpret_cast<const char*>(rawBytes.data()), rawBytes.size()))) {
        return RenderSvgBitmap2x(std::string(reinterpret_cast<const char*>(rawBytes.data()), rawBytes.size()));
    }
    (void)headSample;

    IStream* pStm = SHCreateMemStream(rawBytes.data(), static_cast<UINT>(rawBytes.size()));
    if (!pStm) return out;

    std::unique_ptr<Gdiplus::Bitmap> loaded(Gdiplus::Bitmap::FromStream(pStm, FALSE));
    if (loaded && loaded->GetLastStatus() == Gdiplus::Ok && loaded->GetWidth() > 0 && loaded->GetHeight() > 0) {
        Gdiplus::Bitmap argbCopy(loaded->GetWidth(), loaded->GetHeight(), PixelFormat32bppARGB);
        {
            Gdiplus::Graphics g(&argbCopy);
            g.DrawImage(loaded.get(), 0, 0, loaded->GetWidth(), loaded->GetHeight());
        }
        out = EncodeGdiplusBitmapBundle(argbCopy);
    }
    pStm->Release();
    return out;
}

} // namespace sclip

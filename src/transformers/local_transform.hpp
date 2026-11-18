#pragma once
#include "../common.hpp"
#include "../observer.hpp"

namespace sclip {

inline HGLOBAL BuildCfHDropHGlobal(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return nullptr;
    size_t totalChars = 1; // trailing extra null
    for (const auto& p : paths) {
        totalChars += p.size() + 1;
    }
    size_t totalBytes = sizeof(DROPFILES) + totalChars * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, totalBytes);
    if (!hMem) return nullptr;

    uint8_t* raw = static_cast<uint8_t*>(GlobalLock(hMem));
    if (!raw) {
        GlobalFree(hMem);
        return nullptr;
    }
    auto* df = reinterpret_cast<DROPFILES*>(raw);
    df->pFiles = sizeof(DROPFILES);
    df->pt.x = 0;
    df->pt.y = 0;
    df->fNC = FALSE;
    df->fWide = TRUE;

    wchar_t* dst = reinterpret_cast<wchar_t*>(raw + sizeof(DROPFILES));
    for (const auto& p : paths) {
        std::memcpy(dst, p.c_str(), (p.size() + 1) * sizeof(wchar_t));
        dst += p.size() + 1;
    }
    *dst = L'\0';
    GlobalUnlock(hMem);
    return hMem;
}

inline std::string CollapseLocalPathsForTerminal(const std::vector<std::wstring>& paths,
                                                 const std::string& procName) {
    bool isPosixShell = (ToLowerAscii(procName) == "mintty.exe");
    std::string out;
    for (size_t i = 0; i < paths.size(); ++i) {
        if (i > 0) out += " ";
        std::string utf8 = WideToUtf8(paths[i]);
        if (isPosixShell) {
            std::string posix = ReplaceAll(utf8, "\\", "/");
            if (posix.size() >= 2 && posix[1] == ':') {
                char drive = static_cast<char>(std::tolower(static_cast<unsigned char>(posix[0])));
                posix = std::string("/") + drive + posix.substr(2);
            }
            if (posix.find(' ') != std::string::npos) {
                out += "\"" + ReplaceAll(posix, "\"", "\\\"") + "\"";
            } else {
                out += posix;
            }
        } else {
            bool needQuotes = false;
            for (char c : utf8) {
                if (c == ' ' || c == '&' || c == '(' || c == ')' || c == '[' || c == ']' || c == ';') {
                    needQuotes = true;
                    break;
                }
            }
            if (needQuotes) {
                out += "\"" + utf8 + "\"";
            } else {
                out += utf8;
            }
        }
    }
    return out;
}

inline std::string DetectMimeFromExtension(const std::wstring& path) {
    std::string low = ToLowerAscii(WideToUtf8(path));
    if (EndsWith(low, ".png")) return "image/png";
    if (EndsWith(low, ".jpg") || EndsWith(low, ".jpeg")) return "image/jpeg";
    if (EndsWith(low, ".gif")) return "image/gif";
    if (EndsWith(low, ".webp")) return "image/webp";
    if (EndsWith(low, ".svg")) return "image/svg+xml";
    if (EndsWith(low, ".bmp")) return "image/bmp";
    if (EndsWith(low, ".ico")) return "image/x-icon";
    return "application/octet-stream";
}

inline std::string CollapseLocalFileToDataUri(const std::wstring& path) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return WideToUtf8(path);
    }
    LARGE_INTEGER fileSize = {};
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart <= 0 || fileSize.QuadPart > (32LL << 20)) {
        CloseHandle(hFile);
        return WideToUtf8(path);
    }
    std::vector<uint8_t> buf(static_cast<size_t>(fileSize.QuadPart));
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, buf.data(), static_cast<DWORD>(buf.size()), &bytesRead, nullptr);
    CloseHandle(hFile);
    if (!ok || bytesRead == 0) return WideToUtf8(path);

    std::string mime = DetectMimeFromExtension(path);
    return "data:" + mime + ";base64," + Base64Encode(buf.data(), bytesRead);
}

struct ParsedColor {
    uint8_t r = 59, g = 130, b = 246, a = 255;
    std::string normalizedHex = "#3B82F6";
    int h = 217, s = 91, l = 60;
};

inline ParsedColor ParseHexColorDetails(const std::string& rawHex) {
    ParsedColor pc;
    std::string t = Trim(rawHex);
    if (t.empty() || t[0] != '#') return pc;
    auto hv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    if (t.size() == 4) {
        pc.r = static_cast<uint8_t>(hv(t[1]) * 17);
        pc.g = static_cast<uint8_t>(hv(t[2]) * 17);
        pc.b = static_cast<uint8_t>(hv(t[3]) * 17);
        pc.a = 255;
    } else if (t.size() == 7 || t.size() == 9) {
        pc.r = static_cast<uint8_t>((hv(t[1]) << 4) | hv(t[2]));
        pc.g = static_cast<uint8_t>((hv(t[3]) << 4) | hv(t[4]));
        pc.b = static_cast<uint8_t>((hv(t[5]) << 4) | hv(t[6]));
        pc.a = (t.size() == 9) ? static_cast<uint8_t>((hv(t[7]) << 4) | hv(t[8])) : 255;
    }
    char hexBuf[16];
    if (pc.a == 255) {
        std::snprintf(hexBuf, sizeof(hexBuf), "#%02X%02X%02X", pc.r, pc.g, pc.b);
    } else {
        std::snprintf(hexBuf, sizeof(hexBuf), "#%02X%02X%02X%02X", pc.r, pc.g, pc.b, pc.a);
    }
    pc.normalizedHex = hexBuf;

    double rf = pc.r / 255.0, gf = pc.g / 255.0, bf = pc.b / 255.0;
    double cmax = std::max({rf, gf, bf});
    double cmin = std::min({rf, gf, bf});
    double delta = cmax - cmin;
    double l = (cmax + cmin) / 2.0;
    double h = 0.0, s = 0.0;
    if (delta > 1e-6) {
        s = delta / (1.0 - std::abs(2.0 * l - 1.0));
        if (cmax == rf) {
            h = 60.0 * std::fmod((gf - bf) / delta, 6.0);
        } else if (cmax == gf) {
            h = 60.0 * (((bf - rf) / delta) + 2.0);
        } else {
            h = 60.0 * (((rf - gf) / delta) + 4.0);
        }
        if (h < 0.0) h += 360.0;
    }
    pc.h = static_cast<int>(std::round(h));
    pc.s = static_cast<int>(std::round(s * 100.0));
    pc.l = static_cast<int>(std::round(l * 100.0));
    return pc;
}

inline std::string CollapseUnixTimestamp(const std::string& rawEpoch, const ObserverSnapshot& obs) {
    std::string t = Trim(rawEpoch);
    long long raw = 0;
    try { raw = std::stoll(t); } catch (...) { return rawEpoch; }
    long long sec = (t.size() == 13) ? (raw / 1000LL) : raw;
    long long ms = (t.size() == 13) ? (raw % 1000LL) : 0LL;

    time_t tt = static_cast<time_t>(sec);
    struct tm utcTm = {}, locTm = {};
    gmtime_s(&utcTm, &tt);
    localtime_s(&locTm, &tt);

    char utcIso[64], locStr[64];
    if (ms > 0) {
        std::snprintf(utcIso, sizeof(utcIso), "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",
                      utcTm.tm_year + 1900, utcTm.tm_mon + 1, utcTm.tm_mday,
                      utcTm.tm_hour, utcTm.tm_min, utcTm.tm_sec, ms);
    } else {
        std::snprintf(utcIso, sizeof(utcIso), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                      utcTm.tm_year + 1900, utcTm.tm_mon + 1, utcTm.tm_mday,
                      utcTm.tm_hour, utcTm.tm_min, utcTm.tm_sec);
    }
    std::snprintf(locStr, sizeof(locStr), "%04d-%02d-%02d %02d:%02d:%02d",
                  locTm.tm_year + 1900, locTm.tm_mon + 1, locTm.tm_mday,
                  locTm.tm_hour, locTm.tm_min, locTm.tm_sec);

    if (obs.lang == MicroLangContext::TypeScript || obs.lang == MicroLangContext::JavaScript) {
        long long jsMs = (t.size() == 13) ? raw : (raw * 1000LL);
        return "new Date(" + std::to_string(jsMs) + ") /* " + std::string(utcIso) + " */";
    }
    if (obs.lang == MicroLangContext::Python) {
        return "datetime.fromtimestamp(" + std::to_string(sec) + ", tz=timezone.utc)  # " + std::string(utcIso);
    }
    if (obs.lang == MicroLangContext::SQL || obs.category == HostCategory::DatabaseClient) {
        return "TIMESTAMP '" + std::string(locStr) + "'";
    }
    return std::string(locStr) + " (UTC: " + std::string(utcIso) + ")";
}

} // namespace sclip

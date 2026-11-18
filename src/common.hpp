#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <ole2.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <winhttp.h>
#include <gdiplus.h>
#include <bcrypt.h>
#include <psapi.h>
#include <rpcdce.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace sclip {

inline std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &out[0], needed, nullptr, nullptr);
    return out;
}

inline std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], needed);
    return out;
}

inline std::string Trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && static_cast<unsigned char>(s[start]) <= 0x20) ++start;
    size_t end = s.size();
    while (end > start && static_cast<unsigned char>(s[end - 1]) <= 0x20) --end;
    return s.substr(start, end - start);
}

inline std::string ToLowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

inline std::wstring ToLowerWide(std::wstring s) {
    for (wchar_t& c : s) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    }
    return s;
}

inline bool StartsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

inline bool StartsWithIgnoreCase(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) return false;
    return ToLowerAscii(s.substr(0, prefix.size())) == ToLowerAscii(prefix);
}

inline bool EndsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline bool EndsWithIgnoreCase(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    return ToLowerAscii(s.substr(s.size() - suffix.size())) == ToLowerAscii(suffix);
}

inline std::string ReplaceAll(std::string str, const std::string& from, const std::string& to) {
    if (from.empty()) return str;
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.size(), to);
        pos += to.size();
    }
    return str;
}

inline std::vector<std::string> SplitLines(const std::string& s) {
    std::vector<std::string> lines;
    std::string cur;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
            lines.push_back(cur);
            cur.clear();
        } else if (s[i] == '\n') {
            lines.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(s[i]);
        }
    }
    if (!cur.empty() || (!s.empty() && (s.back() == '\n' || s.back() == '\r'))) {
        lines.push_back(cur);
    }
    return lines;
}

inline std::string HtmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

inline std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

inline std::string UrlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = hexVal(s[i + 1]);
            int lo = hexVal(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

inline std::wstring SanitizeWindowsFilename(const std::wstring& raw, const std::wstring& fallback = L"quantum_output.dat") {
    std::wstring clean;
    clean.reserve(raw.size());
    for (wchar_t c : raw) {
        if (c < 32 || c == L'<' || c == L'>' || c == L':' || c == L'"' ||
            c == L'/' || c == L'\\' || c == L'|' || c == L'?' || c == L'*') {
            clean.push_back(L'_');
        } else {
            clean.push_back(c);
        }
    }
    while (!clean.empty() && (clean.back() == L' ' || clean.back() == L'.')) {
        clean.pop_back();
    }
    if (clean.empty()) return fallback;
    if (clean.size() >= MAX_PATH - 8) {
        clean.resize(MAX_PATH - 8);
    }
    return clean;
}

inline std::string Base64Encode(const uint8_t* data, size_t len) {
    static const char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
        out.push_back(kTable[(n >> 18) & 63]);
        out.push_back(kTable[(n >> 12) & 63]);
        out.push_back((i + 1 < len) ? kTable[(n >> 6) & 63] : '=');
        out.push_back((i + 2 < len) ? kTable[n & 63] : '=');
    }
    return out;
}

inline uint32_t ComputeCrc32(const uint8_t* data, size_t len) {
    static uint32_t table[256];
    static bool initialized = false;
    if (!initialized) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j) {
                c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        initialized = true;
    }
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; ++i) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

// Minimal pure-C++ Stored ZIP archive builder (ISO/IEC 29500 .xlsx compatible)
class StoredZipBuilder {
public:
    struct Entry {
        std::string name;
        std::vector<uint8_t> data;
        uint32_t crc32 = 0;
        uint32_t localHeaderOffset = 0;
    };

    void AddFile(const std::string& pathInZip, const std::string& utf8Content) {
        Entry e;
        e.name = pathInZip;
        e.data.assign(utf8Content.begin(), utf8Content.end());
        e.crc32 = ComputeCrc32(e.data.data(), e.data.size());
        entries_.push_back(std::move(e));
    }

    std::vector<uint8_t> Build() {
        std::vector<uint8_t> out;
        auto write16 = [&](uint16_t v) {
            out.push_back(static_cast<uint8_t>(v & 0xFF));
            out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        };
        auto write32 = [&](uint32_t v) {
            out.push_back(static_cast<uint8_t>(v & 0xFF));
            out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
        };

        for (auto& e : entries_) {
            e.localHeaderOffset = static_cast<uint32_t>(out.size());
            write32(0x04034B50U); // Local file header signature
            write16(20);          // Version needed to extract (2.0)
            write16(0x0800);      // UTF-8 filename flag
            write16(0);           // Compression method: 0 = STORE
            write16(0x5421);      // Mod time
            write16(0x5772);      // Mod date
            write32(e.crc32);
            write32(static_cast<uint32_t>(e.data.size()));
            write32(static_cast<uint32_t>(e.data.size()));
            write16(static_cast<uint16_t>(e.name.size()));
            write16(0); // Extra len
            out.insert(out.end(), e.name.begin(), e.name.end());
            out.insert(out.end(), e.data.begin(), e.data.end());
        }

        uint32_t centralDirOffset = static_cast<uint32_t>(out.size());
        for (const auto& e : entries_) {
            write32(0x02014B50U); // Central directory signature
            write16(20);          // Version made by
            write16(20);          // Version needed
            write16(0x0800);      // UTF-8 flag
            write16(0);           // STORE
            write16(0x5421);
            write16(0x5772);
            write32(e.crc32);
            write32(static_cast<uint32_t>(e.data.size()));
            write32(static_cast<uint32_t>(e.data.size()));
            write16(static_cast<uint16_t>(e.name.size()));
            write16(0); // Extra
            write16(0); // Comment
            write16(0); // Disk start
            write16(0); // Internal attr
            write32(0); // External attr
            write32(e.localHeaderOffset);
            out.insert(out.end(), e.name.begin(), e.name.end());
        }
        uint32_t centralDirSize = static_cast<uint32_t>(out.size()) - centralDirOffset;

        write32(0x06054B50U); // End of central directory
        write16(0);
        write16(0);
        write16(static_cast<uint16_t>(entries_.size()));
        write16(static_cast<uint16_t>(entries_.size()));
        write32(centralDirSize);
        write32(centralDirOffset);
        write16(0);
        return out;
    }

private:
    std::vector<Entry> entries_;
};

// Build standard Windows CF_HTML clipboard payload with exact UTF-8 byte offsets
inline std::string BuildCfHtmlPayload(const std::string& htmlFragment, const std::string& extraStyle = "") {
    const std::string prefix =
        "<!DOCTYPE html>\r\n<html>\r\n<head>\r\n<meta charset=\"utf-8\">\r\n" +
        (extraStyle.empty() ? std::string() : ("<style>" + extraStyle + "</style>\r\n")) +
        "</head>\r\n<body>\r\n<!--StartFragment-->";
    const std::string suffix = "<!--EndFragment-->\r\n</body>\r\n</html>";

    const char* headerTemplate =
        "Version:0.9\r\n"
        "StartHTML:%010zu\r\n"
        "EndHTML:%010zu\r\n"
        "StartFragment:%010zu\r\n"
        "EndFragment:%010zu\r\n";

    char probeHeader[256];
    int headerLen = std::snprintf(probeHeader, sizeof(probeHeader), headerTemplate,
                                  static_cast<size_t>(0), static_cast<size_t>(0),
                                  static_cast<size_t>(0), static_cast<size_t>(0));
    size_t startHtml = static_cast<size_t>(headerLen);
    size_t startFragment = startHtml + prefix.size();
    size_t endFragment = startFragment + htmlFragment.size();
    size_t endHtml = endFragment + suffix.size();

    std::snprintf(probeHeader, sizeof(probeHeader), headerTemplate,
                  startHtml, endHtml, startFragment, endFragment);
    return std::string(probeHeader) + prefix + htmlFragment + suffix;
}

inline HGLOBAL AllocHGlobalBytes(const void* data, size_t size) {
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size == 0 ? 1 : size);
    if (!hMem) return nullptr;
    void* ptr = GlobalLock(hMem);
    if (!ptr) {
        GlobalFree(hMem);
        return nullptr;
    }
    if (size > 0 && data != nullptr) {
        std::memcpy(ptr, data, size);
    } else {
        *static_cast<uint8_t*>(ptr) = 0;
    }
    GlobalUnlock(hMem);
    return hMem;
}

inline HGLOBAL AllocHGlobalWideString(const std::wstring& text) {
    size_t byteLen = (text.size() + 1) * sizeof(wchar_t);
    return AllocHGlobalBytes(text.c_str(), byteLen);
}

inline HGLOBAL AllocHGlobalDword(DWORD value) {
    return AllocHGlobalBytes(&value, sizeof(DWORD));
}

inline std::string Sha256Hex(const uint8_t* data, size_t len) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    std::string hexOut;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        DWORD objLen = 0, cbData = 0;
        BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(DWORD), &cbData, 0);
        std::vector<uint8_t> hashObj(objLen);
        if (BCryptCreateHash(hAlg, &hHash, hashObj.data(), objLen, nullptr, 0, 0) == 0) {
            size_t offset = 0;
            while (offset < len) {
                ULONG chunk = static_cast<ULONG>(std::min<size_t>(len - offset, 1 << 20));
                BCryptHashData(hHash, const_cast<PUCHAR>(data + offset), chunk, 0);
                offset += chunk;
            }
            uint8_t digest[32] = {};
            if (BCryptFinishHash(hHash, digest, sizeof(digest), 0) == 0) {
                char buf[3];
                for (int i = 0; i < 32; ++i) {
                    std::snprintf(buf, sizeof(buf), "%02x", digest[i]);
                    hexOut += buf;
                }
            }
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    return hexOut;
}

inline std::string Sha256FileHex(const std::wstring& filePath) {
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return {};

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    std::string hexOut;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        DWORD objLen = 0, cbData = 0;
        BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(DWORD), &cbData, 0);
        std::vector<uint8_t> hashObj(objLen);
        if (BCryptCreateHash(hAlg, &hHash, hashObj.data(), objLen, nullptr, 0, 0) == 0) {
            std::vector<uint8_t> buf(131072);
            DWORD bytesRead = 0;
            while (ReadFile(hFile, buf.data(), static_cast<DWORD>(buf.size()), &bytesRead, nullptr) && bytesRead > 0) {
                BCryptHashData(hHash, buf.data(), bytesRead, 0);
            }
            uint8_t digest[32] = {};
            if (BCryptFinishHash(hHash, digest, sizeof(digest), 0) == 0) {
                char hex[3];
                for (int i = 0; i < 32; ++i) {
                    std::snprintf(hex, sizeof(hex), "%02x", digest[i]);
                    hexOut += hex;
                }
            }
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    CloseHandle(hFile);
    return hexOut;
}

inline std::string NowTimestampString() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02u:%02u:%02u.%03u",
                  st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buf;
}

inline double ElapsedMs(std::chrono::steady_clock::time_point start) {
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

} // namespace sclip

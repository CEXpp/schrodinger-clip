#pragma once
#include "../common.hpp"

namespace sclip {

struct CrackedUrl {
    bool valid = false;
    bool isHttps = false;
    std::wstring host;
    INTERNET_PORT port = 80;
    std::wstring pathAndQuery;
};

inline CrackedUrl CrackHttpUrl(const std::string& utf8Url) {
    CrackedUrl out;
    std::wstring wurl = Utf8ToWide(Trim(utf8Url));
    if (wurl.empty()) return out;

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = static_cast<DWORD>(-1);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &uc)) {
        return out;
    }
    out.valid = true;
    out.isHttps = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    out.port = uc.nPort;
    if (uc.lpszHostName && uc.dwHostNameLength > 0) {
        out.host.assign(uc.lpszHostName, uc.dwHostNameLength);
    }
    if (uc.lpszUrlPath && uc.dwUrlPathLength > 0) {
        out.pathAndQuery.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    } else {
        out.pathAndQuery = L"/";
    }
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength > 0) {
        out.pathAndQuery.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    }
    return out;
}

inline std::string GuessUrlFilenameUtf8(const std::string& rawUrl, bool isGitHubRepo) {
    std::string u = Trim(rawUrl);
    size_t q = u.find_first_of("?#");
    std::string clean = (q == std::string::npos) ? u : u.substr(0, q);
    while (!clean.empty() && clean.back() == '/') clean.pop_back();

    if (isGitHubRepo) {
        size_t ghPos = ToLowerAscii(clean).find("github.com/");
        if (ghPos != std::string::npos) {
            std::string repoSlug = clean.substr(ghPos + 11);
            if (EndsWithIgnoreCase(repoSlug, ".git")) {
                repoSlug.resize(repoSlug.size() - 4);
            }
            return repoSlug;
        }
    }

    size_t lastSlash = clean.rfind('/');
    if (lastSlash != std::string::npos && lastSlash + 1 < clean.size()) {
        std::string seg = UrlDecode(clean.substr(lastSlash + 1));
        // Ignore bare scheme/host
        if (lastSlash > 7 && !seg.empty()) {
            return seg;
        }
    }
    CrackedUrl cu = CrackHttpUrl(u);
    if (cu.valid && !cu.host.empty()) {
        return WideToUtf8(cu.host) + "_resource.bin";
    }
    return "remote_payload.bin";
}

inline std::string CollapseUrlToMarkdown(const std::string& rawUrl, bool isGitHubRepo) {
    std::string u = Trim(rawUrl);
    std::string label = GuessUrlFilenameUtf8(u, isGitHubRepo);
    return "[" + label + "](" + u + ")";
}

inline std::string CollapseUrlToTerminalCommand(const std::string& rawUrl,
                                                bool isGitHubRepo,
                                                const std::string& observerProcess = "") {
    std::string u = Trim(rawUrl);
    if (isGitHubRepo) {
        std::string cloneUrl = u;
        while (!cloneUrl.empty() && cloneUrl.back() == '/') cloneUrl.pop_back();
        if (!EndsWithIgnoreCase(cloneUrl, ".git")) cloneUrl += ".git";
        return "git clone " + cloneUrl;
    }
    // In Windows PowerShell 5.1 (powershell.exe), 'curl' is aliased to Invoke-WebRequest;
    // invoking 'curl.exe' explicitly guarantees native libcurl execution!
    std::string bin = (ToLowerAscii(observerProcess) == "powershell.exe") ? "curl.exe" : "curl";
    return bin + " -fLO -C - \"" + ReplaceAll(u, "\"", "\\\"") + "\"";
}

struct UrlHeadMetadata {
    bool reachable = false;
    DWORD statusCode = 0;
    bool hasContentLength = false;
    uint64_t contentLength = 0;
    std::string contentType;
    std::wstring suggestedFilename;
};

// Strictly lazy: ONLY called inside IDataObject::GetData at paste time, NEVER at Ctrl+C!
inline UrlHeadMetadata ProbeUrlHeadMetadata(const std::string& rawUrl, bool isGitHubRepo) {
    UrlHeadMetadata meta;
    std::string baseGuess = GuessUrlFilenameUtf8(rawUrl, isGitHubRepo);
    if (isGitHubRepo) {
        // GitHub repo archive fallback if pasted into Explorer!
        meta.suggestedFilename = SanitizeWindowsFilename(Utf8ToWide(ReplaceAll(baseGuess, "/", "_") + ".url"));
        return meta;
    }
    meta.suggestedFilename = SanitizeWindowsFilename(Utf8ToWide(baseGuess), L"remote_payload.bin");

    CrackedUrl cu = CrackHttpUrl(rawUrl);
    if (!cu.valid) return meta;

    HINTERNET hSession = WinHttpOpen(L"SchrodingerClip/1.0 (LazyCOMStream)",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return meta;
    WinHttpSetTimeouts(hSession, 2500, 2500, 3000, 3000);

    HINTERNET hConnect = WinHttpConnect(hSession, cu.host.c_str(), cu.port, 0);
    if (hConnect) {
        DWORD flags = cu.isHttps ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hReq = WinHttpOpenRequest(hConnect, L"HEAD", cu.pathAndQuery.c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (hReq) {
            if (cu.isHttps) {
                DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                                 SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                                 SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
                WinHttpSetOption(hReq, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
            }
            if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hReq, nullptr)) {
                meta.reachable = true;
                DWORD status = 0, szStatus = sizeof(status);
                if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &szStatus, WINHTTP_NO_HEADER_INDEX)) {
                    meta.statusCode = status;
                }

                wchar_t lenBuf[64] = {};
                DWORD lenBytes = sizeof(lenBuf);
                if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_CONTENT_LENGTH,
                                        WINHTTP_HEADER_NAME_BY_INDEX, lenBuf, &lenBytes, WINHTTP_NO_HEADER_INDEX)) {
                    try {
                        meta.contentLength = std::stoull(lenBuf);
                        meta.hasContentLength = true;
                    } catch (...) {}
                }

                wchar_t ctBuf[128] = {};
                DWORD ctBytes = sizeof(ctBuf);
                if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_CONTENT_TYPE,
                                        WINHTTP_HEADER_NAME_BY_INDEX, ctBuf, &ctBytes, WINHTTP_NO_HEADER_INDEX)) {
                    meta.contentType = ToLowerAscii(WideToUtf8(ctBuf));
                }

                // Append image/file extension if URL filename lacked an extension
                if (meta.suggestedFilename.find(L'.') == std::wstring::npos && !meta.contentType.empty()) {
                    if (meta.contentType.find("image/png") != std::string::npos) meta.suggestedFilename += L".png";
                    else if (meta.contentType.find("image/jpeg") != std::string::npos) meta.suggestedFilename += L".jpg";
                    else if (meta.contentType.find("image/gif") != std::string::npos) meta.suggestedFilename += L".gif";
                    else if (meta.contentType.find("image/webp") != std::string::npos) meta.suggestedFilename += L".webp";
                    else if (meta.contentType.find("application/pdf") != std::string::npos) meta.suggestedFilename += L".pdf";
                    else if (meta.contentType.find("application/json") != std::string::npos) meta.suggestedFilename += L".json";
                    else if (meta.contentType.find("text/html") != std::string::npos) meta.suggestedFilename += L".html";
                }
            }
            WinHttpCloseHandle(hReq);
        }
        WinHttpCloseHandle(hConnect);
    }
    WinHttpCloseHandle(hSession);
    return meta;
}

// Used strictly when pasting a remote image URL into Chat/Paint/PPT to decode bitmap
inline bool FetchRemoteImageBytesOnDemand(const std::string& rawUrl, std::vector<uint8_t>& outBytes,
                                          size_t maxBytes = 25 * 1024 * 1024) {
    outBytes.clear();
    CrackedUrl cu = CrackHttpUrl(rawUrl);
    if (!cu.valid) return false;

    HINTERNET hSession = WinHttpOpen(L"SchrodingerClip/1.0 (ImageDecoder)",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    WinHttpSetTimeouts(hSession, 3000, 4000, 5000, 8000);

    bool ok = false;
    HINTERNET hConnect = WinHttpConnect(hSession, cu.host.c_str(), cu.port, 0);
    if (hConnect) {
        DWORD flags = cu.isHttps ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET", cu.pathAndQuery.c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (hReq) {
            if (cu.isHttps) {
                DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                                 SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                                 SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
                WinHttpSetOption(hReq, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
            }
            if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hReq, nullptr)) {
                DWORD status = 0, szStatus = sizeof(status);
                WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &szStatus, WINHTTP_NO_HEADER_INDEX);
                if (status >= 200 && status < 300) {
                    uint8_t chunk[65536];
                    DWORD bytesRead = 0;
                    ok = true;
                    while (WinHttpReadData(hReq, chunk, sizeof(chunk), &bytesRead) && bytesRead > 0) {
                        if (outBytes.size() + bytesRead > maxBytes) {
                            ok = false;
                            break;
                        }
                        outBytes.insert(outBytes.end(), chunk, chunk + bytesRead);
                        bytesRead = 0;
                    }
                    if (outBytes.empty()) ok = false;
                }
            }
            WinHttpCloseHandle(hReq);
        }
        WinHttpCloseHandle(hConnect);
    }
    WinHttpCloseHandle(hSession);
    return ok;
}

} // namespace sclip

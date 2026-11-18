#pragma once
#include "../common.hpp"
#include "structured_transform.hpp"

namespace sclip {

struct ParsedCurl {
    std::string method = "GET";
    bool explicitMethod = false;
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<std::pair<std::string, std::string>> cookies;
    std::string basicUser;
    std::string basicPass;
    std::string body;
    bool hasBody = false;
    bool isJsonBody = false;
    ValNode jsonAst;
};

inline std::vector<std::string> TokenizeCurlCommand(const std::string& raw) {
    // Normalize line continuations: backslash+\r?\n, caret ^+\r?\n (Windows CMD), backtick `+\r?\n (PowerShell)
    std::string norm;
    norm.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if ((raw[i] == '\\' || raw[i] == '^' || raw[i] == '`') && i + 1 < raw.size()) {
            if (raw[i + 1] == '\n') {
                norm.push_back(' ');
                ++i;
                continue;
            }
            if (raw[i + 1] == '\r' && i + 2 < raw.size() && raw[i + 2] == '\n') {
                norm.push_back(' ');
                i += 2;
                continue;
            }
            // CMD caret escaping ^" or ^{ -> unescape next punctuation if CMD style
            if (raw[i] == '^' && (raw[i + 1] == '"' || raw[i + 1] == '{' || raw[i + 1] == '}' ||
                                  raw[i + 1] == '&' || raw[i + 1] == '|' || raw[i + 1] == '^')) {
                norm.push_back(raw[i + 1]);
                ++i;
                continue;
            }
        }
        norm.push_back(raw[i]);
    }

    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < norm.size()) {
        while (i < norm.size() && static_cast<unsigned char>(norm[i]) <= 0x20) ++i;
        if (i >= norm.size()) break;

        std::string tok;
        while (i < norm.size() && static_cast<unsigned char>(norm[i]) > 0x20) {
            if (norm[i] == '$' && i + 1 < norm.size() && norm[i + 1] == '\'') {
                // Bash ANSI-C quoting $'...'
                i += 2;
                while (i < norm.size() && norm[i] != '\'') {
                    if (norm[i] == '\\' && i + 1 < norm.size()) {
                        ++i;
                        switch (norm[i]) {
                            case 'n': tok.push_back('\n'); break;
                            case 'r': tok.push_back('\r'); break;
                            case 't': tok.push_back('\t'); break;
                            case '\'': tok.push_back('\''); break;
                            case '\\': tok.push_back('\\'); break;
                            default: tok.push_back(norm[i]); break;
                        }
                    } else {
                        tok.push_back(norm[i]);
                    }
                    ++i;
                }
                if (i < norm.size() && norm[i] == '\'') ++i;
            } else if (norm[i] == '\'') {
                ++i;
                while (i < norm.size() && norm[i] != '\'') {
                    tok.push_back(norm[i++]);
                }
                if (i < norm.size() && norm[i] == '\'') ++i;
            } else if (norm[i] == '"') {
                ++i;
                while (i < norm.size() && norm[i] != '"') {
                    if (norm[i] == '\\' && i + 1 < norm.size() &&
                        (norm[i + 1] == '"' || norm[i + 1] == '\\')) {
                        ++i;
                    }
                    tok.push_back(norm[i++]);
                }
                if (i < norm.size() && norm[i] == '"') ++i;
            } else if (norm[i] == '\\' && i + 1 < norm.size()) {
                ++i;
                tok.push_back(norm[i++]);
            } else {
                tok.push_back(norm[i++]);
            }
        }
        tokens.push_back(std::move(tok));
    }
    return tokens;
}

inline void ParseCookieHeaderIntoPairs(const std::string& rawCookie,
                                       std::vector<std::pair<std::string, std::string>>& outCookies) {
    size_t pos = 0;
    while (pos < rawCookie.size()) {
        size_t semi = rawCookie.find(';', pos);
        std::string part = Trim(semi == std::string::npos ? rawCookie.substr(pos) : rawCookie.substr(pos, semi - pos));
        if (!part.empty()) {
            size_t eq = part.find('=');
            if (eq != std::string::npos) {
                outCookies.emplace_back(Trim(part.substr(0, eq)), Trim(part.substr(eq + 1)));
            }
        }
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
}

inline ParsedCurl ParseCurlCommand(const std::string& raw) {
    ParsedCurl pc;
    auto tokens = TokenizeCurlCommand(raw);
    if (tokens.empty()) return pc;

    size_t i = 0;
    if (ToLowerAscii(tokens[0]) == "curl" || ToLowerAscii(tokens[0]) == "curl.exe") ++i;

    for (; i < tokens.size(); ++i) {
        const std::string& t = tokens[i];
        if ((t == "-X" || t == "--request") && i + 1 < tokens.size()) {
            pc.method = tokens[++i];
            for (char& c : pc.method) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            pc.explicitMethod = true;
        } else if (StartsWith(t, "-X") && t.size() > 2) {
            pc.method = t.substr(2);
            pc.explicitMethod = true;
        } else if (t == "-I" || t == "--head") {
            pc.method = "HEAD";
            pc.explicitMethod = true;
        } else if ((t == "-H" || t == "--header") && i + 1 < tokens.size()) {
            std::string hdr = tokens[++i];
            size_t colon = hdr.find(':');
            if (colon != std::string::npos) {
                std::string k = Trim(hdr.substr(0, colon));
                std::string v = Trim(hdr.substr(colon + 1));
                if (ToLowerAscii(k) == "cookie") {
                    ParseCookieHeaderIntoPairs(v, pc.cookies);
                } else {
                    pc.headers.emplace_back(k, v);
                }
            }
        } else if ((t == "-b" || t == "--cookie") && i + 1 < tokens.size()) {
            ParseCookieHeaderIntoPairs(tokens[++i], pc.cookies);
        } else if ((t == "-u" || t == "--user") && i + 1 < tokens.size()) {
            std::string auth = tokens[++i];
            size_t colon = auth.find(':');
            if (colon != std::string::npos) {
                pc.basicUser = auth.substr(0, colon);
                pc.basicPass = auth.substr(colon + 1);
            } else {
                pc.basicUser = auth;
            }
        } else if ((t == "-d" || t == "--data" || t == "--data-raw" ||
                    t == "--data-binary" || t == "--data-ascii" || t == "--json") &&
                   i + 1 < tokens.size()) {
            std::string chunk = tokens[++i];
            if (!pc.body.empty()) pc.body += "&";
            pc.body += chunk;
            pc.hasBody = true;
            if (t == "--json") {
                pc.headers.emplace_back("Content-Type", "application/json");
                pc.headers.emplace_back("Accept", "application/json");
            }
        } else if (t == "--url" && i + 1 < tokens.size()) {
            pc.url = tokens[++i];
        } else if (!StartsWith(t, "-") && pc.url.empty() &&
                   (StartsWithIgnoreCase(t, "http://") || StartsWithIgnoreCase(t, "https://"))) {
            pc.url = t;
        }
    }

    if (pc.hasBody && !pc.explicitMethod) {
        pc.method = "POST";
    }
    if (pc.hasBody) {
        std::string tb = Trim(pc.body);
        if (LooksLikeJsonPayload(tb)) {
            JsonParser jp(tb);
            if (jp.Parse(pc.jsonAst)) {
                pc.isJsonBody = true;
            }
        }
    }
    return pc;
}

inline std::string RenderAstAsPythonLiteral(const ValNode& v, int indent = 0) {
    std::string pad(static_cast<size_t>(indent * 4), ' ');
    std::string innerPad(static_cast<size_t>((indent + 1) * 4), ' ');
    switch (v.kind) {
        case ValNode::Null: return "None";
        case ValNode::Bool: return v.boolVal ? "True" : "False";
        case ValNode::Int: return std::to_string(v.intVal);
        case ValNode::Float: {
            std::ostringstream oss;
            oss << v.floatVal;
            return oss.str();
        }
        case ValNode::String: return "\"" + JsonEscape(v.strVal) + "\"";
        case ValNode::Array: {
            if (v.arrItems.empty()) return "[]";
            std::ostringstream oss;
            oss << "[\r\n";
            for (size_t i = 0; i < v.arrItems.size(); ++i) {
                oss << innerPad << RenderAstAsPythonLiteral(v.arrItems[i], indent + 1);
                if (i + 1 < v.arrItems.size()) oss << ",";
                oss << "\r\n";
            }
            oss << pad << "]";
            return oss.str();
        }
        case ValNode::Object: {
            if (v.objFields.empty()) return "{}";
            std::ostringstream oss;
            oss << "{\r\n";
            for (size_t i = 0; i < v.objFields.size(); ++i) {
                oss << innerPad << "\"" << JsonEscape(v.objFields[i].first) << "\": "
                    << RenderAstAsPythonLiteral(v.objFields[i].second, indent + 1);
                if (i + 1 < v.objFields.size()) oss << ",";
                oss << "\r\n";
            }
            oss << pad << "}";
            return oss.str();
        }
    }
    return "None";
}

inline std::string RenderAstAsPrettyJson(const ValNode& v, int indent = 0) {
    std::string pad(static_cast<size_t>(indent * 2), ' ');
    std::string innerPad(static_cast<size_t>((indent + 1) * 2), ' ');
    switch (v.kind) {
        case ValNode::Null: return "null";
        case ValNode::Bool: return v.boolVal ? "true" : "false";
        case ValNode::Int: return std::to_string(v.intVal);
        case ValNode::Float: {
            std::ostringstream oss;
            oss << v.floatVal;
            return oss.str();
        }
        case ValNode::String: return "\"" + JsonEscape(v.strVal) + "\"";
        case ValNode::Array: {
            if (v.arrItems.empty()) return "[]";
            std::ostringstream oss;
            oss << "[\r\n";
            for (size_t i = 0; i < v.arrItems.size(); ++i) {
                oss << innerPad << RenderAstAsPrettyJson(v.arrItems[i], indent + 1);
                if (i + 1 < v.arrItems.size()) oss << ",";
                oss << "\r\n";
            }
            oss << pad << "]";
            return oss.str();
        }
        case ValNode::Object: {
            if (v.objFields.empty()) return "{}";
            std::ostringstream oss;
            oss << "{\r\n";
            for (size_t i = 0; i < v.objFields.size(); ++i) {
                oss << innerPad << "\"" << JsonEscape(v.objFields[i].first) << "\": "
                    << RenderAstAsPrettyJson(v.objFields[i].second, indent + 1);
                if (i + 1 < v.objFields.size()) oss << ",";
                oss << "\r\n";
            }
            oss << pad << "}";
            return oss.str();
        }
    }
    return "null";
}

// 1. cURL -> Python requests
inline std::string CollapseCurlToPythonRequests(const std::string& rawCurl) {
    ParsedCurl pc = ParseCurlCommand(rawCurl);
    std::ostringstream oss;
    oss << "import requests\r\n\r\n";
    oss << "url = \"" << JsonEscape(pc.url) << "\"\r\n";

    if (!pc.headers.empty()) {
        oss << "headers = {\r\n";
        for (const auto& kv : pc.headers) {
            oss << "    \"" << JsonEscape(kv.first) << "\": \"" << JsonEscape(kv.second) << "\",\r\n";
        }
        oss << "}\r\n";
    }
    if (!pc.cookies.empty()) {
        oss << "cookies = {\r\n";
        for (const auto& kv : pc.cookies) {
            oss << "    \"" << JsonEscape(kv.first) << "\": \"" << JsonEscape(kv.second) << "\",\r\n";
        }
        oss << "}\r\n";
    }
    if (pc.hasBody) {
        if (pc.isJsonBody) {
            oss << "payload = " << RenderAstAsPythonLiteral(pc.jsonAst, 0) << "\r\n";
        } else {
            oss << "data = \"" << JsonEscape(pc.body) << "\"\r\n";
        }
    }

    oss << "\r\nresponse = requests.request(\r\n"
        << "    \"" << pc.method << "\",\r\n"
        << "    url,\r\n";
    if (!pc.headers.empty()) oss << "    headers=headers,\r\n";
    if (!pc.cookies.empty()) oss << "    cookies=cookies,\r\n";
    if (!pc.basicUser.empty()) {
        oss << "    auth=(\"" << JsonEscape(pc.basicUser) << "\", \"" << JsonEscape(pc.basicPass) << "\"),\r\n";
    }
    if (pc.hasBody) {
        oss << (pc.isJsonBody ? "    json=payload,\r\n" : "    data=data,\r\n");
    }
    oss << "    timeout=30,\r\n"
        << ")\r\n"
        << "response.raise_for_status()\r\n"
        << "print(response.status_code, response.text)\r\n";
    return oss.str();
}

// 2. cURL -> Modern JS / TS fetch()
inline std::string CollapseCurlToFetch(const std::string& rawCurl) {
    ParsedCurl pc = ParseCurlCommand(rawCurl);
    std::ostringstream oss;
    oss << "async function executeRequest() {\r\n"
        << "  const response = await fetch(\"" << JsonEscape(pc.url) << "\", {\r\n"
        << "    method: \"" << pc.method << "\",\r\n";

    std::vector<std::pair<std::string, std::string>> allHeaders = pc.headers;
    if (!pc.cookies.empty()) {
        std::string cookieLine;
        for (size_t i = 0; i < pc.cookies.size(); ++i) {
            if (i > 0) cookieLine += "; ";
            cookieLine += pc.cookies[i].first + "=" + pc.cookies[i].second;
        }
        allHeaders.emplace_back("Cookie", cookieLine);
    }

    if (!allHeaders.empty()) {
        oss << "    headers: {\r\n";
        for (const auto& kv : allHeaders) {
            oss << "      \"" << JsonEscape(kv.first) << "\": \"" << JsonEscape(kv.second) << "\",\r\n";
        }
        oss << "    },\r\n";
    }

    if (pc.hasBody) {
        if (pc.isJsonBody) {
            std::string pretty = RenderAstAsPrettyJson(pc.jsonAst, 2);
            // Strip leading pad on first line
            oss << "    body: JSON.stringify(" << pretty << "),\r\n";
        } else {
            oss << "    body: \"" << JsonEscape(pc.body) << "\",\r\n";
        }
    }

    oss << "  });\r\n\r\n"
        << "  if (!response.ok) {\r\n"
        << "    throw new Error(`HTTP ${response.status}: ${response.statusText}`);\r\n"
        << "  }\r\n"
        << "  return await response.json();\r\n"
        << "}\r\n";
    return oss.str();
}

// 3. cURL -> Reusable .http REST Client File
inline std::string CollapseCurlToHttpFile(const std::string& rawCurl) {
    ParsedCurl pc = ParseCurlCommand(rawCurl);
    std::ostringstream oss;
    oss << "### Collapsed by SchrodingerClip\r\n"
        << pc.method << " " << pc.url << " HTTP/1.1\r\n";
    for (const auto& kv : pc.headers) {
        oss << kv.first << ": " << kv.second << "\r\n";
    }
    if (!pc.cookies.empty()) {
        oss << "Cookie: ";
        for (size_t i = 0; i < pc.cookies.size(); ++i) {
            if (i > 0) oss << "; ";
            oss << pc.cookies[i].first << "=" << pc.cookies[i].second;
        }
        oss << "\r\n";
    }
    if (pc.hasBody) {
        oss << "\r\n";
        if (pc.isJsonBody) {
            oss << RenderAstAsPrettyJson(pc.jsonAst, 0) << "\r\n";
        } else {
            oss << pc.body << "\r\n";
        }
    }
    return oss.str();
}

} // namespace sclip

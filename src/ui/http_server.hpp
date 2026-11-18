#pragma once
#include "../common.hpp"
#include "../ole/superposition_dataobj.hpp"
#include "../transformers/structured_transform.hpp"
#include "hud_tray.hpp"
#include "observatory_html.hpp"

namespace sclip {

struct SyncArmRequest {
    bool isFileDrop = false;
    std::string utf8Text;
    std::vector<std::wstring> filePaths;
    uint64_t outEnvelopeId = 0;
    DWORD outClipboardSeq = 0;
    bool ok = false;
};

struct SyncPreviewRequest {
    std::string processName;
    std::string windowTitle;
    std::string requestedKind; // "text", "html", "png"
    bool ok = false;
    std::string resultKind;
    std::string textOut;
    std::string htmlFragOut;
    std::string base64Out;
    size_t bytesOut = 0;
    std::string errorMsg;
};

class ObservatoryHttpServer {
public:
    ObservatoryHttpServer(uint16_t port,
                          HWND staHwnd,
                          ObserverContextManager* obsMgr,
                          TelemetryHub* telemetry)
        : port_(port),
          staHwnd_(staHwnd),
          obsMgr_(obsMgr),
          telemetry_(telemetry) {}

    ~ObservatoryHttpServer() {
        Stop();
    }

    bool Start() {
        WSADATA wsa = {};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

        listenSock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSock_ == INVALID_SOCKET) return false;

        int opt = 1;
        setsockopt(listenSock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port_);

        if (bind(listenSock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            closesocket(listenSock_);
            listenSock_ = INVALID_SOCKET;
            return false;
        }
        if (listen(listenSock_, SOMAXCONN) == SOCKET_ERROR) {
            closesocket(listenSock_);
            listenSock_ = INVALID_SOCKET;
            return false;
        }

        running_ = true;
        acceptThread_ = std::thread([this]() { AcceptLoop(); });
        return true;
    }

    void Stop() {
        running_ = false;
        if (listenSock_ != INVALID_SOCKET) {
            closesocket(listenSock_);
            listenSock_ = INVALID_SOCKET;
        }
        if (acceptThread_.joinable()) {
            acceptThread_.join();
        }
    }

    uint16_t Port() const { return port_; }

private:
    uint16_t port_;
    HWND staHwnd_;
    ObserverContextManager* obsMgr_;
    TelemetryHub* telemetry_;
    SOCKET listenSock_ = INVALID_SOCKET;
    std::atomic<bool> running_{false};
    std::thread acceptThread_;

    static void SendAll(SOCKET s, const char* data, size_t len) {
        size_t sent = 0;
        while (sent < len) {
            int n = send(s, data + sent, static_cast<int>(std::min<size_t>(len - sent, 65536)), 0);
            if (n <= 0) break;
            sent += static_cast<size_t>(n);
        }
    }

    static void SendHttpResponse(SOCKET s, int status, const char* statusText,
                                 const char* contentType, const std::string& body) {
        std::ostringstream hdr;
        hdr << "HTTP/1.1 " << status << " " << statusText << "\r\n"
            << "Content-Type: " << contentType << "\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Access-Control-Allow-Origin: *\r\n"
            << "Cache-Control: no-store\r\n"
            << "Connection: close\r\n\r\n";
        std::string h = hdr.str();
        SendAll(s, h.data(), h.size());
        if (!body.empty()) {
            SendAll(s, body.data(), body.size());
        }
    }

    void AcceptLoop() {
        while (running_) {
            sockaddr_in clientAddr = {};
            int addrLen = sizeof(clientAddr);
            SOCKET client = accept(listenSock_, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
            if (client == INVALID_SOCKET) {
                if (!running_) break;
                continue;
            }
            std::thread(&ObservatoryHttpServer::HandleClient, this, client).detach();
        }
    }

    void HandleClient(SOCKET client) {
        DWORD recvTimeout = 5000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recvTimeout), sizeof(recvTimeout));

        std::string rawReq;
        char buf[8192];
        size_t headerEnd = std::string::npos;
        while (headerEnd == std::string::npos && rawReq.size() < (256 * 1024)) {
            int n = recv(client, buf, sizeof(buf), 0);
            if (n <= 0) break;
            rawReq.append(buf, static_cast<size_t>(n));
            headerEnd = rawReq.find("\r\n\r\n");
        }
        if (headerEnd == std::string::npos) {
            closesocket(client);
            return;
        }

        std::string headerBlock = rawReq.substr(0, headerEnd);
        size_t firstLineEnd = headerBlock.find("\r\n");
        std::string reqLine = (firstLineEnd == std::string::npos) ? headerBlock : headerBlock.substr(0, firstLineEnd);

        std::istringstream rl(reqLine);
        std::string method, path;
        rl >> method >> path;

        size_t contentLen = 0;
        for (const auto& ln : SplitLines(headerBlock)) {
            if (StartsWithIgnoreCase(ln, "Content-Length:")) {
                try { contentLen = std::stoull(Trim(ln.substr(15))); } catch (...) {}
            }
        }

        std::string body = rawReq.substr(headerEnd + 4);
        while (body.size() < contentLen && contentLen <= (4 * 1024 * 1024)) {
            int n = recv(client, buf, static_cast<int>(std::min<size_t>(sizeof(buf), contentLen - body.size())), 0);
            if (n <= 0) break;
            body.append(buf, static_cast<size_t>(n));
        }

        RouteRequest(client, method, path, body);
        closesocket(client);
    }

    static const ValNode* FindJsonField(const ValNode& obj, const std::string& key) {
        if (obj.kind != ValNode::Object) return nullptr;
        for (const auto& kv : obj.objFields) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }

    void RouteRequest(SOCKET client, const std::string& method, const std::string& path, const std::string& body) {
        if (method == "OPTIONS") {
            SendHttpResponse(client, 200, "OK", "text/plain", "");
            return;
        }

        if (method == "GET" && (path == "/" || StartsWith(path, "/?"))) {
            SendHttpResponse(client, 200, "OK", "text/html; charset=utf-8", GetObservatoryHtmlPage());
            return;
        }

        // Built-in 24 MB Sandbox Virtual File Streaming Endpoint (HEAD + GET)
        if (StartsWith(path, "/api/demo-stream/")) {
            const uint64_t kStreamBytes = 24ULL * 1024ULL * 1024ULL; // 25,165,824 bytes
            std::ostringstream hdr;
            hdr << "HTTP/1.1 200 OK\r\n"
                << "Content-Type: application/octet-stream\r\n"
                << "Content-Length: " << kStreamBytes << "\r\n"
                << "Connection: close\r\n\r\n";
            std::string h = hdr.str();
            SendAll(client, h.data(), h.size());
            if (method == "GET") {
                std::vector<char> chunk(65536);
                uint64_t offset = 0;
                while (offset < kStreamBytes && running_) {
                    size_t n = static_cast<size_t>(std::min<uint64_t>(chunk.size(), kStreamBytes - offset));
                    for (size_t i = 0; i < n; ++i) {
                        uint64_t idx = offset + i;
                        chunk[i] = static_cast<char>((idx * 1315423911ULL) ^ (idx >> 13));
                    }
                    int sent = send(client, chunk.data(), static_cast<int>(n), 0);
                    if (sent <= 0) break;
                    offset += static_cast<uint64_t>(sent);
                }
            }
            return;
        }

        if (method == "GET" && path == "/api/state") {
            SendHttpResponse(client, 200, "OK", "application/json; charset=utf-8", BuildStateJson());
            return;
        }

        if (method == "POST" && path == "/api/arm") {
            ValNode reqJson;
            JsonParser jp(body);
            if (!jp.Parse(reqJson)) {
                SendHttpResponse(client, 400, "Bad Request", "application/json", "{\"ok\":false}");
                return;
            }
            SyncArmRequest req;
            if (const ValNode* fps = FindJsonField(reqJson, "filePaths"); fps && fps->kind == ValNode::Array) {
                req.isFileDrop = true;
                for (const auto& item : fps->arrItems) {
                    if (item.kind == ValNode::String) req.filePaths.push_back(Utf8ToWide(item.strVal));
                }
            } else if (const ValNode* txt = FindJsonField(reqJson, "text"); txt && txt->kind == ValNode::String) {
                req.utf8Text = txt->strVal;
            }
            SendMessageW(staHwnd_, WM_SCLIP_ARM_SYNC, reinterpret_cast<WPARAM>(&req), 0);
            std::ostringstream resp;
            resp << "{\"ok\":" << (req.ok ? "true" : "false")
                 << ",\"envelopeId\":" << req.outEnvelopeId
                 << ",\"clipboardSeq\":" << req.outClipboardSeq << "}";
            SendHttpResponse(client, 200, "OK", "application/json", resp.str());
            return;
        }

        if (method == "POST" && path == "/api/lock") {
            ValNode reqJson;
            JsonParser jp(body);
            if (jp.Parse(reqJson) && obsMgr_) {
                if (const ValNode* tb = FindJsonField(reqJson, "toggleBypass"); tb && tb->kind == ValNode::Bool && tb->boolVal) {
                    obsMgr_->ToggleRawBypass();
                } else if (const ValNode* m = FindJsonField(reqJson, "mode"); m && m->kind == ValNode::Int) {
                    int mv = std::max(0, std::min(10, static_cast<int>(m->intVal)));
                    obsMgr_->SetManualLockMode(static_cast<ManualLockMode>(mv));
                }
            }
            int curMode = obsMgr_ ? static_cast<int>(obsMgr_->GetManualLockMode()) : 0;
            SendHttpResponse(client, 200, "OK", "application/json",
                             "{\"ok\":true,\"lockMode\":" + std::to_string(curMode) + "}");
            return;
        }

        if (method == "POST" && path == "/api/observer-override") {
            ValNode reqJson;
            JsonParser jp(body);
            if (jp.Parse(reqJson) && obsMgr_) {
                bool en = false;
                std::string proc, title;
                if (const ValNode* v = FindJsonField(reqJson, "enabled"); v && v->kind == ValNode::Bool) en = v->boolVal;
                if (const ValNode* v = FindJsonField(reqJson, "processName"); v && v->kind == ValNode::String) proc = v->strVal;
                if (const ValNode* v = FindJsonField(reqJson, "windowTitle"); v && v->kind == ValNode::String) title = v->strVal;
                obsMgr_->SetSimulatedObserver(en, proc, title);
            }
            SendHttpResponse(client, 200, "OK", "application/json", "{\"ok\":true}");
            return;
        }

        if (method == "POST" && path == "/api/preview-collapse") {
            ValNode reqJson;
            JsonParser jp(body);
            SyncPreviewRequest preq;
            if (jp.Parse(reqJson)) {
                if (const ValNode* v = FindJsonField(reqJson, "processName"); v && v->kind == ValNode::String) preq.processName = v->strVal;
                if (const ValNode* v = FindJsonField(reqJson, "windowTitle"); v && v->kind == ValNode::String) preq.windowTitle = v->strVal;
                if (const ValNode* v = FindJsonField(reqJson, "format"); v && v->kind == ValNode::String) preq.requestedKind = v->strVal;
            }
            SendMessageW(staHwnd_, WM_SCLIP_PREVIEW_SYNC, reinterpret_cast<WPARAM>(&preq), 0);
            std::ostringstream resp;
            resp << "{\"ok\":" << (preq.ok ? "true" : "false")
                 << ",\"kind\":\"" << JsonEscape(preq.resultKind) << "\""
                 << ",\"bytes\":" << preq.bytesOut
                 << ",\"text\":\"" << JsonEscape(preq.textOut) << "\""
                 << ",\"htmlFragment\":\"" << JsonEscape(preq.htmlFragOut) << "\""
                 << ",\"base64\":\"" << preq.base64Out << "\""
                 << ",\"error\":\"" << JsonEscape(preq.errorMsg) << "\"}";
            SendHttpResponse(client, 200, "OK", "application/json; charset=utf-8", resp.str());
            return;
        }

        if (method == "POST" && path == "/api/shutdown") {
            SendHttpResponse(client, 200, "OK", "application/json", "{\"ok\":true}");
            PostMessageW(staHwnd_, WM_SCLIP_SHUTDOWN, 0, 0);
            return;
        }

        SendHttpResponse(client, 404, "Not Found", "application/json", "{\"error\":\"not_found\"}");
    }

    std::string BuildStateJson() const {
        ObserverSnapshot obs = obsMgr_ ? obsMgr_->Capture(false) : ObserverSnapshot{};
        bool hasSeed = false;
        QuantumSeed seed = telemetry_ ? telemetry_->GetActiveSeed(&hasSeed) : QuantumSeed{};
        auto stats = telemetry_ ? telemetry_->Snapshot() : TelemetryHub::StatsSnapshot{};
        auto orbits = BuildWavefunctionOrbits(seed);
        std::string predicted = hasSeed ? PredictCollapseTargetLabel(seed, obs) : "Standby (Waiting for Ctrl+C)";

        std::string preview = seed.rawText;
        if (preview.size() > 600) preview = preview.substr(0, 600) + "\n... [truncated in seed preview]";

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);
        oss << "{";
        oss << "\"observer\":{"
            << "\"processName\":\"" << JsonEscape(obs.processName) << "\","
            << "\"windowTitle\":\"" << JsonEscape(obs.windowTitle) << "\","
            << "\"detectedFile\":\"" << JsonEscape(obs.detectedFile) << "\","
            << "\"category\":\"" << HostCategoryName(obs.category) << "\","
            << "\"lang\":\"" << MicroLangName(obs.lang) << "\","
            << "\"lockMode\":" << static_cast<int>(obs.lockMode) << ","
            << "\"detectTimeMs\":" << obs.detectTimeMs
            << "},";

        oss << "\"seed\":{"
            << "\"hasSeed\":" << (hasSeed ? "true" : "false") << ","
            << "\"envelopeId\":" << seed.envelopeId << ","
            << "\"kindName\":\"" << (hasSeed ? SuperpositionKindName(seed.kind) : "STANDBY") << "\","
            << "\"badgeZh\":\"" << JsonEscape(hasSeed ? SuperpositionKindBadgeZh(seed) : "待命态") << "\","
            << "\"seedRawBytes\":" << seed.seedRawBytes << ","
            << "\"evaluatedBytesAtCopy\":0,"
            << "\"rawPreview\":\"" << JsonEscape(preview) << "\""
            << "},";

        oss << "\"predictedOutcome\":\"" << JsonEscape(predicted) << "\",";

        oss << "\"orbits\":[";
        for (size_t i = 0; i < orbits.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "{\"id\":\"" << JsonEscape(orbits[i].id) << "\","
                << "\"label\":\"" << JsonEscape(orbits[i].label) << "\","
                << "\"formatBadge\":\"" << JsonEscape(orbits[i].formatBadge) << "\","
                << "\"armed\":" << ((hasSeed && orbits[i].armed) ? "true" : "false") << "}";
        }
        oss << "],";

        oss << "\"stats\":{"
            << "\"totalCollapses\":" << stats.totalCollapses << ","
            << "\"totalSniffersBlocked\":" << stats.totalSniffersBlocked << ","
            << "\"totalSynthesizedBytes\":" << stats.totalSynthesizedBytes << ","
            << "\"totalZeroDiskSavedBytes\":" << stats.totalZeroDiskSavedBytes << ","
            << "\"latestEventId\":" << stats.latestEventId
            << "},";

        oss << "\"events\":[";
        for (size_t i = 0; i < stats.events.size() && i < 30; ++i) {
            const auto& ev = stats.events[i];
            if (i > 0) oss << ",";
            oss << "{\"eventId\":" << ev.eventId << ","
                << "\"timestamp\":\"" << JsonEscape(ev.timestamp) << "\","
                << "\"observerProcess\":\"" << JsonEscape(ev.observerProcess) << "\","
                << "\"detectedMicroContext\":\"" << JsonEscape(ev.detectedMicroContext) << "\","
                << "\"requestedFormat\":\"" << JsonEscape(ev.requestedFormat) << "\","
                << "\"orbitId\":\"" << JsonEscape(ev.orbitId) << "\","
                << "\"transformerName\":\"" << JsonEscape(ev.transformerName) << "\","
                << "\"synthesisMs\":" << ev.synthesisMs << ","
                << "\"outputBytes\":" << ev.outputBytes << ","
                << "\"throughputMBps\":" << ev.throughputMBps << ","
                << "\"blockedSniffer\":" << (ev.blockedSniffer ? "true" : "false") << ","
                << "\"previewSnippet\":\"" << JsonEscape(ev.previewSnippet) << "\"}";
        }
        oss << "]}";
        return oss.str();
    }
};

} // namespace sclip

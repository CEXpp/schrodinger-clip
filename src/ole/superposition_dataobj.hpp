#pragma once
#include "../common.hpp"
#include "../observer.hpp"
#include "../classifier.hpp"
#include "../transformers/structured_transform.hpp"
#include "../transformers/url_transform.hpp"
#include "../transformers/visual_transform.hpp"
#include "../transformers/curl_transform.hpp"
#include "../transformers/local_transform.hpp"
#include "enum_formatetc.hpp"
#include "virtual_stream.hpp"

namespace sclip {

struct RegisteredFormats {
    UINT cfEnvelope = 0;
    UINT cfCanIncludeHistory = 0;
    UINT cfCanUploadCloud = 0;
    UINT cfExcludeMonitor = 0;
    UINT cfFileDescriptorW = 0;
    UINT cfFileContents = 0;
    UINT cfPreferredDropEffect = 0;
    UINT cfHtml = 0;
    UINT cfCsv = 0;
    UINT cfPng = 0;

    static const RegisteredFormats& Instance() {
        static RegisteredFormats inst;
        return inst;
    }

private:
    RegisteredFormats() {
        cfEnvelope = RegisterClipboardFormatW(L"SchrodingerClip.SuperpositionEnvelope");
        cfCanIncludeHistory = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
        cfCanUploadCloud = RegisterClipboardFormatW(L"CanUploadToCloudClipboard");
        cfExcludeMonitor = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
        cfFileDescriptorW = RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW);
        cfFileContents = RegisterClipboardFormatW(CFSTR_FILECONTENTS);
        cfPreferredDropEffect = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
        cfHtml = RegisterClipboardFormatW(L"HTML Format");
        cfCsv = RegisterClipboardFormatW(L"CSV");
        cfPng = RegisterClipboardFormatW(L"PNG");
    }
};

struct SuperpositionEnvelopeBlob {
    char magic[12]; // "SCLIP_V1"
    uint64_t envelopeId;
    DWORD ownerPid;
};

struct CollapseEvent {
    uint64_t eventId = 0;
    std::string timestamp;
    std::string observerProcess;
    std::string observerWindow;
    std::string detectedMicroContext;
    std::string requestedFormat;
    std::string orbitId;
    std::string transformerName;
    double synthesisMs = 0.0;
    uint64_t outputBytes = 0;
    double throughputMBps = 0.0;
    uint64_t zeroDiskSavedBytes = 0;
    bool blockedSniffer = false;
    std::string previewSnippet;
};

class TelemetryHub {
public:
    void SetActiveSeed(const QuantumSeed& seed) {
        std::lock_guard<std::mutex> lk(mu_);
        activeSeed_ = seed;
        hasSeed_ = true;
    }

    void ClearSeed() {
        std::lock_guard<std::mutex> lk(mu_);
        activeSeed_ = QuantumSeed{};
        hasSeed_ = false;
    }

    QuantumSeed GetActiveSeed(bool* outHasSeed = nullptr) const {
        std::lock_guard<std::mutex> lk(mu_);
        if (outHasSeed) *outHasSeed = hasSeed_;
        return activeSeed_;
    }

    uint64_t RecordEvent(CollapseEvent ev) {
        std::lock_guard<std::mutex> lk(mu_);
        ev.eventId = ++nextEventId_;
        if (!ev.blockedSniffer) {
            totalCollapses_ += 1;
            totalSynthesizedBytes_ += ev.outputBytes;
            totalZeroDiskSavedBytes_ += ev.zeroDiskSavedBytes;
        } else {
            totalSniffersBlocked_ += 1;
        }
        events_.insert(events_.begin(), std::move(ev));
        if (events_.size() > 80) events_.resize(80);
        return nextEventId_;
    }

    void UpdateStreamCompletion(uint64_t eventId, uint64_t streamedBytes, double elapsedMs) {
        std::lock_guard<std::mutex> lk(mu_);
        double mbps = (elapsedMs > 0.1) ? ((static_cast<double>(streamedBytes) / (1024.0 * 1024.0)) / (elapsedMs / 1000.0)) : 0.0;
        totalSynthesizedBytes_ += streamedBytes;
        totalZeroDiskSavedBytes_ += streamedBytes;
        for (auto& ev : events_) {
            if (ev.eventId == eventId) {
                ev.outputBytes = streamedBytes;
                ev.zeroDiskSavedBytes = streamedBytes;
                ev.synthesisMs = elapsedMs;
                ev.throughputMBps = mbps;
                char buf[128];
                std::snprintf(buf, sizeof(buf), "[Stream Completed] %.2f MB streamed via TYMED_ISTREAM @ %.2f MB/s (0 temp disk)",
                              static_cast<double>(streamedBytes) / (1024.0 * 1024.0), mbps);
                ev.previewSnippet = buf;
                break;
            }
        }
    }

    struct StatsSnapshot {
        uint64_t totalCollapses = 0;
        uint64_t totalSniffersBlocked = 0;
        uint64_t totalSynthesizedBytes = 0;
        uint64_t totalZeroDiskSavedBytes = 0;
        uint64_t latestEventId = 0;
        std::vector<CollapseEvent> events;
    };

    StatsSnapshot Snapshot() const {
        std::lock_guard<std::mutex> lk(mu_);
        return {totalCollapses_, totalSniffersBlocked_, totalSynthesizedBytes_,
                totalZeroDiskSavedBytes_, nextEventId_, events_};
    }

private:
    mutable std::mutex mu_;
    bool hasSeed_ = false;
    QuantumSeed activeSeed_;
    uint64_t nextEventId_ = 0;
    uint64_t totalCollapses_ = 0;
    uint64_t totalSniffersBlocked_ = 0;
    uint64_t totalSynthesizedBytes_ = 0;
    uint64_t totalZeroDiskSavedBytes_ = 0;
    std::vector<CollapseEvent> events_;
};

inline std::string FormatClipboardCodeName(UINT cf) {
    const auto& rf = RegisteredFormats::Instance();
    if (cf == CF_UNICODETEXT) return "CF_UNICODETEXT";
    if (cf == CF_TEXT) return "CF_TEXT";
    if (cf == CF_DIBV5) return "CF_DIBV5";
    if (cf == CF_DIB) return "CF_DIB";
    if (cf == CF_HDROP) return "CF_HDROP";
    if (cf == rf.cfPng) return "PNG";
    if (cf == rf.cfHtml) return "CF_HTML";
    if (cf == rf.cfCsv) return "CSV";
    if (cf == rf.cfFileDescriptorW) return "CFSTR_FILEDESCRIPTORW";
    if (cf == rf.cfFileContents) return "CFSTR_FILECONTENTS (TYMED_ISTREAM)";
    if (cf == rf.cfPreferredDropEffect) return "CFSTR_PREFERREDDROPEFFECT";
    wchar_t nameBuf[128] = {};
    if (GetClipboardFormatNameW(cf, nameBuf, 127) > 0) {
        return WideToUtf8(nameBuf);
    }
    return "CF_" + std::to_string(cf);
}

// ============================================================================
// Native COM SuperpositionDataObject (IDataObject VTable)
// ============================================================================

class SuperpositionDataObject final : public IDataObject {
public:
    SuperpositionDataObject(QuantumSeed seed,
                            ObserverContextManager* observerMgr,
                            std::shared_ptr<TelemetryHub> telemetry)
        : refCount_(1),
          seed_(std::move(seed)),
          observerMgr_(observerMgr),
          telemetry_(std::move(telemetry)) {
        GetSystemTimeAsFileTime(&armedFileTime_);
        BuildAdvertisedFormatList();
    }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDataObject)) {
            *ppvObject = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refCount_;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG c = --refCount_;
        if (c == 0) delete this;
        return c;
    }

    // IDataObject
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* pformatetcIn, STGMEDIUM* pmedium) override {
        if (!pformatetcIn || !pmedium) return E_INVALIDARG;
        std::memset(pmedium, 0, sizeof(STGMEDIUM));

        if (!(pformatetcIn->dwAspect & DVASPECT_CONTENT)) {
            return DV_E_DVASPECT;
        }

        const auto& rf = RegisteredFormats::Instance();
        const UINT cf = pformatetcIn->cfFormat;

        // --------------------------------------------------------------------
        // 1. Metadata & Premature-Collapse Shield Formats (Zero Evaluation!)
        // --------------------------------------------------------------------
        if (cf == rf.cfEnvelope) {
            if (!(pformatetcIn->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
            SuperpositionEnvelopeBlob blob = {};
            std::memcpy(blob.magic, "SCLIP_V1", 9);
            blob.envelopeId = seed_.envelopeId;
            blob.ownerPid = GetCurrentProcessId();
            pmedium->tymed = TYMED_HGLOBAL;
            pmedium->hGlobal = AllocHGlobalBytes(&blob, sizeof(blob));
            return pmedium->hGlobal ? S_OK : E_OUTOFMEMORY;
        }
        if (cf == rf.cfCanIncludeHistory || cf == rf.cfCanUploadCloud) {
            if (!(pformatetcIn->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
            pmedium->tymed = TYMED_HGLOBAL;
            pmedium->hGlobal = AllocHGlobalDword(0); // Official MS flag: DO NOT store in Win+V or Cloud!
            return pmedium->hGlobal ? S_OK : E_OUTOFMEMORY;
        }
        if (cf == rf.cfExcludeMonitor) {
            if (!(pformatetcIn->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
            pmedium->tymed = TYMED_HGLOBAL;
            pmedium->hGlobal = AllocHGlobalDword(1); // Official MS flag: Exclude from clipboard monitors!
            return pmedium->hGlobal ? S_OK : E_OUTOFMEMORY;
        }
        {
            std::lock_guard<std::mutex> lk(lazyCacheMu_);
            auto ait = ancillaryBlobs_.find(cf);
            if (ait != ancillaryBlobs_.end()) {
                if (!(pformatetcIn->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
                pmedium->tymed = TYMED_HGLOBAL;
                pmedium->hGlobal = AllocHGlobalBytes(ait->second.data(), ait->second.size());
                return pmedium->hGlobal ? S_OK : E_OUTOFMEMORY;
            }
        }

        // --------------------------------------------------------------------
        // 2. Capture Real-Time Dual-Layer Observer Context & Sniffer Shield
        // --------------------------------------------------------------------
        auto tStart = std::chrono::steady_clock::now();
        ObserverSnapshot obs = observerMgr_ ? observerMgr_->Capture(true) : ObserverSnapshot{};

        if (obs.isBlockedSniffer) {
            if (telemetry_) {
                CollapseEvent blockedEv;
                blockedEv.timestamp = NowTimestampString();
                blockedEv.observerProcess = obs.processName;
                blockedEv.requestedFormat = FormatClipboardCodeName(cf);
                blockedEv.orbitId = "shield";
                blockedEv.transformerName = "Shield Blocked Background Sniffer";
                blockedEv.synthesisMs = ElapsedMs(tStart);
                blockedEv.blockedSniffer = true;
                blockedEv.previewSnippet = "Prevented premature wavefunction collapse by " + obs.processName;
                telemetry_->RecordEvent(std::move(blockedEv));
            }
            return DV_E_FORMATETC;
        }

        // Unified Observer + Lock-Mode Format Gate (allow paste-time HEAD probe only inside GetData)
        if (!IsFormatAllowedForObserver(cf, obs, true)) {
            return DV_E_FORMATETC;
        }

        if (cf == rf.cfPreferredDropEffect) {
            if (!(pformatetcIn->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
            pmedium->tymed = TYMED_HGLOBAL;
            pmedium->hGlobal = AllocHGlobalDword(DROPEFFECT_COPY);
            return pmedium->hGlobal ? S_OK : E_OUTOFMEMORY;
        }

        // Raw Bypass Mode: only serve raw text or original CF_HDROP
        if (obs.lockMode == ManualLockMode::RawBypass) {
            if (cf == CF_HDROP && seed_.kind == SuperpositionKind::LocalFiles) {
                if (!(pformatetcIn->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
                pmedium->tymed = TYMED_HGLOBAL;
                pmedium->hGlobal = BuildCfHDropHGlobal(seed_.filePaths);
                return pmedium->hGlobal ? S_OK : E_OUTOFMEMORY;
            }
            if (cf == CF_UNICODETEXT || cf == CF_TEXT) {
                return DeliverUtf8Text(cf, pformatetcIn, pmedium, seed_.rawText, obs,
                                       "raw_bypass", "Raw Bypass Passthrough (Alt+Shift+V)", tStart);
            }
            return DV_E_FORMATETC;
        }

        // --------------------------------------------------------------------
        // 3. Route Format Synthesis On-The-Fly!
        // --------------------------------------------------------------------
        if (cf == CF_HDROP && seed_.kind == SuperpositionKind::LocalFiles) {
            if (!(pformatetcIn->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
            pmedium->tymed = TYMED_HGLOBAL;
            pmedium->hGlobal = BuildCfHDropHGlobal(seed_.filePaths);
            if (!pmedium->hGlobal) return E_OUTOFMEMORY;
            RecordCollapse(obs, cf, "shell_cmd", "Local CF_HDROP Native File Copy",
                           ElapsedMs(tStart), seed_.seedRawBytes, 0, WideToUtf8(seed_.filePaths[0]));
            return S_OK;
        }

        if (cf == rf.cfFileDescriptorW) {
            return SynthesizeFileDescriptorW(pformatetcIn, pmedium, obs, tStart);
        }

        if (cf == rf.cfFileContents) {
            return SynthesizeFileContentsStream(pformatetcIn, pmedium, obs, tStart);
        }

        if (cf == rf.cfPng || cf == CF_DIBV5 || cf == CF_DIB) {
            return SynthesizeImageFormat(cf, pformatetcIn, pmedium, obs, tStart);
        }

        if (cf == rf.cfHtml) {
            return SynthesizeHtmlFormat(pformatetcIn, pmedium, obs, tStart);
        }

        if (cf == rf.cfCsv) {
            return SynthesizeCsvFormat(pformatetcIn, pmedium, obs, tStart);
        }

        if (cf == CF_UNICODETEXT || cf == CF_TEXT) {
            return SynthesizeTextForObserver(cf, pformatetcIn, pmedium, obs, tStart);
        }

        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* pformatetc) override {
        if (!pformatetc) return E_INVALIDARG;
        if (!(pformatetc->dwAspect & DVASPECT_CONTENT)) return DV_E_DVASPECT;

        const auto& rf = RegisteredFormats::Instance();
        UINT cf = pformatetc->cfFormat;

        // Check lindex rules: CFSTR_FILECONTENTS uses lindex == 0 (or -1), others use -1
        if (cf == rf.cfFileContents) {
            if (pformatetc->lindex != 0 && pformatetc->lindex != -1) return DV_E_LINDEX;
        } else {
            if (pformatetc->lindex != -1) return DV_E_LINDEX;
        }

        // Always serve shield/envelope/ancillary SetData formats (e.g. "Ole Private Data")
        if (cf == rf.cfEnvelope || cf == rf.cfCanIncludeHistory ||
            cf == rf.cfCanUploadCloud || cf == rf.cfExcludeMonitor) {
            return (pformatetc->tymed & TYMED_HGLOBAL) ? S_OK : DV_E_TYMED;
        }
        {
            std::lock_guard<std::mutex> lk(lazyCacheMu_);
            if (ancillaryBlobs_.count(cf)) {
                return (pformatetc->tymed & TYMED_HGLOBAL) ? S_OK : DV_E_TYMED;
            }
        }

        if (observerMgr_) {
            ObserverSnapshot obs = observerMgr_->Capture(true);
            // QueryGetData must remain strictly zero-network / sub-millisecond!
            if (!IsFormatAllowedForObserver(cf, obs, false)) {
                return DV_E_FORMATETC;
            }
        }

        for (const auto& f : formats_) {
            if (f.cfFormat == cf) {
                if ((f.tymed & pformatetc->tymed) != 0) return S_OK;
                return DV_E_TYMED;
            }
        }
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* pformatetcOut) override {
        if (!pformatetcOut) return E_INVALIDARG;
        pformatetcOut->ptd = nullptr;
        return DATA_S_SAMEFORMATETC;
    }

    HRESULT STDMETHODCALLTYPE SetData(FORMATETC* pformatetc, STGMEDIUM* pmedium, BOOL fRelease) override {
        // Persist ancillary HGLOBAL blobs stored by OLE/Shell (e.g. "Ole Private Data",
        // "Performed DropEffect", "Paste Succeeded") so CClipDataObject & Explorer preserve exact FORMATETC state!
        if (!pformatetc || !pmedium) return E_INVALIDARG;
        if (pmedium->tymed == TYMED_HGLOBAL && pmedium->hGlobal != nullptr) {
            size_t sz = GlobalSize(pmedium->hGlobal);
            const uint8_t* ptr = static_cast<const uint8_t*>(GlobalLock(pmedium->hGlobal));
            if (ptr && sz > 0) {
                std::lock_guard<std::mutex> lk(lazyCacheMu_);
                ancillaryBlobs_[pformatetc->cfFormat].assign(ptr, ptr + sz);
                GlobalUnlock(pmedium->hGlobal);
            }
        }
        if (fRelease) {
            ReleaseStgMedium(pmedium);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppenumFormatEtc) override {
        if (!ppenumFormatEtc) return E_POINTER;
        if (dwDirection == DATADIR_GET) {
            *ppenumFormatEtc = new NativeEnumFormatEtc(formats_);
            return S_OK;
        }
        *ppenumFormatEtc = nullptr;
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

private:
    std::atomic<ULONG> refCount_;
    QuantumSeed seed_;
    ObserverContextManager* observerMgr_ = nullptr;
    std::shared_ptr<TelemetryHub> telemetry_;
    FILETIME armedFileTime_ = {};
    std::vector<FORMATETC> formats_;

    // Lazy paste-time caches (NEVER populated at Ctrl+C!)
    std::mutex lazyCacheMu_;
    bool hasProbedUrlHead_ = false;
    UrlHeadMetadata cachedUrlHead_;
    std::map<UINT, std::vector<uint8_t>> ancillaryBlobs_;

    static FORMATETC MakeFmt(UINT cf, DWORD tymed = TYMED_HGLOBAL, LONG lindex = -1) {
        FORMATETC f = {};
        f.cfFormat = static_cast<CLIPFORMAT>(cf);
        f.ptd = nullptr;
        f.dwAspect = DVASPECT_CONTENT;
        f.lindex = lindex;
        f.tymed = tymed;
        return f;
    }

    UrlHeadMetadata EnsureUrlHeadProbed() {
        std::lock_guard<std::mutex> lk(lazyCacheMu_);
        if (!hasProbedUrlHead_) {
            cachedUrlHead_ = ProbeUrlHeadMetadata(seed_.rawText, seed_.isGitHubRepoUrl);
            hasProbedUrlHead_ = true;
        }
        return cachedUrlHead_;
    }

    static bool HasObviousNonImageExtension(const std::string& rawUrl) {
        std::string path = rawUrl;
        size_t q = path.find_first_of("?#");
        if (q != std::string::npos) path = path.substr(0, q);
        size_t slash = path.rfind('/');
        std::string base = (slash != std::string::npos) ? path.substr(slash + 1) : path;
        size_t dot = base.rfind('.');
        if (dot == std::string::npos || dot + 1 >= base.size()) return false;
        std::string ext = ToLowerAscii(base.substr(dot));
        static const std::set<std::string> kNonImageExts = {
            ".bin", ".zip", ".tar", ".gz", ".tgz", ".7z", ".rar", ".xz", ".bz2",
            ".exe", ".msi", ".dll", ".iso", ".img", ".dmg", ".deb", ".rpm",
            ".pdf", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx",
            ".mp4", ".mkv", ".mov", ".avi", ".webm", ".mp3", ".wav", ".flac",
            ".csv", ".tsv", ".json", ".xml", ".yaml", ".yml", ".txt", ".md", ".log", ".sql"
        };
        return kNonImageExts.count(ext) > 0;
    }

    // Strictly paste-time check: static image suffix OR (when allowed) HTTP HEAD Content-Type: image/*
    bool IsRemoteUrlImageAtPasteTime(bool allowNetworkProbe) {
        if (seed_.kind != SuperpositionKind::RemoteUrl || seed_.isGitHubRepoUrl) return false;
        if (seed_.looksLikeImageUrl) return true;
        if (HasObviousNonImageExtension(seed_.rawText)) return false;
        {
            std::lock_guard<std::mutex> lk(lazyCacheMu_);
            if (hasProbedUrlHead_) {
                return StartsWithIgnoreCase(cachedUrlHead_.contentType, "image/");
            }
        }
        if (!allowNetworkProbe) return false;
        UrlHeadMetadata head = EnsureUrlHeadProbed();
        return StartsWithIgnoreCase(head.contentType, "image/");
    }

    bool SupportsVirtualFileStream() const {
        return seed_.kind == SuperpositionKind::RemoteUrl ||
               seed_.kind == SuperpositionKind::StructuredData ||
               seed_.kind == SuperpositionKind::VisualSource ||
               seed_.kind == SuperpositionKind::CurlCommand ||
               seed_.kind == SuperpositionKind::HexColor;
    }

    bool SupportsAutoVisualImageCollapse(bool allowNetworkProbe) {
        if (seed_.kind == SuperpositionKind::VisualSource ||
            seed_.kind == SuperpositionKind::HexColor ||
            seed_.kind == SuperpositionKind::CurlCommand) {
            return true;
        }
        if (seed_.kind == SuperpositionKind::RemoteUrl && !seed_.isGitHubRepoUrl) {
            return IsRemoteUrlImageAtPasteTime(allowNetworkProbe);
        }
        return false;
    }

    bool IsFormatAllowedForObserver(UINT cf, const ObserverSnapshot& obs, bool allowNetworkProbe) {
        const auto& rf = RegisteredFormats::Instance();
        if (obs.isBlockedSniffer) return false;

        // 1. Raw Bypass Mode
        if (obs.lockMode == ManualLockMode::RawBypass) {
            if (cf == CF_HDROP && seed_.kind == SuperpositionKind::LocalFiles) return true;
            return (cf == CF_UNICODETEXT || cf == CF_TEXT);
        }

        // 2. Manual Lock: 2x PNG Visual Card (works on any seed!)
        if (obs.lockMode == ManualLockMode::LockPngCard) {
            return (cf == rf.cfPng || cf == CF_DIBV5 || cf == CF_DIB);
        }

        // 3. Manual Lock: Excel Multi-Column Grid
        if (obs.lockMode == ManualLockMode::LockExcel) {
            if (seed_.kind == SuperpositionKind::StructuredData && (cf == rf.cfHtml || cf == rf.cfCsv)) {
                return true;
            }
            return (cf == CF_UNICODETEXT || cf == CF_TEXT);
        }

        // 4. Manual Lock: Explicit Code / Markdown / SQL Eigenstate Locks
        if (obs.lockMode != ManualLockMode::Auto) {
            return (cf == CF_UNICODETEXT || cf == CF_TEXT);
        }

        // 5. Auto Observer Mode Routing
        if (cf == CF_HDROP) {
            if (seed_.kind != SuperpositionKind::LocalFiles) return false;
            if (obs.category == HostCategory::Terminal) return false;
            if ((obs.lang == MicroLangContext::HtmlCss || obs.lang == MicroLangContext::Markdown) &&
                seed_.isLocalImageFile && obs.category != HostCategory::FileExplorer) {
                return false;
            }
            return true;
        }

        if (cf == rf.cfFileDescriptorW || cf == rf.cfFileContents || cf == rf.cfPreferredDropEffect) {
            if (!SupportsVirtualFileStream()) return false;
            return (obs.category == HostCategory::FileExplorer || obs.category == HostCategory::PlainGeneric);
        }

        if (cf == rf.cfPng || cf == CF_DIBV5 || cf == CF_DIB) {
            if (obs.category != HostCategory::VisualCanvasOrChat && obs.category != HostCategory::PlainGeneric) {
                return false;
            }
            // In QueryGetData (allowNetworkProbe == false) for extensionless URLs in a chat/canvas host,
            // tentatively advertise image availability so the host calls GetData(PNG) where HEAD is probed lazily.
            if (!allowNetworkProbe && seed_.kind == SuperpositionKind::RemoteUrl &&
                !seed_.isGitHubRepoUrl && obs.category == HostCategory::VisualCanvasOrChat &&
                !HasObviousNonImageExtension(seed_.rawText)) {
                return true;
            }
            return SupportsAutoVisualImageCollapse(allowNetworkProbe);
        }

        if (cf == rf.cfHtml) {
            if (seed_.kind == SuperpositionKind::StructuredData) {
                return (obs.category == HostCategory::Spreadsheet ||
                        obs.category == HostCategory::RichDocument ||
                        obs.category == HostCategory::PlainGeneric);
            }
            if (seed_.kind == SuperpositionKind::VisualSource) {
                return (obs.category == HostCategory::RichDocument ||
                        obs.category == HostCategory::PlainGeneric);
            }
            return false;
        }

        if (cf == rf.cfCsv) {
            if (seed_.kind != SuperpositionKind::StructuredData) return false;
            return (obs.category == HostCategory::Spreadsheet || obs.category == HostCategory::PlainGeneric);
        }

        if (cf == CF_UNICODETEXT || cf == CF_TEXT) {
            if (obs.category == HostCategory::VisualCanvasOrChat && SupportsAutoVisualImageCollapse(allowNetworkProbe)) {
                return false;
            }
            return true;
        }

        return false;
    }

    void BuildAdvertisedFormatList() {
        const auto& rf = RegisteredFormats::Instance();
        formats_.clear();

        // 1. Private Envelope & Microsoft Premature-Collapse Shield Formats
        formats_.push_back(MakeFmt(rf.cfEnvelope, TYMED_HGLOBAL));
        formats_.push_back(MakeFmt(rf.cfCanIncludeHistory, TYMED_HGLOBAL));
        formats_.push_back(MakeFmt(rf.cfCanUploadCloud, TYMED_HGLOBAL));
        formats_.push_back(MakeFmt(rf.cfExcludeMonitor, TYMED_HGLOBAL));

        // 2. Local File Drop (CF_HDROP)
        if (seed_.kind == SuperpositionKind::LocalFiles) {
            formats_.push_back(MakeFmt(CF_HDROP, TYMED_HGLOBAL));
        }

        // 3. Spreadsheet & Rich Document Formats
        if (seed_.kind == SuperpositionKind::StructuredData) {
            formats_.push_back(MakeFmt(rf.cfHtml, TYMED_HGLOBAL));
            formats_.push_back(MakeFmt(rf.cfCsv, TYMED_HGLOBAL));
        } else if (seed_.kind == SuperpositionKind::VisualSource) {
            formats_.push_back(MakeFmt(rf.cfHtml, TYMED_HGLOBAL));
        }

        // 4. Visual Image Formats (PNG + CF_DIBV5 + CF_DIB; gated lazily by observer & lockMode!)
        formats_.push_back(MakeFmt(rf.cfPng, TYMED_HGLOBAL));
        formats_.push_back(MakeFmt(CF_DIBV5, TYMED_HGLOBAL));
        formats_.push_back(MakeFmt(CF_DIB, TYMED_HGLOBAL));

        // 5. Universal Polymorphic Text Formats
        formats_.push_back(MakeFmt(CF_UNICODETEXT, TYMED_HGLOBAL));
        formats_.push_back(MakeFmt(CF_TEXT, TYMED_HGLOBAL));

        // 6. Virtual Streaming File Formats (CFSTR_FILEDESCRIPTORW + CFSTR_FILECONTENTS via TYMED_ISTREAM)
        if (SupportsVirtualFileStream()) {
            formats_.push_back(MakeFmt(rf.cfFileDescriptorW, TYMED_HGLOBAL, -1));
            formats_.push_back(MakeFmt(rf.cfFileContents, TYMED_ISTREAM, 0));
            formats_.push_back(MakeFmt(rf.cfFileContents, TYMED_ISTREAM, -1));
            formats_.push_back(MakeFmt(rf.cfPreferredDropEffect, TYMED_HGLOBAL, -1));
        }
    }

    uint64_t RecordCollapse(const ObserverSnapshot& obs,
                            UINT cf,
                            const std::string& orbitId,
                            const std::string& transformerName,
                            double ms,
                            uint64_t outBytes,
                            uint64_t savedDiskBytes,
                            const std::string& snippet) {
        if (!telemetry_) return 0;
        CollapseEvent ev;
        ev.timestamp = NowTimestampString();
        ev.observerProcess = obs.processName;
        ev.observerWindow = obs.windowTitle;
        ev.detectedMicroContext = obs.detectedFile.empty() ? MicroLangName(obs.lang) : obs.detectedFile;
        ev.requestedFormat = FormatClipboardCodeName(cf);
        ev.orbitId = orbitId;
        ev.transformerName = transformerName;
        ev.synthesisMs = ms;
        ev.outputBytes = outBytes;
        ev.zeroDiskSavedBytes = savedDiskBytes;
        std::string cleanSnip = ReplaceAll(ReplaceAll(snippet, "\r", ""), "\n", " ");
        if (cleanSnip.size() > 180) cleanSnip = cleanSnip.substr(0, 177) + "...";
        ev.previewSnippet = cleanSnip;
        return telemetry_->RecordEvent(std::move(ev));
    }

    HRESULT DeliverUtf8Text(UINT cf,
                            FORMATETC* pfmt,
                            STGMEDIUM* pmedium,
                            const std::string& utf8Text,
                            const ObserverSnapshot& obs,
                            const std::string& orbitId,
                            const std::string& transformerName,
                            std::chrono::steady_clock::time_point tStart) {
        if (!(pfmt->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
        pmedium->tymed = TYMED_HGLOBAL;
        size_t byteCount = 0;
        if (cf == CF_UNICODETEXT) {
            std::wstring w = Utf8ToWide(utf8Text);
            byteCount = (w.size() + 1) * sizeof(wchar_t);
            pmedium->hGlobal = AllocHGlobalWideString(w);
        } else {
            byteCount = utf8Text.size() + 1;
            pmedium->hGlobal = AllocHGlobalBytes(utf8Text.c_str(), byteCount);
        }
        if (!pmedium->hGlobal) return E_OUTOFMEMORY;

        RecordCollapse(obs, cf, orbitId, transformerName, ElapsedMs(tStart), byteCount, 0, utf8Text);
        return S_OK;
    }

    // ------------------------------------------------------------------------
    // Polymorphic Text Collapse Router (CF_UNICODETEXT / CF_TEXT)
    // ------------------------------------------------------------------------
    HRESULT SynthesizeTextForObserver(UINT cf,
                                      FORMATETC* pfmt,
                                      STGMEDIUM* pmedium,
                                      const ObserverSnapshot& obs,
                                      std::chrono::steady_clock::time_point tStart) {
        switch (seed_.kind) {
            case SuperpositionKind::RemoteUrl: {
                if (obs.category == HostCategory::Terminal) {
                    std::string cmd = CollapseUrlToTerminalCommand(seed_.rawText, seed_.isGitHubRepoUrl, obs.processName);
                    return DeliverUtf8Text(cf, pfmt, pmedium, cmd, obs, "shell_cmd",
                                           seed_.isGitHubRepoUrl ? "URL -> git clone" : "URL -> Resumable curl -fLO", tStart);
                }
                if (obs.lang == MicroLangContext::Markdown) {
                    std::string md = CollapseUrlToMarkdown(seed_.rawText, seed_.isGitHubRepoUrl);
                    return DeliverUtf8Text(cf, pfmt, pmedium, md, obs, "rich_doc",
                                           "URL -> Markdown Smart Link", tStart);
                }
                return DeliverUtf8Text(cf, pfmt, pmedium, seed_.rawText, obs, "shell_cmd",
                                       "URL -> Direct URL Eigenstate", tStart);
            }

            case SuperpositionKind::StructuredData: {
                ValNode ast;
                if (!ParseStructuredSeed(seed_, ast)) {
                    return DeliverUtf8Text(cf, pfmt, pmedium, seed_.rawText, obs, "ts_interface",
                                           "Structured Fallback -> Raw Text", tStart);
                }
                if (obs.lang == MicroLangContext::TypeScript || obs.lang == MicroLangContext::JavaScript) {
                    std::string ts = CollapseToTypeScript(ast, "QuantumSchema");
                    return DeliverUtf8Text(cf, pfmt, pmedium, ts, obs, "ts_interface",
                                           "Structured AST -> TypeScript Interface", tStart);
                }
                if (obs.lang == MicroLangContext::Python) {
                    std::string py = CollapseToPythonDataclass(ast, "QuantumModel");
                    return DeliverUtf8Text(cf, pfmt, pmedium, py, obs, "py_model",
                                           "Structured AST -> Python @dataclass", tStart);
                }
                if (obs.lang == MicroLangContext::Go) {
                    std::string go = CollapseToGoStruct(ast, "QuantumRecord");
                    return DeliverUtf8Text(cf, pfmt, pmedium, go, obs, "go_rust",
                                           "Structured AST -> Go JSON Struct", tStart);
                }
                if (obs.lang == MicroLangContext::Rust) {
                    std::string rs = CollapseToRustStruct(ast, "QuantumRecord");
                    return DeliverUtf8Text(cf, pfmt, pmedium, rs, obs, "go_rust",
                                           "Structured AST -> Rust Serde Struct", tStart);
                }
                if (obs.lang == MicroLangContext::SQL || obs.category == HostCategory::DatabaseClient) {
                    std::string sql = CollapseToSql(ast);
                    return DeliverUtf8Text(cf, pfmt, pmedium, sql, obs, "sql_schema",
                                           "Structured AST -> SQL DDL + Batch INSERT", tStart);
                }
                if (obs.category == HostCategory::Spreadsheet) {
                    std::string tsv = CollapseToDelimited(ast, '\t');
                    return DeliverUtf8Text(cf, pfmt, pmedium, tsv, obs, "excel_grid",
                                           "Structured AST -> Flattened TSV Grid", tStart);
                }
                if (obs.lang == MicroLangContext::Markdown) {
                    std::string md = CollapseToMarkdownTable(ast);
                    return DeliverUtf8Text(cf, pfmt, pmedium, md, obs, "rich_doc",
                                           "Structured AST -> Markdown Pipe Table", tStart);
                }
                return DeliverUtf8Text(cf, pfmt, pmedium, seed_.rawText, obs, "ts_interface",
                                       "Structured Seed -> Original Text", tStart);
            }

            case SuperpositionKind::VisualSource: {
                return DeliverUtf8Text(cf, pfmt, pmedium, seed_.rawText, obs, "rich_doc",
                                       "Visual Source -> Pure Source Code", tStart);
            }

            case SuperpositionKind::CurlCommand: {
                if (obs.lang == MicroLangContext::Python) {
                    std::string py = CollapseCurlToPythonRequests(seed_.rawText);
                    return DeliverUtf8Text(cf, pfmt, pmedium, py, obs, "py_model",
                                           "cURL -> Python requests", tStart);
                }
                if (obs.lang == MicroLangContext::TypeScript || obs.lang == MicroLangContext::JavaScript) {
                    std::string js = CollapseCurlToFetch(seed_.rawText);
                    return DeliverUtf8Text(cf, pfmt, pmedium, js, obs, "ts_interface",
                                           "cURL -> Modern Async fetch()", tStart);
                }
                if (obs.lang == MicroLangContext::HttpRest) {
                    std::string httpDoc = CollapseCurlToHttpFile(seed_.rawText);
                    return DeliverUtf8Text(cf, pfmt, pmedium, httpDoc, obs, "istream_file",
                                           "cURL -> REST Client .http", tStart);
                }
                return DeliverUtf8Text(cf, pfmt, pmedium, seed_.rawText, obs, "shell_cmd",
                                       "cURL -> Executable Shell cURL", tStart);
            }

            case SuperpositionKind::LocalFiles: {
                if ((obs.lang == MicroLangContext::HtmlCss || obs.lang == MicroLangContext::Markdown) &&
                    seed_.isLocalImageFile && !seed_.filePaths.empty()) {
                    std::string dataUri = CollapseLocalFileToDataUri(seed_.filePaths[0]);
                    return DeliverUtf8Text(cf, pfmt, pmedium, dataUri, obs, "base64_inline",
                                           "Local Image -> Lazy Base64 Data URI", tStart);
                }
                std::string escaped = CollapseLocalPathsForTerminal(seed_.filePaths, obs.processName);
                return DeliverUtf8Text(cf, pfmt, pmedium, escaped, obs, "shell_cmd",
                                       "Local File -> Shell-Escaped Path", tStart);
            }

            case SuperpositionKind::HexColor: {
                return DeliverUtf8Text(cf, pfmt, pmedium, Trim(seed_.rawText), obs, "base64_inline",
                                       "#HEX -> Literal Color Code", tStart);
            }

            case SuperpositionKind::UnixTimestamp: {
                std::string tsOut = CollapseUnixTimestamp(seed_.rawText, obs);
                return DeliverUtf8Text(cf, pfmt, pmedium, tsOut, obs, "sql_schema",
                                       "Epoch -> Contextual Timestamp", tStart);
            }

            default:
                return DeliverUtf8Text(cf, pfmt, pmedium, seed_.rawText, obs, "rich_doc",
                                       "Plain Text Eigenstate", tStart);
        }
    }

    // ------------------------------------------------------------------------
    // CF_HTML & CSV Collapse Routers
    // ------------------------------------------------------------------------
    HRESULT SynthesizeHtmlFormat(FORMATETC* pfmt,
                                 STGMEDIUM* pmedium,
                                 const ObserverSnapshot& obs,
                                 std::chrono::steady_clock::time_point tStart) {
        if (!(pfmt->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
        const auto& rf = RegisteredFormats::Instance();

        if (seed_.kind == SuperpositionKind::StructuredData) {
            ValNode ast;
            if (!ParseStructuredSeed(seed_, ast)) return DV_E_FORMATETC;
            std::string cfHtml = CollapseToCfHtmlTable(ast);
            pmedium->tymed = TYMED_HGLOBAL;
            pmedium->hGlobal = AllocHGlobalBytes(cfHtml.data(), cfHtml.size() + 1);
            if (!pmedium->hGlobal) return E_OUTOFMEMORY;
            RecordCollapse(obs, rf.cfHtml, "excel_grid", "Structured AST -> CF_HTML Table",
                           ElapsedMs(tStart), cfHtml.size(), 0, cfHtml.substr(0, 160));
            return S_OK;
        }

        if (seed_.kind == SuperpositionKind::VisualSource) {
            std::string cfHtml = CollapseCodeToCfHtml(seed_.rawText);
            pmedium->tymed = TYMED_HGLOBAL;
            pmedium->hGlobal = AllocHGlobalBytes(cfHtml.data(), cfHtml.size() + 1);
            if (!pmedium->hGlobal) return E_OUTOFMEMORY;
            RecordCollapse(obs, rf.cfHtml, "rich_doc", "Visual Source -> Highlighted CF_HTML",
                           ElapsedMs(tStart), cfHtml.size(), 0, seed_.rawText);
            return S_OK;
        }
        return DV_E_FORMATETC;
    }

    HRESULT SynthesizeCsvFormat(FORMATETC* pfmt,
                                STGMEDIUM* pmedium,
                                const ObserverSnapshot& obs,
                                std::chrono::steady_clock::time_point tStart) {
        if (!(pfmt->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
        if (seed_.kind != SuperpositionKind::StructuredData) return DV_E_FORMATETC;

        ValNode ast;
        if (!ParseStructuredSeed(seed_, ast)) return DV_E_FORMATETC;
        std::string csv = CollapseToDelimited(ast, ',');
        pmedium->tymed = TYMED_HGLOBAL;
        pmedium->hGlobal = AllocHGlobalBytes(csv.data(), csv.size() + 1);
        if (!pmedium->hGlobal) return E_OUTOFMEMORY;

        RecordCollapse(obs, RegisteredFormats::Instance().cfCsv, "excel_grid",
                       "Structured AST -> RFC4180 CSV", ElapsedMs(tStart), csv.size(), 0, csv);
        return S_OK;
    }

    // ------------------------------------------------------------------------
    // Visual Bitmap Synthesis Router (PNG + CF_DIBV5 + CF_DIB)
    // ------------------------------------------------------------------------
    RenderedImageBundle SynthesizeVisualBundle(std::string& outDesc, ManualLockMode lockMode = ManualLockMode::Auto) {
        if (seed_.kind == SuperpositionKind::HexColor) {
            outDesc = "#HEX -> 2x Designer Swatch PNG";
            return RenderColorSwatchBitmap2x(seed_.rawText);
        }
        if (seed_.kind == SuperpositionKind::VisualSource) {
            if (seed_.visualSyntax == VisualSyntax::Svg) {
                outDesc = "SVG Vector -> 2x Transparent PNG";
                return RenderSvgBitmap2x(seed_.rawText);
            }
            if (seed_.visualSyntax == VisualSyntax::Mermaid) {
                outDesc = "Mermaid -> 2x Flowchart PNG";
                return RenderMermaidBitmap2x(seed_.rawText);
            }
            if (seed_.visualSyntax == VisualSyntax::MathLatex) {
                outDesc = "Math LaTeX -> 2x Typeset Formula Card PNG";
                return RenderMathFormulaBitmap2x(seed_.rawText);
            }
            outDesc = "Source Code -> 2x Syntax Card PNG";
            return RenderCodeCardBitmap2x(seed_.rawText, "SCHRODINGER CODE CARD // 2X HIDPI");
        }
        if (seed_.kind == SuperpositionKind::CurlCommand) {
            outDesc = "cURL -> 2x API Request Card PNG";
            return RenderCodeCardBitmap2x(seed_.rawText, "NETWORK CURL // 2X HIDPI");
        }
        if (seed_.kind == SuperpositionKind::RemoteUrl && !seed_.isGitHubRepoUrl && IsRemoteUrlImageAtPasteTime(true)) {
            outDesc = "Remote Image URL -> Lazy Decoded Bitmap";
            std::vector<uint8_t> rawImg;
            if (FetchRemoteImageBytesOnDemand(seed_.rawText, rawImg)) {
                return DecodeRawImageBytesToBundle(rawImg);
            }
        }
        if (lockMode == ManualLockMode::LockPngCard && !seed_.rawText.empty()) {
            outDesc = "Locked Visual Eigenstate -> 2x Syntax Card PNG";
            return RenderCodeCardBitmap2x(seed_.rawText, "LOCKED 2X VISUAL CARD // HIDPI");
        }
        return RenderedImageBundle{};
    }

    HRESULT SynthesizeImageFormat(UINT cf,
                                  FORMATETC* pfmt,
                                  STGMEDIUM* pmedium,
                                  const ObserverSnapshot& obs,
                                  std::chrono::steady_clock::time_point tStart) {
        if (!(pfmt->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
        std::string desc;
        RenderedImageBundle bundle = SynthesizeVisualBundle(desc, obs.lockMode);
        if (!bundle.valid) return DV_E_FORMATETC;

        const auto& rf = RegisteredFormats::Instance();
        const std::vector<uint8_t>* targetBytes = nullptr;
        if (cf == rf.cfPng) targetBytes = &bundle.pngBytes;
        else if (cf == CF_DIBV5) targetBytes = &bundle.dibV5Bytes;
        else if (cf == CF_DIB) targetBytes = &bundle.dibBytes;
        if (!targetBytes || targetBytes->empty()) return DV_E_FORMATETC;

        pmedium->tymed = TYMED_HGLOBAL;
        pmedium->hGlobal = AllocHGlobalBytes(targetBytes->data(), targetBytes->size());
        if (!pmedium->hGlobal) return E_OUTOFMEMORY;

        char snip[128];
        std::snprintf(snip, sizeof(snip), "[Synthesized %dx%d 32bpp Image, %zu bytes]",
                      bundle.width, bundle.height, targetBytes->size());
        RecordCollapse(obs, cf, "png_card", desc, ElapsedMs(tStart),
                       targetBytes->size(), targetBytes->size(), snip);
        return S_OK;
    }

    // ------------------------------------------------------------------------
    // Virtual File Streaming Router (CFSTR_FILEDESCRIPTORW + CFSTR_FILECONTENTS)
    // ------------------------------------------------------------------------
    struct VirtualFileSpec {
        bool isRemoteStream = false;
        std::wstring fileName;
        bool hasKnownSize = false;
        uint64_t fileSize = 0;
        std::vector<uint8_t> generatedBytes;
        std::string transformerDesc;
    };

    bool PrepareVirtualFileSpec(VirtualFileSpec& spec, bool needPayloadBytes) {
        if (seed_.kind == SuperpositionKind::RemoteUrl && !seed_.isGitHubRepoUrl) {
            UrlHeadMetadata head = EnsureUrlHeadProbed();
            spec.isRemoteStream = true;
            spec.fileName = head.suggestedFilename.empty()
                                ? SanitizeWindowsFilename(Utf8ToWide(GuessUrlFilenameUtf8(seed_.rawText, false)))
                                : head.suggestedFilename;
            spec.hasKnownSize = head.hasContentLength;
            spec.fileSize = head.contentLength;
            spec.transformerDesc = "Remote URL -> WinHTTP Chunked TYMED_ISTREAM";
            return true;
        }

        if (seed_.kind == SuperpositionKind::RemoteUrl && seed_.isGitHubRepoUrl) {
            std::string slug = ReplaceAll(GuessUrlFilenameUtf8(seed_.rawText, true), "/", "_");
            spec.fileName = SanitizeWindowsFilename(Utf8ToWide(slug + ".url"), L"repository.url");
            std::string urlShortcut = "[InternetShortcut]\r\nURL=" + Trim(seed_.rawText) + "\r\n";
            spec.generatedBytes.assign(urlShortcut.begin(), urlShortcut.end());
            spec.hasKnownSize = true;
            spec.fileSize = spec.generatedBytes.size();
            spec.transformerDesc = "GitHub Repo -> Virtual .url Shortcut Stream";
            return true;
        }

        if (seed_.kind == SuperpositionKind::StructuredData) {
            ValNode ast;
            if (!ParseStructuredSeed(seed_, ast)) return false;
            spec.fileName = L"schrodinger_table.xlsx";
            spec.generatedBytes = CollapseToStyledXlsxBytes(ast);
            spec.hasKnownSize = true;
            spec.fileSize = spec.generatedBytes.size();
            spec.transformerDesc = "Structured AST -> Styled OpenXML .xlsx Stream";
            (void)needPayloadBytes;
            return true;
        }

        if (seed_.kind == SuperpositionKind::VisualSource) {
            if (seed_.visualSyntax == VisualSyntax::Svg) {
                spec.fileName = L"quantum_vector.svg";
                spec.generatedBytes.assign(seed_.rawText.begin(), seed_.rawText.end());
                spec.hasKnownSize = true;
                spec.fileSize = spec.generatedBytes.size();
                spec.transformerDesc = "SVG Source -> Virtual .svg File Stream";
                return true;
            }
            std::string desc;
            RenderedImageBundle bundle = SynthesizeVisualBundle(desc);
            if (!bundle.valid) return false;
            if (seed_.visualSyntax == VisualSyntax::Mermaid) {
                spec.fileName = L"mermaid_diagram_2x.png";
            } else if (seed_.visualSyntax == VisualSyntax::MathLatex) {
                spec.fileName = L"quantum_formula_2x.png";
            } else {
                spec.fileName = L"quantum_code_card_2x.png";
            }
            spec.generatedBytes = std::move(bundle.pngBytes);
            spec.hasKnownSize = true;
            spec.fileSize = spec.generatedBytes.size();
            spec.transformerDesc = "Visual Source -> 2x HiDPI .png File Stream";
            return true;
        }

        if (seed_.kind == SuperpositionKind::CurlCommand) {
            std::string httpContent = CollapseCurlToHttpFile(seed_.rawText);
            spec.fileName = L"api_request.http";
            spec.generatedBytes.assign(httpContent.begin(), httpContent.end());
            spec.hasKnownSize = true;
            spec.fileSize = spec.generatedBytes.size();
            spec.transformerDesc = "cURL -> REST Client .http File Stream";
            return true;
        }

        if (seed_.kind == SuperpositionKind::HexColor) {
            ParsedColor pc = ParseHexColorDetails(seed_.rawText);
            RenderedImageBundle bundle = RenderColorSwatchBitmap2x(seed_.rawText);
            if (!bundle.valid) return false;
            std::string hexClean = pc.normalizedHex.substr(1);
            spec.fileName = SanitizeWindowsFilename(Utf8ToWide("swatch_" + hexClean + ".png"), L"swatch.png");
            spec.generatedBytes = std::move(bundle.pngBytes);
            spec.hasKnownSize = true;
            spec.fileSize = spec.generatedBytes.size();
            spec.transformerDesc = "#HEX -> 2x Swatch .png File Stream";
            return true;
        }

        return false;
    }

    HRESULT SynthesizeFileDescriptorW(FORMATETC* pfmt,
                                      STGMEDIUM* pmedium,
                                      const ObserverSnapshot& obs,
                                      std::chrono::steady_clock::time_point tStart) {
        if (!(pfmt->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
        if (pfmt->lindex != -1) return DV_E_LINDEX;
        if (!SupportsVirtualFileStream()) return DV_E_FORMATETC;

        VirtualFileSpec spec;
        if (!PrepareVirtualFileSpec(spec, false)) return DV_E_FORMATETC;

        size_t totalBytes = sizeof(FILEGROUPDESCRIPTORW);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, totalBytes);
        if (!hMem) return E_OUTOFMEMORY;

        auto* fgd = static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(hMem));
        if (!fgd) {
            GlobalFree(hMem);
            return E_OUTOFMEMORY;
        }

        fgd->cItems = 1;
        FILEDESCRIPTORW& fd = fgd->fgd[0];
        fd.dwFlags = FD_ATTRIBUTES | FD_WRITESTIME | FD_PROGRESSUI | (spec.hasKnownSize ? FD_FILESIZE : 0);
        fd.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        fd.ftLastWriteTime = armedFileTime_;
        fd.ftCreationTime = armedFileTime_;
        fd.ftLastAccessTime = armedFileTime_;
        if (spec.hasKnownSize) {
            fd.nFileSizeHigh = static_cast<DWORD>(spec.fileSize >> 32);
            fd.nFileSizeLow = static_cast<DWORD>(spec.fileSize & 0xFFFFFFFFULL);
        }
        wcsncpy_s(fd.cFileName, MAX_PATH, spec.fileName.c_str(), _TRUNCATE);
        GlobalUnlock(hMem);

        pmedium->tymed = TYMED_HGLOBAL;
        pmedium->hGlobal = hMem;

        std::string descInfo = WideToUtf8(spec.fileName) +
                               (spec.hasKnownSize ? (" (" + std::to_string(spec.fileSize) + " bytes)") : " (chunked)");
        RecordCollapse(obs, RegisteredFormats::Instance().cfFileDescriptorW, "istream_file",
                       spec.transformerDesc + " [Descriptor]", ElapsedMs(tStart), sizeof(FILEGROUPDESCRIPTORW), 0, descInfo);
        return S_OK;
    }

    HRESULT SynthesizeFileContentsStream(FORMATETC* pfmt,
                                         STGMEDIUM* pmedium,
                                         const ObserverSnapshot& obs,
                                         std::chrono::steady_clock::time_point tStart) {
        if (pfmt->lindex != 0 && pfmt->lindex != -1) return DV_E_LINDEX;
        if (!SupportsVirtualFileStream()) return DV_E_FORMATETC;

        VirtualFileSpec spec;
        if (!PrepareVirtualFileSpec(spec, true)) return DV_E_FORMATETC;

        const UINT cf = RegisteredFormats::Instance().cfFileContents;

        // Prefer TYMED_ISTREAM (True zero-buffer streaming!)
        if (pfmt->tymed & TYMED_ISTREAM) {
            pmedium->tymed = TYMED_ISTREAM;
            pmedium->pUnkForRelease = nullptr;

            if (spec.isRemoteStream) {
                uint64_t evId = RecordCollapse(
                    obs, cf, "istream_file", spec.transformerDesc, ElapsedMs(tStart), 0, 0,
                    "[Streaming Active] Pumping " + WideToUtf8(spec.fileName) + " via 64KB~256KB WinHTTP IStream...");
                std::weak_ptr<TelemetryHub> weakHub = telemetry_;
                pmedium->pstm = new HttpChunkedStream(
                    seed_.rawText,
                    spec.fileName,
                    spec.hasKnownSize,
                    spec.fileSize,
                    armedFileTime_,
                    [weakHub, evId](uint64_t bytesStreamed, double totalMs) {
                        if (auto hub = weakHub.lock(); hub && evId != 0) {
                            hub->UpdateStreamCompletion(evId, bytesStreamed, totalMs);
                        }
                    });
                return S_OK;
            } else {
                size_t genSz = spec.generatedBytes.size();
                pmedium->pstm = new SynthesizedMemoryStream(
                    std::move(spec.generatedBytes), spec.fileName, armedFileTime_);
                RecordCollapse(obs, cf, "istream_file", spec.transformerDesc,
                               ElapsedMs(tStart), genSz, genSz,
                               "Streamed virtual file: " + WideToUtf8(spec.fileName));
                return S_OK;
            }
        }

        // Fallback if a legacy non-Explorer consumer requests HGLOBAL on small generated files
        if (!spec.isRemoteStream && (pfmt->tymed & TYMED_HGLOBAL)) {
            pmedium->tymed = TYMED_HGLOBAL;
            pmedium->hGlobal = AllocHGlobalBytes(spec.generatedBytes.data(), spec.generatedBytes.size());
            if (!pmedium->hGlobal) return E_OUTOFMEMORY;
            RecordCollapse(obs, cf, "istream_file", spec.transformerDesc + " [HGLOBAL]",
                           ElapsedMs(tStart), spec.generatedBytes.size(), spec.generatedBytes.size(),
                           WideToUtf8(spec.fileName));
            return S_OK;
        }

        return DV_E_TYMED;
    }
};

} // namespace sclip

#pragma once
#include "../common.hpp"
#include "../transformers/url_transform.hpp"

namespace sclip {

using StreamCompleteHook = std::function<void(uint64_t totalBytes, double elapsedMs)>;

// True chunked network-to-disk COM IStream (zero temp file, < 1 MB RAM for multi-GB files)
class HttpChunkedStream final : public IStream {
public:
    HttpChunkedStream(std::string url,
                      std::wstring fileName,
                      bool hasKnownSize,
                      uint64_t knownSize,
                      FILETIME writeTime,
                      StreamCompleteHook onComplete = nullptr)
        : refCount_(1),
          url_(std::move(url)),
          fileName_(std::move(fileName)),
          hasKnownSize_(hasKnownSize),
          knownSize_(knownSize),
          writeTime_(writeTime),
          onComplete_(std::move(onComplete)),
          startClock_(std::chrono::steady_clock::now()) {}

    ~HttpChunkedStream() {
        CloseHttpHandles();
    }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_ISequentialStream) ||
            IsEqualIID(riid, IID_IStream)) {
            *ppvObject = static_cast<IStream*>(this);
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
        if (c == 0) {
            if (!reportedComplete_ && totalReadBytes_ > 0 && onComplete_) {
                reportedComplete_ = true;
                onComplete_(totalReadBytes_, ElapsedMs(startClock_));
            }
            delete this;
        }
        return c;
    }

    // ISequentialStream
    HRESULT STDMETHODCALLTYPE Read(void* pv, ULONG cb, ULONG* pcbRead) override {
        if (pcbRead) *pcbRead = 0;
        if (!pv) return STG_E_INVALIDPOINTER;
        if (cb == 0) return S_OK;
        if (eof_) return S_FALSE;

        if (!hRequest_) {
            if (!OpenHttpAtOffset(currentPos_)) {
                eof_ = true;
                return STG_E_READFAULT;
            }
        }

        // Stream in 64 KB ~ 256 KB bounded chunks so memory stays strictly < 1 MB
        const ULONG maxChunk = 262144UL; // 256 KB
        ULONG target = std::min<ULONG>(cb, maxChunk);
        uint8_t* dst = static_cast<uint8_t*>(pv);
        ULONG accumulated = 0;

        while (accumulated < target) {
            DWORD got = 0;
            ULONG ask = std::min<ULONG>(target - accumulated, 65536UL);
            if (!WinHttpReadData(hRequest_, dst + accumulated, ask, &got)) {
                if (accumulated > 0) break;
                CloseHttpHandles();
                eof_ = true;
                return STG_E_READFAULT;
            }
            if (got == 0) {
                eof_ = true;
                CloseHttpHandles();
                break;
            }
            accumulated += got;
            currentPos_ += got;
            totalReadBytes_ += got;
        }

        if (pcbRead) *pcbRead = accumulated;

        if (eof_ && !reportedComplete_ && onComplete_) {
            reportedComplete_ = true;
            onComplete_(totalReadBytes_, ElapsedMs(startClock_));
        }
        return (accumulated == cb) ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG* pcbWritten) override {
        if (pcbWritten) *pcbWritten = 0;
        return STG_E_ACCESSDENIED;
    }

    // IStream
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER* plibNewPosition) override {
        int64_t target = 0;
        switch (dwOrigin) {
            case STREAM_SEEK_SET:
                target = dlibMove.QuadPart;
                break;
            case STREAM_SEEK_CUR:
                target = static_cast<int64_t>(currentPos_) + dlibMove.QuadPart;
                break;
            case STREAM_SEEK_END:
                if (!hasKnownSize_) return E_NOTIMPL;
                target = static_cast<int64_t>(knownSize_) + dlibMove.QuadPart;
                break;
            default:
                return STG_E_INVALIDFUNCTION;
        }
        if (target < 0) return STG_E_INVALIDFUNCTION;

        uint64_t uTarget = static_cast<uint64_t>(target);
        if (uTarget != currentPos_) {
            // Re-position lazily on next Read() via HTTP Range or fresh GET
            CloseHttpHandles();
            currentPos_ = uTarget;
            eof_ = (hasKnownSize_ && knownSize_ > 0 && currentPos_ >= knownSize_);
        }
        if (plibNewPosition) {
            plibNewPosition->QuadPart = currentPos_;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }

    HRESULT STDMETHODCALLTYPE CopyTo(IStream* pstm, ULARGE_INTEGER cb,
                                     ULARGE_INTEGER* pcbRead, ULARGE_INTEGER* pcbWritten) override {
        if (!pstm) return STG_E_INVALIDPOINTER;
        uint64_t remaining = cb.QuadPart;
        uint64_t totalR = 0, totalW = 0;
        std::vector<uint8_t> chunkBuf(131072); // 128 KB fixed streaming buffer

        while (remaining > 0) {
            ULONG ask = static_cast<ULONG>(std::min<uint64_t>(remaining, chunkBuf.size()));
            ULONG got = 0;
            HRESULT hr = Read(chunkBuf.data(), ask, &got);
            if (FAILED(hr)) return hr;
            if (got == 0) break;
            totalR += got;
            ULONG written = 0;
            hr = pstm->Write(chunkBuf.data(), got, &written);
            if (FAILED(hr)) return hr;
            totalW += written;
            remaining -= got;
            if (got < ask) break;
        }
        if (pcbRead) pcbRead->QuadPart = totalR;
        if (pcbWritten) pcbWritten->QuadPart = totalW;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }

    HRESULT STDMETHODCALLTYPE Stat(STATSTG* pstatstg, DWORD grfStatFlag) override {
        if (!pstatstg) return STG_E_INVALIDPOINTER;
        std::memset(pstatstg, 0, sizeof(STATSTG));
        pstatstg->type = STGTY_STREAM;
        pstatstg->cbSize.QuadPart = hasKnownSize_ ? knownSize_ : currentPos_;
        pstatstg->mtime = writeTime_;
        pstatstg->ctime = writeTime_;
        pstatstg->atime = writeTime_;
        pstatstg->grfMode = STGM_READ | STGM_SHARE_DENY_WRITE;
        if (!(grfStatFlag & STATFLAG_NONAME) && !fileName_.empty()) {
            size_t bytes = (fileName_.size() + 1) * sizeof(wchar_t);
            wchar_t* nameCopy = static_cast<wchar_t*>(CoTaskMemAlloc(bytes));
            if (!nameCopy) return E_OUTOFMEMORY;
            std::memcpy(nameCopy, fileName_.c_str(), bytes);
            pstatstg->pwcsName = nameCopy;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Clone(IStream** ppstm) override {
        if (!ppstm) return STG_E_INVALIDPOINTER;
        auto* cloned = new HttpChunkedStream(url_, fileName_, hasKnownSize_, knownSize_, writeTime_, nullptr);
        cloned->currentPos_ = currentPos_;
        *ppstm = cloned;
        return S_OK;
    }

private:
    std::atomic<ULONG> refCount_;
    std::string url_;
    std::wstring fileName_;
    bool hasKnownSize_ = false;
    uint64_t knownSize_ = 0;
    FILETIME writeTime_ = {};
    StreamCompleteHook onComplete_;
    std::chrono::steady_clock::time_point startClock_;

    HINTERNET hSession_ = nullptr;
    HINTERNET hConnect_ = nullptr;
    HINTERNET hRequest_ = nullptr;
    uint64_t currentPos_ = 0;
    uint64_t totalReadBytes_ = 0;
    bool eof_ = false;
    bool reportedComplete_ = false;

    void CloseHttpHandles() {
        if (hRequest_) { WinHttpCloseHandle(hRequest_); hRequest_ = nullptr; }
        if (hConnect_) { WinHttpCloseHandle(hConnect_); hConnect_ = nullptr; }
        if (hSession_) { WinHttpCloseHandle(hSession_); hSession_ = nullptr; }
    }

    bool OpenHttpAtOffset(uint64_t offset) {
        CloseHttpHandles();
        CrackedUrl cu = CrackHttpUrl(url_);
        if (!cu.valid) return false;

        hSession_ = WinHttpOpen(L"SchrodingerClip/1.0 (TYMED_ISTREAM)",
                                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession_) return false;
        WinHttpSetTimeouts(hSession_, 4000, 5000, 10000, 30000);

        hConnect_ = WinHttpConnect(hSession_, cu.host.c_str(), cu.port, 0);
        if (!hConnect_) { CloseHttpHandles(); return false; }

        DWORD flags = cu.isHttps ? WINHTTP_FLAG_SECURE : 0;
        hRequest_ = WinHttpOpenRequest(hConnect_, L"GET", cu.pathAndQuery.c_str(),
                                       nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest_) { CloseHttpHandles(); return false; }

        if (cu.isHttps) {
            DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                             SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                             SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
            WinHttpSetOption(hRequest_, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
        }

        std::wstring rangeHeader;
        if (offset > 0) {
            rangeHeader = L"Range: bytes=" + std::to_wstring(offset) + L"-\r\n";
        }

        LPCWSTR hdrPtr = rangeHeader.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : rangeHeader.c_str();
        DWORD hdrLen = rangeHeader.empty() ? 0 : static_cast<DWORD>(rangeHeader.size());

        if (!WinHttpSendRequest(hRequest_, hdrPtr, hdrLen, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(hRequest_, nullptr)) {
            CloseHttpHandles();
            return false;
        }

        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(hRequest_, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        if (status < 200 || status >= 400) {
            CloseHttpHandles();
            return false;
        }

        if (!hasKnownSize_ && offset == 0) {
            wchar_t lenBuf[64] = {};
            DWORD lenBytes = sizeof(lenBuf);
            if (WinHttpQueryHeaders(hRequest_, WINHTTP_QUERY_CONTENT_LENGTH,
                                    WINHTTP_HEADER_NAME_BY_INDEX, lenBuf, &lenBytes, WINHTTP_NO_HEADER_INDEX)) {
                try {
                    knownSize_ = std::stoull(lenBuf);
                    hasKnownSize_ = true;
                } catch (...) {}
            }
        }
        return true;
    }
};

// On-demand synthesized virtual file COM IStream (.xlsx, .svg, .png, .http, .url)
class SynthesizedMemoryStream final : public IStream {
public:
    SynthesizedMemoryStream(std::vector<uint8_t> payload,
                            std::wstring fileName,
                            FILETIME writeTime)
        : refCount_(1),
          data_(std::make_shared<std::vector<uint8_t>>(std::move(payload))),
          fileName_(std::move(fileName)),
          writeTime_(writeTime),
          pos_(0) {}

    SynthesizedMemoryStream(std::shared_ptr<std::vector<uint8_t>> sharedPayload,
                            std::wstring fileName,
                            FILETIME writeTime,
                            size_t initialPos)
        : refCount_(1),
          data_(std::move(sharedPayload)),
          fileName_(std::move(fileName)),
          writeTime_(writeTime),
          pos_(initialPos) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_ISequentialStream) ||
            IsEqualIID(riid, IID_IStream)) {
            *ppvObject = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount_; }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG c = --refCount_;
        if (c == 0) delete this;
        return c;
    }

    HRESULT STDMETHODCALLTYPE Read(void* pv, ULONG cb, ULONG* pcbRead) override {
        if (pcbRead) *pcbRead = 0;
        if (!pv) return STG_E_INVALIDPOINTER;
        if (cb == 0) return S_OK;
        if (!data_ || pos_ >= data_->size()) return S_FALSE;

        size_t avail = data_->size() - pos_;
        ULONG toCopy = static_cast<ULONG>(std::min<size_t>(avail, cb));
        std::memcpy(pv, data_->data() + pos_, toCopy);
        pos_ += toCopy;
        if (pcbRead) *pcbRead = toCopy;
        return (toCopy == cb) ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG* pcbWritten) override {
        if (pcbWritten) *pcbWritten = 0;
        return STG_E_ACCESSDENIED;
    }

    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER* plibNewPosition) override {
        int64_t sz = data_ ? static_cast<int64_t>(data_->size()) : 0;
        int64_t target = 0;
        switch (dwOrigin) {
            case STREAM_SEEK_SET: target = dlibMove.QuadPart; break;
            case STREAM_SEEK_CUR: target = static_cast<int64_t>(pos_) + dlibMove.QuadPart; break;
            case STREAM_SEEK_END: target = sz + dlibMove.QuadPart; break;
            default: return STG_E_INVALIDFUNCTION;
        }
        if (target < 0) return STG_E_INVALIDFUNCTION;
        pos_ = static_cast<size_t>(std::min<int64_t>(target, sz));
        if (plibNewPosition) plibNewPosition->QuadPart = pos_;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }

    HRESULT STDMETHODCALLTYPE CopyTo(IStream* pstm, ULARGE_INTEGER cb,
                                     ULARGE_INTEGER* pcbRead, ULARGE_INTEGER* pcbWritten) override {
        if (!pstm) return STG_E_INVALIDPOINTER;
        size_t avail = (data_ && pos_ < data_->size()) ? (data_->size() - pos_) : 0;
        size_t n = static_cast<size_t>(std::min<uint64_t>(cb.QuadPart, avail));
        ULONG written = 0;
        if (n > 0) {
            HRESULT hr = pstm->Write(data_->data() + pos_, static_cast<ULONG>(n), &written);
            if (FAILED(hr)) return hr;
            pos_ += n;
        }
        if (pcbRead) pcbRead->QuadPart = n;
        if (pcbWritten) pcbWritten->QuadPart = written;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }

    HRESULT STDMETHODCALLTYPE Stat(STATSTG* pstatstg, DWORD grfStatFlag) override {
        if (!pstatstg) return STG_E_INVALIDPOINTER;
        std::memset(pstatstg, 0, sizeof(STATSTG));
        pstatstg->type = STGTY_STREAM;
        pstatstg->cbSize.QuadPart = data_ ? data_->size() : 0;
        pstatstg->mtime = writeTime_;
        pstatstg->ctime = writeTime_;
        pstatstg->atime = writeTime_;
        pstatstg->grfMode = STGM_READ | STGM_SHARE_DENY_WRITE;
        if (!(grfStatFlag & STATFLAG_NONAME) && !fileName_.empty()) {
            size_t bytes = (fileName_.size() + 1) * sizeof(wchar_t);
            wchar_t* nameCopy = static_cast<wchar_t*>(CoTaskMemAlloc(bytes));
            if (!nameCopy) return E_OUTOFMEMORY;
            std::memcpy(nameCopy, fileName_.c_str(), bytes);
            pstatstg->pwcsName = nameCopy;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Clone(IStream** ppstm) override {
        if (!ppstm) return STG_E_INVALIDPOINTER;
        *ppstm = new SynthesizedMemoryStream(data_, fileName_, writeTime_, pos_);
        return S_OK;
    }

private:
    std::atomic<ULONG> refCount_;
    std::shared_ptr<std::vector<uint8_t>> data_;
    std::wstring fileName_;
    FILETIME writeTime_ = {};
    size_t pos_ = 0;
};

} // namespace sclip

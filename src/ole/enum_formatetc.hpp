#pragma once
#include "../common.hpp"

namespace sclip {

class NativeEnumFormatEtc final : public IEnumFORMATETC {
public:
    explicit NativeEnumFormatEtc(std::vector<FORMATETC> formats, size_t cursor = 0)
        : refCount_(1), formats_(std::move(formats)), cursor_(cursor) {
        for (auto& f : formats_) {
            f.ptd = nullptr;
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumFORMATETC)) {
            *ppvObject = static_cast<IEnumFORMATETC*>(this);
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

    HRESULT STDMETHODCALLTYPE Next(ULONG celt, FORMATETC* rgelt, ULONG* pceltFetched) override {
        if (pceltFetched) *pceltFetched = 0;
        if (celt == 0) return S_OK;
        if (!rgelt || (celt > 1 && !pceltFetched)) return E_INVALIDARG;

        ULONG copied = 0;
        while (copied < celt && cursor_ < formats_.size()) {
            rgelt[copied] = formats_[cursor_];
            rgelt[copied].ptd = nullptr;
            ++copied;
            ++cursor_;
        }
        if (pceltFetched) *pceltFetched = copied;
        return (copied == celt) ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Skip(ULONG celt) override {
        if (cursor_ + celt > formats_.size()) {
            cursor_ = formats_.size();
            return S_FALSE;
        }
        cursor_ += celt;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Reset() override {
        cursor_ = 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** ppenum) override {
        if (!ppenum) return E_POINTER;
        *ppenum = new NativeEnumFormatEtc(formats_, cursor_);
        return S_OK;
    }

private:
    std::atomic<ULONG> refCount_;
    std::vector<FORMATETC> formats_;
    size_t cursor_ = 0;
};

} // namespace sclip

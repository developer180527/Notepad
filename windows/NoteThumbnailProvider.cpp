// Windows thumbnail handler for Notepad .note files.
//
// Implements IThumbnailProvider + IInitializeWithStream as an in-process COM
// server. Explorer hands us the file as an IStream; we walk the .note bytes to
// the embedded preview PNG (v3+), decode it with WIC and return an HBITMAP.
//
// The CLSID below must match the one the app writes during self-registration
// (see winregister.cpp). The app registers this DLL under that CLSID and points
// the .note thumbnail handler at it, so regsvr32 is not strictly required —
// but DllRegisterServer/DllUnregisterServer are provided for completeness.
//
// NOTE: builds on Windows (MSVC) only; untested on the authoring machine (macOS).

#include <windows.h>
#include <shlwapi.h>
#include <thumbcache.h>
#include <wincodec.h>
#include <new>
#include <vector>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "windowscodecs.lib")

// {7E4F9A2C-3B1D-4E6A-9C7F-2A5B8D1E0F33}
static const CLSID CLSID_NoteThumbProvider = {
    0x7e4f9a2c, 0x3b1d, 0x4e6a, {0x9c, 0x7f, 0x2a, 0x5b, 0x8d, 0x1e, 0x0f, 0x33}};

static const wchar_t *kCLSIDString = L"{7E4F9A2C-3B1D-4E6A-9C7F-2A5B8D1E0F33}";
static const wchar_t *kFriendlyName = L"Notepad Note Thumbnail Handler";

static long g_dllRefs = 0;

// --------------------------------------------------------------- .note parsing

static uint32_t ReadU32BE(const BYTE *p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

// Returns the embedded preview PNG bytes, or an empty vector.
static std::vector<BYTE> ExtractPreviewPng(const std::vector<BYTE> &file) {
    const BYTE *p = file.data();
    const size_t n = file.size();
    size_t off = 0;
    auto need = [&](size_t k) { return off + k <= n; };

    if (!need(4)) return {};
    uint32_t magicLen = ReadU32BE(p + off); off += 4;
    if (magicLen != 6 || !need(magicLen) || memcmp(p + off, "PPNOTE", 6) != 0) return {};
    off += magicLen;
    if (!need(4)) return {};
    uint32_t version = ReadU32BE(p + off); off += 4;
    if (version < 3) return {};
    if (!need(4)) return {};
    uint32_t previewLen = ReadU32BE(p + off); off += 4;
    if (previewLen == 0 || previewLen == 0xFFFFFFFFu || !need(previewLen)) return {};
    return std::vector<BYTE>(p + off, p + off + previewLen);
}

// --------------------------------------------------------------- the provider

class NoteThumbProvider : public IThumbnailProvider, public IInitializeWithStream {
public:
    NoteThumbProvider() : m_refs(1), m_stream(nullptr) { InterlockedIncrement(&g_dllRefs); }
    ~NoteThumbProvider() { if (m_stream) m_stream->Release(); InterlockedDecrement(&g_dllRefs); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
        static const QITAB qit[] = {
            QITABENT(NoteThumbProvider, IThumbnailProvider),
            QITABENT(NoteThumbProvider, IInitializeWithStream),
            {0},
        };
        return QISearch(this, qit, riid, ppv);
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG r = InterlockedDecrement(&m_refs);
        if (r == 0) delete this;
        return r;
    }

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream *pstream, DWORD) override {
        if (m_stream) return E_UNEXPECTED;
        if (!pstream) return E_INVALIDARG;
        m_stream = pstream;
        m_stream->AddRef();
        return S_OK;
    }

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP *phbmp, WTS_ALPHATYPE *pdwAlpha) override;

private:
    long m_refs;
    IStream *m_stream;
};

static HRESULT ReadStream(IStream *s, std::vector<BYTE> &out) {
    STATSTG st = {};
    if (FAILED(s->Stat(&st, STATFLAG_NONAME))) return E_FAIL;
    ULONGLONG size = st.cbSize.QuadPart;
    if (size == 0 || size > (64ull * 1024 * 1024)) return E_FAIL; // sanity cap 64MB
    out.resize((size_t)size);
    LARGE_INTEGER zero = {};
    s->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG read = 0, total = 0;
    while (total < out.size()) {
        HRESULT hr = s->Read(out.data() + total, (ULONG)(out.size() - total), &read);
        if (FAILED(hr) || read == 0) break;
        total += read;
    }
    out.resize(total);
    return total ? S_OK : E_FAIL;
}

IFACEMETHODIMP NoteThumbProvider::GetThumbnail(UINT cx, HBITMAP *phbmp, WTS_ALPHATYPE *pdwAlpha) {
    *phbmp = nullptr;
    *pdwAlpha = WTSAT_ARGB;
    if (!m_stream) return E_UNEXPECTED;

    std::vector<BYTE> file;
    if (FAILED(ReadStream(m_stream, file))) return E_FAIL;
    std::vector<BYTE> png = ExtractPreviewPng(file);
    if (png.empty()) return E_FAIL;

    IWICImagingFactory *factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return hr;

    IWICStream *wicStream = nullptr;
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICFormatConverter *converter = nullptr;
    IWICBitmapScaler *scaler = nullptr;
    IWICBitmapSource *source = nullptr;

    hr = factory->CreateStream(&wicStream);
    if (SUCCEEDED(hr)) hr = wicStream->InitializeFromMemory(png.data(), (DWORD)png.size());
    if (SUCCEEDED(hr)) hr = factory->CreateDecoderFromStream(wicStream, nullptr,
                                                             WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);

    UINT w = 0, h = 0;
    if (SUCCEEDED(hr)) hr = frame->GetSize(&w, &h);

    // Scale to fit cx preserving aspect ratio.
    UINT tw = w, th = h;
    if (SUCCEEDED(hr) && cx > 0 && (w > cx || h > cx)) {
        double scale = (w >= h) ? double(cx) / w : double(cx) / h;
        tw = (UINT)(w * scale + 0.5);
        th = (UINT)(h * scale + 0.5);
        if (tw == 0) tw = 1;
        if (th == 0) th = 1;
        hr = factory->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(hr)) hr = scaler->Initialize(frame, tw, th, WICBitmapInterpolationModeFant);
        source = scaler;
    } else {
        source = frame;
    }

    if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) hr = converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA,
                                                  WICBitmapDitherTypeNone, nullptr, 0.0,
                                                  WICBitmapPaletteTypeMedianCut);

    if (SUCCEEDED(hr)) {
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = (LONG)tw;
        bmi.bmiHeader.biHeight = -(LONG)th; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void *bits = nullptr;
        HBITMAP hbmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (hbmp && bits) {
            const UINT stride = tw * 4;
            hr = converter->CopyPixels(nullptr, stride, stride * th, (BYTE *)bits);
            if (SUCCEEDED(hr)) *phbmp = hbmp;
            else DeleteObject(hbmp);
        } else {
            hr = E_OUTOFMEMORY;
        }
    }

    if (converter) converter->Release();
    if (scaler) scaler->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (wicStream) wicStream->Release();
    factory->Release();
    return *phbmp ? S_OK : hr;
}

// --------------------------------------------------------------- class factory

class NoteClassFactory : public IClassFactory {
public:
    NoteClassFactory() : m_refs(1) { InterlockedIncrement(&g_dllRefs); }
    ~NoteClassFactory() { InterlockedDecrement(&g_dllRefs); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
    IFACEMETHODIMP_(ULONG) Release() override {
        ULONG r = InterlockedDecrement(&m_refs);
        if (r == 0) delete this;
        return r;
    }
    IFACEMETHODIMP CreateInstance(IUnknown *outer, REFIID riid, void **ppv) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        NoteThumbProvider *p = new (std::nothrow) NoteThumbProvider();
        if (!p) return E_OUTOFMEMORY;
        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL lock) override {
        if (lock) InterlockedIncrement(&g_dllRefs);
        else InterlockedDecrement(&g_dllRefs);
        return S_OK;
    }

private:
    long m_refs;
};

// --------------------------------------------------------------- DLL exports

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv) {
    if (rclsid != CLSID_NoteThumbProvider) return CLASS_E_CLASSNOTAVAILABLE;
    NoteClassFactory *f = new (std::nothrow) NoteClassFactory();
    if (!f) return E_OUTOFMEMORY;
    HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

STDAPI DllCanUnloadNow() { return g_dllRefs == 0 ? S_OK : S_FALSE; }

static HMODULE g_module = nullptr;

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}

static LONG SetReg(HKEY root, const wchar_t *sub, const wchar_t *name, const wchar_t *val) {
    HKEY key;
    LONG r = RegCreateKeyExW(root, sub, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (r != ERROR_SUCCESS) return r;
    r = RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)val,
                       (DWORD)((wcslen(val) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return r;
}

// Registers the CLSID -> this DLL (per-user, no admin). The app also does this;
// either is sufficient. Hooks the .note thumbnail handler to our CLSID.
STDAPI DllRegisterServer() {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(g_module, path, MAX_PATH)) return HRESULT_FROM_WIN32(GetLastError());

    wchar_t clsidKey[128];
    wsprintfW(clsidKey, L"Software\\Classes\\CLSID\\%s", kCLSIDString);
    wchar_t inprocKey[160];
    wsprintfW(inprocKey, L"%s\\InprocServer32", clsidKey);

    SetReg(HKEY_CURRENT_USER, clsidKey, nullptr, kFriendlyName);
    SetReg(HKEY_CURRENT_USER, inprocKey, nullptr, path);
    SetReg(HKEY_CURRENT_USER, inprocKey, L"ThreadingModel", L"Apartment");

    // .note -> thumbnail handler {e357fccd-a995-4576-b01f-234630154e96}
    SetReg(HKEY_CURRENT_USER,
           L"Software\\Classes\\.note\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}",
           nullptr, kCLSIDString);
    return S_OK;
}

STDAPI DllUnregisterServer() {
    wchar_t clsidKey[160];
    wsprintfW(clsidKey, L"Software\\Classes\\CLSID\\%s\\InprocServer32", kCLSIDString);
    RegDeleteTreeW(HKEY_CURRENT_USER, clsidKey);
    wsprintfW(clsidKey, L"Software\\Classes\\CLSID\\%s", kCLSIDString);
    RegDeleteTreeW(HKEY_CURRENT_USER, clsidKey);
    RegDeleteTreeW(HKEY_CURRENT_USER,
                   L"Software\\Classes\\.note\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}");
    return S_OK;
}

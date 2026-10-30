// Cobalt platform katmani — vendor kaynak: third_party/cobalt/src/main.cpp
// (platform namespace birebir; D3D global'leri aegHost uzerinden baglanir)
#include "platform.h"
#include "gfx_host.hpp"
#include "imgui.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <vector>

using namespace aegHost;

static std::wstring Widen(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::string Narrow(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

namespace platform {

ImTextureID CreateTexture(const unsigned char* rgba, int w, int h)
{
    if (!g_device) return 0;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub = {};
    sub.pSysMem = rgba;
    sub.SysMemPitch = w * 4;

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(g_device->CreateTexture2D(&desc, &sub, &tex))) return 0;

    D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = 1;
    ID3D11ShaderResourceView* srv = nullptr;
    g_device->CreateShaderResourceView(tex, &sv, &srv);
    tex->Release();
    return (ImTextureID)(uintptr_t)srv;
}

ImTextureID LoadTextureFile(const std::string& utf8Path, int* outW, int* outH)
{
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return 0;

    ImTextureID result = 0;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    std::wstring wpath = Widen(utf8Path);

    if (SUCCEEDED(factory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(factory->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
    {
        UINT w = 0, h = 0;
        conv->GetSize(&w, &h);
        std::vector<unsigned char> px((size_t)w * h * 4);
        if (SUCCEEDED(conv->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data())))
        {
            result = CreateTexture(px.data(), (int)w, (int)h);
            if (outW) *outW = (int)w;
            if (outH) *outH = (int)h;
        }
    }
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    factory->Release();
    return result;
}

std::string ExeDir()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    PathRemoveFileSpecW(buf);
    return Narrow(buf);
}

bool FileExists(const std::string& utf8Path)
{
    DWORD a = GetFileAttributesW(Widen(utf8Path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::string FindAsset(const char* relative)
{
    std::string exe = ExeDir();
    const std::string candidates[] = {
        exe + "\\" + relative,
        exe + "\\..\\" + relative,
        exe + "\\..\\..\\" + relative,
        std::string(relative),
    };
    for (const std::string& c : candidates)
        if (FileExists(c)) return c;
    return {};
}

std::string SystemFont(const char* fileName)
{
    wchar_t win[MAX_PATH];
    GetWindowsDirectoryW(win, MAX_PATH);
    std::string p = Narrow(win) + "\\Fonts\\" + fileName;
    return FileExists(p) ? p : std::string();
}

std::string ReadFile(const std::string& utf8Path)
{
    HANDLE f = CreateFileW(Widen(utf8Path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    std::string out;
    DWORD size = GetFileSize(f, nullptr), read = 0;
    if (size != INVALID_FILE_SIZE && size > 0)
    {
        out.resize(size);
        ::ReadFile(f, &out[0], size, &read, nullptr);
        out.resize(read);
    }
    CloseHandle(f);
    return out;
}

bool WriteFile(const std::string& utf8Path, const std::string& data)
{
    HANDLE f = CreateFileW(Widen(utf8Path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = ::WriteFile(f, data.data(), (DWORD)data.size(), &written, nullptr);
    CloseHandle(f);
    return ok && written == data.size();
}

std::string UserName()
{
    wchar_t buf[256];
    DWORD n = 256;
    if (GetUserNameW(buf, &n)) return Narrow(buf);
    return "User";
}

std::string HardwareId()
{
    DWORD serial = 0;
    GetVolumeInformationW(L"C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0);
    wchar_t pc[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(pc, &n);
    unsigned int h = 2166136261u ^ serial;
    for (DWORD i = 0; i < n; i++) { h ^= pc[i]; h *= 16777619u; }
    unsigned int h2 = h * 2654435761u ^ (serial >> 3);
    char out[32];
    snprintf(out, sizeof(out), "%04X-%04X-%04X-%04X", (h >> 16) & 0xFFFF, h & 0xFFFF, (h2 >> 16) & 0xFFFF, h2 & 0xFFFF);
    return out;
}

void Minimize() { ShowWindow(g_hwnd, SW_MINIMIZE); }
void Close()    { PostMessageW(g_hwnd, WM_CLOSE, 0, 0); }

void BeginWindowDrag()
{
    g_dragging = true;
    GetCursorPos(&g_dragCursor);
    GetWindowRect(g_hwnd, &g_dragRect);
}

void SetTopmost(bool on)
{
    SetWindowPos(g_hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void SetStreamProof(bool on)
{
    // WDA_EXCLUDEFROMCAPTURE (Windows 10 2004+). Falls back to WDA_MONITOR on older builds.
    if (!SetWindowDisplayAffinity(g_hwnd, on ? 0x00000011 : WDA_NONE) && on)
        SetWindowDisplayAffinity(g_hwnd, WDA_MONITOR);
}

float DpiScale() { return g_dpi; }

} // namespace platform

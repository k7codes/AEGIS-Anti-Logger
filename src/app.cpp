// AEGIS Anti Logger — Cobalt-stili kabuk (vendor ui katmanı birebir kullanılır)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdint>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// vendored Cobalt UI katmanı
#include "platform.h"
#include "config.h"
#include "ui/core.h"
#include "ui/widgets.h"
#include "ui/icons.h"
#include "ui/flags.h"
#include "lang/lang.h"

// AEGIS motorları
#include "common.hpp"
#include "config_store.hpp"
#include "persistence.hpp"
#include "hook_engine.hpp"
#include "noise_engine.hpp"

#include "gfx_host.hpp"

using namespace gfx;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static const int     kBaseW = 1060, kBaseH = 660;
constexpr float      kTitleH = 44.0f;
constexpr float      kSideW  = 214.0f;
static const char    kVersion[] = "v1.0";
static const wchar_t kWndClass[]  = L"AEGISAntiLogger";
constexpr UINT       WM_APP_SHOW  = WM_APP + 0x60;

namespace aegHost {
HWND                  g_hwnd = nullptr;
ID3D11Device*         g_device = nullptr;
ID3D11DeviceContext*  g_context = nullptr;
IDXGISwapChain*       g_swapChain = nullptr;
bool                  g_dragging = false;
POINT                 g_dragCursor = {};
RECT                  g_dragRect = {};
bool                  g_quit = false;
float                 g_dpi = 1.0f;
}

// ------------------------------------------------------------------ d3d helpers
static ID3D11RenderTargetView* g_rtv = nullptr;
static void CreateRenderTarget()
{
    ID3D11Texture2D* back = nullptr;
    aegHost::g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) { aegHost::g_device->CreateRenderTargetView(back, nullptr, &g_rtv); back->Release(); }
}
static void CleanupRenderTarget()
{
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}
static bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &aegHost::g_swapChain, &aegHost::g_device, &got, &aegHost::g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
                                           D3D11_SDK_VERSION, &sd, &aegHost::g_swapChain, &aegHost::g_device, &got, &aegHost::g_context);
    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return true;
}
static void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (aegHost::g_swapChain) { aegHost::g_swapChain->Release(); aegHost::g_swapChain = nullptr; }
    if (aegHost::g_context)   { aegHost::g_context->Release();   aegHost::g_context = nullptr; }
    if (aegHost::g_device)    { aegHost::g_device->Release();    aegHost::g_device = nullptr; }
}

// ------------------------------------------------------------------ app state
namespace {

enum Page { PG_GENEL, PG_MOTOR, PG_GORUNUM, PG_COUNT };

struct State
{
    float       screenT = 0.0f;
    Page        page = PG_GENEL;
    float       pageT = 1.0f;
    std::string user;

    // kilit ekrani
    bool        locked  = true;
    int         lockMode = 0;   // 0: ilk kurulum, 1: giris
    char        buf1[64] = {};
    char        buf2[64] = {};
    char        err[160] = {};
    float       lockT    = 0.0f;   // acilis animasyonu
    float       unlockT  = 0.0f;   // basarili giris fade'i
    float       shake    = 0.0f;
    // sifre degistirme diyalogu
    bool  pwOpen    = false;
    bool  pwFocus   = false;
    float pwT       = 0.0f;
    float pwShake   = 0.0f;
    char  pwCur[64]  = {};
    char  pwNew[64]  = {};
    char  pwNew2[64] = {};
    char  pwErr[160] = {};

    // kisi adi kucuk duzenleme
    bool        focusSet = false;
    bool        editName  = false;
    char        nameBuf[64] = {};
    bool        focusName = false;
} g;

float RealDt() { return ImMin(ImGui::GetIO().DeltaTime, 0.1f); }

bool  g_dirty  = false;
float g_saveCd = 0.0f;

void Publish(const aegis::Config& c)
{
    aegis::PublishConfigSnapshot(c);
    g_dirty = true;
}

// ---- AEGIS kalkanı (tema renkleriyle) ----
void DrawShield(ImDrawList* dl, ImVec2 c, float s, ImU32 fill)
{
    dl->PathClear();
    dl->PathLineTo(ImVec2(c.x - s,         c.y - s * 0.62f));
    dl->PathLineTo(ImVec2(c.x + s,         c.y - s * 0.62f));
    dl->PathLineTo(ImVec2(c.x + s * 0.90f, c.y + s * 0.22f));
    dl->PathLineTo(ImVec2(c.x,             c.y + s * 0.95f));
    dl->PathLineTo(ImVec2(c.x - s * 0.90f, c.y + s * 0.22f));
    dl->PathFillConvex(fill);
    const ImU32 dk = Col(theme::bg0);
    const float w = s * 0.44f;
    dl->AddLine(ImVec2(c.x - w * 0.75f, c.y + s * 0.02f), ImVec2(c.x - w * 0.08f, c.y + s * 0.38f), dk, S(3.2f));
    dl->AddLine(ImVec2(c.x - w * 0.08f, c.y + s * 0.38f), ImVec2(c.x + w * 0.90f, c.y - s * 0.28f), dk, S(3.2f));
}

std::string UptimeText()
{
    ULONGLONG start = aegis::stats().startTime.load(std::memory_order_relaxed);
    ULONGLONG sec = (start ? (GetTickCount64() - start) / 1000ull : 0ull);
    char buf[64];
    snprintf(buf, sizeof(buf), "%02llu:%02llu:%02llu",
             (unsigned long long)(sec / 3600), (unsigned long long)((sec / 60) % 60),
             (unsigned long long)(sec % 60));
    return buf;
}

// ---- canlı osiloskop: gerçek tuş vs gürültü akışı ----
void DrawActivityCanvas(float wFull)
{
    static const int N = 170;
    static float realBuf[N] = {}, noiseBuf[N] = {};
    static int   head = 0;
    static float lastPush = -1.0f;
    static uint64_t lastReal = 0, lastNoise = 0;

    const float now = (float)ImGui::GetTime();
    if (lastPush < 0.0f) { lastPush = now; lastReal = aegis::stats().realSeen.load(); lastNoise = aegis::stats().noiseSent.load(); }
    if (now - lastPush >= 0.10f) {
        const auto& st = aegis::stats();
        const float dts = (now - lastPush < 0.01f) ? 0.10f : (now - lastPush);
        const float rps = (float)(int64_t)(st.realSeen.load() - lastReal) / dts;
        const float nps = (float)(int64_t)(st.noiseSent.load() - lastNoise) / dts;
        lastReal = st.realSeen.load(); lastNoise = st.noiseSent.load();
        realBuf[head] = rps; noiseBuf[head] = nps;
        head = (head + 1) % N;
        lastPush = now;
    }

    const float H = S(100.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.065f, 0.090f, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##activity", ImVec2(wFull, H), ImGuiChildFlags_Borders);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetWindowPos();
    const ImVec2 sz = ImGui::GetWindowSize();

    const float padTop = S(16), padBot = S(16), padX = S(10);
    const float plotH  = sz.y - padTop - padBot;
    const float stepX  = (sz.x - 2 * padX - S(2)) / (float)(N - 1);

    float maxR = 1.0f, maxN = 1.0f;
    for (int k = 0; k < N; ++k) { maxR = ImMax(maxR, realBuf[k]); maxN = ImMax(maxN, noiseBuf[k]); }

    for (int gi = 0; gi < 4; ++gi) {
        float yy = p0.y + padTop + plotH * (gi / 3.0f);
        dl->AddLine(ImVec2(p0.x + padX, yy), ImVec2(p0.x + sz.x - padX, yy), Col(White(0.035f)), 1.0f);
    }

    auto buildPath = [&](float* buf, float maxRef) {
        dl->PathClear();
        for (int k = 0; k < N; ++k) {
            const int idx = (head + k) % N;
            const float x = p0.x + padX + k * stepX;
            const float v = buf[idx] / (maxRef * 1.15f);
            const float y = p0.y + sz.y - padBot - v * plotH;
            dl->PathLineTo(ImVec2(x, ImMax(p0.y + padTop, y)));
        }
    };

    // gürültü (amber) + gerçek tuş (accent) çizgileri
    buildPath(noiseBuf, maxN);
    dl->PathStroke(Col(theme::warning, 0.65f), 0, S(1.3f));
    buildPath(realBuf, maxR);
    dl->PathStroke(Col(theme::accent2, 0.95f), 0, S(2.2f));

    // gerçek tuş alt dolgusu
    dl->PathClear();
    for (int k = 0; k < N; ++k) {
        const int idx = (head + k) % N;
        const float x = p0.x + padX + k * stepX;
        const float v = realBuf[idx] / (maxR * 1.15f);
        const float y = p0.y + sz.y - padBot - v * plotH;
        dl->PathLineTo(ImVec2(x, ImMax(p0.y + padTop, y)));
    }
    dl->PathFillConvex(Col(theme::accent, 0.10f));

    // legend (şu anki değerler)
    const float lastR = realBuf[(head + N - 1) % N];
    const float lastN = noiseBuf[(head + N - 1) % N];
    char l2[24], l4[24];
    snprintf(l2, sizeof(l2), "%.0f/s", lastR);
    snprintf(l4, sizeof(l4), "%.0f/s", lastN);
    Text(dl, fonts::small, ImVec2(p0.x + padX + S(4),   p0.y + S(4)), Col(theme::accent2, 0.9f), "gerçek tuş");
    Text(dl, fonts::small, ImVec2(p0.x + padX + S(74),  p0.y + S(4)), Col(theme::text), l2);
    Text(dl, fonts::small, ImVec2(p0.x + padX + S(130), p0.y + S(4)), Col(theme::warning, 0.9f), "gürültü");
    Text(dl, fonts::small, ImVec2(p0.x + padX + S(196), p0.y + S(4)), Col(theme::text), l4);

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ------------------------------------------------------------------ title bar
void TitleBar(ImVec2 ds)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float th = S(kTitleH);
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(ds.x, th), Col(theme::bg0, 0.55f));
    Gradient(dl, ImVec2(0, th - S(1)), ImVec2(ds.x, th), Col(White(0.02f)), Col(White(0.08f)));

    const float tt = anim::Time();
    const float pulse = 0.5f + 0.5f * sinf(tt * 2.0f);
    const bool prot = aegis::SnapshotConfig().enabled;
    ImVec2 sc(S(25), th * 0.5f);
    if (prot) Glow(dl, sc, S(22 + 5 * pulse), Col(theme::accent, 0.35f));
    DrawIcon(dl, Ico_Shield, sc, S(19), Col(prot ? theme::accent : theme::textMute));
    float x = S(44);
    float w = Spaced(dl, fonts::smallBold, ImVec2(x, th * 0.5f - fonts::smallBold->LegacySize * 0.5f), Col(theme::text), "AEGIS", S(3));
    x += w + S(10);
    ImVec2 vs = Measure(fonts::small, kVersion);
    ImVec2 pa(x, th * 0.5f - S(10)), pb(x + vs.x + S(14), th * 0.5f + S(10));
    dl->AddRectFilled(pa, pb, Col(theme::accent, 0.14f), S(10));
    Text(dl, fonts::small, ImVec2(pa.x + S(7), pa.y + (S(20) - vs.y) * 0.5f), Col(theme::accent2), kVersion);

    // ortada durum
    const char* mid = prot ? "KORUMA AKTIF" : "KORUMA PASIF";
    ImVec2 ms = Measure(fonts::smallBold, mid);
    Text(dl, fonts::smallBold, ImVec2((ds.x - ms.x) * 0.5f, th * 0.5f - ms.y * 0.5f),
         Col(prot ? theme::success : theme::danger), mid);

    float bs = S(32), yb = (th - bs) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(ds.x - S(10) - bs * 2 - S(4), yb));
    if (ui::IconButton("##min", Ico_Minimize, bs, "Küçült")) platform::Minimize();
    ImGui::SetCursorScreenPos(ImVec2(ds.x - S(10) - bs, yb));
    if (ui::IconButton("##close", Ico_Close, bs, "Kapat", Col(theme::danger, 0.9f))) platform::Close();

    if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
        ImGui::IsMouseHoveringRect(ImVec2(0, 0), ImVec2(ds.x, th), false))
        platform::BeginWindowDrag();
}

// ------------------------------------------------------------------ sidebar
void Sidebar(ImVec2 ds)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a(0, S(kTitleH)), b(S(kSideW), ds.y);
    dl->AddRectFilled(a, b, Col(theme::bg0, 0.45f));
    Gradient(dl, ImVec2(b.x - S(1), a.y), b, Col(White(0.07f)), Col(White(0.01f)), 0.0f, true);
    Spaced(dl, fonts::smallBold, ImVec2(S(28), a.y + S(22)), Col(theme::textMute), "MENÜ", S(1.6f));

    static const char* names[PG_COUNT] = { "Genel", "Motor", "Görünüm" };
    static const Icon  icons[PG_COUNT] = { Ico_Home, Ico_Bolt, Ico_Gear };
    float y0 = a.y + S(48), ih = S(44), gap = S(6), iw = b.x - S(28);
    float indY = anim::To(ImGui::GetID("##navind"), y0 + (float)g.page * (ih + gap), 15.0f);
    ImVec2 ia(S(14), indY), ib(S(14) + iw, indY + ih);
    Gradient(dl, ia, ib, Col(theme::accent, 0.26f), Col(theme::accent, 0.02f), S(11));
    dl->AddRect(ia, ib, Col(theme::accent2, 0.12f), S(11));
    Glow(dl, ImVec2(ia.x, indY + ih * 0.5f), S(34), Col(theme::accent2, 0.35f));
    dl->AddRectFilled(ImVec2(ia.x, indY + S(12)), ImVec2(ia.x + S(3), ib.y - S(12)), Col(theme::accent2), S(2));

    for (int i = 0; i < PG_COUNT; i++)
    {
        ImVec2 p(S(14), y0 + i * (ih + gap));
        ImGui::SetCursorScreenPos(p);
        char id[16];
        snprintf(id, sizeof(id), "##nav%d", i);
        bool pressed = ImGui::InvisibleButton(id, ImVec2(iw, ih));
        ImGuiID gid = ImGui::GetItemID();
        bool hov = ImGui::IsItemHovered();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        float h = anim::To(anim::Key(gid, 0), hov ? 1.0f : 0.0f, 14.0f);
        float act = anim::To(anim::Key(gid, 1), g.page == i ? 1.0f : 0.0f, 12.0f);
        if (g.page != i) dl->AddRectFilled(p, p + ImVec2(iw, ih), Col(White(0.035f * h)), S(11));
        ImVec4 col = Mix(Mix(theme::textMute, theme::text, h), theme::text, act);
        DrawIcon(dl, icons[i], ImVec2(p.x + S(26) + S(2) * h, p.y + ih * 0.5f), S(17), Col(Mix(col, theme::accent2, act)));
        Text(dl, act > 0.5f ? fonts::bold : fonts::body,
             ImVec2(p.x + S(48) + S(2) * h, p.y + (ih - fonts::body->LegacySize) * 0.5f), Col(col), names[i]);
        if (pressed && g.page != i) { g.page = (Page)i; g.pageT = 0.0f; }
    }

    // koruma durum satırı
    const bool prot = aegis::SnapshotConfig().enabled;
    {
        float sy = ds.y - S(118);
        ImVec2 dot(S(30), sy + S(8));
        float k = fmodf(anim::Time(), 1.6f) / 1.6f;
        dl->AddCircleFilled(dot, S(3) + S(5) * k, Col(prot ? theme::success : theme::danger, 0.35f * (1.0f - k)), 16);
        dl->AddCircleFilled(dot, S(3.2f), Col(prot ? theme::success : theme::danger), 12);
        char buf[96];
        snprintf(buf, sizeof(buf), "%s  ·  %s", prot ? "Koruma aktif" : "Koruma pasif", UptimeText().c_str());
        Text(dl, fonts::small, ImVec2(S(42), sy), Col(theme::textDim), buf);
    }

    // alt kullanıcı kartı
    ImVec2 ua(S(14), ds.y - S(88)), ub(b.x - S(14), ds.y - S(16));
    dl->AddRectFilled(ua, ub, Col(White(0.04f)), S(14));
    dl->AddRect(ua, ub, Col(White(0.07f)), S(14));
    ImVec2 av(ua.x + S(30), (ua.y + ub.y) * 0.5f);
    Glow(dl, av, S(36), Col(theme::accent, 0.35f));
    int v0 = dl->VtxBuffer.Size;
    dl->AddCircleFilled(av, S(17), IM_COL32_WHITE, 32);
    Shade(dl, v0, av - S2(17, 17), av + S2(17, 17), Col(theme::accent), Col(theme::accent2));
    std::string ini = g.user.empty() ? "?" : g.user.substr(0, 1);
    TextC(dl, fonts::bold, av, Col(theme::bg0), ini.c_str());
    dl->AddCircleFilled(av + S2(12, 12), S(5.5f), Col(theme::bg0), 12);
    dl->AddCircleFilled(av + S2(12, 12), S(3.5f), Col(prot ? theme::success : theme::danger), 12);

    dl->PushClipRect(ua, ImVec2(ub.x - S(12), ub.y), true);
    const float nx = av.x + S(28);
    if (g.editName)
    {
        if (!g.focusName) { ImGui::SetKeyboardFocusHere(); g.focusName = true; }
        ImGui::SetCursorScreenPos(ImVec2(nx - S(6), av.y - S(24)));
        ImGui::SetNextItemWidth(ImMin(S(150), (ub.x - S(12)) - (nx - S(6)) - S(4)));
        const ImGuiInputTextFlags nf = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsNoBlank;
        const bool ent = ImGui::InputText("##uname", g.nameBuf, sizeof(g.nameBuf), nf);
        if (ent)
        {
            char trim[64];
            std::snprintf(trim, sizeof trim, "%s", g.nameBuf);
            char* s = trim; while (*s == ' ') ++s;
            char* e = s + strlen(s); while (e > s && e[-1] == ' ') --e; *e = 0;
            g.user = s;
            aegis::Config c = aegis::SnapshotConfig();
            c.userName = s;
            Publish(c);
            g.editName = false;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) g.editName = false;
    }
    else
    {
        Text(dl, fonts::bold, ImVec2(nx, av.y - S(19)), Col(theme::text), g.user.c_str());
        ImGui::SetCursorScreenPos(ImVec2(nx - S(2), av.y - S(23)));
        ImGui::InvisibleButton("##unameclick", ImVec2(S(128), S(22)));
        if (ImGui::IsItemHovered())
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImVec2 ns = Measure(fonts::bold, g.user.c_str());
            dl->AddRectFilled(ImVec2(nx, av.y - S(4)), ImVec2(nx + ns.x, av.y - S(3)),
                              Col(theme::accent, 0.6f), S(1));
        }
        if (ImGui::IsItemClicked(0))
        {
            g.editName = true;
            g.focusName = false;
            std::snprintf(g.nameBuf, sizeof(g.nameBuf), "%s", g.user.c_str());
        }
    }
    Text(dl, fonts::small, ImVec2(nx, av.y - S(1)), Col(theme::textMute), "anti-logger");
    const ImVec2 sh(ub.x - S(34), av.y + S(2));
    DrawIcon(dl, Ico_Shield, sh, S(11), Col(prot ? theme::accent : theme::textMute));
    ImGui::SetCursorScreenPos(ImVec2(sh.x - S(13), sh.y - S(15)));
    ImGui::InvisibleButton("##pwbtn", ImVec2(S(26), S(30)));
    if (ImGui::IsItemHovered())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("Şifreyi değiştir");
        dl->AddCircle(sh, S(16), Col(theme::accent, 0.6f), 24, S(1));
    }
    if (ImGui::IsItemClicked(0))
    {
        g.pwOpen = true;
        g.pwFocus = false;
        g.pwT = 0.0f;
        g.pwErr[0] = 0;
    }
    dl->PopClipRect();
}

// ------------------------------------------------------------------ pages
void PageHeader(ImVec2 p, float w, const char* title, const char* sub)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    TextGradient(dl, fonts::hero, ImVec2(p.x, p.y + S(16)), Col(theme::text), Col(Mix(theme::text, theme::accent2, 0.55f)), title);
    Text(dl, fonts::small, ImVec2(p.x, p.y + S(74)), Col(theme::textMute), sub);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + S(112)));
    ImGui::Dummy(ImVec2(w, 1));
}

void PageGenel(ImVec2 p, ImVec2 b)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    aegis::Config c = aegis::SnapshotConfig();
    const auto& st = aegis::stats();
    const float w = b.x - p.x;
    const bool prot = c.enabled;

    char hsub[200];
    snprintf(hsub, sizeof(hsub), "%s",
             prot ? "Tüm motorlar çalışıyor: A-Z tuşları keylogger'dan korunuyor."
                  : "Koruma kapalı: aşağıdaki anahtar düğme ile başlat.");
    PageHeader(p, w, "Genel Durum", hsub);
    float y = ImGui::GetCursorScreenPos().y;
    const float colL = ImMin(w - S(310), S(600));
    const float colR = w - S(26) - colL;

    // ---------- SOL: koruma durumu kartı ----------
    ImGui::SetCursorScreenPos(ImVec2(p.x, y));
    ui::CardBegin("Koruma Durumu", Ico_Shield, ImVec2(p.x, y), colL);
    const float iw = ui::CardInnerWidth();
    {
        const float tt = anim::Time();
        const float pulse = 0.5f + 0.5f * sinf(tt * 2.2f);
        ImVec2 c0 = ImGui::GetCursorScreenPos();
        ImVec2 sci(c0.x + S(34), c0.y + S(64));
        if (prot) Glow(dl, sci, S(58 + 10 * pulse), Col(theme::accent, 0.30f));
        dl->AddCircleFilled(sci, S(34), Col(White(0.045f)), 40);
        dl->AddCircle(sci, S(34), Col(White(0.07f)), 40, S(1));
        DrawIcon(dl, Ico_Shield, sci, S(30), Col(prot ? theme::accent : theme::textMute));
        ImVec2 ta(sci.x + S(60), sci.y - S(34));
        char state[16];
        snprintf(state, sizeof(state), "%s", prot ? "AKTİF" : "PASİF");
        const char* stateTitle = prot ? "Koruma sürüyor" : "Koruma kapalı";
        ui::Badge(dl, ta, state, prot ? theme::success : theme::danger);
        Text(dl, fonts::title, ImVec2(ta.x, ta.y + S(30)), Col(theme::text), stateTitle);
        // mod bilgisi başlığın sağında (dikeyde yer kazanmak için)
        ImVec2 tms = Measure(fonts::title, stateTitle);
        Text(dl, fonts::small, ImVec2(ta.x + tms.x + S(28), ta.y + S(36)), Col(theme::textMute),
             c.aggressive ? "Mod: agresif (bloke + yeniden başlar)" : "Mod: sadece gürültü");
        ImGui::Dummy(ImVec2(iw, S(96)));

        // sayaç chips
        ImVec2 cp = ImGui::GetCursorScreenPos();
        char t1[40], t2[40], t3[40];
        snprintf(t1, sizeof(t1), "%llu gerçek tuş", (unsigned long long)st.realSeen.load());
        snprintf(t2, sizeof(t2), "%llu gürültü", (unsigned long long)st.noiseSent.load());
        snprintf(t3, sizeof(t3), "%llu bloke", (unsigned long long)st.blocked.load());
        float cx = cp.x; const float cy = cp.y;
        cx += ui::Chip(dl, ImVec2(cx, cy), t1, theme::accent) + S(8);
        cx += ui::Chip(dl, ImVec2(cx, cy), t2, theme::warning) + S(8);
        ui::Chip(dl, ImVec2(cx, cy), t3, theme::danger);
        ImGui::Dummy(ImVec2(iw, S(32)));
    }
    // ana anahtar düğme
    if (ui::Button(prot ? "KORUMAYI DURDUR" : "KORUMAYI BAŞLAT",
                   ImVec2(iw, S(46)), prot ? ui::Btn_Danger : ui::Btn_Primary,
                   prot ? Ico_Close : Ico_Play)) {
        c.enabled = !prot;
        Publish(c);
        toast::Push(c.enabled ? toast::Success : toast::Warning,
                    c.enabled ? "Koruma başlatıldı" : "Koruma durduruldu",
                    "AEGIS motorları yeniden yapılandırıldı");
    }
    float lb = ui::CardEnd();

    // ---------- SOL: tuş aktivitesi kartı ----------
    float y2 = lb + S(24);
    ImGui::SetCursorScreenPos(ImVec2(p.x, y2));
    ui::CardBegin("Tuş Aktivitesi", Ico_Radar, ImVec2(p.x, y2), colL);
    DrawActivityCanvas(ui::CardInnerWidth());
    ui::CardEnd();

    // ---------- SAĞ: özet kartı ----------
    ImGui::SetCursorScreenPos(ImVec2(p.x + colL + S(26), y));
    ui::CardBegin("Özet", Ico_Cpu, ImVec2(p.x + colL + S(26), y), colR);
    {
        auto Row = [&](const char* k, const char* v, ImVec4 vc) {
            Text(dl, fonts::small, ImGui::GetCursorScreenPos(), Col(theme::textMute), k);
            ImVec2 vs = Measure(fonts::body, v);
            Text(dl, fonts::body, ImVec2(p.x + colL + S(26) + colR - S(22) - vs.x, ImGui::GetCursorScreenPos().y),
                 Col(vc), v);
            ImGui::Dummy(ImVec2(colR - S(44), S(24)));
        };
        Row("Uptime", UptimeText().c_str(), theme::text);
        char vv[48];
        snprintf(vv, sizeof(vv), "her %d s", aegis::SnapshotConfig().rehookSec);
        Row("Hook yenileme", vv, theme::text);
        snprintf(vv, sizeof(vv), "%.0f tu/sn", aegis::SnapshotConfig().noisePerSecond);
        Row("Gürültü hedefi", vv, theme::text);
        snprintf(vv, sizeof(vv), "%s", aegis::SnapshotConfig().autostart ? "açık" : "kapalı");
        Row("Oto başlatma", vv, theme::text);
    }
    float rb = ui::CardEnd();

    // ---------- SAĞ: motorlar kartı ----------
    float y3 = rb + S(24);
    ImGui::SetCursorScreenPos(ImVec2(p.x + colL + S(26), y3));
    ui::CardBegin("Motorlar", Ico_Bolt, ImVec2(p.x + colL + S(26), y3), colR);
    {
        const bool hook = aegis::HookEngine::instance().hookInstalled();
        ImVec2 cp = ImGui::GetCursorScreenPos();
        float cx = cp.x; const float cy = cp.y;
        cx += ui::Chip(dl, ImVec2(cx, cy), hook ? "LL hook kurulu" : "hook yok",
                       hook ? theme::success : theme::danger) + S(8);
        cx += ui::Chip(dl, ImVec2(cx, cy), "gürültü motoru", theme::warning);
        ImGui::Dummy(ImVec2(colR - S(44), S(30)));
        Text(dl, fonts::small, ImGui::GetCursorScreenPos(), Col(theme::textMute),
             "Engellenen harfler, bizden sonra kurulmus\nkeylogger'lara görünmeden yeniden basılır.");
        ImGui::Dummy(ImVec2(colR - S(44), S(34)));
    }
    ui::CardEnd();
}

void PageMotor(ImVec2 p, ImVec2 b)
{
    ImDrawList* dl = ImGui::GetWindowDrawList(); (void)dl;
    aegis::Config c = aegis::SnapshotConfig();
    const float w = b.x - p.x;
    const float colL = ImMin(w - S(330), S(520));
    const float colR = w - S(26) - colL;

    PageHeader(p, w, "Motor Ayarları", "Koruma katmanının davranışını buradan değiştir.");
    float y = ImGui::GetCursorScreenPos().y;

    // SOL: mod karti
    ImGui::SetCursorScreenPos(ImVec2(p.x, y));
    ui::CardBegin("Koruma Modu", Ico_Shield, ImVec2(p.x, y), colL);
    const float iw = ui::CardInnerWidth();
    {
        ImGui::Dummy(ImVec2(iw, S(2)));
        if (ui::Toggle("Koruma", "Ana anahtar: kapalıysa hiçbir motor çalışmaz", &c.enabled, iw))
        {
            Publish(c);
            toast::Push(c.enabled ? toast::Success : toast::Warning,
                        c.enabled ? "Koruma başlatıldı" : "Koruma durduruldu");
        }
        ImGui::Dummy(ImVec2(iw, S(16)));

        if (ui::Toggle("Agresif mod", "LL hook'ta A-Z harfleri bloke edilip tekrar basılır (keylogger orijinali görmez)", &c.aggressive, iw))
        {
            Publish(c);
            toast::Push(toast::Info, c.aggressive ? "Agresif mod açık" : "Gürültü-only modu");
        }
        ImGui::Dummy(ImVec2(iw, S(16)));

        if (ui::Toggle("Gürültü hook zincirine iletilsin", "Bizden önce kurulmuş keylogger hookları gürültüyü görür", &c.forwardNoise, iw))
            Publish(c);
        ImGui::Dummy(ImVec2(iw, S(16)));
        bool as = c.autostart;
        if (ui::Toggle("Windows ile otomatik başlat", "HKCU Run kaydı (yönetici gerekmez), --silent ile sessiz açılır", &as, iw))
        {
            std::wstring err;
            aegis::persistence::SetStartup(as, &err);
            c.autostart = as;
            Publish(c);
            toast::Push(err.empty() ? toast::Success : toast::Error,
                        as ? "Oto başlatma eklendi" : "Oto başlatma kaldırıldı",
                        err.empty() ? nullptr : "Registry güncellenemedi");
        }
        ImGui::Dummy(ImVec2(iw, S(6)));
    }
    ui::CardEnd();

    // SAG: sayisal ayarlar karti
    float rx = p.x + colL + S(26);
    ImGui::SetCursorScreenPos(ImVec2(rx, y));
    ui::CardBegin("İnce Ayar", Ico_Gear, ImVec2(rx, y), colR);
    {
        ImGui::Dummy(ImVec2(colR - S(44), S(2)));
        float nz = c.noisePerSecond;
        if (ui::Slider("Gürültü hızı", &nz, 1.0f, 200.0f, "%.0f tu/sn", colR - S(44)))
        {
            c.noisePerSecond = nz;
            Publish(c);
        }
        ImGui::Dummy(ImVec2(colR - S(44), S(16)));
        float rh = (float)c.rehookSec;
        if (ui::Slider("Hook yenileme", &rh, 1.0f, 30.0f, "%.0f sn", colR - S(44)))
        {
            c.rehookSec = (int)(rh + 0.5f);
            Publish(c);
        }
        ImGui::Dummy(ImVec2(colR - S(44), S(12)));
        Text(dl, fonts::small, ImGui::GetCursorScreenPos(), Col(theme::textMute),
             "Rehook süresi kısa olursa AEGIS hook zincirinin\nbaşında kalır; keylogger'lar bizden sonrasını görür.");
        ImGui::Dummy(ImVec2(colR - S(44), S(30)));
    }
    ui::CardEnd();
}

void PageGorunum(ImVec2 p, ImVec2 b)
{
    ImDrawList* dl = ImGui::GetWindowDrawList(); (void)dl;
    const float w = b.x - p.x;
    const float colL = ImMin(w - S(320), S(540));
    const float colR = w - S(26) - colL;

    PageHeader(p, w, "Görünüm", "Tema rengi, arka plan sahnesi ve ekran efektleri.");
    float y = ImGui::GetCursorScreenPos().y;

    // ---- SOL: tema karti ----
    ImGui::SetCursorScreenPos(ImVec2(p.x, y));
    ui::CardBegin("Tema", Ico_Sparkle, ImVec2(p.x, y), colL);
    const float iw = ui::CardInnerWidth();
    {
        const float rowRight = p.x + colL - S(22);
        // vurgu rengi swatch'lari
        Text(dl, fonts::small, ImGui::GetCursorScreenPos(), Col(theme::textMute), "Vurgu rengi");
        ImGui::Dummy(ImVec2(iw, S(22)));
        float sx = ImGui::GetCursorScreenPos().x, sy2 = ImGui::GetCursorScreenPos().y;
        const float swSize = S(38), swGap = S(12);
        for (int i = 0; i < theme::kPresetCount; i++)
        {
            if (sx + swSize > rowRight) { sx = p.x + S(34); sy2 += swSize + S(12); }
            ImGui::SetCursorScreenPos(ImVec2(sx, sy2));
            char sid[10];
            snprintf(sid, sizeof(sid), "##sw%d", i);
            if (ui::Swatch(sid, theme::kPresets[i].a, theme::kPresets[i].b, config::c.accent == i, swSize))
            {
                config::c.accent = i;
                theme::TargetAccent(i, theme::accent, theme::accent2);
                config::MarkDirty();
                char tbuf[64];
                snprintf(tbuf, sizeof(tbuf), "Tema: %s", theme::kPresets[i].name);
                toast::Push(toast::Success, tbuf);
            }
            sx += swSize + swGap;
        }
        ImGui::Dummy(ImVec2(iw, S(32)));

        // arka plan stili
        Text(dl, fonts::small, ImGui::GetCursorScreenPos(), Col(theme::textMute), "Arka plan");
        ImGui::Dummy(ImVec2(iw, S(22)));
        static const char* bgNames[6] = { "Otomatik", "Aurora", "Dalgalar", "Warp", "Izgara", "Takımyıldız" };
        float bx = p.x + S(22);
        float by2 = ImGui::GetCursorScreenPos().y;
        for (int i = 0; i < 6; i++)
        {
            if (bx + S(112) > rowRight) { bx = p.x + S(34); by2 += S(46); }
            ImGui::SetCursorScreenPos(ImVec2(bx, by2));
            char bid[10];
            snprintf(bid, sizeof(bid), "##bg%d", i);
            if (ui::Button(bgNames[i], ImVec2(S(112), S(36)),
                           config::c.bgStyle == i ? ui::Btn_Primary : ui::Btn_Secondary, Ico_None))
            {
                config::c.bgStyle = i;
                config::MarkDirty();
            }
            bx += S(112) + S(10);
        }
        ImGui::Dummy(ImVec2(iw, S(6)));

        // slider'lar (tema hizasinda dengeli kolon)
        float gl = config::c.glass;
        if (ui::Slider("Cam yoğunluğu", &gl, 0.30f, 1.0f, "%.2f", iw))
        {
            config::c.glass = gl;
            config::MarkDirty();
        }
        ImGui::Dummy(ImVec2(iw, S(6)));
        float sp = config::c.animSpeed;
        if (ui::Slider("Animasyon hızı", &sp, 0.5f, 2.0f, "%.1fx", iw))
        {
            config::c.animSpeed = sp;
            config::MarkDirty();
        }
        ImGui::Dummy(ImVec2(iw, S(2)));
    }
    float lb = ui::CardEnd();

    // ---- SAG: efektler karti (toggle listesi) ----
    float rx = p.x + colL + S(26);
    ImGui::SetCursorScreenPos(ImVec2(rx, y));
    ui::CardBegin("Efektler", Ico_Eye, ImVec2(rx, y), colR);
    {
        const float iwR = ui::CardInnerWidth();
        if (ui::Toggle("Arka plan efektleri", "Sayfa geçişlerinde canlı sahne animasyonları", &config::c.bgEffects, iwR))
            config::MarkDirty();
        ImGui::Dummy(ImVec2(iwR, S(8)));
        if (ui::Toggle("Parçacıklar", "Takımyıldız ve kalıntı efekti parçacıkları", &config::c.particles, iwR))
            config::MarkDirty();
        ImGui::Dummy(ImVec2(iwR, S(8)));
        if (ui::Toggle("Bildirimler", "Sağ üstte kayan toast mesajları", &config::c.notifications, iwR))
            config::MarkDirty();
        ImGui::Dummy(ImVec2(iwR, S(8)));
        if (ui::Toggle("Her zaman üstte", "Pencere daima en önde durur", &config::c.alwaysOnTop, iwR))
        {
            config::MarkDirty();
            platform::SetTopmost(config::c.alwaysOnTop);
        }
        ImGui::Dummy(ImVec2(iwR, S(8)));
        if (ui::Toggle("Ekran kaydı koruması", "OBS ve kayıt yazılımlarında pencere gizlenir", &config::c.streamProof, iwR))
        {
            config::MarkDirty();
            platform::SetStreamProof(config::c.streamProof);
            toast::Push(toast::Info, config::c.streamProof ? "Yayın koruması ON" : "Yayın koruması OFF");
        }
        ImGui::Dummy(ImVec2(iwR, S(8)));
        if (ui::Button("VARSAYILANLARA DÖN", ImVec2(iwR, S(42)), ui::Btn_Secondary, Ico_Refresh))
        {
            config::ResetVisuals();
            platform::SetTopmost(config::c.alwaysOnTop);
            platform::SetStreamProof(config::c.streamProof);
            toast::Push(toast::Success, "Görünüm sıfırlandı");
        }
        ImGui::Dummy(ImVec2(iwR, S(2)));
    }
    ui::CardEnd();
}

// ------------------------------------------------------------------ kilit ekrani
std::string PassHash(const char* pass)
{
    unsigned long long h = 0x9e3779b97f4a7c15ull;
    const char* salt = "AEGIS::pwd::v1";
    for (const char* q = salt; *q; ++q) { h ^= (unsigned char)*q; h *= 1099511628211ull; }
    for (const char* q = pass; *q; ++q) { h ^= (unsigned char)*q; h *= 1099511628211ull; }
    h ^= (unsigned long long)strlen(pass) * 0x84222325cb929cacull;
    h *= 1099511628211ull;
    char out[24];
    std::snprintf(out, sizeof out, "%016llx", (unsigned long long)h);
    return out;
}

void TryCreatePassword()
{
    const size_t n1 = strlen(g.buf1);
    if (n1 < 4) { std::snprintf(g.err, sizeof g.err, "Şifre en az 4 karakter olmalı"); g.shake = 1.0f; return; }
    if (strcmp(g.buf1, g.buf2) != 0) { std::snprintf(g.err, sizeof g.err, "Şifreler eşleşmiyor"); g.shake = 1.0f; return; }
    aegis::Config c = aegis::SnapshotConfig();
    c.passHash = PassHash(g.buf1);
    Publish(c);
    aegis::SaveConfig(); // debounce beklemeden hemen yaz
    memset(g.buf1, 0, sizeof g.buf1);
    memset(g.buf2, 0, sizeof g.buf2);
    g.err[0] = 0;
    g.locked = false;
    g.unlockT = 1.0f;
    g.pageT = 0.0f;
    toast::Push(toast::Success, "Şifre kaydedildi", "Her açılışta istenecek");
}

void TryUnlock()
{
    if (PassHash(g.buf1) == aegis::SnapshotConfig().passHash)
    {
        memset(g.buf1, 0, sizeof g.buf1);
        g.err[0] = 0;
        g.locked = false;
        g.unlockT = 1.0f;
        g.pageT = 0.0f;
    }
    else
    {
        std::snprintf(g.err, sizeof g.err, "Şifre yanlış, tekrar dene");
        memset(g.buf1, 0, sizeof g.buf1);
        g.shake = 1.0f;
        g.focusSet = false; // alana odagi geri ver
    }
}

void LockScreen(ImVec2 ds)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    g.lockT = ImMin(1.0f, g.lockT + RealDt() * 1.7f);
    const float a = anim::OutCubic(g.lockT);
    if (g.shake > 0.0f) g.shake = ImMax(0.0f, g.shake - RealDt() * 2.0f);
    const float tt = anim::Time();
    const float pulse = 0.5f + 0.5f * sinf(tt * 2.2f);

    const bool firstRun = g.lockMode == 0;
    const ImVec2 center(ds.x * 0.5f, ds.y * 0.5f);

    // arka plan karartmasi
    dl->AddRectFilled(ImVec2(0, 0), ds, Col(theme::bg0, 0.78f * a));

    // kart
    const float cw = S(430);
    const float chh = firstRun ? S(360) : S(300);
    const float shakeX = sinf(tt * 34.0f) * S(7.0f) * g.shake;
    const float drop = (1.0f - a) * S(26.0f);
    const ImVec2 p0(center.x - cw * 0.5f + shakeX, center.y - chh * 0.5f + drop);
    const ImVec2 p1 = p0 + ImVec2(cw, chh);

    Glow(dl, ImVec2(center.x, p0.y), S(96), Col(theme::accent, 0.14f * a));
    dl->AddRectFilled(p0, p1, Col(theme::bg0, 0.95f * a), S(16));
    dl->AddRect(p0, p1, Col(White(0.09f), a), S(16));
    dl->AddRectFilled(ImVec2(p0.x + S(28), p0.y), ImVec2(p1.x - S(28), p0.y + S(2)),
                      Col(theme::accent, 0.55f * a), S(2));

    // logo + baslik
    const ImVec2 lc(center.x + shakeX, p0.y + S(52));
    Glow(dl, lc, S(52 + 7 * pulse), Col(theme::accent, 0.35f * a));
    DrawIcon(dl, Ico_Shield, lc, S(28), Col(theme::accent));
    TextC(dl, fonts::hero, ImVec2(center.x + shakeX, p0.y + S(92)), Col(theme::text),
          firstRun ? "AEGIS'i güvenceye al" : "AEGIS kilidi");
    if (g.err[0])
        TextC(dl, fonts::small, ImVec2(center.x + shakeX, p0.y + S(128)), Col(theme::danger), g.err);
    else
        TextC(dl, fonts::small, ImVec2(center.x + shakeX, p0.y + S(128)), Col(theme::textMute),
              firstRun ? "Bu bilgisayara özel bir şifre belirle; her açılışta istenecek."
                       : "Devam etmek için şifreni gir.");

    // form
    const float fx = p0.x + S(24);
    const float fw = cw - S(48);
    const float fy = p0.y + S(166);

    ImGui::PushStyleColor(ImGuiCol_FrameBg,        Col(White(0.05f), a));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Col(White(0.07f), a));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  Col(White(0.09f), a));
    ImGui::PushStyleColor(ImGuiCol_Border,         Col(White(0.08f), a));
    ImGui::PushStyleColor(ImGuiCol_Text,           Col(theme::text, a));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled,   Col(theme::textMute, a));
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, Col(theme::accent, 0.30f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(10));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(14), S(11)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

    const ImGuiInputTextFlags pf = ImGuiInputTextFlags_Password |
                                   ImGuiInputTextFlags_CharsNoBlank |
                                   ImGuiInputTextFlags_EnterReturnsTrue;
    bool enter = false;
    if (!g.focusSet) { ImGui::SetKeyboardFocusHere(); g.focusSet = true; }
    ImGui::SetCursorScreenPos(ImVec2(fx, fy));
    ImGui::SetNextItemWidth(fw);
    enter |= ImGui::InputTextWithHint("##pw1", firstRun ? "Yeni şifre (en az 4 karakter)" : "Şifren",
                                      g.buf1, sizeof(g.buf1), pf);
    bool ok = false;
    if (firstRun)
    {
        ImGui::SetCursorScreenPos(ImVec2(fx, fy + S(56)));
        ImGui::SetNextItemWidth(fw);
        enter |= ImGui::InputTextWithHint("##pw2", "Şifreyi doğrula", g.buf2, sizeof(g.buf2), pf);
        ImGui::SetCursorScreenPos(ImVec2(fx, fy + S(112)));
        ok = ui::Button("ŞİFREYİ OLUŞTUR", ImVec2(fw, S(46)), ui::Btn_Primary, Ico_Play);
    }
    else
    {
        ImGui::SetCursorScreenPos(ImVec2(fx, fy + S(64)));
        ImGui::SetNextItemWidth(fw);
        ok = ui::Button("GİRİŞ YAP", ImVec2(fw, S(46)), ui::Btn_Primary, Ico_Play);
    }
    if (enter) g.err[0] = 0;
    if ((ok || enter) && firstRun) TryCreatePassword();
    else if (ok || enter)          TryUnlock();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(7);
}

void TryChangePassword()
{
    aegis::Config c = aegis::SnapshotConfig();
    if (PassHash(g.pwCur) != c.passHash)
    {
        std::snprintf(g.pwErr, sizeof g.pwErr, "Mevcut şifre yanlış");
        memset(g.pwCur, 0, sizeof g.pwCur);
        g.pwShake = 1.0f;
        g.pwFocus = false;
        return;
    }
    if (strlen(g.pwNew) < 4)
    {
        std::snprintf(g.pwErr, sizeof g.pwErr, "Yeni şifre en az 4 karakter olmalı");
        g.pwShake = 1.0f;
        return;
    }
    if (strcmp(g.pwNew, g.pwNew2) != 0)
    {
        std::snprintf(g.pwErr, sizeof g.pwErr, "Yeni şifreler eşleşmiyor");
        g.pwShake = 1.0f;
        return;
    }
    c.passHash = PassHash(g.pwNew);
    Publish(c);
    aegis::SaveConfig(); // debounce beklemeden hemen yaz
    memset(g.pwCur, 0, sizeof g.pwCur);
    memset(g.pwNew, 0, sizeof g.pwNew);
    memset(g.pwNew2, 0, sizeof g.pwNew2);
    g.pwErr[0] = 0;
    g.pwOpen = false;
    toast::Push(toast::Success, "Şifre güncellendi", "Yeni şifre sonraki açılışta istenecek");
}

void PwDialog(ImVec2 ds)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (!g.pwFocus) g.pwT = 0.0f;
    g.pwT = ImMin(1.0f, g.pwT + RealDt() * 1.7f);
    const float a = anim::OutCubic(g.pwT);
    if (g.pwShake > 0.0f) g.pwShake = ImMax(0.0f, g.pwShake - RealDt() * 2.0f);
    const float tt = anim::Time();
    const float pulse = 0.5f + 0.5f * sinf(tt * 2.2f);

    dl->AddRectFilled(ImVec2(0, 0), ds, Col(theme::bg0, 0.78f * a));

    const float cw = S(430);
    const float chh = S(444);
    const float shakeX = sinf(tt * 34.0f) * S(7.0f) * g.pwShake;
    const float drop = (1.0f - a) * S(26.0f);
    const ImVec2 center(ds.x * 0.5f, ds.y * 0.5f);
    const ImVec2 p0(center.x - cw * 0.5f + shakeX, center.y - chh * 0.5f + drop);
    const ImVec2 p1 = p0 + ImVec2(cw, chh);

    // modal girdi yutucu: tam ekran bos alanmostat (kart ici daha sonra tekrar uzerine yazilir)
    ImGui::SetCursorScreenPos(ImVec2(0, 0));
    ImGui::InvisibleButton("##pwbg", ds);
    if (ImGui::IsItemClicked(0)) // bos alana tiklandi -> iptal
    {
        g.pwOpen = false;
        g.pwErr[0] = 0;
        memset(g.pwCur, 0, sizeof g.pwCur);
        memset(g.pwNew, 0, sizeof g.pwNew);
        memset(g.pwNew2, 0, sizeof g.pwNew2);
    }
    // kart govdesi (padding tıklamalari hicbir sey yapmasin)
    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton("##pwcard", ImVec2(cw, chh));

    Glow(dl, ImVec2(center.x, p0.y), S(96), Col(theme::accent, 0.14f * a));
    dl->AddRectFilled(p0, p1, Col(theme::bg0, 0.95f * a), S(16));
    dl->AddRect(p0, p1, Col(White(0.09f), a), S(16));
    dl->AddRectFilled(ImVec2(p0.x + S(28), p0.y), ImVec2(p1.x - S(28), p0.y + S(2)),
                      Col(theme::accent, 0.55f * a), S(2));

    const ImVec2 lc(center.x + shakeX, p0.y + S(52));
    Glow(dl, lc, S(52 + 7 * pulse), Col(theme::accent, 0.35f * a));
    DrawIcon(dl, Ico_Shield, lc, S(28), Col(theme::accent));
    TextC(dl, fonts::hero, ImVec2(center.x + shakeX, p0.y + S(92)), Col(theme::text), "Şifreyi değiştir");
    if (g.pwErr[0])
        TextC(dl, fonts::small, ImVec2(center.x + shakeX, p0.y + S(128)), Col(theme::danger), g.pwErr);
    else
        TextC(dl, fonts::small, ImVec2(center.x + shakeX, p0.y + S(128)), Col(theme::textMute),
              "Önce mevcut şifreni doğrula; ardından yeni şifreyi gir.");

    const float fx = p0.x + S(24);
    const float fw = cw - S(48);
    const float fy = p0.y + S(166);

    ImGui::PushStyleColor(ImGuiCol_FrameBg,        Col(White(0.05f), a));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Col(White(0.07f), a));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  Col(White(0.09f), a));
    ImGui::PushStyleColor(ImGuiCol_Border,         Col(White(0.08f), a));
    ImGui::PushStyleColor(ImGuiCol_Text,           Col(theme::text, a));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled,   Col(theme::textMute, a));
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, Col(theme::accent, 0.30f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(10));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(14), S(11)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

    const ImGuiInputTextFlags pf = ImGuiInputTextFlags_Password |
                                   ImGuiInputTextFlags_CharsNoBlank;
    const ImGuiInputTextFlags pfEnter = pf | ImGuiInputTextFlags_EnterReturnsTrue;
    if (!g.pwFocus) { ImGui::SetKeyboardFocusHere(); g.pwFocus = true; }
    ImGui::SetCursorScreenPos(ImVec2(fx, fy));
    ImGui::SetNextItemWidth(fw);
    ImGui::InputTextWithHint("##pwc", "Mevcut şifre", g.pwCur, sizeof(g.pwCur), pf);
    ImGui::SetCursorScreenPos(ImVec2(fx, fy + S(54)));
    ImGui::SetNextItemWidth(fw);
    ImGui::InputTextWithHint("##pwn", "Yeni şifre (en az 4 karakter)", g.pwNew, sizeof(g.pwNew), pf);
    ImGui::SetCursorScreenPos(ImVec2(fx, fy + S(108)));
    ImGui::SetNextItemWidth(fw);
    const bool ent3 = ImGui::InputTextWithHint("##pwn2", "Yeni şifreyi doğrula", g.pwNew2, sizeof(g.pwNew2), pfEnter);

    ImGui::SetCursorScreenPos(ImVec2(fx, fy + S(164)));
    if (ui::Button("ŞİFREYİ GÜNCELLE", ImVec2(fw, S(44)), ui::Btn_Primary, Ico_Refresh))
        TryChangePassword();
    if (ent3) TryChangePassword();

    ImGui::SetCursorScreenPos(ImVec2(fx, fy + S(218)));
    if (ui::Button("İPTAL", ImVec2(fw, S(36)), ui::Btn_Secondary, Ico_None) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        g.pwOpen = false;
        g.pwErr[0] = 0;
        memset(g.pwCur, 0, sizeof g.pwCur);
        memset(g.pwNew, 0, sizeof g.pwNew);
        memset(g.pwNew2, 0, sizeof g.pwNew2);
    }

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(7);
}

// ------------------------------------------------------------------ main shell
void Main(ImVec2 ds)
{
    Sidebar(ds);
    g.pageT = ImMin(1.0f, g.pageT + anim::Dt() * 2.4f);
    float pt = anim::OutCubic(g.pageT);
    ImVec2 off(0, (1.0f - pt) * S(16));
    ImVec2 a(S(kSideW), S(kTitleH)), bb(ds.x - S(28), ds.y - S(26));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * pt);
    switch (g.page)
    {
    case PG_GENEL:   PageGenel(a + off, bb + off); break;
    case PG_MOTOR:   PageMotor(a + off, bb + off); break;
    case PG_GORUNUM: PageGorunum(a + off, bb + off); break;
    }
    ImGui::PopStyleVar();
}

void Frame()
{
    ImGuiIO& io = ImGui::GetIO();
    anim::speed = config::c.animSpeed;
    theme::Update();
    config::Tick(RealDt());

    // aegis.ini debounce kayıt
    g_saveCd -= RealDt();
    if (g_dirty && g_saveCd <= 0.0f) { aegis::SaveConfig(); g_dirty = false; g_saveCd = 2.0f; }

    ImVec2 ds = io.DisplaySize;
    bg::SetScene(g.page == PG_GENEL ? bg::BG_AURORA : g.page == PG_MOTOR ? bg::BG_GRID : bg::BG_CONSTELLATION);
    bg::Draw(ImGui::GetBackgroundDrawList(), ImVec2(0, 0), ds);

    g.screenT = ImMin(1.0f, g.screenT + anim::Dt() * 2.2f);

    if (!g.locked && g.unlockT > 0.0f)
    {
        g.unlockT = ImMax(0.0f, g.unlockT - RealDt() * 1.5f);
        ImGui::GetBackgroundDrawList()->AddRectFilled(
            ImVec2(0, 0), ds, Col(theme::bg0, 0.85f * anim::OutCubic(g.unlockT)));
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                    ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavFocus);
    ImGui::PopStyleVar(2);

    if (g.locked) LockScreen(ds);
    else { TitleBar(ds); Main(ds); if (g.pwOpen) PwDialog(ds); }

    if (g.locked) { ImGui::Dummy(ImVec2(0, 0)); }
    ImGui::End();

    if (!g.locked) toast::Render();

    ImDrawList* fg = ImGui::GetForegroundDrawList();
    fg->AddRect(ImVec2(0.5f, 0.5f), ds - ImVec2(0.5f, 0.5f), Col(White(0.09f)), S(12), 0, 1.0f);
}

// ------------------------------------------------------------------ init/shutdown
void Init()
{
    config::Load();
    lang::Init();
    lang::current = (config::c.language >= 0 && config::c.language < LANG_COUNT) ? config::c.language : LANG_TR;

    gfx::dpi = platform::DpiScale();
    fonts::Load(gfx::dpi);
    theme::ApplyStyle(gfx::dpi);
    theme::TargetAccent(config::c.accent, theme::accent, theme::accent2);
    flags::Build();

    g.user = platform::UserName();

    // aegis.ini'de kisi adi tanimliysa onu kullan
    {
        const aegis::Config uc = aegis::SnapshotConfig();
        if (!uc.userName.empty()) g.user = uc.userName;
    }

    platform::SetTopmost(config::c.alwaysOnTop);
    platform::SetStreamProof(config::c.streamProof);

    aegis::LoadConfig();
    aegis::persistence::SyncStartup();

    // kilit: sifre varsa giris ekraninda ac, yoksa ilk kurulum ekraninda ac
    {
        const aegis::Config ic = aegis::SnapshotConfig();
        g.lockMode = ic.passHash.empty() ? 0 : 1;
        g.err[0] = 0;
        g.lockT = 0.0f;
        g.unlockT = 0.0f;
        g.focusSet = false;
        g.locked = true;
    }

    aegis::SaveConfig(); // mevcut iniyi hemen sifreli blob formatina gecir

    aegis::HookEngine::instance().start();
    aegis::NoiseEngine::instance().start();
}

void Shutdown()
{
    config::Save();
    aegis::SaveConfig();
    aegis::NoiseEngine::instance().stop();
    aegis::HookEngine::instance().stop();
}

} // namespace

// ------------------------------------------------------------------ window
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (aegHost::g_device && wParam != SIZE_MINIMIZED && LOWORD(lParam) > 0)
        {
            CleanupRenderTarget();
            aegHost::g_swapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU || (wParam & 0xfff0) == SC_SCREENSAVE || (wParam & 0xfff0) == SC_MONITORPOWER)
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_APP_SHOW: // baska instance: "pencereyi ac" sinyali
        ShowWindow(hWnd, SW_RESTORE);
        SetForegroundWindow(hWnd);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void ApplyRoundedCorners(HWND hwnd, int w, int h)
{
    // Windows 11: native DWM rounding. Windows 10: fallback region.
    const DWORD DWMWA_WINDOW_CORNER_PREFERENCE_ = 33;
    int pref = 2; // DWMWCP_ROUND
    if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE_, &pref, sizeof(pref))))
    {
        int d = (int)(24 * aegHost::g_dpi);
        SetWindowRgn(hwnd, CreateRoundRectRgn(0, 0, w + 1, h + 1, d, d), TRUE);
    }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();
    aegHost::g_dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));
    if (aegHost::g_dpi < 1.0f) aegHost::g_dpi = 1.0f;

    // tek instance + args
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"AEGISAntiLogger_SingleInstance");
    const bool alreadyRunning = (mutex && GetLastError() == ERROR_ALREADY_EXISTS);
    bool bootSilent = false;
    {
        int     nargs = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &nargs);
        if (argv)
        {
            for (int i = 1; i < nargs; ++i)
            {
                if (lstrcmpiW(argv[i], L"--silent") == 0)
                    bootSilent = true;
            }
            LocalFree(argv);
        }
    }
    if (alreadyRunning)
    {
        HWND hwnd = FindWindowW(kWndClass, nullptr);
        if (hwnd) PostMessageW(hwnd, WM_APP_SHOW, 0, 0);
        if (mutex) CloseHandle(mutex);
        CoUninitialize();
        return 0;
    }

    // pencere
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC | CS_DROPSHADOW, WndProc, 0, 0, hInst,
                       LoadIconW(hInst, MAKEINTRESOURCEW(1)),
                       LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)), nullptr, nullptr,
                       L"AEGISAntiLogger", LoadIconW(hInst, MAKEINTRESOURCEW(1)) };
    RegisterClassExW(&wc);

    const int w = (int)(kBaseW * aegHost::g_dpi), h = (int)(kBaseH * aegHost::g_dpi);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + ((work.right - work.left) - w) / 2;
    const int y = work.top + ((work.bottom - work.top) - h) / 2;

    aegHost::g_hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"AEGIS Anti Logger",
                                      WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU, x, y, w, h,
                                      nullptr, nullptr, hInst, nullptr);
    if (!CreateDeviceD3D(aegHost::g_hwnd))
    {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, hInst);
        MessageBoxW(nullptr, L"DirectX 11 başlatılamadı.", L"AEGIS Anti Logger", MB_ICONERROR);
        if (mutex) CloseHandle(mutex);
        CoUninitialize();
        return 1;
    }
    ApplyRoundedCorners(aegHost::g_hwnd, w, h);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplWin32_Init(aegHost::g_hwnd);
    ImGui_ImplDX11_Init(aegHost::g_device, aegHost::g_context);

    Init();

    aegis::Config c0 = aegis::SnapshotConfig();
    const bool visible = !(bootSilent && c0.startHidden);
    if (visible) { ShowWindow(aegHost::g_hwnd, SW_SHOW); UpdateWindow(aegHost::g_hwnd); }

    while (!aegHost::g_quit)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) aegHost::g_quit = true;
        }
        if (aegHost::g_quit) break;

        if (IsIconic(aegHost::g_hwnd) || !IsWindowVisible(aegHost::g_hwnd))
        {
            Sleep(100);
            continue;
        }

        if (aegHost::g_dragging)
        {
            if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
                aegHost::g_dragging = false;
            else
            {
                POINT p;
                GetCursorPos(&p);
                SetWindowPos(aegHost::g_hwnd, nullptr,
                             aegHost::g_dragRect.left + (p.x - aegHost::g_dragCursor.x),
                             aegHost::g_dragRect.top + (p.y - aegHost::g_dragCursor.y),
                             0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        Frame();
        ImGui::Render();

        const float clear[4] = { 0.027f, 0.031f, 0.050f, 1.0f };
        aegHost::g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        aegHost::g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        aegHost::g_swapChain->Present(1, 0);
    }

    Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(aegHost::g_hwnd);
    UnregisterClassW(wc.lpszClassName, hInst);
    if (mutex) CloseHandle(mutex);
    CoUninitialize();
    return 0;
}



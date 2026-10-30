#include "ui_fx.hpp"
#include "common.hpp"
#include "config_store.hpp"
#include <cmath>
#include <cstring>
#include <vector>

namespace fx {

using aegis::Config;
using aegis::SnapshotConfig;

// ---------------------------------------------------------------- tematik
struct Preset { const char* name; ImVec4 a, b; };
static const Preset kPresets[] = {
    { "Turkuaz",  ImVec4(0.00f, 0.95f, 0.65f, 1), ImVec4(0.08f, 0.60f, 0.50f, 1) },
    { "Mor",      ImVec4(0.62f, 0.32f, 1.00f, 1), ImVec4(0.35f, 0.16f, 0.72f, 1) },
    { "Yesil",    ImVec4(0.12f, 1.00f, 0.48f, 1), ImVec4(0.05f, 0.55f, 0.28f, 1) },
    { "Mavi",     ImVec4(0.22f, 0.62f, 1.00f, 1), ImVec4(0.10f, 0.30f, 0.72f, 1) },
    { "Amber",    ImVec4(1.00f, 0.72f, 0.15f, 1), ImVec4(0.62f, 0.40f, 0.05f, 1) },
    { "Kirmizi",  ImVec4(1.00f, 0.30f, 0.30f, 1), ImVec4(0.62f, 0.10f, 0.15f, 1) },
    { "Pembe",    ImVec4(1.00f, 0.36f, 0.72f, 1), ImVec4(0.66f, 0.16f, 0.44f, 1) },
    { "Buz",      ImVec4(0.70f, 0.88f, 1.00f, 1), ImVec4(0.30f, 0.55f, 0.75f, 1) },
};
constexpr int kPresetN = sizeof(kPresets) / sizeof(kPresets[0]);

static ImVec4 gAcc  = kPresets[0].a; // animasyonlu gecerli vurgu
static ImVec4 gAcc2 = kPresets[0].b;

static float Ease(float cur, float target, float dt, float spd) {
    return cur + (target - cur) * (1.0f - std::exp(-dt * spd));
}
static ImVec4 Ease4(ImVec4 cur, ImVec4 target, float dt, float spd) {
    return ImVec4(Ease(cur.x, target.x, dt, spd), Ease(cur.y, target.y, dt, spd),
                  Ease(cur.z, target.z, dt, spd), Ease(cur.w, target.w, dt, spd));
}
static ImVec4 Mix4(ImVec4 a, ImVec4 b, float t) {    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

int PresetCount() { return kPresetN; }
const char* PresetName(int i) { return (i >= 0 && i < kPresetN) ? kPresets[i].name : "?"; }

std::pair<ImVec4, ImVec4> PresetColors(int i) {
    const int idx = (i < 0 || i >= kPresetN) ? 0 : i;
    return { kPresets[idx].a, kPresets[idx].b };
}

ImVec4 Accent()  { return gAcc;  }
ImVec4 Accent2() { return gAcc2; }
ImU32  Acc(float alpha) {
    ImVec4 c = Accent();
    c.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(c);
}

void Tick(float dt) {
    Config c = SnapshotConfig();
    int idx = c.accentIdx < 0 ? 0 : (c.accentIdx >= kPresetN ? kPresetN - 1 : c.accentIdx);
    const float speed = 6.0f;
    gAcc  = Ease4(gAcc,  kPresets[idx].a, dt, speed);
    gAcc2 = Ease4(gAcc2, kPresets[idx].b, dt, speed);
}

// ---------------------------------------------------------------- primitives
void Glow(ImDrawList* dl, ImVec2 c, float radius, ImU32 col) {
    ImVec4 base = ImGui::ColorConvertU32ToFloat4(col);
    if (base.w <= 0.002f || radius <= 0.5f) return;
    // katmanli dolgu halkalari: ucuz ve yumusak (2/3 ucak farki ile)
    const int n = 6;
    for (int i = n; i >= 1; --i) {
        const float t = (float)i / n;
        const float r = radius * t;
        const float a = base.w * (1.0f - t) * (1.0f - t) * 1.1f;
        dl->AddCircleFilled(c, r, ImGui::ColorConvertFloat4ToU32(ImVec4(base.x, base.y, base.z, a)), 40);
    }
    const float aC = base.w * 0.55f;
    dl->AddCircleFilled(c, radius * 0.10f, ImGui::ColorConvertFloat4ToU32(ImVec4(base.x, base.y, base.z, aC)), 40);
}

static void SoftShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float size, ImU32 col) {
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
    if (c.w <= 0.001f) return;
    const int n = 7;
    for (int i = n; i >= 1; --i) {
        const float t = (float)i / n;
        const float g = size * t;
        dl->AddRectFilled(ImVec2(a.x - g, a.y - g + size * 0.4f), ImVec2(b.x + g, b.y + g + size * 0.4f),
                          ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * (1 - t) * (1 - t) * 0.42f)),
                          rounding + g);
    }
}

void GlassPanel(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float strength) {
    Config c = SnapshotConfig();
    const float g = (c.glass * strength);
    SoftShadow(dl, a, b, rounding, 22.0f, IM_COL32(0, 0, 0, (int)(96 * strength)));
    dl->AddRectFilledMultiColor(a, b,
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.070f, 0.085f, 0.115f, 0.85f * g)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.070f, 0.085f, 0.115f, 0.85f * g)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.045f, 0.058f, 0.082f, 0.85f * g)),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.045f, 0.058f, 0.082f, 0.85f * g)));
    dl->AddRect(a, b, IM_COL32(255, 255, 255, (int)(30 * strength)), rounding, 0, 1.2f);
    dl->AddRect(a, b, Acc(0.22f * strength), rounding, 0, 1.0f);
}

// ---------------------------------------------------------------- background

static void DotGrid(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    const ImVec2 m = ImGui::GetIO().MousePos;
    const bool mv = ImGui::IsMousePosValid(&m);
    const float sp = 26.0f, half = 0.9f, spot = 220.0f;
    for (float y = a.y + sp * 0.5f; y < b.y; y += sp) {
        for (float x = a.x + sp * 0.5f; x < b.x; x += sp) {
            float k = 0.0f;
            if (mv) {
                const float dx = x - m.x, dy = y - m.y;
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d < spot) { k = 1.0f - d / spot; k *= k; }
            }
            const ImVec4 cc = Mix4(ImVec4(1, 1, 1, 0.035f), ImVec4(Accent2().x, Accent2().y, Accent2().z, 0.55f), k);
            const float h = half * (1.0f + k * 0.8f);
            dl->AddRectFilled(ImVec2(x - h, y - h), ImVec2(x + h, y + h),
                              ImGui::ColorConvertFloat4ToU32(cc));
        }
    }
}

static void Aurora(ImDrawList* dl, ImVec2 a, ImVec2 b, float t) {
    const ImVec2 sz = ImVec2(b.x - a.x, b.y - a.y);
    const ImVec2 p1 = ImVec2(a.x + sz.x * (0.16f + 0.06f * std::sinf(t * 0.21f)),
                             a.y + sz.y * (0.12f + 0.08f * std::cosf(t * 0.17f)));
    const ImVec2 p2 = ImVec2(a.x + sz.x * (0.86f + 0.05f * std::cosf(t * 0.15f)),
                             a.y + sz.y * (0.84f + 0.07f * std::sinf(t * 0.19f)));
    const ImVec2 p3 = ImVec2(a.x + sz.x * (0.55f + 0.14f * std::sinf(t * 0.11f)),
                             a.y + sz.y * (0.48f + 0.12f * std::sinf(t * 0.13f + 1.0f)));
    Glow(dl, p1, 560.0f, Acc(0.20f));
    Glow(dl, p2, 600.0f, ImGui::ColorConvertFloat4ToU32(ImVec4(Accent2().x, Accent2().y, Accent2().z, 0.13f)));
    ImVec4 mix = Mix4(Accent(), Accent2(), 0.5f); mix.w = 0.07f;
    Glow(dl, p3, 420.0f, ImGui::ColorConvertFloat4ToU32(mix));

    if (SnapshotConfig().dotsFx) DotGrid(dl, a, b);
}

static void Waves(ImDrawList* dl, ImVec2 a, ImVec2 b, float t) {
    const ImVec2 sz = ImVec2(b.x - a.x, b.y - a.y);
    Glow(dl, ImVec2(a.x + sz.x * 0.5f, a.y + sz.y * 0.55f), 620.0f, Acc(0.10f));
    const int lines = 24;
    const float step = 14.0f;
    static ImVec2 pts[512];
    for (int i = 0; i < lines; ++i) {
        const float k = (float)i / (lines - 1);
        const float base = a.y + sz.y * (0.26f + 0.52f * k);
        const float edge = 1.0f - std::fabs(k - 0.5f) * 2.0f;
        int n = 0;
        for (float x = a.x - step; x <= b.x + step && n < 512; x += step) {
            const float y = base
                + std::sinf(x * 0.0055f + t * 0.55f + i * 0.30f) * 34.0f * (0.5f + 0.5f * edge)
                + std::sinf(x * 0.0125f - t * 0.80f + i * 0.17f) * 12.0f;
            pts[n++] = ImVec2(x, y);
        }
        const ImVec4 c = Mix4(ImVec4(1, 1, 1, 1), Accent2(), 0.5f);
        const float alpha = 0.035f + 0.15f * edge * edge;
        dl->AddPolyline(pts, n, ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, alpha)), 0, 1.1f);
    }
    if (SnapshotConfig().dotsFx) DotGrid(dl, a, b);
}

void DrawBackground(ImDrawList* dl, ImVec2 a, ImVec2 b, float t) {
    const Config c = SnapshotConfig();
    if (c.bgFx == 0) Aurora(dl, a, b, t);
    else if (c.bgFx == 1) Waves(dl, a, b, t);
    // 2 = duz: efekt yok
}

// ---------------------------------------------------------------- toasts
struct ToastItem { char text[160]; bool ok; float t0; };
static std::vector<ToastItem> g_toasts;

void Toast(const char* text, bool ok) {
    Config c = SnapshotConfig();
    if (!c.toasts) return;
    ToastItem it{};
    std::strncpy(it.text, text ? text : "", sizeof(it.text) - 1);
    it.ok = ok;
    it.t0 = (float)ImGui::GetTime();
    g_toasts.push_back(it);
    while (g_toasts.size() > 4) g_toasts.erase(g_toasts.begin());
}

static float EaseOutQuint(float t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    const float f = 1.0f - t;
    return 1.0f - f * f * f * f * f;
}

void DrawToasts(ImDrawList* dl, ImVec2 windowRightTop, float t) {
    const float w = 270.0f, h = 42.0f, gap = 8.0f, life = 3.2f;
    float y = windowRightTop.y + 10.0f;
    for (int i = (int)g_toasts.size() - 1; i >= 0; --i) {
        const float age = t - g_toasts[i].t0;
        if (age > life) { g_toasts.erase(g_toasts.begin() + i); continue; }
        const float in  = EaseOutQuint(age / 0.28f);
        const float out = age > life - 0.35f ? (life - age) / 0.35f : 1.0f;
        const float alpha = in * out;
        if (alpha <= 0.01f) continue;
        const float x = windowRightTop.x - w - 12.0f - (1.0f - in) * w;
        const ImVec2 a = ImVec2(x, y), b2 = ImVec2(x + w, y + h);
        GlassPanel(dl, a, b2, 9.0f, alpha * 0.95f);
        // durum noktası
        const ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(
            g_toasts[i].ok ? Accent().x : 1.0f,
            g_toasts[i].ok ? Accent().y : 0.35f,
            g_toasts[i].ok ? Accent().z : 0.35f, alpha));
        dl->AddCircleFilled(ImVec2(a.x + 18.0f, y + h * 0.5f), 4.5f, col, 16);
        dl->AddText(ImVec2(a.x + 30.0f, y + 5.0f),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.92f, 0.95f, 0.94f, alpha)),
                    g_toasts[i].text);
        y += h + gap;
    }
}

void LoadStyle(ImGuiStyle& st) {
    st.FrameRounding = 5.0f; st.TabRounding = 5.0f; st.GrabRounding = 4.0f;
    st.ChildRounding = 6.0f; st.PopupRounding = 6.0f; st.ScrollbarRounding = 6.0f;
    st.WindowPadding = ImVec2(14, 12); st.FramePadding = ImVec2(10, 6);
    st.ItemSpacing = ImVec2(8, 8); st.WindowBorderSize = 0.0f;
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg]           = ImVec4(0.045f, 0.058f, 0.080f, 1.00f);
    c[ImGuiCol_TitleBg]            = ImVec4(0.040f, 0.052f, 0.070f, 1.00f);
    c[ImGuiCol_Tab]                = ImVec4(0.070f, 0.088f, 0.115f, 1.00f);
    c[ImGuiCol_TabHovered]         = Mix4(ImVec4(0.10f, 0.10f, 0.12f, 1), Accent2(), 0.55f);
    c[ImGuiCol_TabSelected]        = Mix4(ImVec4(0.06f, 0.09f, 0.11f, 1), Accent2(), 0.75f);
    c[ImGuiCol_TabDimmed]          = ImVec4(0.060f, 0.075f, 0.095f, 1.00f);
    c[ImGuiCol_TabDimmedSelected]  = ImVec4(0.075f, 0.105f, 0.125f, 1.00f);
    c[ImGuiCol_FrameBg]            = ImVec4(0.095f, 0.115f, 0.150f, 0.95f);
    c[ImGuiCol_FrameBgHovered]     = ImVec4(0.120f, 0.150f, 0.195f, 1.00f);
    c[ImGuiCol_FrameBgActive]      = ImVec4(0.140f, 0.190f, 0.245f, 1.00f);
    c[ImGuiCol_CheckMark]          = Accent();
    c[ImGuiCol_SliderGrab]         = Accent();
    c[ImGuiCol_SliderGrabActive]   = Mix4(Accent(), ImVec4(1, 1, 1, 1), 0.30f);
    c[ImGuiCol_Button]             = ImVec4(0.100f, 0.120f, 0.155f, 1.00f);
    c[ImGuiCol_ButtonHovered]      = Mix4(ImVec4(0.10f, 0.12f, 0.15f, 1), Accent2(), 0.45f);
    c[ImGuiCol_ButtonActive]       = Accent();
    c[ImGuiCol_Separator]          = ImGui::ColorConvertU32ToFloat4(Acc(0.35f));
    c[ImGuiCol_SeparatorActive]    = ImGui::ColorConvertU32ToFloat4(Acc(0.90f));
    c[ImGuiCol_TextDisabled]       = ImVec4(0.500f, 0.550f, 0.620f, 1.00f);
    c[ImGuiCol_Text]               = ImVec4(0.92f, 0.95f, 0.94f, 1.00f);
}

} // namespace fx

#pragma once
// AEGIS gorunum katmani: Cobalt-stili tema (aurora bg, nokta izgara,
// glass panel, toast bildirimleri, vurgu rengi tematik gecisleri).
#include <imgui.h>
#include <utility>

namespace fx {

// Mevcut vurgu rengi (animasyonlu gecişli). Her frame Ease ile hedefe yaklasir.
ImVec4 Accent();
ImVec4 Accent2();
ImU32  Acc(float alpha = 1.0f);   // Accent() -> U32

// 8 preset tema (isim + iki vurgu rengi)
int  PresetCount();
const char* PresetName(int i);
// Preset i'nin ikili vurgu rengi (a = ana, b = koyu)
std::pair<ImVec4, ImVec4> PresetColors(int i);

// Her frame cagir: config'ten hedef rengi okur, akicil lerp yapar.
void Tick(float dt);

// Arka plan: aurora / dalga / duz (config bgFx) + fareyi takip eden noktalar
void DrawBackground(ImDrawList* dl, ImVec2 a, ImVec2 b, float t);

// Yogun isik (radial glow) — katmanli dairelerle.
void Glow(ImDrawList* dl, ImVec2 c, float radius, ImU32 col);

// Glass panel: yumusak gölge + gradient dolgu + gradient kenar.
void GlassPanel(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float strength = 1.0f);

// ---- Toast (kose bildirim) ----
void Toast(const char* text, bool ok = true);
void DrawToasts(ImDrawList* dl, ImVec2 windowRightTop, float t); // sag ustte asagi dogru dizer
void LoadStyle(ImGuiStyle& st); // ana style rengini accent ile baglar

} // namespace fx

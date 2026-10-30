#pragma once
// AEGIS grafik kabugu ile Cobalt platform katmani arasindaki ko-pru.
// app.cpp bu global'leri tanimlar; cobalt_platform.cpp / vendor ui surekli okur.
#include <windows.h>
#include <d3d11.h>

namespace aegHost {
extern HWND                  g_hwnd;
extern ID3D11Device*         g_device;      // may be null until D3D init
extern ID3D11DeviceContext*  g_context;
extern IDXGISwapChain*       g_swapChain;
extern bool                  g_dragging;
extern POINT                 g_dragCursor;
extern RECT                  g_dragRect;
extern bool                  g_quit;
extern float                 g_dpi;
}

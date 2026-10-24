// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/render.cpp and src/hooks.cpp at 89423aa) on
// 2026-10-24. Copyright (C) 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
//
// Trimmed to the capture: MTPlugins' overlay drawing, model and LUT previews are left out. Radiant draws each view
// through its own Direct3D 11 swap chain: Cam_Draw or XY_Draw records the view about to be presented, and the Present
// that follows it is that view's.
#include "render.h"
#include "addrs.h"
#include "hooks.h"
#include "log.h"
#include "plugin.h"
#include "project.h"
#include "qt.h"
#include <windows.h>
#include <d3d11.h>
#include <cstring>
#include "modtools_plugin.h"

namespace host::render
{
    namespace
    {
        using CamDraw_t = __int64(__fastcall*)(BYTE* camWnd, __int64, __int64, __int64);
        using XYDraw_t = __int64(__fastcall*)(BYTE* xyWnd, __int64*, __int64, __int64);
        using Present_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
        using GetFov_t = float(__fastcall*)();

        CamDraw_t o_Cam_Draw = nullptr;
        XYDraw_t o_XY_Draw = nullptr;
        Present_t o_Present = nullptr;

        enum class View { None, Cam, XY };
        View g_view = View::None;      // the view drawn last
        View g_pending = View::None;   // drawn and not presented yet
        CamParams g_cam{};
        XYParams g_xy{};
        HWND g_camHwnd = nullptr;
        HWND g_xyHwnd[3] = {};
        bool g_inFrame = false;
        float g_frameW = 0, g_frameH = 0;

        __int64 __fastcall hk_Cam_Draw(BYTE* camWnd, __int64 a2, __int64 a3, __int64 a4)
        {
            CamParams p{};
            memcpy(p.origin, camWnd + rt::off_CamWnd_Origin, sizeof p.origin);
            memcpy(p.angles, camWnd + rt::off_CamWnd_Angles, sizeof p.angles);
            p.width = *reinterpret_cast<int*>(camWnd + rt::off_CamWnd_Width);
            p.height = *reinterpret_cast<int*>(camWnd + rt::off_CamWnd_Height);
            p.probeView = *rt::Global<char>(rt::g_probeView) != 0;
            p.gameView = *rt::Global<char>(rt::g_gameView) != 0 || p.probeView;
            p.fovDeg = rt::Func<GetFov_t>(rt::Cam_GetFov)();   // what this draw projects with
            g_cam = p;
            g_view = g_pending = View::Cam;
            return o_Cam_Draw(camWnd, a2, a3, a4);
        }

        __int64 __fastcall hk_XY_Draw(BYTE* xyWnd, __int64* a2, __int64 a3, __int64 a4)
        {
            // camera moves queue hidden 2D panes too: only a shown one presents what it draws
            void* widget = *reinterpret_cast<void**>(xyWnd + rt::off_XYWnd_Widget);
            if (!widget || qt::WidgetVisible(widget))
            {
                g_xy.type = *reinterpret_cast<int*>(xyWnd + rt::off_XYWnd_ViewType);
                memcpy(g_xy.origin, xyWnd + rt::off_XYWnd_Origin, sizeof g_xy.origin);
                g_xy.scale = *reinterpret_cast<float*>(xyWnd + rt::off_XYWnd_Scale);
                g_view = g_pending = View::XY;
            }
            return o_XY_Draw(xyWnd, a2, a3, a4);
        }

        void Dispatch(int view, int w, int h)
        {
            ModToolsDrawContext ctx{};
            ctx.view = view;
            ctx.width = w;
            ctx.height = h;
            memcpy(ctx.cam.origin, g_cam.origin, sizeof ctx.cam.origin);
            memcpy(ctx.cam.angles, g_cam.angles, sizeof ctx.cam.angles);
            ctx.cam.width = g_cam.width;
            ctx.cam.height = g_cam.height;
            ctx.cam.gameView = g_cam.gameView ? 1 : 0;
            ctx.xy.type = g_xy.type;
            memcpy(ctx.xy.origin, g_xy.origin, sizeof ctx.xy.origin);
            ctx.xy.scale = g_xy.scale;
            g_frameW = float(w);
            g_frameH = float(h);
            g_inFrame = true;
            plugin::Draw(ctx);
            g_inFrame = false;
        }

        HRESULT STDMETHODCALLTYPE hk_Present(IDXGISwapChain* sc, UINT sync, UINT flags)
        {
            if (g_pending != View::None && g_pending == g_view)
            {
                g_pending = View::None;
                DXGI_SWAP_CHAIN_DESC d{};
                if (SUCCEEDED(sc->GetDesc(&d)))
                {
                    int w = int(d.BufferDesc.Width), h = int(d.BufferDesc.Height);
                    if (g_view == View::Cam)
                    {
                        g_cam.width = w;
                        g_cam.height = h;
                        g_camHwnd = d.OutputWindow;
                        Dispatch(MODTOOLS_VIEW_3D, w, h);
                    }
                    else if (g_xy.type >= 0 && g_xy.type < 3)
                    {
                        HWND& slot = g_xyHwnd[g_xy.type];
                        if (!slot || !IsWindow(slot)) slot = d.OutputWindow;
                        Dispatch(MODTOOLS_VIEW_2D, w, h);
                    }
                }
            }
            return o_Present(sc, sync, flags);
        }

        // Present is a method of dxgi's swap chain: its address comes from the vtable of a throwaway one.
        void* PresentAddress()
        {
            HMODULE d3d11 = GetModuleHandleW(L"d3d11.dll");
            using Create_t = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT,
                                              const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*,
                                              ID3D11DeviceContext**);
            auto create = d3d11 ? reinterpret_cast<Create_t>(GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain")) : nullptr;
            if (!create)
            {
                HLOG("render: d3d11.dll or D3D11CreateDeviceAndSwapChain missing");
                return nullptr;
            }
            WNDCLASSEXW wc{ sizeof wc };
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"T7RadiantBridge_DXProbe";
            RegisterClassExW(&wc);
            HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
            DXGI_SWAP_CHAIN_DESC sd{};
            sd.BufferCount = 1;
            sd.BufferDesc.Width = 64;
            sd.BufferDesc.Height = 64;
            sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.OutputWindow = hwnd;
            sd.SampleDesc.Count = 1;
            sd.Windowed = TRUE;
            sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0, got;
            IDXGISwapChain* sc = nullptr;
            ID3D11Device* dev = nullptr;
            ID3D11DeviceContext* ctx = nullptr;
            HRESULT hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &want, 1, D3D11_SDK_VERSION, &sd, &sc, &dev, &got, &ctx);
            void* present = nullptr;
            if (SUCCEEDED(hr) && sc) present = (*reinterpret_cast<void***>(sc))[8];
            else HLOG("render: the probe swap chain failed (0x%08lX)", hr);
            if (sc) sc->Release();
            if (ctx) ctx->Release();
            if (dev) dev->Release();
            DestroyWindow(hwnd);
            UnregisterClassW(wc.lpszClassName, wc.hInstance);
            return present;
        }

        bool Hook(const char* name, void* target, void* detour, void** original)
        {
            bool ok = target && hooks::Detour(target, detour, original);
            HLOG("hook: %s %s at 0x%p", name, ok ? "installed" : "FAILED", target);
            return ok;
        }
    }

    bool Install()
    {
        bool ok = Hook("CCamWnd::Cam_Draw", reinterpret_cast<void*>(rt::Rva(rt::CCamWnd_Cam_Draw)), reinterpret_cast<void*>(&hk_Cam_Draw), reinterpret_cast<void**>(&o_Cam_Draw));
        ok &= Hook("CXYWnd::XY_Draw", reinterpret_cast<void*>(rt::Rva(rt::CXYWnd_XY_Draw)), reinterpret_cast<void*>(&hk_XY_Draw), reinterpret_cast<void**>(&o_XY_Draw));
        ok &= Hook("IDXGISwapChain::Present", PresentAddress(), reinterpret_cast<void*>(&hk_Present), reinterpret_cast<void**>(&o_Present));
        if (!ok) HLOG("render: capture is off");
        return ok;
    }

    void* CamWindow() { return g_camHwnd; }

    void* XYWindow(int type)
    {
        if (type < 0 || type > 2) return nullptr;
        HWND h = g_xyHwnd[type];
        return h && IsWindow(h) ? h : nullptr;
    }

    bool Project(const float p[3], float& sx, float& sy)
    {
        if (!g_inFrame) return false;
        return g_view == View::Cam ? ProjectCam(g_cam, p, sx, sy) : ProjectXY(g_xy, g_frameW, g_frameH, p, sx, sy);
    }
}

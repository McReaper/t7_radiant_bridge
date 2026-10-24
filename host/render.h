// What the bridge's capture needs from Radiant's drawing: which view is being presented, its window, and where a world
// point lands in it. Nothing is drawn into the views. Main thread (Radiant draws there).
#pragma once

namespace host::render
{
    // Detours CCamWnd::Cam_Draw and CXYWnd::XY_Draw (a view's parameters) and IDXGISwapChain::Present (its window and
    // frame size, then the plugin's OnDraw). False when any is missing: the capture is then off.
    bool Install();

    void* CamWindow();          // the 3D view's window, nullptr before its first frame
    void* XYWindow(int type);   // the window that last presented this 2D axis, nullptr if none did or it is gone
    // Only inside OnDraw: the point in the frame being presented. False outside OnDraw or behind the camera.
    bool Project(const float p[3], float& sx, float& sy);
}

// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/render.cpp at 89423aa) on 2026-10-24. Copyright
// (C) 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
//
// World point -> view pixel, as Radiant's 3D and 2D views draw it. No Radiant, so host/test/host_test.cpp checks it.
#pragma once

namespace host
{
    struct CamParams
    {
        float origin[3];
        float angles[3];   // pitch (positive looks up), yaw (90 = +y), roll, in degrees
        int width, height; // the frame in pixels
        bool gameView;     // GAME VIEW or STATIC PROBE VIEW
        bool probeView;    // STATIC PROBE VIEW
        float fovDeg;      // Cam_GetFov at that draw: horizontal FOV of a 16:9 view, 0 = unknown
    };

    struct XYParams
    {
        int type;          // 2 = XY (top), 1 = XZ (front), 0 = YZ (side)
        float origin[3];   // view centre
        float scale;       // pixels per unit
    };

    // False when the point is less than a unit in front of the camera. (sx, sy): from the frame's top left.
    bool ProjectCam(const CamParams& c, const float p[3], float& sx, float& sy);
    bool ProjectXY(const XYParams& xy, float width, float height, const float p[3], float& sx, float& sy);
}

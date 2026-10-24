// Modified from MTPlugins (https://github.com/Aidan601/MTPlugins, src/render.cpp at 89423aa) on 2026-10-24. Copyright
// (C) 2026 Figglebottom and contributors, GNU GPL v3 like the rest of this repository.
#include "project.h"
#include <cmath>

namespace host
{
    namespace
    {
        constexpr float kDeg = 3.14159265358979f / 180.0f;
        constexpr float kEditorFov = 100.0f;   // Radiant's Fov preference default (a 16:9 view's horizontal FOV)
        constexpr float kGameFov = 65.0f;      // GAME VIEW: cg_fov, a 4:3 view's horizontal FOV

        // Quake axes: x forward, y left, z up.
        void AngleVectors(const float angles[3], float fwd[3], float right[3], float up[3])
        {
            float p = angles[0] * kDeg, y = angles[1] * kDeg;
            float cp = cosf(p), sp = sinf(p), cy = cosf(y), sy = sinf(y);
            fwd[0] = cp * cy;
            fwd[1] = cp * sy;
            fwd[2] = sp;
            right[0] = fwd[1];   // forward x world up
            right[1] = -fwd[0];
            right[2] = 0;
            float rl = sqrtf(right[0] * right[0] + right[1] * right[1]);
            if (rl < 1e-5f)
            {
                right[0] = 0;
                right[1] = -1;
            }
            else
            {
                right[0] /= rl;
                right[1] /= rl;
            }
            up[0] = right[1] * fwd[2] - right[2] * fwd[1];   // right x forward
            up[1] = right[2] * fwd[0] - right[0] * fwd[2];
            up[2] = right[0] * fwd[1] - right[1] * fwd[0];
        }

        // Radiant keeps the vertical FOV and lets the horizontal one follow the pane's aspect: the editor's FOV is a 16:9
        // view's, the game view's a 4:3 view's.
        float TanHalfVertical(const CamParams& c)
        {
            if (c.fovDeg > 1.0f && c.fovDeg < 179.0f) return tanf(c.fovDeg * 0.5f * kDeg) * (9.0f / 16.0f);
            return c.gameView ? tanf(kGameFov * 0.5f * kDeg) * 0.75f : tanf(kEditorFov * 0.5f * kDeg) * (9.0f / 16.0f);
        }

        // The game view draws its 16:9 lens into a centred 16k x 9k rectangle, the probe view its 90/90 lens into a square.
        float Focal(const CamParams& c)
        {
            float w = float(c.width), h = float(c.height);
            if (c.probeView) return (w < h ? w : h) * 0.5f;
            if (!c.gameView) return h * 0.5f / TanHalfVertical(c);
            int k = w <= 16.0f / 9.0f * h ? int(w / 16.0f) : int(h / 9.0f);
            return float(9 * k) * 0.5f / TanHalfVertical(c);
        }
    }

    bool ProjectCam(const CamParams& c, const float p[3], float& sx, float& sy)
    {
        float f[3], r[3], u[3];
        AngleVectors(c.angles, f, r, u);
        float d[3] = { p[0] - c.origin[0], p[1] - c.origin[1], p[2] - c.origin[2] };
        float z = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
        if (z < 1.0f) return false;
        float x = d[0] * r[0] + d[1] * r[1] + d[2] * r[2];
        float y = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
        float focal = Focal(c);
        sx = float(c.width) * 0.5f + x / z * focal;
        sy = float(c.height) * 0.5f - y / z * focal;
        return true;
    }

    bool ProjectXY(const XYParams& xy, float width, float height, const float p[3], float& sx, float& sy)
    {
        int ha = xy.type == 0 ? 1 : 0;   // horizontal axis
        int va = xy.type == 2 ? 1 : 2;   // vertical axis
        sx = width * 0.5f + (p[ha] - xy.origin[ha]) * xy.scale;
        sy = height * 0.5f - (p[va] - xy.origin[va]) * xy.scale;
        return true;
    }
}

// Camera math for putting Skyrim's camera where San Andreas's is (BodyState camera), from PerkeleRim.
// Header-only and free of Skyrim types, so the protocol tests cover it.
//
// Matrices are m[row][col] and map a camera-local vector to Skyrim world space (world = m * local),
// like NiMatrix3 in a NiTransform. Camera-local axes: +X right, +Y forward, +Z up.

#pragma once

#include <cmath>

namespace AhShitRim::Camera
{
    using Matrix = float[3][3];

    inline void Multiply(const Matrix& a, const Matrix& b, Matrix& out)
    {
        Matrix r{};
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
            }
        }
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                out[i][j] = r[i][j];
            }
        }
    }

    inline void Transpose(const Matrix& a, Matrix& out)
    {
        Matrix r{};
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                r[i][j] = a[j][i];
            }
        }
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                out[i][j] = r[i][j];
            }
        }
    }

    // A view turned to a_heading (radians clockwise from north), a_pitch (positive looks down) and
    // a_roll (positive tilts right): Rz(-heading) * Rx(-pitch) * Ry(roll).
    inline void Basis(float a_heading, float a_pitch, float a_roll, Matrix& out)
    {
        const float ch = std::cos(-a_heading), sh = std::sin(-a_heading);
        const float cp = std::cos(-a_pitch), sp = std::sin(-a_pitch);
        const float cr = std::cos(a_roll), sr = std::sin(a_roll);
        const Matrix z = { { ch, -sh, 0.0f }, { sh, ch, 0.0f }, { 0.0f, 0.0f, 1.0f } };
        const Matrix x = { { 1.0f, 0.0f, 0.0f }, { 0.0f, cp, -sp }, { 0.0f, sp, cp } };
        const Matrix y = { { cr, 0.0f, sr }, { 0.0f, 1.0f, 0.0f }, { -sr, 0.0f, cr } };
        Matrix zx;
        Multiply(z, x, zx);
        Multiply(zx, y, out);
    }

    struct Angles
    {
        float heading = 0.0f;
        float pitch = 0.0f;
        float roll = 0.0f;
    };

    // The angles Basis takes to a view looking along a_forward with a_up as its up (unit vectors).
    // Straight down the heading is a guess from the forward's last crumbs, and the roll makes up the
    // rest of the turn about the view: San Andreas's death camera swings that way. A zero a_up (none
    // sent) is no roll.
    inline Angles AnglesOf(const float (&a_forward)[3], const float (&a_up)[3])
    {
        Angles a;
        a.heading = std::atan2(a_forward[0], a_forward[1]);
        a.pitch = std::atan2(-a_forward[2], std::hypot(a_forward[0], a_forward[1]));
        const float sh = std::sin(a.heading), ch = std::cos(a.heading);
        const float sp = std::sin(a.pitch), cp = std::cos(a.pitch);
        const float right[3] = { ch, -sh, 0.0f };       // Basis's local x without roll
        const float up[3] = { sh * sp, ch * sp, cp };    // and its local z
        const float x = a_up[0] * right[0] + a_up[1] * right[1] + a_up[2] * right[2];
        const float z = a_up[0] * up[0] + a_up[1] * up[1] + a_up[2] * up[2];
        if (x * x + z * z > 1e-6f) {
            a.roll = std::atan2(x, z);
        }
        return a;
    }

    // What Skyrim's camera rotation a_skyrim, built for a view turned to (a_fromHeading, a_fromPitch),
    // does on top of plain heading and pitch: its own local axes, and in a dialogue its turn to the
    // speaker. Kept from before a menu, it leaves that turn out.
    inline void OffsetOf(const Matrix& a_skyrim, float a_fromHeading, float a_fromPitch, Matrix& out)
    {
        Matrix from, fromT;
        Basis(a_fromHeading, a_fromPitch, 0.0f, from);
        Transpose(from, fromT);
        Multiply(fromT, a_skyrim, out);
    }

    // A view turned to (a_heading, a_pitch, a_roll) with an OffsetOf on top.
    inline void Apply(const Matrix& a_offset, float a_heading, float a_pitch, float a_roll, Matrix& out)
    {
        Matrix to;
        Basis(a_heading, a_pitch, a_roll, to);
        Multiply(to, a_offset, out);
    }

    // Skyrim's camera rotation a_skyrim was built for a view turned to (a_fromHeading, a_fromPitch);
    // returns it turned to (a_heading, a_pitch, a_roll) instead. Whatever Skyrim's camera node does
    // on top of plain heading and pitch (its own local axes) carries over unchanged.
    inline void Retarget(const Matrix& a_skyrim, float a_fromHeading, float a_fromPitch, float a_heading, float a_pitch, float a_roll, Matrix& out)
    {
        Matrix offset;
        OffsetOf(a_skyrim, a_fromHeading, a_fromPitch, offset);
        Apply(offset, a_heading, a_pitch, a_roll, out);
    }

}

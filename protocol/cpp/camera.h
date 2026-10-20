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

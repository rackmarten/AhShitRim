// How a vehicle rides, measured from where it's drawn: the test kit's asr.drive (skse/src/debug/TestKit.cpp) samples
// the driven vehicle each frame and this sums up the leg. Game-free, so the protocol tests cover it.
//
// The slip is the angle between where the vehicle's origin moves and the way it faces (as fake_vehicles' handling
// check works it out): a vehicle turning about its middle moves along its heading (slip near 0), a car turning about
// its rear axle swings its middle out (San Andreas's Rhino slid 14 to 17 degrees off on flat ground), and one sliding
// sideways on rocks reads up to 90.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "body.h"

namespace AhShitRim::Ride
{
    inline constexpr float kDegrees = 57.29578f;

    // How the vehicle's frame is turned off level, degrees: pitch (its nose up), roll (its right side up) and tilt (its
    // up axis off the vertical: 90 on its side, 180 on its roof).
    struct Attitude
    {
        float pitch = 0.0f;
        float roll = 0.0f;
        float tilt = 0.0f;
    };

    // a_frame: the vehicle's matrix (Body::ToMatrix of BodyState.vehicleFrame: X right, Y forward, Z up as columns).
    inline Attitude AttitudeOf(const Body::Matrix34& a_frame)
    {
        const auto clamp = [](float a_v) { return std::clamp(a_v, -1.0f, 1.0f); };
        return { std::asin(clamp(a_frame.m[2][1])) * kDegrees, std::asin(clamp(a_frame.m[2][0])) * kDegrees, std::acos(clamp(a_frame.m[2][2])) * kDegrees };
    }

    // A leg's numbers so far. Units are the caller's (Skyrim units for the kit), headings Skyrim radians (0 north,
    // clockwise: it faces (sin h, cos h)).
    class Leg
    {
    public:
        // Slip only counts while it moves at least this fast (units/s): standing, any jitter is a wild angle.
        explicit Leg(float a_minSpeed) : _minSpeed(a_minSpeed) {}

        void Add(double a_seconds, float a_x, float a_y, float a_z, float a_heading, const Attitude& a_attitude)
        {
            _maxTilt = (std::max)(_maxTilt, a_attitude.tilt);
            _maxRoll = (std::max)(_maxRoll, std::abs(a_attitude.roll));
            _maxPitch = (std::max)(_maxPitch, std::abs(a_attitude.pitch));
            _lastSlip = 0.0f;
            _lastSpeed = 0.0f;
            if (!_samples) {
                _startX = a_x;
                _startY = a_y;
                _minZ = _maxZ = a_z;
            } else if (a_seconds > _t) {
                _path += std::hypot(a_x - _x, a_y - _y);
                _yaw += std::remainder(a_heading - _heading, 6.2831853f);
                // Speed, rise and slip over the last kWindow seconds, not a frame: a frame's sample time and the drawn
                // pose's don't quite match, and a frame's few units of move read as wild speeds.
                const Sample* from = nullptr;
                for (int i = 0; i < _history; i++) {
                    const auto& s = _ring[(_next - 1 - i + kRing) % kRing];
                    if (a_seconds - s.t >= kWindow) {
                        from = &s;
                        break;
                    }
                }
                if (from) {
                    const float dt = static_cast<float>(a_seconds - from->t);
                    const float dx = a_x - from->x, dy = a_y - from->y, move = std::hypot(dx, dy);
                    _lastSpeed = move / dt;
                    _topSpeed = (std::max)(_topSpeed, _lastSpeed);
                    // An impact: under half the speed it had a window ago, from a run. Counted once, until it runs at
                    // most of that speed again (it may bounce off and roll a little first).
                    if (_inImpact) {
                        _inImpact = _lastSpeed < 0.8f * _impactFrom;
                    } else if (from->speed >= 2.0f * _minSpeed && _lastSpeed < 0.5f * from->speed) {
                        _impacts++;
                        _inImpact = true;
                        _impactFrom = from->speed;
                    }
                    _maxRise = (std::max)(_maxRise, (a_z - from->z) / dt);
                    if (_lastSpeed >= _minSpeed) {
                        // Its sideways share of the move, against its heading half way through (a circle's chord
                        // runs along its tangent at the middle).
                        const float h = from->heading + std::remainder(a_heading - from->heading, 6.2831853f) * 0.5f;
                        const float side = std::abs(dx * std::cos(h) - dy * std::sin(h)) / move;
                        _lastSlip = std::asin(std::clamp(side, 0.0f, 1.0f)) * kDegrees;
                        _slipSum += _lastSlip;
                        _slips.push_back(_lastSlip);
                        _slipMax = (std::max)(_slipMax, _lastSlip);
                        _slipSamples++;
                    }
                }
            } else {
                return;  // the same moment again: nothing new
            }
            _ring[_next] = { a_seconds, a_x, a_y, a_z, a_heading, _lastSpeed };
            _next = (_next + 1) % kRing;
            _history = (std::min)(_history + 1, kRing);
            _minZ = (std::min)(_minZ, a_z);
            _maxZ = (std::max)(_maxZ, a_z);
            _t = a_seconds;
            _x = a_x;
            _y = a_y;
            _z = a_z;
            _heading = a_heading;
            _samples++;
        }

        int   Samples() const { return _samples; }
        float Turned() const { return _yaw * kDegrees; }  // signed: clockwise (right) positive
        float Path() const { return _path; }
        float Moved() const { return std::hypot(_x - _startX, _y - _startY); }
        float TopSpeed() const { return _topSpeed; }
        float MeanSlip() const { return _slipSamples ? _slipSum / static_cast<float>(_slipSamples) : 0.0f; }
        float MaxSlip() const { return _slipMax; }
        // The median: a few frames of an impact (a tree, a rock face) throw it sideways at 50 degrees and more, which
        // a mean over a leg takes up but the median doesn't.
        float MedianSlip() const
        {
            if (_slips.empty()) {
                return 0.0f;
            }
            auto       sorted = _slips;
            const auto middle = sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2);
            std::nth_element(sorted.begin(), middle, sorted.end());
            return *middle;
        }
        int Impacts() const { return _impacts; }  // times it was stopped short by something
        int   SlipSamples() const { return _slipSamples; }
        float LastSlip() const { return _lastSlip; }
        float LastSpeed() const { return _lastSpeed; }
        float MaxTilt() const { return _maxTilt; }
        float MaxRoll() const { return _maxRoll; }
        float MaxPitch() const { return _maxPitch; }
        float MaxRise() const { return _maxRise; }  // units/s up, the fastest
        float HeightRange() const { return _samples ? _maxZ - _minZ : 0.0f; }

        // The turn's radius from the path and the turn (a circle's arc over its angle), 0 when it hardly turned.
        float Radius() const { return std::abs(_yaw) > 0.05f ? _path / std::abs(_yaw) : 0.0f; }

        static constexpr double kWindow = 0.1;  // seconds the speed, rise and slip are taken over
        static constexpr int    kRing = 64;     // samples kept for it: 0.1 s at up to 600 a second

    private:
        struct Sample
        {
            double t;
            float  x, y, z, heading;
            float  speed;  // over the window before it
        };
        Sample _ring[kRing]{};
        int    _next = 0, _history = 0;
        float  _minSpeed;
        int    _samples = 0, _slipSamples = 0;
        double _t = 0.0;
        float  _x = 0.0f, _y = 0.0f, _z = 0.0f, _heading = 0.0f, _startX = 0.0f, _startY = 0.0f, _minZ = 0.0f, _maxZ = 0.0f;
        float  _path = 0.0f, _yaw = 0.0f, _topSpeed = 0.0f, _maxRise = 0.0f;
        float  _slipSum = 0.0f, _slipMax = 0.0f, _lastSlip = 0.0f, _lastSpeed = 0.0f;
        float  _maxTilt = 0.0f, _maxRoll = 0.0f, _maxPitch = 0.0f;
        std::vector<float> _slips;
        int                _impacts = 0;
        bool               _inImpact = false;
        float              _impactFrom = 0.0f;
    };
}

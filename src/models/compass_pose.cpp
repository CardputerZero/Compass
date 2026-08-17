#include "models/compass_pose.hpp"

#include <algorithm>
#include <cmath>

namespace compass {

namespace {

constexpr float kPi                 = 3.14159265359f;
constexpr float kRadToDeg           = 180.0f / kPi;
constexpr float kBubbleTiltRangeDeg = 18.0f;
constexpr float kMinimumVectorNorm  = 1.0e-5f;
// Keep heading usable while the top edge is steep, but avoid amplifying the
// noise once that edge is effectively parallel to gravity (about 87 degrees).
constexpr float kMinimumForwardNorm            = 0.05f;
constexpr float kMicroteslaPerGauss            = 100.0f;
constexpr float kGravityMetersPerSecondSquared = 9.81f;
constexpr float kGyroBaselineRadiansPerSecond  = 10.0f / kRadToDeg;

bool isFinite(const Axis3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

float dot(const Axis3& lhs, const Axis3& rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Axis3 cross(const Axis3& lhs, const Axis3& rhs)
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

Axis3 subtract(const Axis3& lhs, const Axis3& rhs)
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Axis3 scale(const Axis3& value, float factor)
{
    return {value.x * factor, value.y * factor, value.z * factor};
}

bool normalize(Axis3& value, float minimumNorm = kMinimumVectorNorm)
{
    const float norm = std::sqrt(dot(value, value));
    if (!std::isfinite(norm) || norm < minimumNorm) {
        return false;
    }

    value = scale(value, 1.0f / norm);
    return true;
}

float normalizeDegrees(float degrees)
{
    degrees = std::fmod(degrees, 360.0f);
    if (degrees < 0.0f) {
        degrees += 360.0f;
    }
    return degrees;
}

float clampUnit(float value)
{
    return std::clamp(value, -1.0f, 1.0f);
}

float maxAbsComponent(const Axis3& value)
{
    return std::max({std::abs(value.x), std::abs(value.y), std::abs(value.z)});
}

}  // namespace

Axis3 mapBmi270ToScreen(const Axis3& sensor)
{
    return {sensor.y, -sensor.x, sensor.z};
}

Axis3 mapBmm150ToScreen(const Axis3& sensor)
{
    return {sensor.y, sensor.x, -sensor.z};
}

Axis3 gaussToMicrotesla(const Axis3& gauss)
{
    return scale(gauss, kMicroteslaPerGauss);
}

bool isUsableVector(const Axis3& value)
{
    if (!isFinite(value)) {
        return false;
    }

    const float norm_squared = dot(value, value);
    return std::isfinite(norm_squared) && norm_squared >= kMinimumVectorNorm * kMinimumVectorNorm;
}

CompassPose calculateCompassPose(const Axis3& screenAccel, const Axis3& screenMag)
{
    CompassPose pose;
    if (!isFinite(screenAccel)) {
        return pose;
    }

    const float pitch = std::atan2(screenAccel.y, std::hypot(screenAccel.x, screenAccel.z));
    const float roll  = std::atan2(screenAccel.x, std::hypot(screenAccel.y, screenAccel.z));

    pose.pitchDeg = pitch * kRadToDeg;
    pose.rollDeg  = roll * kRadToDeg;
    pose.bubbleX  = clampUnit(pose.rollDeg / kBubbleTiltRangeDeg);
    pose.bubbleY  = clampUnit(-pose.pitchDeg / kBubbleTiltRangeDeg);

    if (!isFinite(screenMag)) {
        return pose;
    }

    Axis3 up = screenAccel;
    if (!normalize(up)) {
        return pose;
    }

    constexpr Axis3 kScreenTop{0.0f, 1.0f, 0.0f};
    Axis3 forward = subtract(kScreenTop, scale(up, dot(kScreenTop, up)));
    if (!normalize(forward, kMinimumForwardNorm)) {
        return pose;
    }

    Axis3 north = subtract(screenMag, scale(up, dot(screenMag, up)));
    if (!normalize(north)) {
        return pose;
    }

    Axis3 right = cross(forward, up);
    if (!normalize(right)) {
        return pose;
    }

    // The accelerometer points out of the screen when the display is face-up
    // and into it when the display is face-down.  The projected top edge is
    // unchanged by that flip, but cross(forward, up) changes the sign of the
    // right edge.  Align it with the screen's +X direction so turning the
    // device over does not introduce a 180-degree heading error.
    if (up.z < 0.0f) {
        right = scale(right, -1.0f);
    }

    pose.headingDeg   = normalizeDegrees(std::atan2(-dot(north, right), dot(north, forward)) * kRadToDeg);
    pose.headingValid = std::isfinite(pose.headingDeg);
    return pose;
}

float calculateBubbleMotion(const Axis3& screenAccel, const Axis3& screenGyro)
{
    if (!isFinite(screenAccel) || !isFinite(screenGyro)) {
        return 0.0f;
    }

    const float accel_level = maxAbsComponent(screenAccel) / kGravityMetersPerSecondSquared;
    const float gyro_level  = maxAbsComponent(screenGyro) / kGyroBaselineRadiansPerSecond;
    return std::clamp(std::max(accel_level, gyro_level) - 1.0f, 0.0f, 1.0f);
}

}  // namespace compass

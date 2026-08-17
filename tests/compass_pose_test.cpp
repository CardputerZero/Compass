#include "models/compass_pose.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr float kPi       = 3.14159265359f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kGravity  = 9.81f;

int failures = 0;

void require(bool condition, const std::string& message)
{
    if (condition) {
        return;
    }

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void requireNear(float actual, float expected, float tolerance, const std::string& message)
{
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
            message + " (actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected) + ")");
}

void requireAngleNear(float actual, float expected, float tolerance, const std::string& message)
{
    float delta = std::fmod(actual - expected, 360.0f);
    if (delta < -180.0f) {
        delta += 360.0f;
    } else if (delta > 180.0f) {
        delta -= 360.0f;
    }
    require(std::isfinite(actual) && std::abs(delta) <= tolerance,
            message + " (actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected) + ")");
}

void requireAxisNear(const compass::Axis3& actual, const compass::Axis3& expected, const std::string& message,
                     float tolerance = 0.0f)
{
    requireNear(actual.x, expected.x, tolerance, message + " X");
    requireNear(actual.y, expected.y, tolerance, message + " Y");
    requireNear(actual.z, expected.z, tolerance, message + " Z");
}

float dot(const compass::Axis3& lhs, const compass::Axis3& rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

compass::Axis3 add(const compass::Axis3& lhs, const compass::Axis3& rhs)
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

compass::Axis3 subtract(const compass::Axis3& lhs, const compass::Axis3& rhs)
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

compass::Axis3 scale(const compass::Axis3& value, float factor)
{
    return {value.x * factor, value.y * factor, value.z * factor};
}

compass::Axis3 worldToScreen(const compass::Axis3& world, float headingDeg, float pitchDeg, float rollDeg)
{
    const float heading = headingDeg * kDegToRad;
    const float pitch   = pitchDeg * kDegToRad;
    const float roll    = rollDeg * kDegToRad;

    const float sinHeading = std::sin(heading);
    const float cosHeading = std::cos(heading);
    const float sinPitch   = std::sin(pitch);
    const float cosPitch   = std::cos(pitch);
    const float sinRoll    = std::sin(roll);
    const float cosRoll    = std::cos(roll);

    const compass::Axis3 right{cosHeading, -sinHeading, 0.0f};
    const compass::Axis3 top{sinHeading, cosHeading, 0.0f};
    const compass::Axis3 out{0.0f, 0.0f, 1.0f};

    const compass::Axis3 tiltedTop   = add(scale(top, cosPitch), scale(out, sinPitch));
    const compass::Axis3 pitchedOut  = add(scale(out, cosPitch), scale(top, -sinPitch));
    const compass::Axis3 tiltedRight = add(scale(right, cosRoll), scale(pitchedOut, sinRoll));
    const compass::Axis3 tiltedOut   = add(scale(pitchedOut, cosRoll), scale(right, -sinRoll));

    return {dot(world, tiltedRight), dot(world, tiltedTop), dot(world, tiltedOut)};
}

void testMountTransforms()
{
    const compass::Axis3 rawX{1.0f, 0.0f, 0.0f};
    const compass::Axis3 rawY{0.0f, 1.0f, 0.0f};
    const compass::Axis3 rawZ{0.0f, 0.0f, 1.0f};

    requireAxisNear(compass::mapBmi270ToScreen(rawX), {0.0f, -1.0f, 0.0f}, "BMI raw X mount rotation");
    requireAxisNear(compass::mapBmi270ToScreen(rawY), {1.0f, 0.0f, 0.0f}, "BMI raw Y mount rotation");
    requireAxisNear(compass::mapBmi270ToScreen(rawZ), {0.0f, 0.0f, 1.0f}, "BMI raw Z mount rotation");

    requireAxisNear(compass::mapBmm150ToScreen(rawX), {0.0f, 1.0f, 0.0f}, "BMM raw X mount rotation");
    requireAxisNear(compass::mapBmm150ToScreen(rawY), {1.0f, 0.0f, 0.0f}, "BMM raw Y mount rotation");
    requireAxisNear(compass::mapBmm150ToScreen(rawZ), {0.0f, 0.0f, -1.0f}, "BMM raw Z mount rotation");

    requireAxisNear(compass::gaussToMicrotesla({-0.105625f, 0.7725f, 0.276875f}), {-10.5625f, 77.25f, 27.6875f},
                    "BMM IIO gauss to microtesla", 0.0001f);
}

void testObservedBmm150CardinalDeltas()
{
    const compass::Axis3 north{-61.9f, 1210.3f, 387.2f};
    const compass::Axis3 east{-513.9f, 857.3f, 377.0f};
    const compass::Axis3 south{-695.3f, 1258.9f, 322.9f};
    const compass::Axis3 west{-391.8f, 1662.1f, 338.2f};

    const auto screenNorthSouth = compass::mapBmm150ToScreen(subtract(north, south));
    require(screenNorthSouth.y > 0.0f, "north-minus-south points toward positive screen Y");
    require(std::abs(screenNorthSouth.y) > 5.0f * std::abs(screenNorthSouth.x),
            "north-minus-south is dominated by screen Y");
    require(std::abs(screenNorthSouth.y) > 5.0f * std::abs(screenNorthSouth.z),
            "north-minus-south rejects the BMM Z axis");

    const auto screenWestEast = compass::mapBmm150ToScreen(subtract(west, east));
    require(screenWestEast.x > 0.0f, "west-minus-east points toward positive screen X");
    require(std::abs(screenWestEast.x) > 5.0f * std::abs(screenWestEast.y), "west-minus-east is dominated by screen X");
    require(std::abs(screenWestEast.x) > 5.0f * std::abs(screenWestEast.z), "west-minus-east rejects the BMM Z axis");
}

void testVectorValidation()
{
    require(compass::isUsableVector({1.0f, -2.0f, 3.0f}), "finite non-zero sensor vector is usable");
    require(!compass::isUsableVector({0.0f, 0.0f, 0.0f}), "zero sensor vector is rejected");
    require(!compass::isUsableVector({std::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f}),
            "non-finite sensor vector is rejected");

    auto pose = compass::calculateCompassPose({3.0f, 0.0f, 9.34f}, {});
    require(!pose.headingValid, "zero magnetic field does not produce a heading");
    require(pose.bubbleX > 0.0f, "zero magnetic field does not disable the level bubble");

    pose = compass::calculateCompassPose({0.0f, 3.0f, 9.34f}, {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f});
    require(!pose.headingValid, "non-finite magnetic field does not produce a heading");
    require(pose.bubbleY < 0.0f, "non-finite magnetic field does not disable the level bubble");
}

void testObservedTiltDirections()
{
    const compass::Axis3 northMag{0.0f, 42.0f, -18.0f};

    auto pose = compass::calculateCompassPose(compass::mapBmi270ToScreen({-3.0f, 0.0f, 9.34f}), northMag);
    require(pose.pitchDeg > 0.0f, "tilting the screen toward the user produces positive pitch");
    require(pose.bubbleY < 0.0f, "tilting the screen toward the user moves the bubble upward");

    pose = compass::calculateCompassPose(compass::mapBmi270ToScreen({3.0f, 0.0f, 9.34f}), northMag);
    require(pose.pitchDeg < 0.0f, "tilting the screen away produces negative pitch");
    require(pose.bubbleY > 0.0f, "tilting the screen away moves the bubble downward");

    pose = compass::calculateCompassPose(compass::mapBmi270ToScreen({0.0f, 3.0f, 9.34f}), northMag);
    require(pose.rollDeg > 0.0f, "left tilt reported as raw +Y produces positive screen roll");
    require(pose.bubbleX > 0.0f, "left tilt reported as raw +Y moves the bubble right");

    pose = compass::calculateCompassPose(compass::mapBmi270ToScreen({0.0f, -3.0f, 9.34f}), northMag);
    require(pose.rollDeg < 0.0f, "right tilt reported as raw -Y produces negative screen roll");
    require(pose.bubbleX < 0.0f, "right tilt reported as raw -Y moves the bubble left");
}

void testFlatOnBothFaces()
{
    const compass::Axis3 northMag{0.0f, 42.0f, -18.0f};

    auto pose = compass::calculateCompassPose({0.0f, 0.0f, kGravity}, northMag);
    require(pose.headingValid, "screen-up heading is valid");
    requireNear(pose.pitchDeg, 0.0f, 0.001f, "screen-up pitch is level");
    requireNear(pose.rollDeg, 0.0f, 0.001f, "screen-up roll is level");
    requireNear(pose.bubbleX, 0.0f, 0.001f, "screen-up bubble X is centered");
    requireNear(pose.bubbleY, 0.0f, 0.001f, "screen-up bubble Y is centered");

    pose = compass::calculateCompassPose({0.0f, 0.0f, -kGravity}, northMag);
    require(pose.headingValid, "screen-down heading is valid");
    requireNear(pose.pitchDeg, 0.0f, 0.001f, "screen-down pitch is level");
    requireNear(pose.rollDeg, 0.0f, 0.001f, "screen-down roll is level");
    requireNear(pose.bubbleX, 0.0f, 0.001f, "screen-down bubble X is centered");
    requireNear(pose.bubbleY, 0.0f, 0.001f, "screen-down bubble Y is centered");

    const std::vector<std::pair<compass::Axis3, float>> faceDownHeadings{
        {northMag, 0.0f},
        {{-42.0f, 0.0f, -18.0f}, 90.0f},
        {{0.0f, -42.0f, -18.0f}, 180.0f},
        {{42.0f, 0.0f, -18.0f}, 270.0f},
    };
    for (const auto& [mag, expected] : faceDownHeadings) {
        pose = compass::calculateCompassPose({0.0f, 0.0f, -kGravity}, mag);
        require(pose.headingValid, "screen-down cardinal heading is valid");
        requireAngleNear(pose.headingDeg, expected, 0.001f, "screen-down cardinal heading");
    }
}

void testCardinalHeadings()
{
    const compass::Axis3 flat{0.0f, 0.0f, kGravity};
    struct Case {
        compass::Axis3 mag;
        float expected;
        const char* name;
    };
    const std::vector<Case> cases{
        {{0.0f, 42.0f, -18.0f}, 0.0f, "north"},
        {{-42.0f, 0.0f, -18.0f}, 90.0f, "east"},
        {{0.0f, -42.0f, -18.0f}, 180.0f, "south"},
        {{42.0f, 0.0f, -18.0f}, 270.0f, "west"},
    };

    for (const auto& test : cases) {
        const auto pose = compass::calculateCompassPose(flat, test.mag);
        require(pose.headingValid, std::string(test.name) + " heading is valid");
        requireAngleNear(pose.headingDeg, test.expected, 0.001f, std::string(test.name) + " heading");
    }
}

void testTiltCompensatedHeading()
{
    constexpr float expectedHeading = 37.0f;
    const compass::Axis3 worldUp{0.0f, 0.0f, kGravity};
    const compass::Axis3 worldMag{0.0f, 42.0f, -18.0f};
    const std::vector<std::pair<float, float>> tilts{
        {0.0f, 0.0f}, {20.0f, 0.0f}, {-20.0f, 0.0f}, {0.0f, 25.0f}, {0.0f, -25.0f}, {18.0f, -23.0f},
    };

    for (const auto& [pitch, roll] : tilts) {
        const auto accel = worldToScreen(worldUp, expectedHeading, pitch, roll);
        const auto mag   = worldToScreen(worldMag, expectedHeading, pitch, roll);
        const auto pose  = compass::calculateCompassPose(accel, mag);
        require(pose.headingValid, "tilted heading remains valid");
        requireAngleNear(pose.headingDeg, expectedHeading, 0.01f, "tilt-compensated heading remains stable");
    }
}

void testBubbleClampAndVerticalHeading()
{
    const compass::Axis3 northMag{0.0f, 42.0f, -18.0f};
    const float angle = 30.0f * kDegToRad;

    auto pose = compass::calculateCompassPose({std::sin(angle) * kGravity, 0.0f, std::cos(angle) * kGravity}, northMag);
    requireNear(pose.bubbleX, 1.0f, 0.001f, "positive roll bubble clamps at one");

    pose = compass::calculateCompassPose({0.0f, std::sin(angle) * kGravity, std::cos(angle) * kGravity}, northMag);
    requireNear(pose.bubbleY, -1.0f, 0.001f, "positive pitch bubble clamps at negative one");

    pose = compass::calculateCompassPose({0.0f, kGravity, 0.0f}, northMag);
    require(!pose.headingValid, "heading is undefined when the screen top is vertical");
    require(std::isfinite(pose.pitchDeg) && std::isfinite(pose.rollDeg), "vertical pose angles stay finite");

    const auto nearVerticalAccel = worldToScreen({0.0f, 0.0f, kGravity}, 37.0f, 85.0f, 0.0f);
    const auto nearVerticalMag   = worldToScreen(northMag, 37.0f, 85.0f, 0.0f);
    pose                         = compass::calculateCompassPose(nearVerticalAccel, nearVerticalMag);
    require(pose.headingValid, "heading remains valid at a steep but usable tilt");
    requireAngleNear(pose.headingDeg, 37.0f, 0.01f, "steep tilt heading remains stable");

    const auto verticalAccel = worldToScreen({0.0f, 0.0f, kGravity}, 37.0f, 89.0f, 0.0f);
    const auto verticalMag   = worldToScreen(northMag, 37.0f, 89.0f, 0.0f);
    pose                     = compass::calculateCompassPose(verticalAccel, verticalMag);
    require(!pose.headingValid, "heading is rejected when the top edge is effectively vertical");
}

void testBubbleMotion()
{
    constexpr float tenDegreesPerSecond = 10.0f * kDegToRad;

    requireNear(compass::calculateBubbleMotion({0.0f, 0.0f, kGravity}, {}), 0.0f, 0.001f,
                "stationary gravity keeps the bubble at minimum size");
    requireNear(compass::calculateBubbleMotion({0.0f, 0.0f, kGravity * 1.5f}, {}), 0.5f, 0.001f,
                "one and a half g grows the bubble halfway");
    requireNear(compass::calculateBubbleMotion({}, {0.0f, tenDegreesPerSecond * 1.5f, 0.0f}), 0.5f, 0.001f,
                "fifteen degrees per second grows the bubble halfway");
    requireNear(compass::calculateBubbleMotion({}, {tenDegreesPerSecond * 2.0f, 0.0f, 0.0f}), 1.0f, 0.001f,
                "twenty degrees per second grows the bubble to maximum size");
    requireNear(compass::calculateBubbleMotion({kGravity * 3.0f, 0.0f, 0.0f}, {}), 1.0f, 0.001f,
                "large acceleration clamps the bubble at maximum size");
    requireNear(compass::calculateBubbleMotion({std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}, {}), 0.0f, 0.0f,
                "invalid motion data falls back to minimum size");
}

}  // namespace

int main()
{
    testMountTransforms();
    testObservedBmm150CardinalDeltas();
    testVectorValidation();
    testObservedTiltDirections();
    testFlatOnBothFaces();
    testCardinalHeadings();
    testTiltCompensatedHeading();
    testBubbleClampAndVerticalHeading();
    testBubbleMotion();

    if (failures != 0) {
        std::cerr << failures << " compass pose test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All compass pose tests passed\n";
    return EXIT_SUCCESS;
}

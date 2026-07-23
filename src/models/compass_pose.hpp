#pragma once

namespace compass {

struct Axis3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct CompassPose {
    bool headingValid = false;
    float headingDeg  = 0.0f;
    float pitchDeg    = 0.0f;
    float rollDeg     = 0.0f;
    float bubbleX     = 0.0f;
    float bubbleY     = 0.0f;
};

// Screen coordinates are +X right, +Y toward the top edge, and +Z out of the display.
Axis3 mapBmi270ToScreen(const Axis3& sensor);
Axis3 mapBmm150ToScreen(const Axis3& sensor);
Axis3 gaussToMicrotesla(const Axis3& gauss);

bool isUsableVector(const Axis3& value);
CompassPose calculateCompassPose(const Axis3& screenAccel, const Axis3& screenMag);
float calculateBubbleMotion(const Axis3& screenAccel, const Axis3& screenGyro);

}  // namespace compass

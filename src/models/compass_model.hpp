#pragma once

#include "models/compass_pose.hpp"
#include <tools/observable/single_observable.hpp>
#include <cstdint>
#include <memory>
#include <string>

namespace compass {

enum class CompassDataSource {
    Mock,
    Iio,
};

struct CompassSample {
    CompassDataSource source = CompassDataSource::Mock;
    bool available           = false;
    uint32_t sequence        = 0;
    std::string status;
    float headingDeg = 0.0f;
    float pitchDeg   = 0.0f;
    float rollDeg    = 0.0f;
    float bubbleX    = 0.0f;
    float bubbleY    = 0.0f;
    // UI-facing vectors use screen coordinates. mag is uT; rawMag stays in the BMM150 frame in gauss.
    Axis3 accel;
    Axis3 gyro;
    Axis3 mag;
    Axis3 rawMag;
};

class CompassModel {
public:
    CompassModel();
    ~CompassModel();

    CompassModel(const CompassModel&)            = delete;
    CompassModel& operator=(const CompassModel&) = delete;

    smooth_ui_toolkit::SingleObservable<CompassSample>& sample()
    {
        return _sample;
    }

    void tick(uint32_t nowMs);
    bool reloadCalibration();

private:
    struct Impl;

    smooth_ui_toolkit::SingleObservable<CompassSample> _sample{CompassSample{}};
    std::unique_ptr<Impl> _impl;
};

}  // namespace compass

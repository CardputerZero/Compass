#pragma once

#include "models/compass_model.hpp"
#include <tools/observable/single_observable.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace compass {

enum class CalibrationState {
    Idle,
    Running,
    Done,
};

struct CompassCalibration {
    bool valid = false;
    // BMM150 calibration is stored in its native IIO unit: gauss.
    Axis3 mag_offset;
    // Symmetric soft-iron correction matrix. The diagonal remains in mag_scale
    // so version 1 calibration files and callers stay source-compatible.
    Axis3 mag_scale{1.0f, 1.0f, 1.0f};
    float mag_cross_xy = 0.0f;
    float mag_cross_xz = 0.0f;
    float mag_cross_yz = 0.0f;
};

Axis3 applyMagCalibration(const Axis3& mag, const CompassCalibration& calibration);

class CalibrationModel {
public:
    CalibrationModel();

    smooth_ui_toolkit::SingleObservable<CalibrationState>& state()
    {
        return _state;
    }

    smooth_ui_toolkit::SingleObservable<std::string>& status()
    {
        return _status;
    }

    smooth_ui_toolkit::SingleObservable<float>& progress()
    {
        return _progress;
    }

    const CompassCalibration& calibration() const
    {
        return _calibration;
    }

    uint32_t sampleCount() const
    {
        return _sample_count;
    }

    void start();
    bool finish();
    void stop();
    void tick(uint32_t nowMs);
    void updateSample(const CompassSample& sample);

    static std::filesystem::path configPath();
    static bool load(CompassCalibration& calibration);
    static bool loadFrom(const std::filesystem::path& path, CompassCalibration& calibration);
    static bool save(const CompassCalibration& calibration);
    static bool saveTo(const std::filesystem::path& path, const CompassCalibration& calibration);

private:
    smooth_ui_toolkit::SingleObservable<CalibrationState> _state{CalibrationState::Idle};
    smooth_ui_toolkit::SingleObservable<std::string> _status{"Ready"};
    smooth_ui_toolkit::SingleObservable<float> _progress{0.0f};
    CompassCalibration _calibration;
    std::vector<Axis3> _mag_samples;
    uint32_t _sample_count         = 0;
    uint32_t _start_ms             = 0;
    uint32_t _last_sample_sequence = 0;
    std::size_t _next_sample_index = 0;

    void resetCapture();
    void captureMag(const Axis3& mag);
    bool buildCalibration(CompassCalibration& calibration) const;
    void updateProgress();
};

}  // namespace compass

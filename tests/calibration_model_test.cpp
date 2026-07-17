#include "models/calibration_model.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

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

void feedMagneticSpan(compass::CalibrationModel& model, float spanGauss)
{
    compass::CompassSample sample;
    sample.available = true;

    for (int i = 0; i < 20; ++i) {
        const float value = (i & 1) == 0 ? -spanGauss * 0.5f : spanGauss * 0.5f;
        sample.rawMag     = {value, value, value};
        model.updateSample(sample);
    }
}

void testCalibrationUnits()
{
    compass::CompassCalibration calibration;
    calibration.valid      = true;
    calibration.mag_offset = {0.1f, -0.2f, 0.3f};
    calibration.mag_scale  = {2.0f, 0.5f, 1.5f};

    const auto result = compass::applyMagCalibration({0.4f, 0.2f, -0.1f}, calibration);
    requireNear(result.x, 0.6f, 0.0001f, "calibration applies gauss X offset and scale");
    requireNear(result.y, 0.2f, 0.0001f, "calibration applies gauss Y offset and scale");
    requireNear(result.z, -0.6f, 0.0001f, "calibration applies gauss Z offset and scale");

    const auto screenMicrotesla = compass::gaussToMicrotesla(compass::mapBmm150ToScreen(result));
    requireNear(screenMicrotesla.x, 20.0f, 0.0001f, "calibrated BMM Y maps to screen X in microtesla");
    requireNear(screenMicrotesla.y, 60.0f, 0.0001f, "calibrated BMM X maps to screen Y in microtesla");
    requireNear(screenMicrotesla.z, 60.0f, 0.0001f, "calibrated BMM Z is inverted and converted once");
}

void testLegacyConfigKeepsGaussOffsets()
{
    const auto path = std::filesystem::temp_directory_path() / "compass_calibration_v1_test.conf";
    {
        std::ofstream file(path, std::ios::trunc);
        file << "# Legacy device calibration\n";
        file << "version=1\n";
        file << "mag_offset_x=-0.143437\n";
        file << "mag_offset_y=0.5575\n";
        file << "mag_offset_z=-0.0090625\n";
        file << "mag_scale_x=1.02865\n";
        file << "mag_scale_y=1.14051\n";
        file << "mag_scale_z=0.868773\n";
    }

    compass::CompassCalibration calibration;
    require(compass::CalibrationModel::loadFrom(path, calibration), "legacy v1 calibration loads");
    requireNear(calibration.mag_offset.x, -0.143437f, 0.000001f, "legacy X offset remains in gauss");
    requireNear(calibration.mag_offset.y, 0.5575f, 0.000001f, "legacy Y offset remains in gauss");
    requireNear(calibration.mag_offset.z, -0.0090625f, 0.000001f, "legacy Z offset remains in gauss");
    requireNear(calibration.mag_scale.x, 1.02865f, 0.000001f, "legacy dimensionless X scale is unchanged");

    std::error_code error;
    std::filesystem::remove(path, error);
}

void testProgressUsesGauss()
{
    compass::CalibrationModel model;

    model.start();
    feedMagneticSpan(model, 0.4f);
    require(model.sampleCount() == 20, "calibration captures the minimum sample count");
    requireNear(model.progress().get(), 0.5f, 0.0001f, "0.4 gauss coverage reports half progress");

    model.start();
    feedMagneticSpan(model, 0.8f);
    requireNear(model.progress().get(), 1.0f, 0.0001f, "0.8 gauss coverage reports full progress");
}

void testInvalidSamplesAreIgnored()
{
    compass::CalibrationModel model;
    compass::CompassSample sample;
    sample.available = true;

    model.start();
    model.updateSample(sample);
    require(model.sampleCount() == 0, "zero magnetic samples are ignored");
    requireNear(model.progress().get(), 0.0f, 0.0f, "invalid samples do not advance progress");
}

}  // namespace

int main()
{
    testCalibrationUnits();
    testLegacyConfigKeepsGaussOffsets();
    testProgressUsesGauss();
    testInvalidSamplesAreIgnored();

    if (failures != 0) {
        std::cerr << failures << " calibration model test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All calibration model tests passed\n";
    return EXIT_SUCCESS;
}

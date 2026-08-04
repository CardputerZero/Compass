#include "models/calibration_model.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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

    for (int i = 0; i < 120; ++i) {
        const int octant = i % 8;
        const float half = spanGauss * 0.5f;
        sample.rawMag    = {
            (octant & 1) != 0 ? half : -half,
            (octant & 2) != 0 ? half : -half,
            (octant & 4) != 0 ? half : -half,
        };
        model.updateSample(sample);
    }
}

using Matrix3 = std::array<std::array<double, 3>, 3>;

double determinant(const Matrix3& matrix)
{
    return matrix[0][0] * (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1]) -
           matrix[0][1] * (matrix[1][0] * matrix[2][2] - matrix[1][2] * matrix[2][0]) +
           matrix[0][2] * (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]);
}

Matrix3 inverse(const Matrix3& matrix)
{
    const double det = determinant(matrix);
    return {{{(matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1]) / det,
              (matrix[0][2] * matrix[2][1] - matrix[0][1] * matrix[2][2]) / det,
              (matrix[0][1] * matrix[1][2] - matrix[0][2] * matrix[1][1]) / det},
             {(matrix[1][2] * matrix[2][0] - matrix[1][0] * matrix[2][2]) / det,
              (matrix[0][0] * matrix[2][2] - matrix[0][2] * matrix[2][0]) / det,
              (matrix[0][2] * matrix[1][0] - matrix[0][0] * matrix[1][2]) / det},
             {(matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]) / det,
              (matrix[0][1] * matrix[2][0] - matrix[0][0] * matrix[2][1]) / det,
              (matrix[0][0] * matrix[1][1] - matrix[0][1] * matrix[1][0]) / det}}};
}

compass::Axis3 multiply(const Matrix3& matrix, const compass::Axis3& value)
{
    return {
        static_cast<float>(matrix[0][0] * value.x + matrix[0][1] * value.y + matrix[0][2] * value.z),
        static_cast<float>(matrix[1][0] * value.x + matrix[1][1] * value.y + matrix[1][2] * value.z),
        static_cast<float>(matrix[2][0] * value.x + matrix[2][1] * value.y + matrix[2][2] * value.z),
    };
}

compass::Axis3 add(const compass::Axis3& lhs, const compass::Axis3& rhs)
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

float norm(const compass::Axis3& value)
{
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

void requireDirectionNear(const compass::Axis3& actual, const compass::Axis3& expected, float tolerance,
                          const std::string& message)
{
    const float actual_norm   = norm(actual);
    const float expected_norm = norm(expected);
    const float cosine =
        (actual.x * expected.x + actual.y * expected.y + actual.z * expected.z) / (actual_norm * expected_norm);
    require(std::isfinite(cosine) && cosine >= std::cos(tolerance),
            message + " (cosine=" + std::to_string(cosine) + ")");
}

std::vector<compass::Axis3> makeSphereSamples(const compass::Axis3& offset, const Matrix3& correction)
{
    constexpr size_t kSampleCount = 360;
    constexpr double kGoldenAngle = 2.39996322972865332;
    constexpr float kFieldGauss   = 0.48f;
    const Matrix3 distortion      = inverse(correction);

    std::vector<compass::Axis3> samples;
    samples.reserve(kSampleCount);
    for (size_t index = 0; index < kSampleCount; ++index) {
        const double z      = 1.0 - 2.0 * (static_cast<double>(index) + 0.5) / static_cast<double>(kSampleCount);
        const double radial = std::sqrt(std::max(0.0, 1.0 - z * z));
        const double angle  = static_cast<double>(index) * kGoldenAngle;
        const compass::Axis3 field{static_cast<float>(kFieldGauss * radial * std::cos(angle)),
                                   static_cast<float>(kFieldGauss * radial * std::sin(angle)),
                                   static_cast<float>(kFieldGauss * z)};
        samples.push_back(add(offset, multiply(distortion, field)));
    }
    return samples;
}

void writeConfig(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::trunc);
    file << text;
}

void testCalibrationUnits()
{
    compass::CompassCalibration calibration;
    calibration.valid        = true;
    calibration.mag_offset   = {0.1f, -0.2f, 0.3f};
    calibration.mag_scale    = {2.0f, 0.5f, 1.5f};
    calibration.mag_cross_xy = 0.1f;
    calibration.mag_cross_xz = -0.2f;
    calibration.mag_cross_yz = 0.05f;

    const auto result = compass::applyMagCalibration({0.4f, 0.2f, -0.1f}, calibration);
    requireNear(result.x, 0.72f, 0.0001f, "calibration applies gauss X matrix row");
    requireNear(result.y, 0.21f, 0.0001f, "calibration applies gauss Y matrix row");
    requireNear(result.z, -0.64f, 0.0001f, "calibration applies gauss Z matrix row");

    const auto screenMicrotesla = compass::gaussToMicrotesla(compass::mapBmm150ToScreen(result));
    requireNear(screenMicrotesla.x, 21.0f, 0.0001f, "calibrated BMM Y maps to screen X in microtesla");
    requireNear(screenMicrotesla.y, 72.0f, 0.0001f, "calibrated BMM X maps to screen Y in microtesla");
    requireNear(screenMicrotesla.z, 64.0f, 0.0001f, "calibrated BMM Z is inverted and converted once");
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
    requireNear(calibration.mag_cross_xy, 0.0f, 0.0f, "legacy calibration has no XY correction");
    requireNear(calibration.mag_cross_xz, 0.0f, 0.0f, "legacy calibration has no XZ correction");
    requireNear(calibration.mag_cross_yz, 0.0f, 0.0f, "legacy calibration has no YZ correction");

    writeConfig(path,
                "mag_offset_x=-0.143437\nmag_offset_y=0.5575\nmag_offset_z=-0.0090625\n"
                "mag_scale_x=1.02865\nmag_scale_y=1.14051\nmag_scale_z=0.868773\n");
    require(compass::CalibrationModel::loadFrom(path, calibration), "versionless legacy calibration loads as v1");
    requireNear(calibration.mag_cross_xy, 0.0f, 0.0f, "versionless legacy calibration stays diagonal");

    std::error_code error;
    std::filesystem::remove(path, error);
}

void testProgressUsesGauss()
{
    compass::CalibrationModel model;

    model.start();
    feedMagneticSpan(model, 0.4f);
    require(model.sampleCount() == 120, "calibration captures the minimum sample count");
    requireNear(model.progress().get(), 0.5f, 0.0001f, "0.4 gauss coverage reports half progress");

    model.start();
    feedMagneticSpan(model, 0.8f);
    requireNear(model.progress().get(), 1.0f, 0.0001f, "0.8 gauss coverage reports full progress");
}

void testProgressRequiresDirectionalCoverage()
{
    compass::CalibrationModel model;
    compass::CompassSample sample;
    sample.available = true;

    model.start();
    for (int index = 0; index < 120; ++index) {
        const float value = (index & 1) == 0 ? -0.4f : 0.4f;
        sample.rawMag     = {value, value, value};
        model.updateSample(sample);
    }

    requireNear(model.progress().get(), 2.0f / 8.0f, 0.0001f,
                "two-octant motion cannot report complete calibration coverage");
}

void testDuplicateHardwareSamplesAreIgnored()
{
    compass::CalibrationModel model;
    compass::CompassSample sample;
    sample.available = true;
    sample.sequence  = 42;
    sample.rawMag    = {0.2f, 0.3f, -0.1f};

    model.start();
    model.updateSample(sample);
    model.updateSample(sample);
    require(model.sampleCount() == 1, "the same hardware sample is captured only once");

    ++sample.sequence;
    model.updateSample(sample);
    require(model.sampleCount() == 2, "a new hardware sample sequence is captured");
}

void testV2RoundTripAndValidation()
{
    const auto path = std::filesystem::temp_directory_path() / "compass_calibration_v2_test.conf";
    compass::CompassCalibration calibration;
    calibration.valid        = true;
    calibration.mag_offset   = {-0.19f, 0.67f, -0.16f};
    calibration.mag_scale    = {1.08f, 0.96f, 1.01f};
    calibration.mag_cross_xy = 0.08f;
    calibration.mag_cross_xz = -0.03f;
    calibration.mag_cross_yz = 0.04f;

    require(compass::CalibrationModel::saveTo(path, calibration), "valid v2 calibration saves");
    compass::CompassCalibration loaded;
    require(compass::CalibrationModel::loadFrom(path, loaded), "valid v2 calibration loads");
    requireNear(loaded.mag_cross_xy, calibration.mag_cross_xy, 0.000001f, "v2 XY coefficient round trips");
    requireNear(loaded.mag_cross_xz, calibration.mag_cross_xz, 0.000001f, "v2 XZ coefficient round trips");
    requireNear(loaded.mag_cross_yz, calibration.mag_cross_yz, 0.000001f, "v2 YZ coefficient round trips");

    writeConfig(path,
                "version=2\nmag_offset_x=nan\nmag_offset_y=0\nmag_offset_z=0\nmag_scale_x=1\nmag_scale_y=1\n"
                "mag_scale_z=1\nmag_cross_xy=0\nmag_cross_xz=0\nmag_cross_yz=0\n");
    require(!compass::CalibrationModel::loadFrom(path, loaded), "non-finite v2 calibration is rejected");

    writeConfig(path,
                "version=2\nmag_offset_x=0\nmag_offset_y=0\nmag_offset_z=0\nmag_scale_x=1\nmag_scale_y=1\n"
                "mag_scale_z=1\nmag_cross_xy=1\nmag_cross_xz=0\nmag_cross_yz=0\n");
    require(!compass::CalibrationModel::loadFrom(path, loaded), "singular v2 calibration matrix is rejected");

    writeConfig(path,
                "version=2\nmag_offset_x=0\nmag_offset_y=0\nmag_offset_z=0\nmag_scale_x=1\nmag_scale_y=1\n"
                "mag_scale_z=1\nmag_cross_xy=0\nmag_cross_xz=0\n");
    require(!compass::CalibrationModel::loadFrom(path, loaded), "incomplete v2 calibration matrix is rejected");

    std::error_code error;
    std::filesystem::remove(path, error);
}

void testFullEllipsoidFit()
{
    const auto path = std::filesystem::temp_directory_path() / "compass_calibration_fit_test.conf";
    std::error_code filesystem_error;
    std::filesystem::remove(path, filesystem_error);
    setenv("COMPASS_CALIBRATION_PATH", path.c_str(), 1);

    const compass::Axis3 expected_offset{-0.21f, 0.68f, -0.14f};
    const Matrix3 expected_correction{{{1.15, 0.12, -0.05}, {0.12, 0.91, 0.06}, {-0.05, 0.06, 0.97}}};
    const auto samples = makeSphereSamples(expected_offset, expected_correction);

    compass::CalibrationModel model;
    model.start();
    for (const auto& raw : samples) {
        compass::CompassSample sample;
        sample.available = true;
        sample.rawMag    = raw;
        model.updateSample(sample);
    }
    require(model.finish(), "well-covered ellipsoid calibration succeeds");
    require(model.state().get() == compass::CalibrationState::Done, "successful fit reaches done state");

    const auto& fitted = model.calibration();
    requireNear(fitted.mag_offset.x, expected_offset.x, 0.002f, "ellipsoid fit recovers X offset");
    requireNear(fitted.mag_offset.y, expected_offset.y, 0.002f, "ellipsoid fit recovers Y offset");
    requireNear(fitted.mag_offset.z, expected_offset.z, 0.002f, "ellipsoid fit recovers Z offset");
    require(std::abs(fitted.mag_cross_xy) > 0.03f, "ellipsoid fit retains XY cross-axis correction");
    require(std::abs(fitted.mag_cross_xz) > 0.01f, "ellipsoid fit retains XZ cross-axis correction");

    double norm_sum = 0.0;
    std::vector<float> corrected_norms;
    corrected_norms.reserve(samples.size());
    for (size_t index = 0; index < samples.size(); ++index) {
        const auto corrected = compass::applyMagCalibration(samples[index], fitted);
        corrected_norms.push_back(norm(corrected));
        norm_sum += corrected_norms.back();

        const double z      = 1.0 - 2.0 * (static_cast<double>(index) + 0.5) / static_cast<double>(samples.size());
        const double radial = std::sqrt(std::max(0.0, 1.0 - z * z));
        const double angle  = static_cast<double>(index) * 2.39996322972865332;
        const compass::Axis3 expected{static_cast<float>(radial * std::cos(angle)),
                                      static_cast<float>(radial * std::sin(angle)), static_cast<float>(z)};
        requireDirectionNear(corrected, expected, 0.01f, "full matrix restores magnetic direction");
    }
    const double mean = norm_sum / static_cast<double>(corrected_norms.size());
    double variance   = 0.0;
    for (float value : corrected_norms) {
        const double delta = value / mean - 1.0;
        variance += delta * delta;
    }
    require(std::sqrt(variance / corrected_norms.size()) < 0.002, "full matrix makes corrected field spherical");

    std::ifstream saved(path);
    std::ostringstream contents;
    contents << saved.rdbuf();
    require(contents.str().find("version=2") != std::string::npos, "ellipsoid fit persists calibration v2");
    require(contents.str().find("mag_cross_xy=") != std::string::npos, "ellipsoid fit persists matrix cross terms");

    unsetenv("COMPASS_CALIBRATION_PATH");
    std::filesystem::remove(path, filesystem_error);
}

void testDegenerateFitIsRejected()
{
    const auto path = std::filesystem::temp_directory_path() / "compass_calibration_degenerate_test.conf";
    std::error_code error;
    std::filesystem::remove(path, error);
    setenv("COMPASS_CALIBRATION_PATH", path.c_str(), 1);

    compass::CalibrationModel model;
    model.start();
    for (int index = 0; index < 180; ++index) {
        const float angle = static_cast<float>(index) * 0.1f;
        compass::CompassSample sample;
        sample.available = true;
        sample.rawMag    = {0.5f * std::cos(angle), 0.5f * std::sin(angle), 0.02f * std::sin(angle * 2.0f)};
        model.updateSample(sample);
    }
    require(!model.finish(), "nearly planar calibration motion is rejected");
    require(model.state().get() == compass::CalibrationState::Running,
            "rejected fit remains available for more motion");
    require(!std::filesystem::exists(path), "rejected fit does not replace saved calibration");

    unsetenv("COMPASS_CALIBRATION_PATH");
}

void testIncompleteOctantCoverageIsRejected()
{
    const auto path = std::filesystem::temp_directory_path() / "compass_calibration_incomplete_octants_test.conf";
    std::error_code error;
    std::filesystem::remove(path, error);
    setenv("COMPASS_CALIBRATION_PATH", path.c_str(), 1);

    const compass::Axis3 expected_offset{-0.21f, 0.68f, -0.14f};
    const Matrix3 expected_correction{{{1.15, 0.12, -0.05}, {0.12, 0.91, 0.06}, {-0.05, 0.06, 0.97}}};
    const auto samples = makeSphereSamples(expected_offset, expected_correction);
    compass::CompassCalibration expected_calibration;
    expected_calibration.valid        = true;
    expected_calibration.mag_offset   = expected_offset;
    expected_calibration.mag_scale    = {static_cast<float>(expected_correction[0][0]),
                                         static_cast<float>(expected_correction[1][1]),
                                         static_cast<float>(expected_correction[2][2])};
    expected_calibration.mag_cross_xy = static_cast<float>(expected_correction[0][1]);
    expected_calibration.mag_cross_xz = static_cast<float>(expected_correction[0][2]);
    expected_calibration.mag_cross_yz = static_cast<float>(expected_correction[1][2]);

    compass::CalibrationModel model;
    model.start();
    for (const auto& raw : samples) {
        const auto corrected = compass::applyMagCalibration(raw, expected_calibration);
        const uint8_t octant = static_cast<uint8_t>((corrected.x >= 0.0f ? 1U : 0U) | (corrected.y >= 0.0f ? 2U : 0U) |
                                                    (corrected.z >= 0.0f ? 4U : 0U));
        if (octant >= 6) {
            continue;
        }

        compass::CompassSample sample;
        sample.available = true;
        sample.rawMag    = raw;
        model.updateSample(sample);
    }

    require(!model.finish(), "six-octant calibration is rejected instead of extrapolating missing directions");
    require(model.state().get() == compass::CalibrationState::Running,
            "incomplete directional coverage remains available for more motion");
    require(!std::filesystem::exists(path), "incomplete directional coverage does not replace saved calibration");

    unsetenv("COMPASS_CALIBRATION_PATH");
}

void testRollingSampleWindowAcceptsLaterCoverage()
{
    const auto path = std::filesystem::temp_directory_path() / "compass_calibration_rolling_test.conf";
    std::error_code error;
    std::filesystem::remove(path, error);
    setenv("COMPASS_CALIBRATION_PATH", path.c_str(), 1);

    compass::CalibrationModel model;
    model.start();
    compass::CompassSample sample;
    sample.available = true;
    for (uint32_t index = 0; index < 2048; ++index) {
        const float value = (index & 1U) == 0 ? -0.4f : 0.4f;
        sample.sequence   = index + 1;
        sample.rawMag     = {value, value, value};
        model.updateSample(sample);
    }

    const compass::Axis3 expected_offset{-0.21f, 0.68f, -0.14f};
    const Matrix3 expected_correction{{{1.15, 0.12, -0.05}, {0.12, 0.91, 0.06}, {-0.05, 0.06, 0.97}}};
    const auto samples = makeSphereSamples(expected_offset, expected_correction);
    for (uint32_t index = 0; index < 2048; ++index) {
        sample.sequence = 2049 + index;
        sample.rawMag   = samples[index % samples.size()];
        model.updateSample(sample);
    }

    require(model.sampleCount() == 4096, "rolling calibration keeps counting fresh hardware samples");
    require(model.finish(), "later complete motion replaces an initially degenerate sample window");

    unsetenv("COMPASS_CALIBRATION_PATH");
    std::filesystem::remove(path, error);
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
    testProgressRequiresDirectionalCoverage();
    testDuplicateHardwareSamplesAreIgnored();
    testInvalidSamplesAreIgnored();
    testV2RoundTripAndValidation();
    testFullEllipsoidFit();
    testDegenerateFitIsRejected();
    testIncompleteOctantCoverageIsRejected();
    testRollingSampleWindowAcceptsLaterCoverage();

    if (failures != 0) {
        std::cerr << failures << " calibration model test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All calibration model tests passed\n";
    return EXIT_SUCCESS;
}

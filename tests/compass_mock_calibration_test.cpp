#include "models/calibration_model.hpp"
#include "models/compass_model.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

int failures = 0;

void require(bool condition, const char* message)
{
    if (condition) {
        return;
    }

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

}  // namespace

int main()
{
    const auto path = std::filesystem::temp_directory_path() / "compass_mock_calibration_test.conf";
    std::error_code error;
    setenv("COMPASS_CALIBRATION_PATH", path.c_str(), 1);

    // Exercise several entry phases because the application clock is already
    // running when the user opens calibration. The mock must finish from any
    // phase, rather than relying on a particular launch timestamp.
    for (const uint32_t start_ms : {0U, 3000U, 7000U, 12000U, 18000U}) {
        std::filesystem::remove(path, error);

        compass::CompassModel compass_model;
        compass::CalibrationModel calibration;
        calibration.start();

        for (uint32_t now = start_ms; now <= start_ms + 21000U; now += 33U) {
            compass_model.tick(now);
            calibration.updateSample(compass_model.sample().get());
            calibration.tick(now);
            if (calibration.state().get() == compass::CalibrationState::Done) {
                break;
            }
        }

        require(calibration.sampleCount() >= 120, "SDL mock produced enough calibration samples");
        require(calibration.progress().get() >= 1.0f, "SDL mock reached full calibration progress");
        require(calibration.state().get() == compass::CalibrationState::Done,
                "SDL mock calibration reaches Done automatically");
    }

    unsetenv("COMPASS_CALIBRATION_PATH");
    std::filesystem::remove(path, error);

    if (failures != 0) {
        std::cerr << failures << " SDL mock calibration test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "SDL mock calibration test passed\n";
    return EXIT_SUCCESS;
}

#include "models/compass_model.hpp"
#include "view_models/compass_view_model.hpp"
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

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

bool writeFile(const std::filesystem::path& path, const std::string& value)
{
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    file << value;
    return file.good();
}

bool createFakeIioDevices(const std::filesystem::path& root)
{
    const auto imu = root / "iio:device0";
    const auto mag = root / "iio:device2";
    std::filesystem::create_directories(imu);
    std::filesystem::create_directories(mag);

    return writeFile(imu / "name", "bmi270\n") && writeFile(imu / "in_accel_scale", "0.001\n") &&
           writeFile(imu / "in_accel_x_raw", "0\n") && writeFile(imu / "in_accel_y_raw", "0\n") &&
           writeFile(imu / "in_accel_z_raw", "9810\n") && writeFile(imu / "in_anglvel_scale", "0.001\n") &&
           writeFile(imu / "in_anglvel_x_raw", "0\n") && writeFile(imu / "in_anglvel_y_raw", "0\n") &&
           writeFile(imu / "in_anglvel_z_raw", "0\n") && writeFile(mag / "name", "bmm150\n") &&
           writeFile(mag / "in_magn_scale", "0.001\n") && writeFile(mag / "in_magn_x_raw", "100\n") &&
           writeFile(mag / "in_magn_y_raw", "300\n") && writeFile(mag / "in_magn_z_raw", "-100\n");
}

void requireInvalidIioRootIsHandled(const std::filesystem::path& root)
{
    const auto invalid_root = root / "iio-not-a-directory";
    require(writeFile(invalid_root, "not a directory\n"), "invalid IIO root fixture is created");
    require(setenv("COMPASS_IIO_SYSFS_ROOT", invalid_root.c_str(), 1) == 0, "invalid IIO root environment is set");

    try {
        compass::CompassModel model;
        require(!model.sample().get().available, "invalid IIO root publishes an unavailable sample");
    } catch (const std::filesystem::filesystem_error& exception) {
        require(false, std::string("invalid IIO root must not throw: ") + exception.what());
    }
}

}  // namespace

int main()
{
    const auto root = std::filesystem::temp_directory_path() /
                      ("compass-model-unavailable-" + std::to_string(static_cast<long long>(getpid())));
    const auto iio_root = root / "iio";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(iio_root, error);
    require(!error, "temporary IIO root is created");

    require(setenv("COMPASS_I2C_SYSFS_ROOT", (root / "i2c").c_str(), 1) == 0, "I2C test root environment is set");
    require(setenv("COMPASS_CONFIG_DIR", (root / "config").c_str(), 1) == 0, "test configuration environment is set");
    requireInvalidIioRootIsHandled(root);
    require(setenv("COMPASS_IIO_SYSFS_ROOT", iio_root.c_str(), 1) == 0, "IIO test root environment is set");
    if (failures != 0) {
        std::filesystem::remove_all(root, error);
        return EXIT_FAILURE;
    }

    compass::CompassModel model;
    require(!model.sample().get().available, "missing IIO nodes publish an unavailable sample");
    require(model.sample().get().source == compass::CompassDataSource::Iio,
            "missing device sample still identifies the IIO source");
    require(model.sample().get().status.find("not found") != std::string::npos,
            "missing device sample includes the initialization reason");

    compass::CompassRouter router;
    compass::CompassViewModel view_model(router, model);
    view_model.onEnter();
    view_model.onKey('8');
    require(!view_model.infoExpanded().get(), "unavailable compass ignores the info shortcut");
    view_model.onKey('7');
    require(router.page() == compass::PageId::Compass, "unavailable compass cannot enter calibration");

    model.tick(1);
    require(createFakeIioDevices(iio_root), "fake IIO nodes are created");
    model.tick(5000);
    require(!model.sample().get().available, "backend does not retry before the interval");

    model.tick(5033);
    require(model.sample().get().available, "backend recovers after IIO nodes appear");
    require(model.sample().get().magnetometerAvailable, "recovered nine-axis sample has magnetic data");
    require(model.sample().get().source == compass::CompassDataSource::Iio,
            "recovered sample identifies the IIO source");
    require(model.sample().get().sequence != 0, "recovered hardware sample has a sequence number");
    view_model.onKey('8');
    require(view_model.infoExpanded().get(), "recovered compass restores normal shortcuts");

    std::filesystem::remove(iio_root / "iio:device2" / "in_magn_x_raw");
    view_model.tick(5066);
    require(model.sample().get().available && !model.sample().get().magnetometerAvailable,
            "magnetometer read failure preserves six-axis availability");
    require(model.sample().get().rawMag.x == 0 && model.sample().get().mag.y == 0 &&
                model.sample().get().headingDeg == 0,
            "magnetic failure clears stale magnetic data and heading");
    require(view_model.infoExpanded().get(), "magnetic loss preserves the original expanded state");
    view_model.onKey('7');
    for (int i = 0; i < 3; ++i) {
        view_model.onKey(' ');
    }
    require(router.page() == compass::PageId::Compass && view_model.infoExpanded().get() &&
                view_model.magic().get() == 1,
            "six-axis mode removes calibration and preserves other original interactions");
    view_model.onKey('8');
    require(!view_model.infoExpanded().get(), "six-axis mode retains the original collapse shortcut");
    view_model.onKey('8');
    require(view_model.infoExpanded().get(), "six-axis mode retains the original expand shortcut");
    require(writeFile(iio_root / "iio:device0" / "in_accel_y_raw", "2500\n"), "six-axis data changes");
    view_model.tick(5099);
    require(model.sample().get().accel.x == 2.5f, "six-axis values continue updating without magnetic data");

    require(writeFile(iio_root / "iio:device2" / "in_magn_x_raw", "100\n"), "failed IIO channel is restored");
    model.tick(10065);
    require(model.sample().get().available && !model.sample().get().magnetometerAvailable,
            "magnetic recovery observes the retry interval while six-axis data stays live");
    model.tick(10098);
    require(model.sample().get().available && model.sample().get().magnetometerAvailable,
            "runtime magnetic read failure recovers after the retry interval");

    std::filesystem::remove_all(iio_root / "iio:device2");
    std::filesystem::rename(iio_root / "iio:device0", iio_root / "iio:device7");
    {
        compass::CompassModel six_axis_model;
        const auto& first = six_axis_model.sample().get();
        require(first.available && !first.magnetometerAvailable && first.accel.x == 2.5f,
                "a device with only BMI270 starts with real six-axis data at a discovered IIO path");
        const auto sequence = first.sequence;
        require(writeFile(iio_root / "iio:device7" / "in_anglvel_z_raw", "123\n"), "gyro data changes");
        six_axis_model.tick(33);
        require(six_axis_model.sample().get().sequence > sequence &&
                    std::abs(six_axis_model.sample().get().gyro.z - 0.123f) < 0.00001f,
                "six-axis-only startup keeps publishing fresh gyro samples");

        std::filesystem::remove(iio_root / "iio:device7" / "in_accel_x_raw");
        six_axis_model.tick(66);
        require(!six_axis_model.sample().get().available &&
                    six_axis_model.sample().get().source == compass::CompassDataSource::Iio,
                "BMI270 failure remains unavailable and never falls back to mock data");
        require(writeFile(iio_root / "iio:device7" / "in_accel_x_raw", "0\n"), "accel channel restored");
        six_axis_model.tick(5065);
        require(!six_axis_model.sample().get().available, "six-axis failure observes retry delay");
        six_axis_model.tick(5098);
        require(six_axis_model.sample().get().available && !six_axis_model.sample().get().magnetometerAvailable,
                "BMI270 read failure recovers into six-axis mode");
    }

    std::filesystem::remove_all(root, error);
    unsetenv("COMPASS_IIO_SYSFS_ROOT");
    unsetenv("COMPASS_I2C_SYSFS_ROOT");
    unsetenv("COMPASS_CONFIG_DIR");
    if (failures != 0) {
        std::cerr << failures << " compass model unavailable test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All compass model unavailable tests passed\n";
    return EXIT_SUCCESS;
}

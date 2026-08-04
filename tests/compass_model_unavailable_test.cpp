#include "models/compass_model.hpp"
#include "view_models/compass_view_model.hpp"
#include <cstdlib>
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
    require(model.sample().get().source == compass::CompassDataSource::Iio,
            "recovered sample identifies the IIO source");
    require(model.sample().get().sequence != 0, "recovered hardware sample has a sequence number");
    view_model.onKey('8');
    require(view_model.infoExpanded().get(), "recovered compass restores normal shortcuts");

    std::filesystem::remove(iio_root / "iio:device2" / "in_magn_x_raw");
    view_model.tick(5066);
    require(!model.sample().get().available, "runtime IIO read failure publishes unavailable state");
    require(model.sample().get().status.find("read") != std::string::npos,
            "runtime IIO failure includes the read reason");
    require(!view_model.infoExpanded().get(), "runtime sensor failure collapses the info panel");

    require(writeFile(iio_root / "iio:device2" / "in_magn_x_raw", "100\n"), "failed IIO channel is restored");
    model.tick(10065);
    require(!model.sample().get().available, "runtime failure observes the retry interval");
    model.tick(10098);
    require(model.sample().get().available, "runtime read failure recovers after the retry interval");

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

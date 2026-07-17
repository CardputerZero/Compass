#include "models/compass_model.hpp"
#include "models/calibration_model.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <utility>

namespace compass {

namespace {

constexpr uint32_t kSampleIntervalMs = 33;
constexpr float kPi                  = 3.14159265359f;
constexpr float kDegToRad            = kPi / 180.0f;

constexpr const char* kBmi270I2cSysfsRoot     = "/sys/bus/i2c/devices";
constexpr const char* kBmi270IioSysfsRoot     = "/sys/bus/iio/devices";
constexpr const char* kBmi270Compatible       = "bosch,bmi270";
constexpr const char* kBmi270DeviceName       = "bmi270";
constexpr const char* kBmi270I2cAddressSuffix = "-0068";
constexpr const char* kBmi270IioDevicePath    = "/sys/bus/iio/devices/iio:device0";
constexpr const char* kBmm150IioDevicePath    = "/sys/bus/iio/devices/iio:device2";
constexpr const char* kBmm150Compatible       = "bosch,bmm150";
constexpr const char* kBmm150DeviceName       = "bmm150";
constexpr const char* kIioAccelXRaw           = "in_accel_x_raw";
constexpr const char* kIioAccelYRaw           = "in_accel_y_raw";
constexpr const char* kIioAccelZRaw           = "in_accel_z_raw";
constexpr const char* kIioAccelScale          = "in_accel_scale";
constexpr const char* kIioGyroXRaw            = "in_anglvel_x_raw";
constexpr const char* kIioGyroYRaw            = "in_anglvel_y_raw";
constexpr const char* kIioGyroZRaw            = "in_anglvel_z_raw";
constexpr const char* kIioGyroScale           = "in_anglvel_scale";
constexpr const char* kIioMagnXRaw            = "in_magn_x_raw";
constexpr const char* kIioMagnYRaw            = "in_magn_y_raw";
constexpr const char* kIioMagnZRaw            = "in_magn_z_raw";
constexpr const char* kIioMagnScale           = "in_magn_scale";

struct ImuDevice {
    std::string i2c_path;
    std::string iio_path;
    std::string mag_iio_path;
    std::string display_name;
    std::string mag_display_name;
};

std::string trim(std::string value)
{
    auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(),
                                            [&](char ch) { return !is_space(static_cast<unsigned char>(ch)); }));
    value.erase(
        std::find_if(value.rbegin(), value.rend(), [&](char ch) { return !is_space(static_cast<unsigned char>(ch)); })
            .base(),
        value.end());
    return value;
}

std::string lowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool readTextFile(const std::filesystem::path& path, std::string& value)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    value = trim(buffer.str());
    return true;
}

bool readDoubleFile(const std::filesystem::path& path, double& value)
{
    std::string text;
    if (!readTextFile(path, text)) {
        return false;
    }

    try {
        value = std::stod(text);
    } catch (...) {
        return false;
    }
    return true;
}

bool containsBmi270(const std::string& text)
{
    const auto lower = lowerCopy(text);
    return lower.find(kBmi270DeviceName) != std::string::npos || lower.find(kBmi270Compatible) != std::string::npos;
}

bool containsBmm150(const std::string& text)
{
    const auto lower = lowerCopy(text);
    return lower.find(kBmm150DeviceName) != std::string::npos || lower.find(kBmm150Compatible) != std::string::npos;
}

bool isI2cBmi270Node(const std::filesystem::path& path)
{
    const auto filename     = path.filename().string();
    const size_t suffix_len = std::char_traits<char>::length(kBmi270I2cAddressSuffix);
    if (filename.size() >= suffix_len &&
        filename.compare(filename.size() - suffix_len, suffix_len, kBmi270I2cAddressSuffix) == 0) {
        return true;
    }

    std::string text;
    if (readTextFile(path / "name", text) && containsBmi270(text)) {
        return true;
    }
    if (readTextFile(path / "modalias", text) && containsBmi270(text)) {
        return true;
    }
    if (readTextFile(path / "of_node" / "compatible", text) && containsBmi270(text)) {
        return true;
    }
    return false;
}

bool findI2cNode(std::string& i2c_path)
{
    const std::filesystem::path root(kBmi270I2cSysfsRoot);
    if (!std::filesystem::exists(root)) {
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_directory() && !entry.is_symlink()) {
            continue;
        }
        if (isI2cBmi270Node(entry.path())) {
            i2c_path = entry.path().string();
            return true;
        }
    }
    return false;
}

bool isIioBmi270Node(const std::filesystem::path& path)
{
    std::string name;
    if (readTextFile(path / "name", name) && containsBmi270(name)) {
        return true;
    }

    return std::filesystem::exists(path / kIioAccelXRaw) && std::filesystem::exists(path / kIioGyroXRaw);
}

bool isIioBmm150Node(const std::filesystem::path& path)
{
    std::string name;
    if (readTextFile(path / "name", name) && containsBmm150(name)) {
        return true;
    }

    return std::filesystem::exists(path / kIioMagnXRaw) && std::filesystem::exists(path / kIioMagnYRaw) &&
           std::filesystem::exists(path / kIioMagnZRaw);
}

bool readIioDisplayName(const std::filesystem::path& path, const char* fallback, std::string& display_name)
{
    if (!readTextFile(path / "name", display_name) || display_name.empty()) {
        display_name = fallback && fallback[0] != '\0' ? fallback : path.filename().string();
    }
    return true;
}

bool findIioNode(const char* preferred_path, bool (*matches)(const std::filesystem::path&), const char* fallback_name,
                 std::string& iio_path, std::string& display_name)
{
    if (preferred_path && preferred_path[0] != '\0') {
        const std::filesystem::path preferred(preferred_path);
        if (std::filesystem::exists(preferred) && matches(preferred)) {
            iio_path = preferred.string();
            readIioDisplayName(preferred, fallback_name, display_name);
            return true;
        }
    }

    const std::filesystem::path root(kBmi270IioSysfsRoot);
    if (!std::filesystem::exists(root)) {
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_directory() && !entry.is_symlink()) {
            continue;
        }
        if (!matches(entry.path())) {
            continue;
        }

        iio_path = entry.path().string();
        readIioDisplayName(entry.path(), fallback_name, display_name);
        return true;
    }
    return false;
}

bool readScaledAxis(const std::filesystem::path& root, const char* raw_file, double scale, float& value)
{
    double raw = 0.0;
    if (!readDoubleFile(root / raw_file, raw)) {
        return false;
    }
    value = static_cast<float>(raw * scale);
    return true;
}

float normalizeDegrees(float deg)
{
    while (deg < 0.0f) {
        deg += 360.0f;
    }
    while (deg >= 360.0f) {
        deg -= 360.0f;
    }
    return deg;
}

float dot(const Axis3& lhs, const Axis3& rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Axis3 add(const Axis3& lhs, const Axis3& rhs)
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

Axis3 scale(const Axis3& value, float factor)
{
    return {value.x * factor, value.y * factor, value.z * factor};
}

Axis3 screenToBmi270(const Axis3& screen)
{
    return {-screen.y, screen.x, screen.z};
}

Axis3 screenToBmm150(const Axis3& screen)
{
    return {screen.y, screen.x, -screen.z};
}

Axis3 mockWorldToScreen(const Axis3& world, float heading, float pitch, float roll)
{
    const float sin_heading = std::sin(heading);
    const float cos_heading = std::cos(heading);
    const float sin_pitch   = std::sin(pitch);
    const float cos_pitch   = std::cos(pitch);
    const float sin_roll    = std::sin(roll);
    const float cos_roll    = std::cos(roll);

    const Axis3 right{cos_heading, -sin_heading, 0.0f};
    const Axis3 top{sin_heading, cos_heading, 0.0f};
    const Axis3 out{0.0f, 0.0f, 1.0f};

    const Axis3 tilted_top   = add(scale(top, cos_pitch), scale(out, sin_pitch));
    const Axis3 pitched_out  = add(scale(out, cos_pitch), scale(top, -sin_pitch));
    const Axis3 tilted_right = add(scale(right, cos_roll), scale(pitched_out, sin_roll));
    const Axis3 tilted_out   = add(scale(pitched_out, cos_roll), scale(right, -sin_roll));

    return {dot(world, tilted_right), dot(world, tilted_top), dot(world, tilted_out)};
}

bool fillPose(CompassSample& sample)
{
    const CompassPose pose = calculateCompassPose(sample.accel, sample.mag);

    sample.headingDeg = pose.headingDeg;
    sample.pitchDeg   = pose.pitchDeg;
    sample.rollDeg    = pose.rollDeg;
    sample.bubbleX    = pose.bubbleX;
    sample.bubbleY    = pose.bubbleY;
    return pose.headingValid;
}

class ImuBackend {
public:
    virtual ~ImuBackend()                                                        = default;
    virtual bool init(std::string& error)                                        = 0;
    virtual bool read(uint32_t nowMs, CompassSample& sample, std::string& error) = 0;
};

class MockImuBackend : public ImuBackend {
public:
    bool init(std::string& error) override
    {
        (void)error;
        return true;
    }

    bool read(uint32_t nowMs, CompassSample& sample, std::string& error) override
    {
        (void)error;

        const float t       = static_cast<float>(nowMs) / 1000.0f;
        const float heading = normalizeDegrees(t * 18.0f);
        const float pitch   = std::sin(t * 0.8f) * 9.0f;
        const float roll    = std::cos(t * 0.65f) * 12.0f;

        const float heading_rad = heading * kDegToRad;
        const float pitch_rad   = pitch * kDegToRad;
        const float roll_rad    = roll * kDegToRad;

        const Axis3 screen_accel     = mockWorldToScreen({0.0f, 0.0f, 9.81f}, heading_rad, pitch_rad, roll_rad);
        const Axis3 screen_mag_gauss = mockWorldToScreen({0.0f, 0.42f, -0.18f}, heading_rad, pitch_rad, roll_rad);
        const Axis3 raw_accel        = screenToBmi270(screen_accel);
        const Axis3 raw_mag          = screenToBmm150(screen_mag_gauss);

        sample.source    = CompassDataSource::Mock;
        sample.available = true;
        sample.status    = "Mock IMU";
        sample.accel     = mapBmi270ToScreen(raw_accel);
        sample.gyro      = {std::cos(t * 0.8f) * 0.12f, std::sin(t * 0.65f) * 0.10f, 0.31f};
        sample.rawMag    = raw_mag;
        return true;
    }
};

#if COMPASS_USE_IIO_IMU
class IioImuBackend : public ImuBackend {
public:
    bool init(std::string& error) override
    {
        findI2cNode(_device.i2c_path);

        if (!findIioNode(kBmi270IioDevicePath, isIioBmi270Node, kBmi270DeviceName, _device.iio_path,
                         _device.display_name)) {
            error = "BMI270 IIO device not found";
            return false;
        }

        if (!findIioNode(kBmm150IioDevicePath, isIioBmm150Node, kBmm150DeviceName, _device.mag_iio_path,
                         _device.mag_display_name)) {
            error = "BMM150 IIO device not found";
            return false;
        }

        if (_device.display_name.empty()) {
            _device.display_name = kBmi270DeviceName;
        }
        if (_device.mag_display_name.empty()) {
            _device.mag_display_name = kBmm150DeviceName;
        }

        spdlog::info("CompassModel: IIO IMU initialized imu={} mag={} i2c={}", _device.iio_path, _device.mag_iio_path,
                     _device.i2c_path.empty() ? "(none)" : _device.i2c_path);
        return true;
    }

    bool read(uint32_t nowMs, CompassSample& sample, std::string& error) override
    {
        (void)nowMs;

        const std::filesystem::path imu_root(_device.iio_path);
        const std::filesystem::path mag_root(_device.mag_iio_path);
        double accel_scale = 0.0;
        double gyro_scale  = 0.0;
        double magn_scale  = 0.0;
        if (!readDoubleFile(imu_root / kIioAccelScale, accel_scale)) {
            error = "Failed to read BMI270 accel scale";
            return false;
        }
        if (!readDoubleFile(imu_root / kIioGyroScale, gyro_scale)) {
            error = "Failed to read BMI270 gyro scale";
            return false;
        }
        if (!readDoubleFile(mag_root / kIioMagnScale, magn_scale)) {
            error = "Failed to read BMM150 magnetometer scale";
            return false;
        }

        sample.source    = CompassDataSource::Iio;
        sample.available = true;
        sample.status    = _device.display_name + " + " + _device.mag_display_name;

        Axis3 raw_accel;
        Axis3 raw_gyro;
        if (!readScaledAxis(imu_root, kIioAccelXRaw, accel_scale, raw_accel.x) ||
            !readScaledAxis(imu_root, kIioAccelYRaw, accel_scale, raw_accel.y) ||
            !readScaledAxis(imu_root, kIioAccelZRaw, accel_scale, raw_accel.z) ||
            !readScaledAxis(imu_root, kIioGyroXRaw, gyro_scale, raw_gyro.x) ||
            !readScaledAxis(imu_root, kIioGyroYRaw, gyro_scale, raw_gyro.y) ||
            !readScaledAxis(imu_root, kIioGyroZRaw, gyro_scale, raw_gyro.z) ||
            !readScaledAxis(mag_root, kIioMagnXRaw, magn_scale, sample.rawMag.x) ||
            !readScaledAxis(mag_root, kIioMagnYRaw, magn_scale, sample.rawMag.y) ||
            !readScaledAxis(mag_root, kIioMagnZRaw, magn_scale, sample.rawMag.z)) {
            error = "Failed to read BMI270/BMM150 nine-axis data";
            return false;
        }

        sample.accel = mapBmi270ToScreen(raw_accel);
        sample.gyro  = mapBmi270ToScreen(raw_gyro);
        return true;
    }

private:
    ImuDevice _device;
};
#endif

std::unique_ptr<ImuBackend> makePrimaryBackend()
{
#if COMPASS_USE_MOCK_IMU
    return std::make_unique<MockImuBackend>();
#elif COMPASS_USE_IIO_IMU
    return std::make_unique<IioImuBackend>();
#else
    return std::make_unique<MockImuBackend>();
#endif
}

}  // namespace

struct CompassModel::Impl {
    std::unique_ptr<ImuBackend> backend = makePrimaryBackend();
    uint32_t last_sample_ms             = 0;
    bool using_mock                     = false;
    bool has_valid_heading              = false;
    float last_heading_deg              = 0.0f;
    CompassCalibration calibration;

    Impl()
    {
#if COMPASS_USE_MOCK_IMU
        using_mock = true;
#endif
    }

    bool init()
    {
        loadCalibration();

        std::string error;
        if (backend && backend->init(error)) {
            spdlog::info("CompassModel: using {} backend", using_mock ? "mock" : "IIO");
            return true;
        }

        spdlog::warn("CompassModel: IMU init failed: {}; falling back to mock", error);
        useMock("Mock IMU");
        return backend->init(error);
    }

    void useMock(const std::string& reason)
    {
        using_mock = true;
        backend    = std::make_unique<MockImuBackend>();
        spdlog::info("CompassModel: fallback backend active: {}", reason);
    }

    bool loadCalibration()
    {
        CompassCalibration next;
        if (CalibrationModel::load(next)) {
            calibration = next;
            spdlog::info("CompassModel: calibration loaded from {}", CalibrationModel::configPath().string());
            return true;
        }

        calibration = CompassCalibration{};
        spdlog::info("CompassModel: no calibration file at {}", CalibrationModel::configPath().string());
        return false;
    }
};

CompassModel::CompassModel() : _impl(std::make_unique<Impl>())
{
    _impl->init();
}

CompassModel::~CompassModel() = default;

bool CompassModel::reloadCalibration()
{
    return _impl->loadCalibration();
}

void CompassModel::tick(uint32_t nowMs)
{
    if (_impl->last_sample_ms != 0 && nowMs - _impl->last_sample_ms < kSampleIntervalMs) {
        return;
    }
    _impl->last_sample_ms = nowMs;

    CompassSample next;
    std::string error;
    if (!_impl->backend->read(nowMs, next, error)) {
        spdlog::warn("CompassModel: IMU read failed: {}; falling back to mock", error);
        _impl->useMock(error);
        error.clear();
        if (!_impl->backend->init(error) || !_impl->backend->read(nowMs, next, error)) {
            next.source    = CompassDataSource::Mock;
            next.available = false;
            next.status    = error.empty() ? "No IMU data" : error;
        }
    }

    bool heading_valid = false;
    if (next.available) {
        if (isUsableVector(next.rawMag)) {
            const Axis3 calibrated_mag = next.source == CompassDataSource::Iio
                                             ? applyMagCalibration(next.rawMag, _impl->calibration)
                                             : next.rawMag;
            next.mag                   = gaussToMicrotesla(mapBmm150ToScreen(calibrated_mag));
        } else {
            next.mag = {};
        }
        heading_valid = fillPose(next);
    }

    if (next.available && heading_valid && std::isfinite(next.headingDeg)) {
        _impl->last_heading_deg  = next.headingDeg;
        _impl->has_valid_heading = true;
    } else if (next.available && _impl->has_valid_heading) {
        next.headingDeg = _impl->last_heading_deg;
    }

    _sample.set(std::move(next));
}

}  // namespace compass

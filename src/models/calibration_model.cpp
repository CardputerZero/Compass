#include "models/calibration_model.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace compass {

namespace {

constexpr uint32_t kAutoFinishMs              = 20000;
constexpr uint32_t kMinimumCalibrationSamples = 120;
constexpr size_t kMaximumCalibrationSamples   = 2048;
constexpr float kMinimumAxisSpan              = 0.40f;
constexpr float kCoverageTarget               = 0.8f;
constexpr float kMinimumCoverage              = 0.65f;
constexpr size_t kMinimumCoveredOctants       = 8;
constexpr double kMaximumMatrixCondition      = 10.0;
constexpr double kMaximumNormalizedResidual   = 0.12;
constexpr double kMinimumFieldGauss           = 0.05;
constexpr double kMaximumFieldGauss           = 2.0;
constexpr const char* kCalibrationPathEnv     = "COMPASS_CALIBRATION_PATH";
constexpr const char* kConfigDirEnv           = "COMPASS_CONFIG_DIR";
constexpr const char* kCalibrationFileName    = "calibration.conf";

using Matrix3 = std::array<std::array<double, 3>, 3>;
using Vector3 = std::array<double, 3>;

struct FitMetrics {
    double normalized_residual = 0.0;
    double mean_field_gauss    = 0.0;
    double matrix_condition    = 0.0;
    size_t covered_octants     = 0;
    std::array<size_t, 8> octant_counts{};
};

bool sampleBounds(const std::vector<Axis3>& samples, Axis3& minimum, Axis3& maximum)
{
    if (samples.empty()) {
        return false;
    }

    minimum = samples.front();
    maximum = samples.front();
    for (const Axis3& sample : samples) {
        minimum.x = std::min(minimum.x, sample.x);
        minimum.y = std::min(minimum.y, sample.y);
        minimum.z = std::min(minimum.z, sample.z);
        maximum.x = std::max(maximum.x, sample.x);
        maximum.y = std::max(maximum.y, sample.y);
        maximum.z = std::max(maximum.z, sample.z);
    }
    return true;
}

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

bool envValue(const char* key, std::filesystem::path& path)
{
    const char* value = std::getenv(key);
    if (!value || value[0] == '\0') {
        return false;
    }

    path = value;
    return true;
}

bool configFloat(const std::unordered_map<std::string, std::string>& values, const char* key, float& value)
{
    const auto it = values.find(key);
    if (it == values.end()) {
        return false;
    }

    try {
        size_t parsed    = 0;
        const float next = std::stof(it->second, &parsed);
        if (parsed != it->second.size() || !std::isfinite(next)) {
            return false;
        }
        value = next;
        return true;
    } catch (...) {
        return false;
    }
}

bool configVersion(const std::unordered_map<std::string, std::string>& values, uint32_t& version)
{
    const auto it = values.find("version");
    if (it == values.end()) {
        version = 1;
        return true;
    }

    try {
        size_t parsed           = 0;
        const unsigned long raw = std::stoul(it->second, &parsed);
        if (parsed != it->second.size() || raw > std::numeric_limits<uint32_t>::max()) {
            return false;
        }
        version = static_cast<uint32_t>(raw);
        return true;
    } catch (...) {
        return false;
    }
}

Matrix3 calibrationMatrix(const CompassCalibration& calibration)
{
    return {{{calibration.mag_scale.x, calibration.mag_cross_xy, calibration.mag_cross_xz},
             {calibration.mag_cross_xy, calibration.mag_scale.y, calibration.mag_cross_yz},
             {calibration.mag_cross_xz, calibration.mag_cross_yz, calibration.mag_scale.z}}};
}

double determinant(const Matrix3& matrix)
{
    return matrix[0][0] * (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1]) -
           matrix[0][1] * (matrix[1][0] * matrix[2][2] - matrix[1][2] * matrix[2][0]) +
           matrix[0][2] * (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]);
}

bool calibrationIsValid(const CompassCalibration& calibration)
{
    if (!calibration.valid || !std::isfinite(calibration.mag_offset.x) || !std::isfinite(calibration.mag_offset.y) ||
        !std::isfinite(calibration.mag_offset.z)) {
        return false;
    }

    const Matrix3 matrix = calibrationMatrix(calibration);
    for (const auto& row : matrix) {
        for (double value : row) {
            if (!std::isfinite(value)) {
                return false;
            }
        }
    }

    constexpr double kPositiveThreshold = 1.0e-8;
    const double leading_minor_2        = matrix[0][0] * matrix[1][1] - matrix[0][1] * matrix[1][0];
    return matrix[0][0] > kPositiveThreshold && leading_minor_2 > kPositiveThreshold &&
           determinant(matrix) > kPositiveThreshold;
}

template <size_t N>
bool solveLinearSystem(std::array<std::array<double, N + 1>, N>& augmented, std::array<double, N>& solution)
{
    double matrix_scale = 0.0;
    for (const auto& row : augmented) {
        for (size_t column = 0; column < N; ++column) {
            matrix_scale = std::max(matrix_scale, std::abs(row[column]));
        }
    }
    if (!std::isfinite(matrix_scale) || matrix_scale <= 0.0) {
        return false;
    }

    for (size_t column = 0; column < N; ++column) {
        size_t pivot = column;
        for (size_t row = column + 1; row < N; ++row) {
            if (std::abs(augmented[row][column]) > std::abs(augmented[pivot][column])) {
                pivot = row;
            }
        }

        if (std::abs(augmented[pivot][column]) <= matrix_scale * 1.0e-10) {
            return false;
        }
        if (pivot != column) {
            std::swap(augmented[pivot], augmented[column]);
        }

        const double divisor = augmented[column][column];
        for (size_t item = column; item <= N; ++item) {
            augmented[column][item] /= divisor;
        }

        for (size_t row = 0; row < N; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = augmented[row][column];
            for (size_t item = column; item <= N; ++item) {
                augmented[row][item] -= factor * augmented[column][item];
            }
        }
    }

    for (size_t row = 0; row < N; ++row) {
        solution[row] = augmented[row][N];
        if (!std::isfinite(solution[row])) {
            return false;
        }
    }
    return true;
}

bool invertMatrix(const Matrix3& matrix, Matrix3& inverse)
{
    const double det = determinant(matrix);
    if (!std::isfinite(det) || std::abs(det) < 1.0e-10) {
        return false;
    }

    inverse = {{{(matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1]) / det,
                 (matrix[0][2] * matrix[2][1] - matrix[0][1] * matrix[2][2]) / det,
                 (matrix[0][1] * matrix[1][2] - matrix[0][2] * matrix[1][1]) / det},
                {(matrix[1][2] * matrix[2][0] - matrix[1][0] * matrix[2][2]) / det,
                 (matrix[0][0] * matrix[2][2] - matrix[0][2] * matrix[2][0]) / det,
                 (matrix[0][2] * matrix[1][0] - matrix[0][0] * matrix[1][2]) / det},
                {(matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]) / det,
                 (matrix[0][1] * matrix[2][0] - matrix[0][0] * matrix[2][1]) / det,
                 (matrix[0][0] * matrix[1][1] - matrix[0][1] * matrix[1][0]) / det}}};
    return true;
}

Vector3 multiply(const Matrix3& matrix, const Vector3& vector)
{
    Vector3 result{};
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 3; ++column) {
            result[row] += matrix[row][column] * vector[column];
        }
    }
    return result;
}

double dot(const Vector3& lhs, const Vector3& rhs)
{
    double result = 0.0;
    for (size_t index = 0; index < 3; ++index) {
        result += lhs[index] * rhs[index];
    }
    return result;
}

bool symmetricEigenDecomposition(Matrix3 matrix, Vector3& eigenvalues, Matrix3& eigenvectors)
{
    eigenvectors = {{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};

    for (size_t iteration = 0; iteration < 48; ++iteration) {
        size_t p = 0;
        size_t q = 1;
        for (size_t row = 0; row < 3; ++row) {
            for (size_t column = row + 1; column < 3; ++column) {
                if (std::abs(matrix[row][column]) > std::abs(matrix[p][q])) {
                    p = row;
                    q = column;
                }
            }
        }

        const double diagonal_scale =
            std::max({1.0, std::abs(matrix[0][0]), std::abs(matrix[1][1]), std::abs(matrix[2][2])});
        if (std::abs(matrix[p][q]) <= diagonal_scale * 1.0e-12) {
            eigenvalues = {matrix[0][0], matrix[1][1], matrix[2][2]};
            return true;
        }

        const double angle  = 0.5 * std::atan2(2.0 * matrix[p][q], matrix[q][q] - matrix[p][p]);
        const double cosine = std::cos(angle);
        const double sine   = std::sin(angle);
        const double app    = matrix[p][p];
        const double aqq    = matrix[q][q];
        const double apq    = matrix[p][q];

        for (size_t index = 0; index < 3; ++index) {
            if (index == p || index == q) {
                continue;
            }
            const double aip = matrix[index][p];
            const double aiq = matrix[index][q];
            matrix[index][p] = matrix[p][index] = cosine * aip - sine * aiq;
            matrix[index][q] = matrix[q][index] = sine * aip + cosine * aiq;
        }
        matrix[p][p] = cosine * cosine * app - 2.0 * sine * cosine * apq + sine * sine * aqq;
        matrix[q][q] = sine * sine * app + 2.0 * sine * cosine * apq + cosine * cosine * aqq;
        matrix[p][q] = matrix[q][p] = 0.0;

        for (size_t row = 0; row < 3; ++row) {
            const double vip     = eigenvectors[row][p];
            const double viq     = eigenvectors[row][q];
            eigenvectors[row][p] = cosine * vip - sine * viq;
            eigenvectors[row][q] = sine * vip + cosine * viq;
        }
    }

    return false;
}

size_t popcount(uint8_t value)
{
    size_t count = 0;
    while (value != 0) {
        count += value & 1U;
        value >>= 1U;
    }
    return count;
}

bool fitEllipsoid(const std::vector<Axis3>& samples, const Axis3& minimum, const Axis3& maximum,
                  CompassCalibration& calibration, FitMetrics& metrics, std::string& error)
{
    if (samples.size() < kMinimumCalibrationSamples) {
        error = "not enough samples";
        return false;
    }

    const Axis3 span{maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z};
    const float coverage = std::min({span.x, span.y, span.z}) / kCoverageTarget;
    if (span.x < kMinimumAxisSpan || span.y < kMinimumAxisSpan || span.z < kMinimumAxisSpan ||
        coverage < kMinimumCoverage) {
        error = "insufficient 3D coverage";
        return false;
    }

    const Vector3 seed_center{(maximum.x + minimum.x) * 0.5, (maximum.y + minimum.y) * 0.5,
                              (maximum.z + minimum.z) * 0.5};
    const double seed_scale = (span.x + span.y + span.z) / 6.0;
    if (!std::isfinite(seed_scale) || seed_scale <= 1.0e-6) {
        error = "invalid sample scale";
        return false;
    }

    constexpr size_t kTerms = 9;
    std::array<std::array<double, kTerms + 1>, kTerms> normal{};
    for (const Axis3& sample : samples) {
        const double x = (sample.x - seed_center[0]) / seed_scale;
        const double y = (sample.y - seed_center[1]) / seed_scale;
        const double z = (sample.z - seed_center[2]) / seed_scale;
        const std::array<double, kTerms> terms{x * x,       y * y,   z * z,   2.0 * x * y, 2.0 * x * z,
                                               2.0 * y * z, 2.0 * x, 2.0 * y, 2.0 * z};
        for (size_t row = 0; row < kTerms; ++row) {
            for (size_t column = 0; column < kTerms; ++column) {
                normal[row][column] += terms[row] * terms[column];
            }
            normal[row][kTerms] += terms[row];
        }
    }

    std::array<double, kTerms> coefficients{};
    if (!solveLinearSystem(normal, coefficients)) {
        error = "singular ellipsoid fit";
        return false;
    }

    Matrix3 quadratic{{{coefficients[0], coefficients[3], coefficients[4]},
                       {coefficients[3], coefficients[1], coefficients[5]},
                       {coefficients[4], coefficients[5], coefficients[2]}}};
    const Vector3 linear{coefficients[6], coefficients[7], coefficients[8]};
    Matrix3 quadratic_inverse{};
    if (!invertMatrix(quadratic, quadratic_inverse)) {
        error = "singular quadratic form";
        return false;
    }

    Vector3 center = multiply(quadratic_inverse, linear);
    for (double& value : center) {
        value = -value;
    }
    const Vector3 quadratic_center = multiply(quadratic, center);
    const double normalization     = 1.0 + dot(center, quadratic_center);
    if (!std::isfinite(normalization) || normalization <= 1.0e-8) {
        error = "invalid ellipsoid normalization";
        return false;
    }
    for (auto& row : quadratic) {
        for (double& value : row) {
            value /= normalization;
        }
    }

    Vector3 eigenvalues{};
    Matrix3 eigenvectors{};
    if (!symmetricEigenDecomposition(quadratic, eigenvalues, eigenvectors)) {
        error = "ellipsoid eigen decomposition did not converge";
        return false;
    }
    const auto [minimum_eigenvalue, maximum_eigenvalue] = std::minmax_element(eigenvalues.begin(), eigenvalues.end());
    if (!std::isfinite(*minimum_eigenvalue) || !std::isfinite(*maximum_eigenvalue) || *minimum_eigenvalue <= 1.0e-8) {
        error = "ellipsoid is not positive definite";
        return false;
    }
    metrics.matrix_condition = std::sqrt(*maximum_eigenvalue / *minimum_eigenvalue);
    if (!std::isfinite(metrics.matrix_condition) || metrics.matrix_condition > kMaximumMatrixCondition) {
        error = "ellipsoid is poorly conditioned";
        return false;
    }

    const double eigen_product = eigenvalues[0] * eigenvalues[1] * eigenvalues[2];
    const double volume_scale  = std::pow(eigen_product, -1.0 / 6.0);
    Matrix3 correction{};
    for (size_t eigen_index = 0; eigen_index < 3; ++eigen_index) {
        const double correction_eigenvalue = std::sqrt(eigenvalues[eigen_index]) * volume_scale;
        for (size_t row = 0; row < 3; ++row) {
            for (size_t column = 0; column < 3; ++column) {
                correction[row][column] +=
                    correction_eigenvalue * eigenvectors[row][eigen_index] * eigenvectors[column][eigen_index];
            }
        }
    }

    CompassCalibration next;
    next.valid        = true;
    next.mag_offset.x = static_cast<float>(seed_center[0] + seed_scale * center[0]);
    next.mag_offset.y = static_cast<float>(seed_center[1] + seed_scale * center[1]);
    next.mag_offset.z = static_cast<float>(seed_center[2] + seed_scale * center[2]);
    next.mag_scale    = {static_cast<float>(correction[0][0]), static_cast<float>(correction[1][1]),
                         static_cast<float>(correction[2][2])};
    next.mag_cross_xy = static_cast<float>(correction[0][1]);
    next.mag_cross_xz = static_cast<float>(correction[0][2]);
    next.mag_cross_yz = static_cast<float>(correction[1][2]);
    if (!calibrationIsValid(next)) {
        error = "non-finite or non-positive correction matrix";
        return false;
    }

    double field_sum = 0.0;
    std::vector<double> field_norms;
    field_norms.reserve(samples.size());
    uint8_t octant_mask = 0;
    for (const Axis3& sample : samples) {
        const Axis3 corrected = applyMagCalibration(sample, next);
        const double norm =
            std::sqrt(static_cast<double>(corrected.x) * corrected.x + static_cast<double>(corrected.y) * corrected.y +
                      static_cast<double>(corrected.z) * corrected.z);
        if (!std::isfinite(norm) || norm <= 1.0e-8) {
            error = "invalid corrected field sample";
            return false;
        }
        field_norms.push_back(norm);
        field_sum += norm;
        const uint8_t octant = static_cast<uint8_t>((corrected.x >= 0.0f ? 1U : 0U) | (corrected.y >= 0.0f ? 2U : 0U) |
                                                    (corrected.z >= 0.0f ? 4U : 0U));
        octant_mask |= static_cast<uint8_t>(1U << octant);
        ++metrics.octant_counts[octant];
    }

    metrics.covered_octants  = popcount(octant_mask);
    metrics.mean_field_gauss = field_sum / static_cast<double>(field_norms.size());
    double residual_sum      = 0.0;
    for (double norm : field_norms) {
        const double residual = norm / metrics.mean_field_gauss - 1.0;
        residual_sum += residual * residual;
    }
    metrics.normalized_residual = std::sqrt(residual_sum / static_cast<double>(field_norms.size()));

    if (metrics.covered_octants < kMinimumCoveredOctants) {
        error = "too few magnetic field octants covered";
        return false;
    }
    if (!std::isfinite(metrics.mean_field_gauss) || metrics.mean_field_gauss < kMinimumFieldGauss ||
        metrics.mean_field_gauss > kMaximumFieldGauss) {
        error = "corrected magnetic field magnitude is implausible";
        return false;
    }
    if (!std::isfinite(metrics.normalized_residual) || metrics.normalized_residual > kMaximumNormalizedResidual) {
        error = "ellipsoid residual is too high";
        return false;
    }

    calibration = next;
    return true;
}

}  // namespace

Axis3 applyMagCalibration(const Axis3& mag, const CompassCalibration& calibration)
{
    if (!calibration.valid) {
        return mag;
    }

    const float x = mag.x - calibration.mag_offset.x;
    const float y = mag.y - calibration.mag_offset.y;
    const float z = mag.z - calibration.mag_offset.z;
    return Axis3{
        calibration.mag_scale.x * x + calibration.mag_cross_xy * y + calibration.mag_cross_xz * z,
        calibration.mag_cross_xy * x + calibration.mag_scale.y * y + calibration.mag_cross_yz * z,
        calibration.mag_cross_xz * x + calibration.mag_cross_yz * y + calibration.mag_scale.z * z,
    };
}

CalibrationModel::CalibrationModel()
{
    if (load(_calibration)) {
        _status.set("Calibration loaded");
    }
}

void CalibrationModel::start()
{
    resetCapture();
    _status.set("Rotate through every direction");
    _progress.set(0.0f);
    _state.set(CalibrationState::Running);
}

bool CalibrationModel::finish()
{
    if (_state.get() != CalibrationState::Running) {
        return false;
    }

    CompassCalibration calibration;
    if (!buildCalibration(calibration)) {
        _status.set("Need more motion");
        return false;
    }

    if (!save(calibration)) {
        _status.set("Failed to save calibration");
        return false;
    }

    _calibration = calibration;
    _progress.set(1.0f);
    _status.set("Saved");
    _state.set(CalibrationState::Done);
    spdlog::info("CalibrationModel: calibration saved to {}", configPath().string());
    return true;
}

void CalibrationModel::stop()
{
    _state.set(CalibrationState::Idle);
}

void CalibrationModel::tick(uint32_t nowMs)
{
    if (_state.get() != CalibrationState::Running) {
        return;
    }

    if (_start_ms == 0) {
        _start_ms = nowMs;
        return;
    }

    if (nowMs - _start_ms >= kAutoFinishMs) {
        if (!finish()) {
            _start_ms = nowMs;
        }
    }
}

void CalibrationModel::updateSample(const CompassSample& sample)
{
    if (_state.get() != CalibrationState::Running) {
        return;
    }
    if (sample.sequence != 0 && sample.sequence == _last_sample_sequence) {
        return;
    }
    if (sample.sequence != 0) {
        _last_sample_sequence = sample.sequence;
    }
    if (!sample.available || !isUsableVector(sample.rawMag)) {
        return;
    }

    captureMag(sample.rawMag);
    updateProgress();
}

std::filesystem::path CalibrationModel::configPath()
{
    std::filesystem::path path;
    if (envValue(kCalibrationPathEnv, path)) {
        return path;
    }

    if (envValue(kConfigDirEnv, path)) {
        return path / kCalibrationFileName;
    }

    return std::filesystem::path("/tmp/Compass") / kCalibrationFileName;
}

bool CalibrationModel::load(CompassCalibration& calibration)
{
    return loadFrom(configPath(), calibration);
}

bool CalibrationModel::loadFrom(const std::filesystem::path& path, CompassCalibration& calibration)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::unordered_map<std::string, std::string> values;
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        values[trim(line.substr(0, separator))] = trim(line.substr(separator + 1));
    }

    uint32_t version = 0;
    if (!configVersion(values, version) || (version != 1 && version != 2)) {
        spdlog::warn("CalibrationModel: rejected {}: unsupported or invalid version", path.string());
        return false;
    }

    CompassCalibration next;
    next.valid = true;
    if (!configFloat(values, "mag_offset_x", next.mag_offset.x) ||
        !configFloat(values, "mag_offset_y", next.mag_offset.y) ||
        !configFloat(values, "mag_offset_z", next.mag_offset.z) ||
        !configFloat(values, "mag_scale_x", next.mag_scale.x) ||
        !configFloat(values, "mag_scale_y", next.mag_scale.y) ||
        !configFloat(values, "mag_scale_z", next.mag_scale.z)) {
        spdlog::warn("CalibrationModel: rejected {}: missing or non-finite coefficients", path.string());
        return false;
    }
    if (version == 2 && (!configFloat(values, "mag_cross_xy", next.mag_cross_xy) ||
                         !configFloat(values, "mag_cross_xz", next.mag_cross_xz) ||
                         !configFloat(values, "mag_cross_yz", next.mag_cross_yz))) {
        spdlog::warn("CalibrationModel: rejected {}: missing or non-finite v2 cross-axis coefficients", path.string());
        return false;
    }
    if (!calibrationIsValid(next)) {
        spdlog::warn("CalibrationModel: rejected {}: correction matrix is not positive definite", path.string());
        return false;
    }

    calibration = next;
    return true;
}

bool CalibrationModel::save(const CompassCalibration& calibration)
{
    return saveTo(configPath(), calibration);
}

bool CalibrationModel::saveTo(const std::filesystem::path& path, const CompassCalibration& calibration)
{
    if (!calibrationIsValid(calibration)) {
        spdlog::warn("CalibrationModel: refused to save invalid calibration to {}", path.string());
        return false;
    }

    std::error_code error;
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            spdlog::warn("CalibrationModel: failed to create config dir {}: {}", parent.string(), error.message());
            return false;
        }
    }

    const auto tmp_path = path.string() + ".tmp";
    {
        std::ofstream file(tmp_path, std::ios::trunc);
        if (!file.is_open()) {
            return false;
        }

        file << "# M5CardputerZero Compass calibration\n";
        file << "# Magnetic offsets are stored in gauss\n";
        file << "version=2\n";
        file << "mag_offset_x=" << calibration.mag_offset.x << "\n";
        file << "mag_offset_y=" << calibration.mag_offset.y << "\n";
        file << "mag_offset_z=" << calibration.mag_offset.z << "\n";
        file << "mag_scale_x=" << calibration.mag_scale.x << "\n";
        file << "mag_scale_y=" << calibration.mag_scale.y << "\n";
        file << "mag_scale_z=" << calibration.mag_scale.z << "\n";
        file << "mag_cross_xy=" << calibration.mag_cross_xy << "\n";
        file << "mag_cross_xz=" << calibration.mag_cross_xz << "\n";
        file << "mag_cross_yz=" << calibration.mag_cross_yz << "\n";
        file.flush();
        file.close();
        if (file.fail()) {
            spdlog::warn("CalibrationModel: failed to write calibration data to {}", tmp_path);
            std::filesystem::remove(tmp_path, error);
            return false;
        }
    }

    std::filesystem::rename(tmp_path, path, error);
    if (error) {
        spdlog::warn("CalibrationModel: failed to save config {}: {}", path.string(), error.message());
        std::filesystem::remove(tmp_path, error);
        return false;
    }

    return true;
}

void CalibrationModel::resetCapture()
{
    _sample_count         = 0;
    _start_ms             = 0;
    _last_sample_sequence = 0;
    _next_sample_index    = 0;
    _mag_samples.clear();
    _mag_samples.reserve(kMaximumCalibrationSamples);
}

void CalibrationModel::captureMag(const Axis3& mag)
{
    if (_mag_samples.size() < kMaximumCalibrationSamples) {
        _mag_samples.push_back(mag);
    } else {
        _mag_samples[_next_sample_index] = mag;
        _next_sample_index               = (_next_sample_index + 1) % kMaximumCalibrationSamples;
    }
    ++_sample_count;
}

bool CalibrationModel::buildCalibration(CompassCalibration& calibration) const
{
    if (_mag_samples.size() < kMinimumCalibrationSamples) {
        spdlog::warn("CalibrationModel: fit rejected (samples={}): not enough samples", _mag_samples.size());
        return false;
    }

    Axis3 minimum;
    Axis3 maximum;
    if (!sampleBounds(_mag_samples, minimum, maximum)) {
        return false;
    }

    FitMetrics metrics;
    std::string error;
    if (!fitEllipsoid(_mag_samples, minimum, maximum, calibration, metrics, error)) {
        spdlog::warn("CalibrationModel: fit rejected (samples={}): {}", _mag_samples.size(), error);
        return false;
    }

    spdlog::info(
        "CalibrationModel: v2 fit samples={} octants={} counts=[{},{},{},{},{},{},{},{}] residual={:.4f} "
        "field={:.4f}G condition={:.3f} "
        "offset=({:.6f},{:.6f},{:.6f}) matrix=({:.6f},{:.6f},{:.6f};{:.6f},{:.6f},{:.6f};{:.6f},{:.6f},{:.6f})",
        _mag_samples.size(), metrics.covered_octants, metrics.octant_counts[0], metrics.octant_counts[1],
        metrics.octant_counts[2], metrics.octant_counts[3], metrics.octant_counts[4], metrics.octant_counts[5],
        metrics.octant_counts[6], metrics.octant_counts[7], metrics.normalized_residual, metrics.mean_field_gauss,
        metrics.matrix_condition, calibration.mag_offset.x, calibration.mag_offset.y, calibration.mag_offset.z,
        calibration.mag_scale.x, calibration.mag_cross_xy, calibration.mag_cross_xz, calibration.mag_cross_xy,
        calibration.mag_scale.y, calibration.mag_cross_yz, calibration.mag_cross_xz, calibration.mag_cross_yz,
        calibration.mag_scale.z);
    return true;
}

void CalibrationModel::updateProgress()
{
    Axis3 minimum;
    Axis3 maximum;
    if (!sampleBounds(_mag_samples, minimum, maximum)) {
        _progress.set(0.0f);
        return;
    }

    const float span_x          = maximum.x - minimum.x;
    const float span_y          = maximum.y - minimum.y;
    const float span_z          = maximum.z - minimum.z;
    const float coverage        = std::min({span_x, span_y, span_z}) / kCoverageTarget;
    const float sample_progress = static_cast<float>(_sample_count) / static_cast<float>(kMinimumCalibrationSamples);

    const Axis3 center{(maximum.x + minimum.x) * 0.5f, (maximum.y + minimum.y) * 0.5f, (maximum.z + minimum.z) * 0.5f};
    uint8_t octant_mask = 0;
    for (const Axis3& sample : _mag_samples) {
        const uint8_t octant = static_cast<uint8_t>(
            (sample.x >= center.x ? 1U : 0U) | (sample.y >= center.y ? 2U : 0U) | (sample.z >= center.z ? 4U : 0U));
        octant_mask |= static_cast<uint8_t>(1U << octant);
    }
    const float octant_progress =
        static_cast<float>(popcount(octant_mask)) / static_cast<float>(kMinimumCoveredOctants);
    _progress.set(std::clamp(std::min({coverage, sample_progress, octant_progress}), 0.0f, 1.0f));
}

}  // namespace compass

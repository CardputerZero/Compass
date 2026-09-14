#include "core/compass_app.hpp"

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
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

lv_obj_t* visibleLabel(lv_obj_t* root, const std::string& text)
{
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) {
        return nullptr;
    }
    if (lv_obj_check_type(root, &lv_label_class) && text == lv_label_get_text(root)) {
        return root;
    }
    for (uint32_t index = 0; index < lv_obj_get_child_count(root); ++index) {
        if (auto* found = visibleLabel(lv_obj_get_child(root, index), text)) {
            return found;
        }
    }
    return nullptr;
}

bool visible(const std::string& text)
{
    return visibleLabel(lv_screen_active(), text) != nullptr;
}

void checkNoticeLayout(const char* title, const char* body, const char* action)
{
    lv_obj_update_layout(lv_screen_active());
    lv_area_t previous{};
    for (const char* text : {title, body, action}) {
        auto* label = visibleLabel(lv_screen_active(), text);
        require(label != nullptr, std::string("notice shows ") + text);
        if (!label) {
            continue;
        }
        lv_area_t area;
        lv_obj_get_coords(label, &area);
        require(area.x1 >= 0 && area.y1 >= 0 && area.x2 < 320 && area.y2 < 170,
                "notice text stays within the display");
        require(area.y1 > previous.y2, "notice text does not overlap");
        require(lv_obj_get_height(label) >= lv_obj_get_self_height(label), "notice text is not clipped");
        previous = area;
    }
}

void feedFullRotation(compass::CalibrationModel& model)
{
    compass::CompassSample sample;
    sample.available = true;
    for (int index = 0; index < 360; ++index) {
        const double z = 1.0 - 2.0 * (index + 0.5) / 360.0;
        const double radius = std::sqrt(1.0 - z * z);
        const double angle = index * 2.39996322972865332;
        sample.rawMag = {static_cast<float>(0.48 * radius * std::cos(angle)),
                         static_cast<float>(0.48 * radius * std::sin(angle)), static_cast<float>(0.48 * z)};
        model.updateSample(sample);
    }
}

void testRequiredStartup()
{
    compass::CompassApp app;
    app.start();
    checkNoticeLayout("Calibration required", "Calibrate before first use.\nKeep away from metal and magnets.",
                      "Enter: start  Esc: exit");
    for (uint32_t key : {'4', '6', '7', '8', ' '}) {
        app.onKey(key);
        require(visible("Calibration required"), "other keys cannot dismiss the required dialog");
    }
    app.onKey(compass::compass_key::Help);
    require(!visible("KEY_HELP / Esc: close"), "help does not cover the required dialog");
    app.onLvglKey(LV_KEY_ENTER, nullptr);
    require(!visible("Calibration required") && visible("Rotate through every direction"),
            "one Enter starts calibration directly");
    require(visible("Auto-check in 20s"), "running calibration explains the automatic check delay");
    app.onKey('4');
    require(visible("Rotate through every direction"), "back cannot bypass required calibration");
    app.onKey('\r');
    require(visible("Collecting samples: keep rotating"), "insufficient samples do not show success");
    app.onKey('\x1b');
    require(app.quitRequested(), "Esc exits instead of bypassing calibration");
}

void testCompletion(const std::filesystem::path& config, bool automatic, bool fail_save)
{
    std::filesystem::remove_all(config);
    compass::CompassRouter router;
    compass::CalibrationModel model;
    compass::CompassModel compass_model;
    compass::CalibrationViewModel vm(router, model, compass_model);
    compass::CalibrationView view(vm);
    router.replace(compass::PageId::Calibration);
    vm.onEnter();
    view.onEnter(lv_screen_active());
    vm.onKey('\r');
    feedFullRotation(model);
    if (fail_save) {
        std::filesystem::create_directory(config);
        vm.onKey('\r');
        require(vm.required(), "failed save does not unlock the compass");
        require(visible("Failed to save calibration") && !visible("Calibration complete"),
                "failed save shows an error without success");
        vm.onKey('4');
        require(router.page() == compass::PageId::Calibration, "failed save cannot be bypassed");
        std::filesystem::remove(config);
    }
    if (automatic) {
        vm.tick(100);
        vm.tick(20100);
    } else {
        vm.onKey('\r');
    }
    require(model.state().get() == compass::CalibrationState::Done, "full rotation completes calibration");
    require(!vm.required(), "successful save unlocks compass navigation");
    require(router.page() == compass::PageId::Calibration, "completion waits for acknowledgement");
    checkNoticeLayout("Calibration complete", "Calibration saved.\nYour compass is ready to use.",
                      "Enter: return to compass");
    compass::CompassCalibration saved;
    require(compass::CalibrationModel::load(saved), "completed calibration is persisted");
    vm.onKey('\r');
    require(router.page() == compass::PageId::Compass, "Enter returns to compass after success");
    view.onExit();
    vm.onExit();

    compass::CompassApp app;
    app.start();
    require(!visible("Calibration required"), "subsequent launch skips the required dialog");
    app.tick(100);
    app.onKey('8');
    app.onKey('7');
    require(visible("Press Enter to start calibration"), "manual recalibration remains available");
    app.onKey('\x1b');
    require(!app.quitRequested() && !visible("Press Enter to start calibration"),
            "manual recalibration can be cancelled");
}

void testSensorFailureDisplay()
{
    setenv("COMPASS_MOCK_SENSOR_UNAVAILABLE", "1", 1);
    compass::CompassApp app;
    app.start();
    app.onKey('\r');
    app.tick(1);
    require(visible("BMM150 IIO device not found"), "calibration displays the sensor failure");
    require(visible("Coverage: 0%  Samples: 0"), "zero progress and sample count remain visible");
    app.onKey('\r');
    require(visible("BMM150 IIO device not found"), "Enter does not hide the sensor failure");
    app.tick(20001);
    require(visible("BMM150 IIO device not found") && !visible("Calibration complete"),
            "an empty capture cannot complete automatically after 20 seconds");
    app.onKey('\r');
    require(visible("BMM150 IIO device not found"), "Enter cannot accept an empty capture");
    app.onKey('\x1b');
    require(app.quitRequested(), "sensor failure does not trap the user in calibration");
    unsetenv("COMPASS_MOCK_SENSOR_UNAVAILABLE");
}

void testLowQualityKeepsCollecting(const std::filesystem::path& config)
{
    std::filesystem::remove(config);
    compass::CompassRouter router;
    compass::CalibrationModel model;
    compass::CompassModel compass_model;
    compass::CalibrationViewModel vm(router, model, compass_model);
    compass::CalibrationView view(vm);
    router.replace(compass::PageId::Calibration);
    view.onEnter(lv_screen_active());
    vm.onKey('\r');
    model.tick(0);
    compass::CompassSample stationary;
    stationary.available = true;
    stationary.rawMag = {0.1f, 0.2f, 0.3f};
    for (int i = 0; i < 180; ++i) {
        model.updateSample(stationary);
    }
    vm.tick(20000);
    require(model.state().get() == compass::CalibrationState::Running && visible("Rotate on all three axes"),
            "poor coverage keeps collecting after 20 seconds");
    vm.onKey('\r');
    require(router.page() == compass::PageId::Calibration && vm.required(),
            "Enter cannot apply a low-quality capture");
    require(!std::filesystem::exists(config), "low-quality capture is not persisted");
}

}  // namespace

int main()
{
    const auto config = std::filesystem::temp_directory_path() / "compass_calibration_flow_test.conf";
    std::filesystem::remove_all(config);
    setenv("COMPASS_CALIBRATION_PATH", config.c_str(), 1);
    lv_init();
    auto* display = lv_display_create(320, 170);

    testRequiredStartup();
    {
        std::ofstream file(config);
        file << "invalid calibration\n";
    }
    testRequiredStartup();
    testCompletion(config, false, true);
    testCompletion(config, true, false);
    // Clearing temporary storage on reboot must require calibration again.
    std::filesystem::remove(config);
    testRequiredStartup();
    testLowQualityKeepsCollecting(config);
    testSensorFailureDisplay();

    lv_display_delete(display);
    lv_deinit();
    std::filesystem::remove_all(config);
    unsetenv("COMPASS_CALIBRATION_PATH");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

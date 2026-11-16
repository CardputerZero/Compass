#include "core/compass_app.hpp"
#include "assets/assets.h"
#include <core/hal/hal.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;
uint32_t animation_tick = 0;

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

bool visibleImage(lv_obj_t* root, const lv_image_dsc_t* image)
{
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) {
        return false;
    }
    if (lv_obj_check_type(root, &lv_image_class) && lv_image_get_src(root) == image &&
        lv_obj_get_style_opa(root, LV_PART_MAIN) > 0) {
        lv_area_t area;
        lv_obj_get_coords(root, &area);
        return area.x1 >= 0 && area.x2 < 320 && area.y1 >= 0 && area.y2 < 170;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        if (visibleImage(lv_obj_get_child(root, i), image)) {
            return true;
        }
    }
    return false;
}

lv_obj_t* visibleCheckbox(lv_obj_t* root)
{
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) {
        return nullptr;
    }
    if (lv_obj_check_type(root, &lv_checkbox_class)) {
        return root;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        if (auto* checkbox = visibleCheckbox(lv_obj_get_child(root, i))) {
            return checkbox;
        }
    }
    return nullptr;
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
    sample.magnetometerAvailable = true;
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
    {
        compass::CompassRouter router;
        compass::CalibrationModel model;
        compass::CompassModel compass_model;
        compass::CalibrationViewModel vm(router, model, compass_model);
        compass::CalibrationView view(vm);
        view.onEnter(lv_screen_active());
        vm.onKey('\r');
        vm.tick(1);
        require(visible("BMM150 IIO device not found"), "calibration displays the sensor failure");
        require(visible("Coverage: 0%  Samples: 0"), "zero progress and sample count remain visible");
        vm.onKey('\r');
        vm.tick(20001);
        require(visible("BMM150 IIO device not found") && !visible("Calibration complete"),
                "an empty capture cannot complete automatically after 20 seconds");
    }
    compass::CompassApp app;
    app.start();
    require(visible("Compass unavailable") && !visible("Calibration required"),
            "missing sensors do not trap startup in required calibration");
    unsetenv("COMPASS_MOCK_SENSOR_UNAVAILABLE");
}

void checkSixAxisLayout(lv_obj_t* root, int& axes)
{
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (lv_obj_check_type(root, &lv_label_class)) {
        const std::string text = lv_label_get_text(root);
        if (text.size() > 2 && text[2] == ':' && (text[0] == 'A' || text[0] == 'G' || text[0] == 'M')) {
            require(text[0] != 'M', "six-axis mode hides all magnetic channels");
            lv_area_t area;
            lv_obj_get_coords(root, &area);
            require(area.x1 >= 184 && area.x2 < 320 && area.y1 >= 30 && area.y2 < 115,
                    "original six-axis panels are centered in the existing data area");
            ++axes;
        }
        require(text != "N" && text != "E" && text != "S" && text != "W",
                "six-axis mode hides compass directions");
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        checkSixAxisLayout(lv_obj_get_child(root, i), axes);
    }
}

lv_obj_t* tiltBall(lv_obj_t* root);

void testSixAxisStartup()
{
    setenv("COMPASS_MOCK_NO_MAGNETOMETER", "1", 1);
    compass::CompassApp app;
    app.start();
    require(visible("Compass not supported") && !visible("Calibration required") && !visible("Compass unavailable"),
            "no magnetometer shows an entry notice regardless of calibration file");
    lv_obj_update_layout(lv_screen_active());
    auto* notice = visibleLabel(lv_screen_active(), "Compass not supported");
    require(notice && lv_obj_get_width(notice) == lv_obj_get_self_width(notice) &&
                lv_obj_get_width(notice) <= 320,
            "English notice fits without clipping");
    auto* checkbox = visibleCheckbox(lv_screen_active());
    require(checkbox && std::string(lv_checkbox_get_text(checkbox)) == "Don't show again" && visible("Continue"),
            "entry notice has a checkbox and continue action");
    app.onKey('7');
    require(checkbox && lv_obj_has_state(checkbox, LV_STATE_CHECKED), "7 checks the do-not-show option");
    app.onKey(' ');
    require(checkbox && !lv_obj_has_state(checkbox, LV_STATE_CHECKED), "Space unchecks the do-not-show option");
    app.onKey('\r');
    require(!visible("Compass not supported") && tiltBall(lv_screen_active()),
            "continue reveals the original tilt bubble without a permanent warning");
    app.tick(33);
    animation_tick += 1000;
    app.tick(34);
    lv_obj_update_layout(lv_screen_active());
    require(visibleImage(lv_screen_active(), &image_icon_more),
            "six-axis home retains the original bottom-right More button");
    require(!visibleImage(lv_screen_active(), &image_icon_back), "home starts collapsed as in the original app");
    auto* ball = tiltBall(lv_screen_active());
    require(ball && lv_obj_get_x(lv_obj_get_parent(ball)) == 75,
            "original tilt panel starts centered before expanding the data");
    app.onKey('8');
    app.tick(67);
    animation_tick += 1000;
    app.tick(68);
    lv_obj_update_layout(lv_screen_active());
    require(visibleImage(lv_screen_active(), &image_icon_back) && !visibleImage(lv_screen_active(), &image_icon_more),
            "8 expands the data and replaces More with the original Back button");
    require(!visibleImage(lv_screen_active(), &image_icon_calibration),
            "six-axis mode removes only the magnetic calibration button");
    app.onKey('7');
    require(!visible("Press Enter to start calibration"), "the removed calibration shortcut is inactive");
    int axes = 0;
    checkSixAxisLayout(lv_screen_active(), axes);
    require(axes == 6, "the original expanded view displays exactly six channels");
    app.onKey('8');
    app.tick(101);
    animation_tick += 1000;
    app.tick(102);
    lv_obj_update_layout(lv_screen_active());
    require(visibleImage(lv_screen_active(), &image_icon_more) && !visibleImage(lv_screen_active(), &image_icon_back),
            "8 collapses the data and restores the original More button");
    require(ball && lv_obj_get_x(lv_obj_get_parent(ball)) == 75,
            "collapsing returns the original tilt panel to the center");
    require(!visible("TILT") && !visible("ACCEL m/s2") && !visible("GYRO rad/s"),
            "six-axis mode adds no new titles or data styles");
    require(!visible("Compass not supported"), "the entry notice stays dismissed during live updates");
    unsetenv("COMPASS_MOCK_NO_MAGNETOMETER");
}

lv_obj_t* tiltBall(lv_obj_t* root)
{
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) {
        return nullptr;
    }
    if (lv_color_eq(lv_obj_get_style_bg_color(root, LV_PART_MAIN), lv_color_hex(0x303030)) &&
        lv_obj_get_style_radius(root, LV_PART_MAIN) == LV_RADIUS_CIRCLE) {
        return root;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        if (auto* ball = tiltBall(lv_obj_get_child(root, i))) {
            return ball;
        }
    }
    return nullptr;
}

void testTiltGeometry()
{
    setenv("COMPASS_MOCK_NO_MAGNETOMETER", "1", 1);
    compass::CompassRouter router;
    compass::CompassModel model;
    compass::CompassViewModel vm(router, model);
    compass::CompassView view(vm);
    vm.onEnter();
    vm.onKey('8');
    view.onEnter(lv_screen_active());
    compass::CompassSample sample = model.sample().get();
    // Exercise the largest animated bubble at all four travel limits.
    sample.accel = {19.62f, 0.0f, 0.0f};
    uint32_t now = 100;
    for (float x : {-1.0f, 1.0f}) {
        for (float y : {-1.0f, 1.0f}) {
            sample.bubbleX = x;
            sample.bubbleY = y;
            model.sample().set(sample);
            view.tick(now);
            animation_tick += 1000;
            view.tick(now + 1);
            lv_obj_update_layout(lv_screen_active());
            auto* ball = tiltBall(lv_screen_active());
            require(ball && lv_obj_get_width(ball) == 87 && lv_obj_get_height(ball) == 87,
                    "six-axis mode scales the original bubble size animation by 1.5");
            if (ball) {
                lv_area_t area;
                lv_obj_get_coords(ball, &area);
                require(area.x1 >= 9 && area.x2 <= 175 && area.y1 >= 2 && area.y2 <= 168,
                        "enlarged tilt stays within the screen and clear of sensor data at every travel limit");
                const float center_x = (area.x1 + area.x2 + 1) / 2.0f;
                const float center_y = (area.y1 + area.y2 + 1) / 2.0f;
                require(std::abs(center_x - (92 + 39 * x)) <= 1 && std::abs(center_y - (85 + 39 * y)) <= 1,
                        "six-axis tilt scales the original travel around the same center");
            }
            now += 100;
        }
    }
    sample.magnetometerAvailable = true;
    model.sample().set(sample);
    view.tick(now);
    lv_obj_update_layout(lv_screen_active());
    require(visible("N"), "magnetic recovery restores the compass directions");
    auto* ball = tiltBall(lv_screen_active());
    require(ball && lv_obj_get_width(ball) == 58, "devices with a magnetometer keep the original bubble size");
    unsetenv("COMPASS_MOCK_NO_MAGNETOMETER");
}

void testNoticePreference(const std::filesystem::path& preference)
{
    setenv("COMPASS_MOCK_NO_MAGNETOMETER", "1", 1);
    require(!std::filesystem::exists(preference), "ordinary dismissals do not suppress future entry notices");
    {
        compass::CompassApp app;
        app.start();
        require(visible("Compass not supported"), "entry notice returns on a later launch");
        app.onKey(compass::compass_key::Help);
        require(visible("Compass not supported") && !visible("Compass"), "help does not cover the entry notice");
        app.onKey('7');
        app.onKey('8');
        require(!visible("Compass not supported"), "checking the option and continuing dismisses the notice");
        require(std::filesystem::exists(preference), "do-not-show preference is saved");
    }
    {
        compass::CompassApp app;
        app.start();
        require(!visible("Compass not supported") && tiltBall(lv_screen_active()),
                "do-not-show preference survives an app restart");
        app.onLvglKeyState(LV_KEY_ESC, nullptr, true);
        app.tick(lv_tick_get() + 800);
        require(app.quitRequested(), "normal held-Esc exit remains available after skipping the notice");
    }
    unsetenv("COMPASS_MOCK_NO_MAGNETOMETER");
}

void testMagnetometerLossDuringCalibration(const std::filesystem::path& config)
{
    std::filesystem::remove(config);
    compass::CompassApp app;
    app.start();
    app.onKey('\r');
    require(visible("Rotate through every direction"), "calibration starts with magnetic data");
    setenv("COMPASS_MOCK_NO_MAGNETOMETER", "1", 1);
    app.tick(33);
    require(visible("Compass not supported") && !visible("Rotate through every direction"),
            "magnetic loss during required calibration returns to six-axis data");
    require(!std::filesystem::exists(config), "six-axis data cannot save a magnetic calibration");
    unsetenv("COMPASS_MOCK_NO_MAGNETOMETER");
    app.tick(66);
    require(visible("Calibration required"), "recovered magnetometer still requires valid calibration");
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
    stationary.magnetometerAvailable = true;
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
    const auto preference = std::filesystem::temp_directory_path() / "compass_notice_test" / "skip-notice";
    std::filesystem::remove_all(config);
    std::filesystem::remove_all(preference.parent_path());
    setenv("COMPASS_CALIBRATION_PATH", config.c_str(), 1);
    setenv("COMPASS_PREFERENCES_PATH", preference.c_str(), 1);
    smooth_ui_toolkit::ui_hal::on_get_tick([] { return animation_tick; });
    lv_init();
    auto* display = lv_display_create(320, 170);

    testSixAxisStartup();
    testRequiredStartup();
    {
        std::ofstream file(config);
        file << "invalid calibration\n";
    }
    testRequiredStartup();
    testCompletion(config, false, true);
    testCompletion(config, true, false);
    testSixAxisStartup();
    // Clearing temporary storage on reboot must require calibration again.
    std::filesystem::remove(config);
    testRequiredStartup();
    testLowQualityKeepsCollecting(config);
    testSensorFailureDisplay();
    testMagnetometerLossDuringCalibration(config);
    testTiltGeometry();
    testNoticePreference(preference);

    lv_display_delete(display);
    lv_deinit();
    std::filesystem::remove_all(config);
    std::filesystem::remove_all(preference.parent_path());
    unsetenv("COMPASS_CALIBRATION_PATH");
    unsetenv("COMPASS_PREFERENCES_PATH");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

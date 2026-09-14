#include "views/calibration_view.hpp"
#include "assets/assets.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdio>

namespace compass {

namespace {

constexpr int32_t kProgressTrackWidth  = 126;
constexpr int32_t kProgressTrackHeight = 3;
constexpr uint32_t kTitleColor         = 0xFFFFFF;
constexpr uint32_t kHintColor          = 0xF2F2F2;
constexpr uint32_t kProgressTrackColor = 0x333333;
constexpr uint32_t kProgressFillColor  = 0x53D671;
constexpr const char* kDefaultStatus   = "Rotate through every direction";
constexpr uint32_t kBackgroundColor     = 0x000000;
constexpr uint32_t kDoneBackgroundColor = 0x18A058;
constexpr uint32_t kDoneTrackColor      = 0x126A3F;
constexpr uint32_t kDoneFillColor       = 0xB5FFD0;

}  // namespace

CalibrationView::CalibrationView(CalibrationViewModel& view_model) : _view_model(view_model)
{
}

CalibrationView::~CalibrationView()
{
    onExit();
}

void CalibrationView::onEnter(lv_obj_t* parent)
{
    onExit();

    _root = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent);
    _root->setSize(lv_pct(100), lv_pct(100));
    _root->setBgColor(lv_color_hex(kBackgroundColor));
    _root->setBgOpa(LV_OPA_COVER);
    _root->setBorderWidth(0);
    _root->setPaddingAll(0);
    _root->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _root->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _gesture_image = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Image>(_root->raw_ptr());
    _gesture_image->setSrc(&image_calibration_guesture);
    _gesture_image->align(LV_ALIGN_CENTER, 0, -17 - 12);

    _hint_label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_root->raw_ptr());
    _hint_label->setTextFont(&font_chivo_medium_14);
    _hint_label->setTextColor(lv_color_hex(kHintColor));
    _hint_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _hint_label->setSize(300, 18);
    _hint_label->align(LV_ALIGN_CENTER, 0, 14);

    _auto_check_label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_root->raw_ptr());
    _auto_check_label->setText("Auto-check in 20s");
    _auto_check_label->setTextFont(&font_chivo_mono_medium_12);
    _auto_check_label->setTextColor(lv_color_hex(0x9A9A9A));
    _auto_check_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _auto_check_label->setSize(300, 14);
    _auto_check_label->align(LV_ALIGN_CENTER, 0, 31);

    _progress_track = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_root->raw_ptr());
    _progress_track->setSize(kProgressTrackWidth, kProgressTrackHeight);
    _progress_track->setBgColor(lv_color_hex(kProgressTrackColor));
    _progress_track->setBgOpa(LV_OPA_COVER);
    _progress_track->setBorderWidth(0);
    _progress_track->setOutlineWidth(0);
    _progress_track->setShadowWidth(0);
    _progress_track->setPaddingAll(0);
    _progress_track->setRadius(kProgressTrackHeight / 2);
    _progress_track->align(LV_ALIGN_CENTER, 0, 52 - 12);
    _progress_track->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _progress_fill = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_progress_track->raw_ptr());
    _progress_fill->setSize(0, kProgressTrackHeight);
    _progress_fill->setBgColor(lv_color_hex(kProgressFillColor));
    _progress_fill->setBgOpa(LV_OPA_COVER);
    _progress_fill->setBorderWidth(0);
    _progress_fill->setOutlineWidth(0);
    _progress_fill->setShadowWidth(0);
    _progress_fill->setPaddingAll(0);
    _progress_fill->setRadius(kProgressTrackHeight / 2);
    _progress_fill->setPos(0, 0);
    _progress_fill->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _progress_label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_root->raw_ptr());
    _progress_label->setTextFont(&font_chivo_mono_medium_12);
    _progress_label->setTextColor(lv_color_hex(kHintColor));
    _progress_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _progress_label->setSize(300, 14);
    _progress_label->align(LV_ALIGN_BOTTOM_MID, 0, -25);

    _notice_panel = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_root->raw_ptr());
    _notice_panel->setSize(286, 122);
    _notice_panel->align(LV_ALIGN_CENTER, 0, 0);
    _notice_panel->setBgColor(lv_color_hex(0x101010));
    _notice_panel->setBgOpa(LV_OPA_COVER);
    _notice_panel->setBorderColor(lv_color_hex(0x363636));
    _notice_panel->setBorderWidth(1);
    _notice_panel->setRadius(4);
    _notice_panel->setPaddingAll(12);
    _notice_panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _notice_title = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_notice_panel->raw_ptr());
    _notice_title->setTextFont(&font_chivo_medium_14);
    _notice_title->setTextColor(lv_color_hex(kTitleColor));
    _notice_title->setWidth(260);
    _notice_title->align(LV_ALIGN_TOP_LEFT, 0, 0);

    _notice_body = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_notice_panel->raw_ptr());
    _notice_body->setTextFont(&font_chivo_medium_14);
    _notice_body->setTextColor(lv_color_hex(kHintColor));
    _notice_body->setWidth(260);
    _notice_body->align(LV_ALIGN_TOP_LEFT, 0, 26);

    _action_label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_root->raw_ptr());
    _action_label->setTextFont(&font_chivo_mono_medium_12);
    _action_label->setTextColor(lv_color_hex(0x9A9A9A));
    _action_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _action_label->setWidth(300);

    _view_model.state().observe(this, onStateChanged);
    _view_model.status().observe(this, onStatusChanged);
    _view_model.progress().observe(this, onProgressChanged);
    renderState(_view_model.state().get());
    renderStatus(_view_model.status().get());
    renderProgress(_view_model.progress().get());

    spdlog::info("CalibrationView enter");
}

void CalibrationView::onExit()
{
    _view_model.progress().removeObserver();
    _view_model.status().removeObserver();
    _view_model.state().removeObserver();
    _action_label.reset();
    _notice_body.reset();
    _notice_title.reset();
    _notice_panel.reset();
    _progress_label.reset();
    _auto_check_label.reset();
    _progress_fill.reset();
    _progress_track.reset();
    _hint_label.reset();
    _gesture_image.reset();
    _root.reset();
}

void CalibrationView::tick(uint32_t nowMs)
{
    (void)nowMs;
}

void CalibrationView::renderState(CalibrationState state)
{
    if (!_root || !_hint_label || !_progress_track || !_progress_fill) {
        return;
    }

    const bool first_use = state == CalibrationState::Idle && _view_model.required();
    const bool done = state == CalibrationState::Done;
    _notice_panel->setHidden(!first_use && !done);
    _gesture_image->setHidden(first_use || done);
    _hint_label->setHidden(first_use || done);
    _progress_track->setHidden(first_use || done);
    _progress_label->setHidden(state != CalibrationState::Running);
    _auto_check_label->setHidden(state != CalibrationState::Running);
    _action_label->align(LV_ALIGN_BOTTOM_MID, 0, first_use || done ? -38 : -8);

    _root->setBgColor(lv_color_hex(done ? kDoneBackgroundColor : kBackgroundColor));
    _hint_label->setTextColor(lv_color_hex(done ? 0xFFFFFF : kHintColor));
    _progress_track->setBgColor(lv_color_hex(done ? kDoneTrackColor : kProgressTrackColor));
    _progress_fill->setBgColor(lv_color_hex(done ? kDoneFillColor : kProgressFillColor));

    switch (state) {
        case CalibrationState::Idle:
            _notice_title->setText("Calibration required");
            _notice_title->setTextColor(lv_color_hex(kTitleColor));
            _notice_body->setText("Calibrate before first use.\nKeep away from metal and magnets.");
            _action_label->setText(first_use ? "Enter: start  Esc: exit" : "Enter: start  Esc: back");
            _hint_label->setText("Press Enter to start calibration");
            break;
        case CalibrationState::Running:
            _action_label->setText(_view_model.required() ? "Enter: save  Esc: exit" : "Enter: save  Esc: back");
            renderStatus(_view_model.status().get());
            break;
        case CalibrationState::Done:
            _notice_title->setText("Calibration complete");
            _notice_title->setTextColor(lv_color_hex(kProgressFillColor));
            _notice_body->setText("Calibration saved.\nYour compass is ready to use.");
            _action_label->setText("Enter: return to compass");
            break;
    }
}

void CalibrationView::renderStatus(const std::string& status)
{
    if (!_hint_label || _view_model.state().get() != CalibrationState::Running) {
        return;
    }

    const bool default_status = status.empty() || status == kDefaultStatus;
    _hint_label->setSize(300, default_status ? 18 : 34);
    _auto_check_label->setHidden(!default_status);
    _hint_label->setText(default_status ? kDefaultStatus : status);
}

void CalibrationView::renderProgress(float progress)
{
    if (!_progress_fill) {
        return;
    }

    progress = std::clamp(progress, 0.0f, 1.0f);
    _progress_fill->setWidth(static_cast<int32_t>(progress * kProgressTrackWidth));
    char text[64];
    std::snprintf(text, sizeof(text), "Coverage: %u%%  Samples: %u", static_cast<unsigned>(progress * 100.0f),
                  static_cast<unsigned>(_view_model.sampleCount()));
    _progress_label->setText(text);
}

void CalibrationView::onStateChanged(void* context, const CalibrationState& state)
{
    auto* self = static_cast<CalibrationView*>(context);
    if (self) {
        self->renderState(state);
    }
}

void CalibrationView::onStatusChanged(void* context, const std::string& status)
{
    auto* self = static_cast<CalibrationView*>(context);
    if (self) {
        self->renderStatus(status);
    }
}

void CalibrationView::onProgressChanged(void* context, const float& progress)
{
    auto* self = static_cast<CalibrationView*>(context);
    if (self) {
        self->renderProgress(progress);
    }
}

}  // namespace compass

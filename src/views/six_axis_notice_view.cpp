#include "views/six_axis_notice_view.hpp"
#include "assets/assets.h"
#include "core/compass_types.hpp"
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace compass {
namespace {

std::filesystem::path preferencePath()
{
    if (const char* path = std::getenv("COMPASS_PREFERENCES_PATH"); path && path[0]) {
        return path;
    }
    if (const char* config = std::getenv("XDG_CONFIG_HOME"); config && config[0]) {
        return std::filesystem::path(config) / "Compass" / "skip-six-axis-notice";
    }
    if (const char* home = std::getenv("HOME"); home && home[0]) {
        return std::filesystem::path(home) / ".config" / "Compass" / "skip-six-axis-notice";
    }
    return "/tmp/Compass/skip-six-axis-notice";
}

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color, int x, int y)
{
    auto* result = lv_label_create(parent);
    lv_obj_set_style_text_font(result, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(result, lv_color_hex(color), LV_PART_MAIN);
    lv_label_set_text(result, text);
    lv_obj_set_pos(result, x, y);
    return result;
}

}  // namespace

SixAxisNoticeView::SixAxisNoticeView(lv_obj_t* parent)
{
    std::ifstream file(preferencePath());
    std::string value;
    _skip_notice = (file >> value) && value == "1";

    _overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(_overlay);
    lv_obj_set_size(_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_overlay, LV_OPA_80, LV_PART_MAIN);
    lv_obj_remove_flag(_overlay, LV_OBJ_FLAG_SCROLLABLE);

    auto* panel = lv_obj_create(_overlay);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 292, 150);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x141414), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x3A3A3A), LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 10, LV_PART_MAIN);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    label(panel, "6-AXIS MODE", &font_chivo_mono_medium_12, 0xFED40D, 16, 13);
    label(panel, "Compass not supported", &font_chivo_medium_14, 0xFFFFFF, 16, 36);
    label(panel, "Tilt + accelerometer + gyroscope", &font_chivo_mono_medium_12, 0x9A9A9A, 16, 63);

    _checkbox = lv_checkbox_create(panel);
    lv_checkbox_set_text(_checkbox, "Don't show again");
    lv_obj_set_pos(_checkbox, 16, 87);
    lv_obj_set_style_text_font(_checkbox, &font_chivo_medium_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(_checkbox, lv_color_hex(0xD6D6D6), LV_PART_MAIN);
    lv_obj_set_style_pad_column(_checkbox, 8, LV_PART_MAIN);
    lv_obj_set_style_text_font(_checkbox, &lv_font_montserrat_14, LV_PART_INDICATOR);
    lv_obj_set_style_radius(_checkbox, 3, LV_PART_INDICATOR);
    lv_obj_set_style_border_color(_checkbox, lv_color_hex(0x707070), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_checkbox, lv_color_hex(0x202020), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_checkbox, lv_color_hex(0xFED40D), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(_checkbox, lv_color_hex(0xFED40D), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(_checkbox, lv_color_black(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    label(panel, "7 / Space", &font_chivo_mono_medium_12, 0x737373, 208, 92);
    label(panel, "8 / Enter", &font_chivo_mono_medium_12, 0x9A9A9A, 16, 126);

    auto* button = lv_button_create(panel);
    lv_obj_set_size(button, 86, 25);
    lv_obj_set_pos(button, 190, 116);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xFED40D), LV_PART_MAIN);
    lv_obj_set_style_radius(button, 5, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    auto* action = label(button, "Continue", &font_chivo_medium_14, 0x101010, 0, 0);
    lv_obj_center(action);
    lv_obj_add_event_cb(button, onContinue, LV_EVENT_CLICKED, this);
    lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
}

SixAxisNoticeView::~SixAxisNoticeView()
{
    lv_obj_delete(_overlay);
}

void SixAxisNoticeView::update(bool six_axis_only)
{
    if (!six_axis_only) {
        lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (_shown_this_session || _skip_notice) {
        return;
    }
    _shown_this_session = true;
    lv_obj_remove_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_overlay);
}

bool SixAxisNoticeView::visible() const
{
    return !lv_obj_has_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
}

void SixAxisNoticeView::onKey(uint32_t key)
{
    switch (key) {
        case '7':
        case ' ':
        case compass_key::Up:
        case compass_key::Down:
            if (lv_obj_has_state(_checkbox, LV_STATE_CHECKED)) {
                lv_obj_remove_state(_checkbox, LV_STATE_CHECKED);
            } else {
                lv_obj_add_state(_checkbox, LV_STATE_CHECKED);
            }
            break;
        case '8':
        case '\r':
        case '\n':
        case '\x1b':
            dismiss();
            break;
        default:
            break;
    }
}

void SixAxisNoticeView::dismiss()
{
    if (lv_obj_has_state(_checkbox, LV_STATE_CHECKED)) {
        const auto path = preferencePath();
        std::error_code error;
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path(), error);
        }
        std::ofstream file;
        if (!error) {
            file.open(path);
            file << "1\n";
            file.close();
        }
        if (error || !file) {
            spdlog::warn("Compass: cannot save six-axis notice preference to {}", path.string());
        } else {
            _skip_notice = true;
        }
    }
    lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
}

void SixAxisNoticeView::onContinue(lv_event_t* event)
{
    static_cast<SixAxisNoticeView*>(lv_event_get_user_data(event))->dismiss();
}

}  // namespace compass

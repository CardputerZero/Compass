#include "views/help_view.hpp"

#include "assets/assets.h"

namespace compass {

namespace {

constexpr uint32_t kOverlayColor = 0x000000;
constexpr uint32_t kPanelColor   = 0x101010;
constexpr uint32_t kBorderColor  = 0x363636;
constexpr uint32_t kTitleColor   = 0xFFFFFF;
constexpr uint32_t kBodyColor    = 0xF2F2F2;
constexpr uint32_t kFooterColor  = 0x9A9A9A;

}  // namespace

HelpView::HelpView(lv_obj_t* parent)
{
    if (!parent) {
        return;
    }

    _overlay = lv_obj_create(parent);
    lv_obj_set_size(_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_align(_overlay, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(_overlay, lv_color_hex(kOverlayColor), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_overlay, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* panel = lv_obj_create(_overlay);
    lv_obj_set_size(panel, 286, 122);
    lv_obj_set_align(panel, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(panel, lv_color_hex(kPanelColor), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_hex(kBorderColor), LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_left(panel, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_right(panel, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_top(panel, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(panel, 8, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(panel);
    lv_obj_set_width(title, 258);
    lv_obj_set_style_text_font(title, &font_chivo_medium_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(kTitleColor), LV_PART_MAIN);
    lv_label_set_text(title, "Compass");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* body = lv_label_create(panel);
    lv_obj_set_width(body, 258);
    lv_obj_set_style_text_font(body, &font_chivo_medium_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(body, lv_color_hex(kBodyColor), LV_PART_MAIN);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(body,
                      "Electronic compass and real-time IMU data.\n"
                      "Calibration is required before use.\n\n"
                      "Number key 8: more");
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, 23);

    lv_obj_t* footer = lv_label_create(panel);
    lv_obj_set_width(footer, 258);
    lv_obj_set_style_text_font(footer, &font_chivo_mono_medium_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(footer, lv_color_hex(kFooterColor), LV_PART_MAIN);
    lv_obj_set_style_text_align(footer, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_label_set_text(footer, "KEY_HELP / Esc: close");
    lv_obj_align(footer, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
}

HelpView::~HelpView()
{
    if (_overlay) {
        lv_obj_delete(_overlay);
        _overlay = nullptr;
    }
}

void HelpView::show()
{
    if (!_overlay) {
        return;
    }
    lv_obj_remove_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_overlay);
}

void HelpView::hide()
{
    if (_overlay) {
        lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

void HelpView::toggle()
{
    if (visible()) {
        hide();
    } else {
        show();
    }
}

bool HelpView::visible() const
{
    return _overlay && !lv_obj_has_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace compass

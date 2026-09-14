#include "core/compass_app.hpp"
#include "assets/assets.h"
#include <lvgl.h>
#include <spdlog/spdlog.h>

#if LV_USE_SDL
#include LV_SDL_INCLUDE_PATH
#endif

namespace compass {

namespace {

constexpr uint32_t kEscLongPressMs = 700;

#if LV_USE_SDL
bool sdlKeyHeld(SDL_Scancode scancode)
{
    int key_count      = 0;
    const Uint8* state = SDL_GetKeyboardState(&key_count);
    const int index    = static_cast<int>(scancode);
    return state && index >= 0 && index < key_count && state[index] != 0;
}
#endif

}  // namespace

CompassApp::CompassApp()
    : _compass_vm(_router, _compass_model),
      _calibration_vm(_router, _calibration_model, _compass_model),
      _compass_view(_compass_vm),
      _calibration_view(_calibration_vm),
      _help_view(lv_screen_active()),
      _view_models{&_compass_vm, &_calibration_vm},
      _views{&_compass_view, &_calibration_view}
{
}

CompassApp::~CompassApp()
{
    hideExitHint();
    if (_exit_hint && lv_obj_is_valid(_exit_hint)) {
        lv_obj_delete(_exit_hint);
    }
    _exit_hint = nullptr;
    if (_route_observer_id != 0) {
        _router.currentPage().removeObserver(_route_observer_id);
    }
    if (_input_group) {
#if LV_USE_SDL
        lv_indev_t* indev = lv_indev_get_next(nullptr);
        while (indev) {
            if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
                lv_indev_remove_event_cb_with_user_data(indev, onKeyboardEvent, this);
            }
            indev = lv_indev_get_next(indev);
        }
#endif
        lv_group_del(_input_group);
        _input_group = nullptr;
    }
}

void CompassApp::start()
{
    spdlog::info("CompassApp start");
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, LV_PART_MAIN);
    createExitHint();
    _help_view.hide();
    setupInputGroup();
    if (_calibration_vm.required()) {
        _router.replace(PageId::Calibration);
    }
    _route_observer_id = _router.currentPage().observe(this, onRouteChanged);
    setCurrentPage(_router.page());
}

void CompassApp::onKey(uint32_t key)
{
    if (_calibration_vm.required() && !_help_view.visible()) {
        if (key == '\x1b') {
            _quit_requested = true;
            return;
        }
        if (_calibration_vm.state().get() == CalibrationState::Idle) {
            _calibration_vm.onKey(key);
            return;
        }
    }

    if (key == compass_key::Help) {
        _help_view.toggle();
        return;
    }

    if (_help_view.visible()) {
        if (key == '\x1b') {
            _help_view.hide();
        }
        return;
    }

    if (key == '\x1b') {
        // The home page uses a held Esc for quitting. Other pages retain the
        // usual short-press back behavior.
        if (_router.page() != PageId::Compass && _current_vm) {
            _current_vm->onKey(key);
        } else if (_router.page() == PageId::Compass) {
            showExitHint();
        }
        return;
    }

    if (_current_vm) {
        _current_vm->onKey(key);
    }
}

void CompassApp::onLvglKey(uint32_t lv_key, const char* utf8)
{
    (void)onLvglKeyState(lv_key, utf8, true);
}

bool CompassApp::onLvglKeyState(uint32_t lv_key, const char* utf8, bool pressed)
{
#if LV_USE_SDL
    const bool desktop_help = lv_key == 'h' || lv_key == 'H';
    if (desktop_help) {
        // The SDL keyboard driver emits a synthetic release immediately after
        // text input and does not report SDL_KEYUP through LVGL. Toggle only on
        // the press edge, then clear the latch from tick() once H is released.
        if (!pressed && sdlKeyHeld(SDL_SCANCODE_H)) {
            return true;
        }
        if (pressed && !_help_pressed) {
            onKey(compass_key::Help);
        }
        _help_pressed = pressed;
        return true;
    }
#endif

    if (lv_key == compass_key::Help) {
        if (pressed) {
            onKey(compass_key::Help);
        }
        return true;
    }

    if (lv_key == LV_KEY_ESC) {
        if (pressed && !_esc_pressed) {
            _esc_pressed       = true;
            _esc_pressed_at    = lv_tick_get();
            _esc_long_consumed = false;
            _esc_exit_armed    = _router.page() == PageId::Compass && !_help_view.visible();

            if (_esc_exit_armed) {
                showExitHint();
            }
        } else if (!pressed && _esc_pressed) {
#if LV_USE_SDL
            // LVGL's SDL driver may synthesize a release while the physical
            // key is still down. Keep the long-press edge armed in that case.
            if (sdlKeyHeld(SDL_SCANCODE_ESCAPE)) {
                return true;
            }
#endif
            releaseEscPress();
        }
        return true;
    }

    if (_help_view.visible()) {
        return true;
    }

    if (!pressed) {
        return true;
    }

    if (lv_key == LV_KEY_ENTER) {
        onKey('\r');
        return true;
    }

    switch (lv_key) {
        case LV_KEY_UP:
            onKey(compass_key::Up);
            return true;
        case LV_KEY_DOWN:
            onKey(compass_key::Down);
            return true;
        default:
            break;
    }

    if (utf8 && utf8[0] == ' ') {
        onKey(' ');
        return true;
    }

    if (utf8 && utf8[0] >= '0' && utf8[0] <= '9') {
        onKey(static_cast<uint32_t>(utf8[0]));
        return true;
    }

    // Compass owns the keypad routing. Consume unmapped keys as well so they
    // are not delivered a second time through LVGL's default group handling.
    return true;
}

void CompassApp::tick(uint32_t nowMs)
{
#if LV_USE_SDL
    if (_help_pressed && !sdlKeyHeld(SDL_SCANCODE_H)) {
        _help_pressed = false;
    }
#endif
    if (_esc_exit_armed && (_router.page() != PageId::Compass || _help_view.visible())) {
        _esc_exit_armed = false;
        hideExitHint();
    }
    if (_esc_pressed && !_esc_long_consumed && _esc_exit_armed && nowMs - _esc_pressed_at >= kEscLongPressMs) {
        _esc_long_consumed = true;
        hideExitHint();
        spdlog::info("CompassApp: quit requested after holding Esc for {} ms", kEscLongPressMs);
        _quit_requested = true;
    }
#if LV_USE_SDL
    if (_esc_pressed && !sdlKeyHeld(SDL_SCANCODE_ESCAPE)) {
        releaseEscPress();
    }
#endif
    if (_current_vm) {
        _current_vm->tick(nowMs);
    }
    if (_current_view) {
        _current_view->tick(nowMs);
    }
}

ViewModel* CompassApp::viewModelFor(PageId page)
{
    for (auto* vm : _view_models) {
        if (vm && vm->pageId() == page) {
            return vm;
        }
    }
    return nullptr;
}

View* CompassApp::viewFor(PageId page)
{
    const auto index = static_cast<size_t>(page);
    if (index >= _views.size()) {
        return nullptr;
    }
    return _views[index];
}

void CompassApp::setupInputGroup()
{
    if (_input_group) {
        return;
    }

    _input_group = lv_group_create();

    lv_indev_t* indev = lv_indev_get_next(nullptr);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
            lv_indev_set_group(indev, _input_group);
#if LV_USE_SDL
            lv_indev_add_event_cb(indev, onKeyboardEvent, LV_EVENT_KEY, this);
            lv_indev_add_event_cb(indev, onKeyboardEvent, LV_EVENT_RELEASED, this);
#endif
        }
        indev = lv_indev_get_next(indev);
    }
}

void CompassApp::setCurrentPage(PageId page)
{
    ViewModel* next = viewModelFor(page);
    View* next_view = viewFor(page);
    if (!next || (next == _current_vm && next_view == _current_view)) {
        return;
    }

    if (page != PageId::Compass && _esc_exit_armed) {
        _esc_exit_armed = false;
        hideExitHint();
    }

    if (_current_view) {
        _current_view->onExit();
    }
    if (_current_vm) {
        _current_vm->onExit();
    }
    _current_vm   = next;
    _current_view = next_view;
    spdlog::info("Compass route -> {}", pageIdName(page));
    _current_vm->onEnter();
    if (_current_view) {
        _current_view->onEnter(lv_screen_active());
    }
}

void CompassApp::createExitHint()
{
    if (_exit_hint) {
        return;
    }

    _exit_hint = lv_label_create(lv_layer_top());
    if (!_exit_hint) {
        return;
    }

    lv_label_set_text(_exit_hint, "Hold ESC to exit");
    lv_obj_set_size(_exit_hint, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(_exit_hint, &font_chivo_mono_medium_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(_exit_hint, lv_color_hex(0xFED40D), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_exit_hint, lv_color_hex(0x474747), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_exit_hint, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_exit_hint, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(_exit_hint, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_left(_exit_hint, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(_exit_hint, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(_exit_hint, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(_exit_hint, 5, LV_PART_MAIN);
    lv_obj_set_style_text_align(_exit_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(_exit_hint, LV_ALIGN_BOTTOM_MID, 0, -38);
    lv_obj_clear_flag(_exit_hint, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(_exit_hint, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(_exit_hint, LV_OBJ_FLAG_HIDDEN);
}

void CompassApp::showExitHint()
{
    if (!_exit_hint || !lv_obj_is_valid(_exit_hint)) {
        return;
    }
    lv_obj_remove_flag(_exit_hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_exit_hint);
}

void CompassApp::hideExitHint()
{
    if (_exit_hint && lv_obj_is_valid(_exit_hint)) {
        lv_obj_add_flag(_exit_hint, LV_OBJ_FLAG_HIDDEN);
    }
}

void CompassApp::releaseEscPress()
{
    if (!_esc_pressed) {
        return;
    }

    if (!_esc_long_consumed && (!_esc_exit_armed || _help_view.visible())) {
        onKey('\x1b');
    }
    hideExitHint();
    _esc_pressed       = false;
    _esc_long_consumed = false;
    _esc_exit_armed    = false;
    _esc_pressed_at    = 0;
}

void CompassApp::onRouteChanged(void* context, const PageId& page)
{
    auto* self = static_cast<CompassApp*>(context);
    if (self) {
        self->setCurrentPage(page);
    }
}

void CompassApp::onKeyboardEvent(lv_event_t* event)
{
    auto* self  = static_cast<CompassApp*>(lv_event_get_user_data(event));
    auto* indev = static_cast<lv_indev_t*>(lv_event_get_target(event));
    if (!self || !indev) {
        return;
    }

    const uint32_t key = lv_indev_get_key(indev);
    char utf8[2]       = {0, 0};
    if (key >= 0x20 && key < 0x7f) {
        utf8[0] = static_cast<char>(key);
    }
    const bool pressed =
        lv_event_get_code(event) == LV_EVENT_KEY && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;
    (void)self->onLvglKeyState(key, utf8, pressed);
}

}  // namespace compass

#pragma once

#include "core/compass_router.hpp"
#include "models/calibration_model.hpp"
#include "models/compass_model.hpp"
#include "view_models/calibration_view_model.hpp"
#include "view_models/compass_view_model.hpp"
#include "views/calibration_view.hpp"
#include "views/compass_view.hpp"
#include "views/help_view.hpp"
#include "views/view.hpp"
#include <lvgl.h>
#include <array>

namespace compass {

class CompassApp {
public:
    CompassApp();
    ~CompassApp();

    CompassApp(const CompassApp&)            = delete;
    CompassApp& operator=(const CompassApp&) = delete;

    void start();
    void onKey(uint32_t key);
    void onLvglKey(uint32_t lv_key, const char* utf8);
    bool onLvglKeyState(uint32_t lv_key, const char* utf8, bool pressed);
    void tick(uint32_t nowMs);

    bool quitRequested() const
    {
        return _quit_requested;
    }

private:
    CompassRouter _router;
    CompassModel _compass_model;
    CalibrationModel _calibration_model;
    CompassViewModel _compass_vm;
    CalibrationViewModel _calibration_vm;
    CompassView _compass_view;
    CalibrationView _calibration_view;
    HelpView _help_view;
    lv_obj_t* _exit_hint      = nullptr;
    ViewModel* _current_vm    = nullptr;
    View* _current_view       = nullptr;
    lv_group_t* _input_group  = nullptr;
    size_t _route_observer_id = 0;
    bool _quit_requested      = false;
    bool _esc_pressed         = false;
    bool _esc_long_consumed   = false;
    bool _esc_exit_armed      = false;
    bool _help_pressed        = false;
    uint32_t _esc_pressed_at  = 0;

    std::array<ViewModel*, 2> _view_models;
    std::array<View*, 2> _views;

    ViewModel* viewModelFor(PageId page);
    View* viewFor(PageId page);
    void setupInputGroup();
    void setCurrentPage(PageId page);
    void createExitHint();
    void showExitHint();
    void hideExitHint();
    void releaseEscPress();
    static void onRouteChanged(void* context, const PageId& page);
    static void onKeyboardEvent(lv_event_t* event);
};

}  // namespace compass

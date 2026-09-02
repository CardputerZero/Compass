#pragma once

#include <lvgl.h>

namespace compass {

// Full-screen help overlay shared by the compass and calibration pages.
class HelpView {
public:
    explicit HelpView(lv_obj_t* parent);
    ~HelpView();

    HelpView(const HelpView&)            = delete;
    HelpView& operator=(const HelpView&) = delete;

    void show();
    void hide();
    void toggle();
    bool visible() const;

private:
    lv_obj_t* _overlay = nullptr;
};

}  // namespace compass

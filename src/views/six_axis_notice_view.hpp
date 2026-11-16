#pragma once

#include <lvgl.h>
#include <cstdint>

namespace compass {

class SixAxisNoticeView {
public:
    explicit SixAxisNoticeView(lv_obj_t* parent);
    ~SixAxisNoticeView();

    SixAxisNoticeView(const SixAxisNoticeView&)            = delete;
    SixAxisNoticeView& operator=(const SixAxisNoticeView&) = delete;

    void update(bool six_axis_only);
    void onKey(uint32_t key);
    bool visible() const;

private:
    lv_obj_t* _overlay       = nullptr;
    lv_obj_t* _checkbox      = nullptr;
    bool _shown_this_session = false;
    bool _skip_notice        = false;

    void dismiss();
    static void onContinue(lv_event_t* event);
};

}  // namespace compass

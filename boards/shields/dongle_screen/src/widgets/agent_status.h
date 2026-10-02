#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

#define AGENT_STATUS_WIDTH 280
#define AGENT_STATUS_HEIGHT 240

typedef void (*agent_status_active_cb_t)(bool active);

struct zmk_widget_agent_status
{
    lv_obj_t *obj;
};

int zmk_widget_agent_status_init(struct zmk_widget_agent_status *widget, lv_obj_t *parent,
                                 agent_status_active_cb_t active_cb);
lv_obj_t *zmk_widget_agent_status_obj(struct zmk_widget_agent_status *widget);

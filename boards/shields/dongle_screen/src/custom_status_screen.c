/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "custom_status_screen.h"

#if CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE
#include "widgets/output_status.h"
static struct zmk_widget_output_status output_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_LAYER_ACTIVE
#include "widgets/layer_status.h"
static struct zmk_widget_layer_status layer_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_BATTERY_ACTIVE
#include "widgets/battery_status.h"
static struct zmk_widget_dongle_battery_status dongle_battery_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_WPM_ACTIVE
#include "widgets/wpm_status.h"
static struct zmk_widget_wpm_status wpm_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_MODIFIER_ACTIVE
#include "widgets/mod_status.h"
static struct zmk_widget_mod_status mod_widget;
#endif

#if CONFIG_DONGLE_SCREEN_KEY_ACTIVE
#include "widgets/key_status.h"
static struct zmk_widget_key_status key_widget;
#endif

#if CONFIG_DONGLE_SCREEN_AGENT_ACTIVE
#include "widgets/agent_status.h"
static struct zmk_widget_agent_status agent_status_widget;
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

lv_style_t global_style;

#if CONFIG_DONGLE_SCREEN_AGENT_ACTIVE
static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (hidden)
    {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void agent_active_changed(bool active)
{
#if CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE
    set_hidden(zmk_widget_output_status_obj(&output_status_widget), active);
#endif
#if CONFIG_DONGLE_SCREEN_WPM_ACTIVE
    set_hidden(zmk_widget_wpm_status_obj(&wpm_status_widget), active);
#endif
#if CONFIG_DONGLE_SCREEN_MODIFIER_ACTIVE
    set_hidden(zmk_widget_mod_status_obj(&mod_widget), active);
#endif
#if CONFIG_DONGLE_SCREEN_KEY_ACTIVE
    set_hidden(zmk_widget_key_status_obj(&key_widget), active);
#endif
#if CONFIG_DONGLE_SCREEN_LAYER_ACTIVE
    set_hidden(zmk_widget_layer_status_obj(&layer_status_widget), active);
#endif
#if CONFIG_DONGLE_SCREEN_BATTERY_ACTIVE
    set_hidden(zmk_widget_dongle_battery_status_obj(&dongle_battery_status_widget), active);
#endif
}
#endif

lv_obj_t *zmk_display_status_screen()
{
    lv_obj_t *screen;

    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, 255, LV_PART_MAIN);

    lv_style_init(&global_style);
    // lv_style_set_text_font(&global_style, &lv_font_unscii_8); // ToDo: Font is not recognized
    lv_style_set_text_color(&global_style, lv_color_white());
    lv_style_set_text_letter_space(&global_style, 1);
    lv_style_set_text_line_space(&global_style, 1);
    lv_obj_add_style(screen, &global_style, LV_PART_MAIN);

#if CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE
    zmk_widget_output_status_init(&output_status_widget, screen);
    lv_obj_align(zmk_widget_output_status_obj(&output_status_widget), LV_ALIGN_TOP_MID, 0, 10);
#endif

#if CONFIG_DONGLE_SCREEN_BATTERY_ACTIVE
    zmk_widget_dongle_battery_status_init(&dongle_battery_status_widget, screen);
    lv_obj_align(zmk_widget_dongle_battery_status_obj(&dongle_battery_status_widget), LV_ALIGN_BOTTOM_MID, 0, 0);
#endif

#if CONFIG_DONGLE_SCREEN_WPM_ACTIVE
    zmk_widget_wpm_status_init(&wpm_status_widget, screen);
    lv_obj_align(zmk_widget_wpm_status_obj(&wpm_status_widget), LV_ALIGN_TOP_LEFT, 20, 20);
#endif

#if CONFIG_DONGLE_SCREEN_LAYER_ACTIVE
    zmk_widget_layer_status_init(&layer_status_widget, screen);
    lv_obj_align(zmk_widget_layer_status_obj(&layer_status_widget), LV_ALIGN_CENTER, 0, 0);
#endif

#if CONFIG_DONGLE_SCREEN_MODIFIER_ACTIVE
    zmk_widget_mod_status_init(&mod_widget, screen);
    lv_obj_align(zmk_widget_mod_status_obj(&mod_widget), LV_ALIGN_CENTER, 0, 35);
#endif

#if CONFIG_DONGLE_SCREEN_KEY_ACTIVE
    zmk_widget_key_status_init(&key_widget, screen);
    lv_obj_align(zmk_widget_key_status_obj(&key_widget), LV_ALIGN_CENTER, 0, 65);
#endif

#if CONFIG_DONGLE_SCREEN_AGENT_ACTIVE
    zmk_widget_agent_status_init(&agent_status_widget, screen, agent_active_changed);
    lv_obj_align(zmk_widget_agent_status_obj(&agent_status_widget), LV_ALIGN_TOP_MID, 0, 0);
#endif

    return screen;
}
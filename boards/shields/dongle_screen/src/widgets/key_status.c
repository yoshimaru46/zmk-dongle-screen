#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/hid.h>
#include <lvgl.h>
#include "key_status.h"
#include <fonts.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct key_name_entry
{
    uint32_t usage;
    const char *name;
};

static const struct key_name_entry key_names[] = {
    {HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION, "1"},
    {HID_USAGE_KEY_KEYBOARD_2_AND_AT, "2"},
    {HID_USAGE_KEY_KEYBOARD_3_AND_HASH, "3"},
    {HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR, "4"},
    {HID_USAGE_KEY_KEYBOARD_5_AND_PERCENT, "5"},
    {HID_USAGE_KEY_KEYBOARD_6_AND_CARET, "6"},
    {HID_USAGE_KEY_KEYBOARD_7_AND_AMPERSAND, "7"},
    {HID_USAGE_KEY_KEYBOARD_8_AND_ASTERISK, "8"},
    {HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS, "9"},
    {HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS, "0"},
    {HID_USAGE_KEY_KEYBOARD_RETURN_ENTER, "Ent"},
    {HID_USAGE_KEY_KEYBOARD_ESCAPE, "Esc"},
    {HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE, "BS"},
    {HID_USAGE_KEY_KEYBOARD_TAB, "Tab"},
    {HID_USAGE_KEY_KEYBOARD_SPACEBAR, "Spc"},
    {HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE, "-"},
    {HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS, "="},
    {HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE, "["},
    {HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE, "]"},
    {HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE, "\\"},
    {HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON, ";"},
    {HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE, "'"},
    {HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE, "`"},
    {HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN, ","},
    {HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN, "."},
    {HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK, "/"},
    {HID_USAGE_KEY_KEYBOARD_CAPS_LOCK, "Caps"},
    {HID_USAGE_KEY_KEYBOARD_F1, "F1"},
    {HID_USAGE_KEY_KEYBOARD_F2, "F2"},
    {HID_USAGE_KEY_KEYBOARD_F3, "F3"},
    {HID_USAGE_KEY_KEYBOARD_F4, "F4"},
    {HID_USAGE_KEY_KEYBOARD_F5, "F5"},
    {HID_USAGE_KEY_KEYBOARD_F6, "F6"},
    {HID_USAGE_KEY_KEYBOARD_F7, "F7"},
    {HID_USAGE_KEY_KEYBOARD_F8, "F8"},
    {HID_USAGE_KEY_KEYBOARD_F9, "F9"},
    {HID_USAGE_KEY_KEYBOARD_F10, "F10"},
    {HID_USAGE_KEY_KEYBOARD_F11, "F11"},
    {HID_USAGE_KEY_KEYBOARD_F12, "F12"},
    {HID_USAGE_KEY_KEYBOARD_DELETE_FORWARD, "Del"},
    {HID_USAGE_KEY_KEYBOARD_RIGHTARROW, "Right"},
    {HID_USAGE_KEY_KEYBOARD_LEFTARROW, "Left"},
    {HID_USAGE_KEY_KEYBOARD_DOWNARROW, "Down"},
    {HID_USAGE_KEY_KEYBOARD_UPARROW, "Up"},
};

// HID usage 0x04-0x1D は A-Z が連番で並ぶため専用テーブルは持たず算術変換する
static int append_key_name(char *dst, size_t dst_size, uint32_t usage)
{
    if (usage >= HID_USAGE_KEY_KEYBOARD_A && usage <= HID_USAGE_KEY_KEYBOARD_Z)
    {
        return snprintf(dst, dst_size, "%c", 'A' + (usage - HID_USAGE_KEY_KEYBOARD_A));
    }

    for (size_t i = 0; i < ARRAY_SIZE(key_names); i++)
    {
        if (key_names[i].usage == usage)
        {
            return snprintf(dst, dst_size, "%s", key_names[i].name);
        }
    }

    return 0;
}

static void update_key_status(struct zmk_widget_key_status *widget)
{
    char text[64] = "";
    size_t idx = 0;
    int n = 0;

    for (uint32_t usage = HID_USAGE_KEY_KEYBOARD_A; usage <= ZMK_HID_KEYBOARD_MAX_USAGE; usage++)
    {
        if (idx + 6 >= sizeof(text))
        {
            break;
        }
        if (!zmk_hid_keyboard_is_pressed(usage))
        {
            continue;
        }

        if (n > 0)
        {
            idx += snprintf(&text[idx], sizeof(text) - idx, " ");
        }
        idx += append_key_name(&text[idx], sizeof(text) - idx, usage);
        n++;
    }

    lv_label_set_text(widget->label, n ? text : "");
}

static void key_status_timer_cb(struct k_timer *timer)
{
    struct zmk_widget_key_status *widget = k_timer_user_data_get(timer);
    update_key_status(widget);
}

static struct k_timer key_status_timer;

int zmk_widget_key_status_init(struct zmk_widget_key_status *widget, lv_obj_t *parent)
{
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 220, 30);

    widget->label = lv_label_create(widget->obj);
    lv_obj_align(widget->label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(widget->label, "");
    lv_obj_set_style_text_font(widget->label, &NerdFonts_Regular_20, 0);

    k_timer_init(&key_status_timer, key_status_timer_cb, NULL);
    k_timer_user_data_set(&key_status_timer, widget);
    k_timer_start(&key_status_timer, K_MSEC(100), K_MSEC(100));

    return 0;
}

lv_obj_t *zmk_widget_key_status_obj(struct zmk_widget_key_status *widget)
{
    return widget->obj;
}

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/split/central.h>
#include <raw_hid/events.h>

#include "agent_status.h"
#include "../brightness.h"

/*
 * Raw HID packets (host -> dongle). The host sorts, truncates and formats;
 * the dongle only draws what it is told.
 *
 *   ROW : 0x01 | row | status | state text[6] | name[23]
 *   META: 0x02 | agent count | hidden count | wake
 */
#define CMD_ROW 0x01
#define CMD_META 0x02

// 画面の角が丸いので、上下端はヘッダー・フッターの短い中央寄せテキストだけにする
#define EDGE_MARGIN 8
#define ROW_COUNT 4
#define ROW_TOP 36
#define ROW_HEIGHT 40
#define ROW_PITCH 41
#define ROW_PAD 10
#define STATE_WIDTH 78

#define STATE_OFFSET 3
#define STATE_LEN 6
#define NAME_OFFSET 9
#define NAME_LEN 23

#define OFFLINE_AFTER K_SECONDS(6)
#define FALLBACK_AFTER K_SECONDS(60)

#define BATTERY_COUNT MIN(ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT, 2)

#define COLOR_MUTED 0x9E9E9E
#define COLOR_ONLINE 0x43A047
#define COLOR_OFFLINE 0xE53935

enum agent_state
{
    AGENT_NONE,
    AGENT_IDLE,
    AGENT_WORKING,
    AGENT_BLOCKED,
    AGENT_DONE,
    AGENT_UNKNOWN,
};

struct agent_row
{
    uint8_t status;
    char state[STATE_LEN + 1];
    char name[NAME_LEN + 1];
};

struct agent_table
{
    struct agent_row rows[ROW_COUNT];
    uint8_t count;
    uint8_t hidden;
    bool wake;
};

struct header_state
{
    uint8_t layer;
    uint8_t battery[BATTERY_COUNT];
};

struct row_object
{
    lv_obj_t *obj;
    lv_obj_t *name;
    lv_obj_t *state;
};

static struct k_spinlock lock;
static struct agent_table pending;
static struct agent_table shown;
static struct header_state pending_header;

static struct row_object row_objects[ROW_COUNT];
static lv_obj_t *container;
static lv_obj_t *header_label;
static lv_obj_t *footer_dot;
static lv_obj_t *footer_label;
static agent_status_active_cb_t active_cb;
static bool active;
static bool offline;

// フォントに ASCII 以外のグリフが無い
static void copy_text(char *dst, size_t dst_size, const uint8_t *src, size_t src_len)
{
    size_t n = MIN(dst_size - 1, src_len);
    size_t i;

    for (i = 0; i < n && src[i] != '\0'; i++)
    {
        dst[i] = (src[i] >= 0x20 && src[i] <= 0x7e) ? src[i] : '?';
    }
    dst[i] = '\0';
}

static void set_active(bool value)
{
    if (active == value)
    {
        return;
    }
    active = value;
    if (value)
    {
        lv_obj_clear_flag(container, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);
    }
    if (active_cb != NULL)
    {
        active_cb(value);
    }
}

static void draw_row(struct row_object *row, const struct agent_row *data)
{
    lv_color_t bg;
    lv_color_t fg = lv_color_white();

    if (data->status == AGENT_NONE)
    {
        lv_obj_add_flag(row->obj, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    switch (data->status)
    {
    case AGENT_BLOCKED:
        bg = lv_color_hex(0xD32F2F);
        break;
    case AGENT_DONE:
        bg = lv_color_hex(0x2E7D32);
        break;
    case AGENT_WORKING:
        bg = lv_color_hex(0x1565C0);
        break;
    default:
        bg = lv_color_hex(0x2A2A2A);
        fg = lv_color_hex(COLOR_MUTED);
        break;
    }

    lv_obj_set_style_bg_color(row->obj, bg, 0);
    lv_obj_set_style_text_color(row->obj, fg, 0);
    lv_label_set_text(row->name, data->name);
    lv_label_set_text(row->state, data->state);
    lv_obj_clear_flag(row->obj, LV_OBJ_FLAG_HIDDEN);
}

static void draw_footer(uint8_t hidden)
{
    if (offline)
    {
        lv_label_set_text(footer_label, "herdr offline");
    }
    else if (hidden > 0)
    {
        lv_label_set_text_fmt(footer_label, "herdr online  +%u", hidden);
    }
    else
    {
        lv_label_set_text(footer_label, "herdr online");
    }

    lv_obj_set_style_text_color(footer_dot, lv_color_hex(offline ? COLOR_OFFLINE : COLOR_ONLINE), 0);
}

static void draw_header(struct header_state state)
{
    static const char sides[] = {'L', 'R'};
    char text[48];
    const char *name = zmk_keymap_layer_name(state.layer);
    int idx;

    if (name != NULL)
    {
        idx = snprintf(text, sizeof(text), "%.12s", name);
    }
    else
    {
        idx = snprintf(text, sizeof(text), "%u", state.layer);
    }

    for (int i = 0; i < BATTERY_COUNT && idx < (int)sizeof(text); i++)
    {
        if (state.battery[i] > 0)
        {
            idx += snprintf(&text[idx], sizeof(text) - idx, " " LV_SYMBOL_BULLET " %c %u%%", sides[i],
                            state.battery[i]);
        }
        else
        {
            idx += snprintf(&text[idx], sizeof(text) - idx, " " LV_SYMBOL_BULLET " %c --", sides[i]);
        }
    }

    lv_label_set_text(header_label, text);
}

// 途絶したらまず警告だけ出し、古い一覧を出し続けないよう時間を置いて従来の画面へ戻す
static void host_timeout_work_cb(struct k_work *work)
{
    if (!offline)
    {
        offline = true;
        draw_footer(shown.hidden);
        k_work_reschedule_for_queue(zmk_display_work_q(), k_work_delayable_from_work(work), FALLBACK_AFTER);
    }
    else
    {
        set_active(false);
    }
}

static K_WORK_DELAYABLE_DEFINE(host_timeout_work, host_timeout_work_cb);

static void agent_status_update_work_cb(struct k_work *work)
{
    k_spinlock_key_t key = k_spin_lock(&lock);
    struct agent_table table = pending;
    pending.wake = false;
    k_spin_unlock(&lock, key);

    for (int i = 0; i < ROW_COUNT; i++)
    {
        if (memcmp(&shown.rows[i], &table.rows[i], sizeof(struct agent_row)) != 0)
        {
            draw_row(&row_objects[i], &table.rows[i]);
        }
    }

    if (offline || shown.hidden != table.hidden)
    {
        offline = false;
        draw_footer(table.hidden);
    }

    shown = table;
    set_active(table.count > 0);
    k_work_reschedule_for_queue(zmk_display_work_q(), &host_timeout_work, OFFLINE_AFTER);

#if CONFIG_DONGLE_SCREEN_IDLE_TIMEOUT_S > 0
    if (table.wake)
    {
        brightness_wake_screen_on_reconnect();
    }
#endif
}

static K_WORK_DEFINE(agent_status_update_work, agent_status_update_work_cb);

static void header_update_work_cb(struct k_work *work)
{
    k_spinlock_key_t key = k_spin_lock(&lock);
    struct header_state state = pending_header;
    k_spin_unlock(&lock, key);

    draw_header(state);
}

static K_WORK_DEFINE(header_update_work, header_update_work_cb);

static int agent_status_listener(const zmk_event_t *eh)
{
    const struct raw_hid_received_event *ev = as_raw_hid_received_event(eh);
    if (ev == NULL || ev->length < 3)
    {
        return ZMK_EV_EVENT_BUBBLE;
    }

    const uint8_t *data = ev->data;
    k_spinlock_key_t key = k_spin_lock(&lock);

    switch (data[0])
    {
    case CMD_ROW:
        if (data[1] < ROW_COUNT)
        {
            struct agent_row *row = &pending.rows[data[1]];
            size_t state_len = ev->length > STATE_OFFSET ? ev->length - STATE_OFFSET : 0;
            size_t name_len = ev->length > NAME_OFFSET ? ev->length - NAME_OFFSET : 0;

            // memcmp で差分を取るため末尾まで 0 埋めしておく
            memset(row, 0, sizeof(*row));
            row->status = data[2];
            copy_text(row->state, sizeof(row->state), &data[STATE_OFFSET], MIN(state_len, STATE_LEN));
            copy_text(row->name, sizeof(row->name), &data[NAME_OFFSET], MIN(name_len, NAME_LEN));
        }
        break;
    case CMD_META:
        pending.count = data[1];
        pending.hidden = data[2];
        if (ev->length > 3 && data[3] != 0)
        {
            pending.wake = true;
        }
        break;
    default:
        k_spin_unlock(&lock, key);
        return ZMK_EV_EVENT_BUBBLE;
    }

    k_spin_unlock(&lock, key);

    if (zmk_display_is_initialized())
    {
        k_work_submit_to_queue(zmk_display_work_q(), &agent_status_update_work);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(widget_agent_status, agent_status_listener);
ZMK_SUBSCRIPTION(widget_agent_status, raw_hid_received_event);

static int agent_header_listener(const zmk_event_t *eh)
{
    const struct zmk_peripheral_battery_state_changed *battery = as_zmk_peripheral_battery_state_changed(eh);
    uint8_t layer = zmk_keymap_highest_layer_active();

    k_spinlock_key_t key = k_spin_lock(&lock);
    pending_header.layer = layer;
    if (battery != NULL && battery->source < BATTERY_COUNT)
    {
        pending_header.battery[battery->source] = battery->state_of_charge;
    }
    k_spin_unlock(&lock, key);

    if (zmk_display_is_initialized())
    {
        k_work_submit_to_queue(zmk_display_work_q(), &header_update_work);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(widget_agent_header, agent_header_listener);
ZMK_SUBSCRIPTION(widget_agent_header, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(widget_agent_header, zmk_peripheral_battery_state_changed);

int zmk_widget_agent_status_init(struct zmk_widget_agent_status *widget, lv_obj_t *parent,
                                 agent_status_active_cb_t cb)
{
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, AGENT_STATUS_WIDTH, AGENT_STATUS_HEIGHT);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);

    header_label = lv_label_create(widget->obj);
    lv_obj_align(header_label, LV_ALIGN_TOP_MID, 0, EDGE_MARGIN);

    for (int i = 0; i < ROW_COUNT; i++)
    {
        struct row_object *row = &row_objects[i];

        row->obj = lv_obj_create(widget->obj);
        lv_obj_set_size(row->obj, AGENT_STATUS_WIDTH, ROW_HEIGHT);
        lv_obj_set_pos(row->obj, 0, ROW_TOP + i * ROW_PITCH);
        lv_obj_clear_flag(row->obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(row->obj, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row->obj, 4, 0);
        lv_obj_add_flag(row->obj, LV_OBJ_FLAG_HIDDEN);

        row->name = lv_label_create(row->obj);
        lv_label_set_long_mode(row->name, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_width(row->name, AGENT_STATUS_WIDTH - ROW_PAD * 3 - STATE_WIDTH);
        lv_obj_align(row->name, LV_ALIGN_LEFT_MID, ROW_PAD, 0);
        lv_label_set_text(row->name, "");

        row->state = lv_label_create(row->obj);
        lv_obj_align(row->state, LV_ALIGN_RIGHT_MID, -ROW_PAD, 0);
        lv_label_set_text(row->state, "");
    }

    footer_label = lv_label_create(widget->obj);
    lv_obj_set_style_text_color(footer_label, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_align(footer_label, LV_ALIGN_BOTTOM_MID, 8, -EDGE_MARGIN);

    // 文字数で幅が変わるラベルに追従させるため、子にしてはみ出し表示する
    lv_obj_add_flag(footer_label, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    footer_dot = lv_label_create(footer_label);
    lv_label_set_text(footer_dot, LV_SYMBOL_BULLET);
    lv_obj_align(footer_dot, LV_ALIGN_LEFT_MID, -16, 0);

    active_cb = cb;
    container = widget->obj;
    lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);

    pending_header.layer = zmk_keymap_highest_layer_active();
    for (int i = 0; i < BATTERY_COUNT; i++)
    {
        zmk_split_central_get_peripheral_battery_level(i, &pending_header.battery[i]);
    }
    draw_header(pending_header);
    draw_footer(0);

    return 0;
}

lv_obj_t *zmk_widget_agent_status_obj(struct zmk_widget_agent_status *widget)
{
    return widget->obj;
}

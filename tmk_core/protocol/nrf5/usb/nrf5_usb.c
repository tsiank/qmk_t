#ifdef RAW_ENABLE
#include "via_transport.h"
#endif
#include <stdint.h>
#include "nrf5_usb.h"
#include "ble_service.h"
#include "outputselect.h"
#include "nrf_delay.h"
#include "usb_device_state.h"
#ifdef NRF5_ESB_RX
#    include "esb_packet.h"
#endif
#ifdef MIDI_ENABLE
#    include "midi_transport.h"
#endif
#ifdef MUSIC_STREAM_ENABLE
#    include "music_stream.h"
#endif

#ifdef NKRO_ENABLE
#    include "keycode_config.h"
extern keymap_config_t keymap_config;
STATIC_ASSERT(NKRO_EPSIZE > 1 && NKRO_EPSIZE <= NKRO_REPORT_BITS + 1, "nRF NKRO report size exceeds the QMK payload");
#endif

static uint8_t keyboard_led_stats = 0;

static bool m_report_pending;

static const app_usbd_hid_subclass_desc_t *keyboard_hid_report_desc[] = {&kbd_desc};
//APP_USBD_HID_GENERIC_GLOBAL_DEF(m_app_hid_kbd, HID_KBD_INTERFACE, hid_kbd_user_ev_handler, (NRF_DRV_USBD_EPIN6), keyboard_hid_report_desc, 1, 1, APP_USBD_HID_SUBCLASS_BOOT, APP_USBD_HID_PROTO_KEYBOARD);
APP_USBD_HID_GENERIC_GLOBAL_DEF(m_app_hid_kbd, HID_KBD_INTERFACE, hid_kbd_user_ev_handler, (NRF_DRV_USBD_EPIN6), keyboard_hid_report_desc, 1, 1, APP_USBD_HID_SUBCLASS_NONE, APP_USBD_HID_PROTO_KEYBOARD);  //tsiank modified

#ifdef NKRO_ENABLE
static const app_usbd_hid_subclass_desc_t *keyboard2_hid_report_desc[] = {&kbd2_desc};
APP_USBD_HID_GENERIC_GLOBAL_DEF(m_app_hid_kbd2, HID_KBD2_INTERFACE, hid_kbd_user_ev_handler, (NRF_DRV_USBD_EPIN2), keyboard2_hid_report_desc, 1, 1, APP_USBD_HID_SUBCLASS_NONE, APP_USBD_HID_PROTO_GENERIC);
#endif

#ifdef MOUSE_ENABLE
static const app_usbd_hid_subclass_desc_t *mouse_hid_report_desc[] = {&mouse_desc};
APP_USBD_HID_GENERIC_GLOBAL_DEF(m_app_hid_mouse, HID_MOUSE_INTERFACE, hid_user_ev_handler, (NRF_DRV_USBD_EPIN3), mouse_hid_report_desc, 1, 0, APP_USBD_HID_SUBCLASS_NONE, APP_USBD_HID_PROTO_GENERIC);
#endif

#ifdef EXTRAKEY_ENABLE
static const app_usbd_hid_subclass_desc_t *extra_hid_report_desc[] = {&extrakey_desc};
APP_USBD_HID_GENERIC_GLOBAL_DEF(m_app_hid_extra, HID_EXTRA_INTERFACE, hid_user_ev_handler, (NRF_DRV_USBD_EPIN4), extra_hid_report_desc, 1, 0, APP_USBD_HID_SUBCLASS_NONE, APP_USBD_HID_PROTO_GENERIC);
#endif

#ifdef NRF5_ESB_RX
static const app_usbd_class_inst_t *esb_usb_active;
static uint8_t esb_usb_buffer[NRF5_ESB_REPORT_MAX];
static void esb_usb_done(const app_usbd_class_inst_t *instance) {
    if (instance == esb_usb_active) esb_usb_active = NULL;
}
bool nrf5_usb_esb_report(uint8_t kind, const uint8_t *data, uint8_t length) {
    if (esb_usb_active || !NRF_USBD->ENABLE || app_usbd_core_state_get() != APP_USBD_STATE_Configured) return false;
    const app_usbd_hid_generic_t *hid;
    switch (kind) {
        case NRF5_ESB_KEYBOARD:
            if (length != 8) return true;
            hid = &m_app_hid_kbd;
            break;
        case NRF5_ESB_NKRO:
#    ifdef NKRO_ENABLE
            if (length != 31) return true;
            length = NKRO_EPSIZE;
            hid = &m_app_hid_kbd2;
            break;
#    else
            return true;
#    endif
        case NRF5_ESB_MOUSE:
#    ifdef MOUSE_ENABLE
            if (length != 5) return true;
            hid = &m_app_hid_mouse;
            break;
#    else
            return true;
#    endif
        case NRF5_ESB_EXTRA:
#    ifdef EXTRAKEY_ENABLE
            if (length != 3) return true;
            hid = &m_app_hid_extra;
            break;
#    else
            return true;
#    endif
        default: return true;
    }
    memcpy(esb_usb_buffer, data, length);
    ret_code_t rc = app_usbd_hid_generic_in_report_set(hid, esb_usb_buffer, length);
    if (rc != NRF_SUCCESS) {
        // The SDK may have queued the pointer before transfer setup failed.
        // Abort/clear it before allowing the same DMA buffer to be reused.
        UNUSED_RETURN_VALUE(hid_generic_clear_buffer(app_usbd_hid_generic_class_inst_get(hid)));
        return false;
    }
    esb_usb_active = app_usbd_hid_generic_class_inst_get(hid);
    return true;
}
#endif

#ifdef MUSIC_STREAM_ENABLE
// Vendor collection; 20-byte payload plus explicit report ID on either EP0
// SET_REPORT or the interrupt OUT endpoint. Keyboard LED reports stay separate.
APP_USBD_HID_GENERIC_SUBCLASS_REPORT_DESC(music_desc,
    {0x06, 0x67, 0xFF, 0x09, 0x67, 0xA1, 0x01,
     0x85, MUSIC_USB_REPORT_ID, 0x15, 0x00, 0x26, 0xFF, 0x00,
     0x75, 0x08, 0x95, MUSIC_PACKET_SIZE, 0x09, 0x62, 0x81, 0x02,
     0x95, MUSIC_PACKET_SIZE, 0x09, 0x63, 0x91, 0x02, 0xC0});
static const app_usbd_hid_subclass_desc_t *music_reports[] = {&music_desc};
static void music_usb_event(app_usbd_class_inst_t const *instance, app_usbd_hid_user_event_t event);
APP_USBD_HID_GENERIC_GLOBAL_DEF(m_app_hid_music, 4, music_usb_event,
    (NRF_DRV_USBD_EPIN1, NRF_DRV_USBD_EPOUT1), music_reports, 1,
    MUSIC_PACKET_SIZE, APP_USBD_HID_SUBCLASS_NONE, APP_USBD_HID_PROTO_GENERIC);

#ifdef KEYBOARD_CONTROL_ENABLE
static bool control_busy;
static uint8_t control_report[21];
bool keyboard_control_usb_reply(const uint8_t *data) {
    if (!NRF_USBD->ENABLE) return true;
    if (control_busy) return false;
    control_report[0] = MUSIC_USB_REPORT_ID;
    memcpy(control_report + 1, data, 20);
    ret_code_t result = app_usbd_hid_generic_in_report_set(&m_app_hid_music, control_report, 21);
    if (result == NRF_SUCCESS) control_busy = true;
    return result == NRF_SUCCESS;
}
#endif
static void music_usb_event(app_usbd_class_inst_t const *instance, app_usbd_hid_user_event_t event) {
#ifdef KEYBOARD_CONTROL_ENABLE
    if (event == APP_USBD_HID_USER_EVT_IN_REPORT_DONE) control_busy = false;
#endif
    if (event == APP_USBD_HID_USER_EVT_OUT_REPORT_READY) {
        size_t size;
        const uint8_t *report = app_usbd_hid_generic_out_report_get(
            app_usbd_hid_generic_class_get(instance), &size);
        if (report && size == MUSIC_PACKET_SIZE + 1 && report[0] == MUSIC_USB_REPORT_ID)
            music_stream_receive(report + 1, MUSIC_PACKET_SIZE, MUSIC_SOURCE_USB);
    }
}
#endif

#ifdef RAW_ENABLE
// Standard VIA Raw HID collection, no report ID; EP7 is independent of
// music EP1, MIDI EP5, keyboard EP6/NKRO EP2, mouse EP3 and media EP4.
APP_USBD_HID_GENERIC_SUBCLASS_REPORT_DESC(via_desc,
    {0x06, 0x60, 0xFF, 0x09, 0x61, 0xA1, 0x01,
     0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 32,
     0x09, 0x62, 0x81, 0x02, 0x95, 32, 0x09, 0x63, 0x91, 0x02, 0xC0});
static const app_usbd_hid_subclass_desc_t *via_reports[] = {&via_desc};
static void via_usb_event(app_usbd_class_inst_t const *instance, app_usbd_hid_user_event_t event);
// Keep the already shipped music and MIDI interface numbers unchanged.
#    ifdef MIDI_ENABLE
#        ifdef MUSIC_STREAM_ENABLE
#            define VIA_INTERFACE 7
#        else
#            define VIA_INTERFACE 6
#        endif
#    elif defined(MUSIC_STREAM_ENABLE)
#        define VIA_INTERFACE 5
#    else
#        define VIA_INTERFACE 4
#    endif
APP_USBD_HID_GENERIC_GLOBAL_DEF(m_app_hid_via, VIA_INTERFACE, via_usb_event,
    (NRF_DRV_USBD_EPIN7, NRF_DRV_USBD_EPOUT7), via_reports, 1,
    32, APP_USBD_HID_SUBCLASS_NONE, APP_USBD_HID_PROTO_GENERIC);
static bool via_tx_busy;
static uint8_t via_tx[32];
bool nrf_via_usb_reply(const uint8_t *data) {
    if (!NRF_USBD->ENABLE || via_tx_busy) return false;
    memcpy(via_tx, data, sizeof(via_tx));
    ret_code_t result = app_usbd_hid_generic_in_report_set(&m_app_hid_via, via_tx, sizeof(via_tx));
    if (result == NRF_SUCCESS) via_tx_busy = true;
    return result == NRF_SUCCESS;
}
static void via_usb_event(app_usbd_class_inst_t const *instance, app_usbd_hid_user_event_t event) {
    if (event == APP_USBD_HID_USER_EVT_IN_REPORT_DONE) via_tx_busy = false;
    if (event == APP_USBD_HID_USER_EVT_OUT_REPORT_READY) {
        size_t size;
        const uint8_t *report = app_usbd_hid_generic_out_report_get(app_usbd_hid_generic_class_get(instance), &size);
        nrf_via_usb_receive(report, size);
    }
}
#endif

#define REPORT_QUEUE_LEN 32

static uint8_t kbd_report_queue[REPORT_QUEUE_LEN][KEYBOARD_REPORT_SIZE];
#ifdef NKRO_ENABLE
static uint8_t kbd2_report_queue[REPORT_QUEUE_LEN][NKRO_EPSIZE];
#endif

static volatile uint8_t widx = 0;

static void hid_kbd_process_state(report_keyboard_t *report) {
    if (m_report_pending) return;
    ret_code_t ret;
    
    memcpy(kbd_report_queue[widx], report, KEYBOARD_REPORT_SIZE);

        // uint8_t asdf[8] = {};
        // memcpy(&asdf[0], &report->mods, 8);
        // NRF_LOG_INFO("%d %d %d %d", asdf[widx], asdf[widx], asdf[2], asdf[3]);
        // NRF_LOG_INFO("%d %d %d %d", asdf[4], asdf[5], asdf[6], asdf[7]);
        // ret = app_usbd_hid_generic_in_report_set(&m_app_hid_kbd, &report->mods, 8);
     ret = app_usbd_hid_generic_in_report_set(&m_app_hid_kbd, kbd_report_queue[widx++], KEYBOARD_REPORT_SIZE);
     widx %= REPORT_QUEUE_LEN;
     
    APP_ERROR_CHECK(ret);
}

#ifdef NKRO_ENABLE
static void hid_nkro_process_state(report_nkro_t *report) {
    if (m_report_pending) return;
    ret_code_t ret;
    
    memcpy(kbd2_report_queue[widx], &report->mods, NKRO_EPSIZE);
    ret = app_usbd_hid_generic_in_report_set(&m_app_hid_kbd2, kbd2_report_queue[widx++], NKRO_EPSIZE);
    widx %= REPORT_QUEUE_LEN;

    APP_ERROR_CHECK(ret);
}
#endif


#ifdef MOUSE_ENABLE
static void hid_mouse_process_state(report_mouse_t *report) {
    if (m_report_pending) return;
    ret_code_t ret;

    ret = app_usbd_hid_generic_in_report_set(&m_app_hid_mouse, &report->buttons, 5);
    APP_ERROR_CHECK(ret);
}
#endif

#ifdef EXTRAKEY_ENABLE
#define EXTRA_QUEUE_LENGTH 32
static uint8_t extra_reports[EXTRA_QUEUE_LENGTH][3];
static uint8_t extra_head, extra_count;
static bool extra_active;

static void extra_reset(void) {
    extra_head = extra_count = 0;
    extra_active = false;
}

static void extra_append(uint8_t id, uint16_t value) {
    uint8_t *report = extra_reports[(extra_head + extra_count) % EXTRA_QUEUE_LENGTH];
    report[0] = id; report[1] = value; report[2] = value >> 8;
    extra_count++;
}

void nrf5_usb_extra_task(void) {
    if (m_report_pending || !NRF_USBD->ENABLE || extra_active || !extra_count) return;
    // The SDK queues the pointer, not a copy. Keep the head untouched until
    // IN_REPORT_DONE, and retry BUSY instead of resetting or losing release.
    if (app_usbd_hid_generic_in_report_set(&m_app_hid_extra, extra_reports[extra_head], 3) == NRF_SUCCESS)
        extra_active = true;
}

static void extra_done(void) {
    if (!extra_active) return;
    extra_active = false;
    extra_head = (extra_head + 1) % EXTRA_QUEUE_LENGTH;
    extra_count--;
}

static void hid_extra_process_state(uint8_t report_id, uint16_t data) {
    if (m_report_pending || !NRF_USBD->ENABLE) return;
    if (extra_count == EXTRA_QUEUE_LENGTH) {
        // Preserve DMA's head. Resynchronize both usages before the newest
        // state, so overflowing a fast encoder cannot leave either held down.
        extra_count = extra_active ? 1 : 0;
        extra_append(REPORT_ID_SYSTEM, 0);
        extra_append(REPORT_ID_CONSUMER, 0);
    }
    extra_append(report_id, data);
    nrf5_usb_extra_task();
}
#endif

static void kbd_status(void) {
    size_t         led_stats_size = 0;
    const uint8_t *p_buff;
#ifdef NKRO_ENABLE
    if (keyboard_protocol && keymap_config.nkro) {
        p_buff = app_usbd_hid_generic_out_report_get(&m_app_hid_kbd2, &led_stats_size);
    } else
#endif
    {
        p_buff = app_usbd_hid_generic_out_report_get(&m_app_hid_kbd, &led_stats_size);
    }
    if (p_buff != NULL && led_stats_size >= 2) {
        keyboard_led_stats = p_buff[1];
        usb_device_state_set_leds(keyboard_led_stats);
    }
}

/**
 * @brief Class specific event handler.
 *
 * @param p_inst    Class instance.
 * @param event     Class specific event.
 * */
void hid_kbd_user_ev_handler(app_usbd_class_inst_t const *p_inst, app_usbd_hid_user_event_t event) {
#ifdef NRF5_ESB_RX
    if (event == APP_USBD_HID_USER_EVT_IN_REPORT_DONE) esb_usb_done(p_inst);
    if (event == APP_USBD_HID_USER_EVT_SET_BOOT_PROTO || event == APP_USBD_HID_USER_EVT_SET_REPORT_PROTO) esb_usb_done(p_inst);
#endif
    switch (event) {
        case APP_USBD_HID_USER_EVT_OUT_REPORT_READY: {
            m_report_pending = false;
            kbd_status();
            break;
        }
        case APP_USBD_HID_USER_EVT_IN_REPORT_DONE: {
            m_report_pending = false;
            // hid_generic_mouse_process_state();
            // bsp_board_led_invert(LED_HID_REP_IN);
            break;
        }
        case APP_USBD_HID_USER_EVT_SET_BOOT_PROTO: {
            keyboard_protocol = 0;
            usb_device_state_set_protocol(USB_PROTOCOL_BOOT);
            UNUSED_RETURN_VALUE(hid_generic_clear_buffer(p_inst));
            NRF_LOG_INFO("SET_BOOT_PROTO");
            break;
        }
        case APP_USBD_HID_USER_EVT_SET_REPORT_PROTO: {
            keyboard_protocol = 1;
            usb_device_state_set_protocol(USB_PROTOCOL_REPORT);
            UNUSED_RETURN_VALUE(hid_generic_clear_buffer(p_inst));
            NRF_LOG_INFO("SET_REPORT_PROTO");
            break;
        }
        default:
            break;
    }
}

void hid_user_ev_handler(app_usbd_class_inst_t const *p_inst, app_usbd_hid_user_event_t event) {
#ifdef NRF5_ESB_RX
    if (event == APP_USBD_HID_USER_EVT_IN_REPORT_DONE) esb_usb_done(p_inst);
    if (event == APP_USBD_HID_USER_EVT_SET_BOOT_PROTO || event == APP_USBD_HID_USER_EVT_SET_REPORT_PROTO) esb_usb_done(p_inst);
#endif
    switch (event) {
        case APP_USBD_HID_USER_EVT_OUT_REPORT_READY: {
            m_report_pending = false;
            /* No output report defined for this example.*/
            ASSERT(0);
            break;
        }
        case APP_USBD_HID_USER_EVT_IN_REPORT_DONE: {
            m_report_pending = false;
#ifdef EXTRAKEY_ENABLE
            if (p_inst == app_usbd_hid_generic_class_inst_get(&m_app_hid_extra)) extra_done();
#endif
            // hid_generic_mouse_process_state();
            // bsp_board_led_invert(LED_HID_REP_IN);
            break;
        }
        case APP_USBD_HID_USER_EVT_SET_BOOT_PROTO: {
            UNUSED_RETURN_VALUE(hid_generic_clear_buffer(p_inst));
            NRF_LOG_INFO("SET_BOOT_PROTO");
            break;
        }
        case APP_USBD_HID_USER_EVT_SET_REPORT_PROTO: {
            UNUSED_RETURN_VALUE(hid_generic_clear_buffer(p_inst));
            NRF_LOG_INFO("SET_REPORT_PROTO");
            break;
        }
        default:
            break;
    }
}

/**
 * @brief USBD library specific event handler.
 *
 * @param event     USBD library event.
 * */
static void usbd_user_ev_handler(app_usbd_event_type_t event) {
    switch (event) {
        case APP_USBD_EVT_DRV_SOF:
            break;
        case APP_USBD_EVT_DRV_RESET:
#ifdef NRF5_ESB_RX
            esb_usb_active = NULL;
#endif
#ifdef EXTRAKEY_ENABLE
            extra_reset();
#endif
#ifdef RAW_ENABLE
            via_tx_busy = false;
            nrf_via_disconnect(NRF_VIA_USB);
#endif
#ifdef MUSIC_STREAM_ENABLE
            music_stream_disconnect(MUSIC_SOURCE_USB);
#endif
            m_report_pending = false;
            keyboard_protocol = 1;
            usb_device_state_set_protocol(USB_PROTOCOL_REPORT);
#ifdef KEYBOARD_CONTROL_ENABLE
            control_busy = false;
#endif
            usb_device_state_set_reset();
            break;
        case APP_USBD_EVT_DRV_SUSPEND:  // 3
#ifdef MUSIC_STREAM_ENABLE
            music_stream_disconnect(MUSIC_SOURCE_USB);
#endif
            m_report_pending = false;
            usb_device_state_set_suspend(false, 0);
            app_usbd_suspend_req();  // Allow the library to put the peripheral into sleep mode
            // bsp_board_leds_off();
            break;
        case APP_USBD_EVT_DRV_RESUME:  // 4
            m_report_pending = false;
            usb_device_state_set_resume(app_usbd_core_state_get() == APP_USBD_STATE_Configured, 1);
            // bsp_board_led_on(LED_USB_START);
            kbd_status(); /* Restore LED state - during SUSPEND all LEDS are turned off */
            break;
        case APP_USBD_EVT_STARTED:  // 2
            m_report_pending = false;
            // bsp_board_led_on(LED_USB_START);
            break;
        case APP_USBD_EVT_STOPPED:
#ifdef NRF5_ESB_RX
            esb_usb_active = NULL;
#endif
            app_usbd_disable();
#ifdef EXTRAKEY_ENABLE
            extra_reset();
#endif
            // bsp_board_leds_off();
            break;
        case APP_USBD_EVT_POWER_DETECTED:  // 0
            NRF_LOG_INFO("USB power detected");
            if (!nrf_drv_usbd_is_enabled()) {
                app_usbd_enable();
            }
#ifndef NRF5_ESB_TX
            ble_disconnect();
#endif
            set_output(OUTPUT_USB);
            break;
        case APP_USBD_EVT_POWER_REMOVED:
#ifdef RAW_ENABLE
            via_tx_busy = false;
            nrf_via_disconnect(NRF_VIA_USB);
#endif
#ifdef MUSIC_STREAM_ENABLE
            music_stream_disconnect(MUSIC_SOURCE_USB);
#endif
            NRF_LOG_INFO("USB power removed");
#ifdef KEYBOARD_CONTROL_ENABLE
            control_busy = false;
#endif
            usb_device_state_set_reset();
            app_usbd_stop();
            advertising_start(false);
            break;
        case APP_USBD_EVT_POWER_READY:  // 1
            NRF_LOG_INFO("USB ready");
            app_usbd_start();
            break;
        default:
            break;
    }
}

#ifdef MOUSE_ENABLE
static ret_code_t idle_handle(app_usbd_class_inst_t const *p_inst, uint8_t report_id) {
    switch (report_id) {
        case 0: {
            uint8_t report[] = {0xBE, 0xEF};
            return app_usbd_hid_generic_idle_report_set(&m_app_hid_mouse, report, sizeof(report));
        }
        default:
            return NRF_ERROR_NOT_SUPPORTED;
    }
}
#endif

static uint8_t keyboard_leds(void);
static void    send_keyboard(report_keyboard_t *report);
static void    send_nkro(report_nkro_t *report);
static void    send_mouse(report_mouse_t *report);
static void send_extra(report_extra_t *report);

static host_driver_t driver = {
    .keyboard_leds = keyboard_leds,
    .send_keyboard = send_keyboard,
    .send_nkro = send_nkro,
    .send_mouse = send_mouse,
    .send_extra = send_extra,
#ifdef RAW_ENABLE
    .send_raw_hid = nrf_via_send,
#endif
};

host_driver_t *nrf5_usb_driver(void) { return &driver; }

static uint8_t keyboard_leds(void) { return keyboard_led_stats; }

static void send_keyboard(report_keyboard_t *report) {
    if (NRF_USBD->ENABLE) {
        hid_kbd_process_state(report);
    }
}

static void send_nkro(report_nkro_t *report) {
#ifdef NKRO_ENABLE
    if (NRF_USBD->ENABLE) {
        hid_nkro_process_state(report);
    }
#endif
}

static void send_mouse(report_mouse_t *report) {
#ifdef MOUSE_ENABLE
    if (NRF_USBD->ENABLE) {
        hid_mouse_process_state(report);
    }
#endif
}

static void send_extra(report_extra_t *report) {
#ifdef EXTRAKEY_ENABLE
    if (NRF_USBD->ENABLE) {
        hid_extra_process_state(report->report_id, report->usage);
    }
#endif
}

void usb_keyboard_init(void) {
    ret_code_t                     ret;
    usb_device_state_init();
    static const app_usbd_config_t usbd_config = {.ev_state_proc = usbd_user_ev_handler,
                                                  // chie4 add
                                                  .enable_sof = true};

    ret = app_usbd_init(&usbd_config);
    APP_ERROR_CHECK(ret);

    app_usbd_class_inst_t const *class_inst_kbd;
    class_inst_kbd = app_usbd_hid_generic_class_inst_get(&m_app_hid_kbd);
    ret            = app_usbd_class_append(class_inst_kbd);
    APP_ERROR_CHECK(ret);

#ifdef NKRO_ENABLE
    app_usbd_class_inst_t const *class_inst_kbd2;
    class_inst_kbd2 = app_usbd_hid_generic_class_inst_get(&m_app_hid_kbd2);
    ret             = app_usbd_class_append(class_inst_kbd2);
    APP_ERROR_CHECK(ret);
#endif

#ifdef MOUSE_ENABLE
    app_usbd_class_inst_t const *class_inst_mouse;
    class_inst_mouse = app_usbd_hid_generic_class_inst_get(&m_app_hid_mouse);
    ret              = hid_generic_idle_handler_set(class_inst_mouse, idle_handle);
    ret              = app_usbd_class_append(class_inst_mouse);
    APP_ERROR_CHECK(ret);
#endif

#ifdef EXTRAKEY_ENABLE
    app_usbd_class_inst_t const *class_inst_extra;
    class_inst_extra = app_usbd_hid_generic_class_inst_get(&m_app_hid_extra);
    ret              = app_usbd_class_append(class_inst_extra);
    APP_ERROR_CHECK(ret);
#endif

#ifdef MUSIC_STREAM_ENABLE
    ret = app_usbd_class_append(app_usbd_hid_generic_class_inst_get(&m_app_hid_music));
    APP_ERROR_CHECK(ret);
#endif

#ifdef MIDI_ENABLE
    nrf_midi_usb_init();
#endif

#ifdef RAW_ENABLE
    ret = app_usbd_class_append(app_usbd_hid_generic_class_inst_get(&m_app_hid_via));
    APP_ERROR_CHECK(ret);
#endif

    NRF_LOG_INFO("USBD HID composite example started.");

    nrf_delay_ms(50);
    ret = app_usbd_power_events_enable();
    APP_ERROR_CHECK(ret);
}

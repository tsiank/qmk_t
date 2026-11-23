// SPDX-License-Identifier: GPL-2.0-or-later
// USB HID bridge and receiver management. Independent of QMK/VIA services.
#include "nrf5_usb.h"
#include "esb_radio.h"
#include "nrf5_esb.h"
#include "esb_packet.h"
#include "esb_pair.h"
#include "esb_settings.h"
#include "bootloader.h"
#include "timer.h"
#include "esb_bridge.h"
#include <string.h>

static void report_event(app_usbd_class_inst_t const *, app_usbd_hid_user_event_t);
static void control_event(app_usbd_class_inst_t const *, app_usbd_hid_user_event_t);
static const app_usbd_hid_subclass_desc_t *kbd_reports[] = {&kbd_desc};
static const app_usbd_hid_subclass_desc_t *nkro_reports[] = {&kbd2_desc};
static const app_usbd_hid_subclass_desc_t *mouse_reports[] = {&mouse_desc};
static const app_usbd_hid_subclass_desc_t *extra_reports[] = {&extrakey_desc};
APP_USBD_HID_GENERIC_GLOBAL_DEF(kbd, 0, report_event, (NRF_DRV_USBD_EPIN1), kbd_reports, 1, 1, APP_USBD_HID_SUBCLASS_NONE, APP_USBD_HID_PROTO_KEYBOARD);
APP_USBD_HID_GENERIC_GLOBAL_DEF(nkro, 1, report_event, (NRF_DRV_USBD_EPIN2), nkro_reports, 1, 1, APP_USBD_HID_SUBCLASS_NONE, APP_USBD_HID_PROTO_GENERIC);
APP_USBD_HID_GENERIC_GLOBAL_DEF(mouse, 2, report_event, (NRF_DRV_USBD_EPIN3), mouse_reports, 1, 0, APP_USBD_HID_SUBCLASS_NONE, APP_USBD_HID_PROTO_GENERIC);
APP_USBD_HID_GENERIC_GLOBAL_DEF(extra, 3, report_event, (NRF_DRV_USBD_EPIN4), extra_reports, 1, 0, APP_USBD_HID_SUBCLASS_NONE, APP_USBD_HID_PROTO_GENERIC);

// Usage FF67:0068 is distinct from both VIA and keyboard music control.
APP_USBD_HID_GENERIC_SUBCLASS_REPORT_DESC(control_desc,
    {0x06,0x67,0xFF,0x09,0x68,0xA1,0x01,0x15,0x00,0x26,0xFF,0x00,
     0x75,0x08,0x95,0x20,0x09,0x01,0x81,0x02,0x95,0x20,0x09,0x02,0x91,0x02,0xC0});
static const app_usbd_hid_subclass_desc_t *control_reports[] = {&control_desc};
APP_USBD_HID_GENERIC_GLOBAL_DEF(control, 4, control_event,
    (NRF_DRV_USBD_EPIN5, NRF_DRV_USBD_EPOUT5), control_reports, 1, 32,
    APP_USBD_HID_SUBCLASS_NONE, APP_USBD_HID_PROTO_GENERIC);

static uint8_t leds, hid_buffers[4][32];
static uint8_t led_report_prefix, led_report_count;
static bool mouse_state_valid;
static uint8_t last_mouse_buttons;
static bool extra_state_valid[2];
static uint16_t last_extra_usage[2];
static const app_usbd_class_inst_t *hid_active;
static uint8_t requests[4][32], request_head, request_count, reply[32];
static bool reply_ready, control_busy, dfu_requested, dfu_armed;
static uint32_t dfu_at;
static bool usb_window_pending;
static uint8_t remote_pending, remote_opcode, remote_sequence, midi_panic;
static uint16_t remote_token;
static uint32_t remote_at;
void nrf_midi_usb_init(void);
void nrf_midi_usb_task(void);
void nrf_midi_usb_panic(void);
bool nrf_midi_usb_send(const uint8_t packet[4]);
void nrf_midi_reset(void) { midi_panic=16; }
void nrf_midi_receive(const uint8_t packet[4]) {
    if(midi_panic || !nrf5_esb_connected())return;
    if((packet[0]>>4)||(packet[0]&15)<2)return;
    if(!nrf5_bridge_submit(NRF5_ESB_DOWN_MIDI,packet,4,false))midi_panic=16;
}
static void remote_error(void) {
    memset(reply,0,32);
    if(remote_pending==2) { reply[0]=0xC7;reply[1]=0xFF;reply[2]=remote_sequence; }
    else reply[0]=0xFF;
    remote_pending=0;reply_ready=true;
}
bool nrf5_bridge_up_deliver(uint8_t channel,uint16_t token,const uint8_t *data,uint8_t length) {
    if(channel==NRF5_BRIDGE_MIDI)return length!=4 || nrf_midi_usb_send(data);
    if(channel==NRF5_BRIDGE_VIA && length==32 && remote_pending==1 && token==remote_token && (data[0]==remote_opcode || data[0]==0xFF)) {
        if(reply_ready)return false;
        memcpy(reply,data,32);reply_ready=true;remote_pending=0;
    } else if(channel==NRF5_BRIDGE_CONTROL && length==20 && remote_pending==2 && data[2]==remote_sequence) {
        if(reply_ready)return false;
        memset(reply,0,32);memcpy(reply,data,20);reply_ready=true;remote_pending=0;
    }
    return true;
}
void nrf5_bridge_link_reset(void) {
    if(remote_pending)remote_error();
    request_head=request_count=0;
    nrf_midi_usb_panic();midi_panic=16;
}

static uint8_t keyboard_leds(void) { return leds; }
void nrf5_esb_led_diagnostics(uint8_t data[3]) {
    data[0] = leds; data[1] = led_report_prefix; data[2] = led_report_count;
}
static host_driver_t driver = {.keyboard_leds = keyboard_leds};
host_driver_t *nrf5_usb_driver(void) { return &driver; }

bool nrf5_usb_esb_report(uint8_t kind, const uint8_t *data, uint8_t length) {
    if (hid_active || app_usbd_core_state_get() != APP_USBD_STATE_Configured) return false;
    const app_usbd_hid_generic_t *hid;
    uint8_t index;
    int8_t extra_slot = -1;
    switch (kind) {
        case NRF5_ESB_KEYBOARD: if (length != 8) return true; hid = &kbd; index = 0; break;
        case NRF5_ESB_NKRO: if (length != 31) return true; hid = &nkro; index = 1; break;
        case NRF5_ESB_MOUSE:
            if (length != 5) return true;
            // RF snapshots recover lost button releases, but forwarding every
            // stationary duplicate makes Windows see input even while idle.
            // Relative motion/scroll must always pass, including repeated deltas.
            if (mouse_state_valid && data[0] == last_mouse_buttons &&
                !(data[1] || data[2] || data[3] || data[4])) return true;
            hid = &mouse; index = 2; break;
        case NRF5_ESB_EXTRA:
            if (length != 3) return true;
            extra_slot = data[0] == REPORT_ID_SYSTEM ? 0 : data[0] == REPORT_ID_CONSUMER ? 1 : -1;
            // Consumer and system collections retain independent states. Even
            // an unchanged released consumer report counts as input on Windows.
            if (extra_slot >= 0 && extra_state_valid[extra_slot] &&
                last_extra_usage[extra_slot] == ((uint16_t)data[1] | (uint16_t)data[2] << 8)) return true;
            hid = &extra; index = 3; break;
        default: return true;
    }
    memcpy(hid_buffers[index], data, length);
    ret_code_t rc = app_usbd_hid_generic_in_report_set(hid, hid_buffers[index], length);
    if (rc != NRF_SUCCESS) {
        UNUSED_RETURN_VALUE(hid_generic_clear_buffer(app_usbd_hid_generic_class_inst_get(hid)));
        return false;
    }
    if (kind == NRF5_ESB_MOUSE) {
        last_mouse_buttons = data[0]; mouse_state_valid = true;
    }
    if (extra_slot >= 0) {
        last_extra_usage[extra_slot] = (uint16_t)data[1] | (uint16_t)data[2] << 8;
        extra_state_valid[extra_slot] = true;
    }
    hid_active = app_usbd_hid_generic_class_inst_get(hid);
    return true;
}

static void report_event(app_usbd_class_inst_t const *instance, app_usbd_hid_user_event_t event) {
    if (event == APP_USBD_HID_USER_EVT_IN_REPORT_DONE && hid_active == instance) hid_active = NULL;
    if (event == APP_USBD_HID_USER_EVT_OUT_REPORT_READY) {
        size_t length;
        const uint8_t *data = app_usbd_hid_generic_out_report_get(app_usbd_hid_generic_class_get(instance), &length);
        if (data && length) { led_report_prefix = data[0]; led_report_count++; }
        // The SDK prepends a synthetic report ID for descriptors without IDs.
        if (data && length == 2) leds = data[1];
    }
    if (event == APP_USBD_HID_USER_EVT_SET_BOOT_PROTO || event == APP_USBD_HID_USER_EVT_SET_REPORT_PROTO) {
        UNUSED_RETURN_VALUE(hid_generic_clear_buffer(instance));
        if (hid_active == instance) hid_active = NULL;
    }
}

static void control_event(app_usbd_class_inst_t const *instance, app_usbd_hid_user_event_t event) {
    if (event == APP_USBD_HID_USER_EVT_IN_REPORT_DONE) control_busy = false;
    if (event != APP_USBD_HID_USER_EVT_OUT_REPORT_READY || dfu_armed) return;
    size_t length;
    const uint8_t *data = app_usbd_hid_generic_out_report_get(app_usbd_hid_generic_class_get(instance), &length);
    if (!data) return;
    // SDK reports buffer capacity, not transfer length. Interrupt OUT puts
    // 32 payload bytes at [0]; SET_REPORT prepends its synthetic zero ID.
    if (length == 33) { if (data[0] == 0) data++; length = 32; }
    if (length != 32 || request_count == 4) return;
    memcpy(requests[(request_head + request_count) % 4], data, 32);
    request_count++;
}

static void reset_transfers(void) {
    hid_active = NULL;
    control_busy = reply_ready = dfu_requested = dfu_armed = false;
    request_head = request_count = 0;
    leds = 0; mouse_state_valid = false; last_mouse_buttons = 0;
    memset(extra_state_valid, 0, sizeof(extra_state_valid));
    remote_pending=0;nrf5_esb_reset_link();reply_ready=false;
    memset(requests, 0, sizeof(requests));
    memset(reply, 0, sizeof(reply));
}
static void usb_event(app_usbd_event_type_t event) {
    switch (event) {
        case APP_USBD_EVT_DRV_RESET: reset_transfers(); break;
        case APP_USBD_EVT_DRV_SUSPEND: app_usbd_suspend_req(); break;
        case APP_USBD_EVT_STOPPED: reset_transfers(); app_usbd_disable(); break;
        case APP_USBD_EVT_POWER_DETECTED:
            if (!nrf_drv_usbd_is_enabled()) app_usbd_enable();
            break;
        case APP_USBD_EVT_POWER_READY:
            app_usbd_start(); usb_window_pending = true; break;
        case APP_USBD_EVT_POWER_REMOVED:
            usb_window_pending = false; reset_transfers(); app_usbd_stop(); break;
        default: break;
    }
}

void nrf5_dongle_usb_task(void) {
    nrf_midi_usb_task();
    if(midi_panic && nrf5_esb_connected()) {
        const uint8_t packet[4]={0x0B,0xB0|(16-midi_panic),120,0};
        if(nrf5_bridge_submit(NRF5_ESB_DOWN_MIDI,packet,4,false))midi_panic--;
    }
    if(remote_pending && timer_elapsed32(remote_at)>=3000)remote_error();
    // Begin the physical insertion window after enumeration, not before Flash
    // initialization or host enumeration has consumed some of its time. Also
    // works when USB is reinserted without a full MCU power-cycle.
    if (usb_window_pending && app_usbd_core_state_get() == APP_USBD_STATE_Configured) {
        usb_window_pending = false;
        nrf5_pair_usb_window();
    }
    if (dfu_armed) {
        if (!control_busy && !reply_ready && timer_elapsed32(dfu_at) >= 200) bootloader_jump();
        return;
    }
    if (app_usbd_core_state_get() != APP_USBD_STATE_Configured || control_busy) return;
    if (!reply_ready && !remote_pending && request_count) {
        const uint8_t *request=requests[request_head];
        memcpy(reply,request,32);
        bool respond=true;
        if (reply[0] == 0x84) {
            bool enter_dfu = reply[1] == 7;
            if (enter_dfu) reply[1] = 0;
            nrf5_esb_command(reply, true);
            if (enter_dfu && !reply[2]) {
                if (nrf5_pair_busy() || nrf5_esb_settings_pending()) reply[2] = 3;
                else dfu_requested = true;
            }
        } else if (reply[0] == 0x83) nrf5_esb_diagnostics(reply);
        else if (reply[0] == 0x87) nrf5_esb_radio_diagnostics(reply);
        else if(reply[0]==0xC7) {
            bool valid=reply[1]>=1 && reply[1]<=6;
            for(unsigned i=20;i<32;i++)valid &= reply[i]==0;
            if(valid && nrf5_esb_connected()) {
                bool latest=reply[1]==1 && (reply[3]&3);
                if(!nrf5_bridge_submit(NRF5_ESB_DOWN_MUSIC,reply,20,latest))return;
                respond=false;
                if(reply[1]>=3) { remote_pending=2;remote_sequence=reply[2];remote_at=timer_read32(); }
            } else if(reply[1]>=3) { memset(reply,0,32);reply[0]=0xC7;reply[1]=0xFF;reply[2]=request[2]; }
            else respond=false;
        } else if(nrf5_esb_connected()) {
            uint16_t token=remote_token+1;if(!token)token++;
            if(!nrf5_bridge_via_request(token,reply))return;
            remote_token=token;remote_opcode=reply[0];remote_pending=1;remote_at=timer_read32();respond=false;
        } else { memset(reply,0,32);reply[0]=0xFF; }
        memset(requests[request_head],0,32);request_head=(request_head+1)%4;request_count--;
        reply_ready=respond;
    }
    if (reply_ready) {
        ret_code_t rc = app_usbd_hid_generic_in_report_set(&control, reply, 32);
        if (rc == NRF_SUCCESS) {
            control_busy = true; reply_ready = false;
            if (dfu_requested) { dfu_armed = true; dfu_at = timer_read32(); }
        }
        else {
            UNUSED_RETURN_VALUE(hid_generic_clear_buffer(app_usbd_hid_generic_class_inst_get(&control)));
            // Keep the response pending. An endpoint-busy condition must not
            // turn an accepted command into an unexplained client timeout.
        }
    }
}

void nrf5_dongle_usb_init(void) {
    static const app_usbd_config_t config = {.ev_state_proc = usb_event, .enable_sof = false};
    APP_ERROR_CHECK(app_usbd_init(&config));
    APP_ERROR_CHECK(app_usbd_class_append(app_usbd_hid_generic_class_inst_get(&kbd)));
    APP_ERROR_CHECK(app_usbd_class_append(app_usbd_hid_generic_class_inst_get(&nkro)));
    APP_ERROR_CHECK(app_usbd_class_append(app_usbd_hid_generic_class_inst_get(&mouse)));
    APP_ERROR_CHECK(app_usbd_class_append(app_usbd_hid_generic_class_inst_get(&extra)));
    APP_ERROR_CHECK(app_usbd_class_append(app_usbd_hid_generic_class_inst_get(&control)));
    nrf_midi_usb_init();
    APP_ERROR_CHECK(app_usbd_power_events_enable());
}

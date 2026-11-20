#include "natives_internal.h"
#include "../uforth.h"
#include <Arduino.h>

#if !defined(LITE_VERSION) && CONFIG_BT_ENABLED

#include <NimBLEDevice.h>
#include "modules/ble/ble_p4_compat.h"

#ifndef ESP_PWR_LVL_P9
#define ESP_PWR_LVL_P9 9
#endif

// ---------------------------------------------------------------------------
// Operational variables (pointers inside uforth_ram)
// ---------------------------------------------------------------------------
static struct ble_gatt_svc_def *_active_svcs        = nullptr;
static struct ble_gatt_chr_def *_active_chrs        = nullptr;
static uint16_t                *_active_val_handles = nullptr;

static uint16_t _max_svcs = 0;
static uint16_t _max_chrs = 0;
static uint16_t _cur_svc  = 0;
static uint16_t _cur_chr  = 0;

// Event ring buffer inside Forth memory
// Formats:
//   CONN  (1): [ 1 ]
//   DISC  (2): [ 2 ]
//   SUB   (4): [ 4, chr_id, state ]
//   WRITE (3): [ 3, chr_id, len, byte0, byte1, ... ]
static uint8_t          *_evt_buf          = nullptr;
static uint16_t          _evt_buf_size     = 0;
static volatile uint16_t _evt_head         = 0;
static volatile uint16_t _evt_tail         = 0;

// Radio GAP & Connection state
static volatile bool     _connected       = false;
static volatile uint16_t _conn_handle     = BLE_HS_CONN_HANDLE_NONE;
static volatile uint16_t _subscribed_mask = 0;

static char          _device_name[32] = "Bruce-BLE";
static uint16_t      _adv_svc_uuid16  = 0;
static ble_uuid128_t _adv_svc_uuid128 = {};
static bool          _has_adv_svc128  = false;

// ---------------------------------------------------------------------------
// Event Queue Helpers (Sequential byte-stream in uforth_ram)
// ---------------------------------------------------------------------------
static uint16_t evt_bytes_available() {
    if (!_evt_buf || _evt_buf_size == 0) return 0;
    if (_evt_tail >= _evt_head) {
        return (_evt_buf_size - 1) - (_evt_tail - _evt_head);
    }
    return (_evt_head - _evt_tail) - 1;
}

static void evt_write_byte(uint8_t b) {
    _evt_buf[_evt_tail] = b;
    _evt_tail = (_evt_tail + 1) % _evt_buf_size;
}

static void enqueue_event_bytes(const uint8_t *data, uint16_t total_len) {
    if (!_evt_buf || _evt_buf_size == 0 || total_len == 0) return;
    if (evt_bytes_available() < total_len) {
        // Buffer full: drop event to avoid blocking
        return;
    }
    for (uint16_t i = 0; i < total_len; i++) {
        evt_write_byte(data[i]);
    }
}

// ---------------------------------------------------------------------------
// NimBLE C Callbacks (GAP & GATT)
// ---------------------------------------------------------------------------
static void start_low_level_advertising();

static int forth_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                _connected   = true;
                _conn_handle = event->connect.conn_handle;
                uint8_t ev[1] = { 1 }; // CONN
                enqueue_event_bytes(ev, 1);
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            _connected       = false;
            _conn_handle     = BLE_HS_CONN_HANDLE_NONE;
            _subscribed_mask = 0;
            {
                uint8_t ev[1] = { 2 }; // DISC
                enqueue_event_bytes(ev, 1);
            }
            break;

        case BLE_GAP_EVENT_SUBSCRIBE: {
            uint16_t attr_handle = event->subscribe.attr_handle;
            uint8_t cur_notify   = event->subscribe.cur_notify ? 1 : 0;
            if (_active_val_handles) {
                for (uint16_t i = 0; i < _cur_chr; i++) {
                    if (_active_val_handles[i] == attr_handle) {
                        if (cur_notify) {
                            _subscribed_mask |= (1 << i);
                        } else {
                            _subscribed_mask &= ~(1 << i);
                        }
                        uint8_t ev[3] = { 4, (uint8_t)i, cur_notify }; // SUB
                        enqueue_event_bytes(ev, 3);
                        break;
                    }
                }
            }
            break;
        }

        default:
            break;
    }
    return 0;
}

static int forth_gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg) {
    uint8_t chr_id = (uint8_t)(uintptr_t)arg;

    // Incoming write from client
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > 255) len = 255;

        if (evt_bytes_available() >= (3 + len)) {
            evt_write_byte(3);      // Type 3: WRITE
            evt_write_byte(chr_id); // Characteristic index
            evt_write_byte((uint8_t)len);
            for (uint16_t i = 0; i < len; i++) {
                uint8_t b = 0;
                os_mbuf_copydata(ctxt->om, i, 1, &b);
                evt_write_byte(b);
            }
        }
        return 0;
    }

    return 0;
}

// ---------------------------------------------------------------------------
// NimBLE Stack Initialization and Reset
// ---------------------------------------------------------------------------
static void ensure_stack_init() {
    if (!NimBLEDevice::isInitialized()) {
        NimBLEDevice::init(_device_name);
        NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    }
}

static void ble_core_reset() {
    if (!NimBLEDevice::isInitialized()) return;

    ble_gap_adv_stop();
    if (_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        _conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }
    _connected       = false;
    _subscribed_mask = 0;
    _evt_head        = 0;
    _evt_tail        = 0;

    ble_gatts_reset();
}

static void fn_ble_reset() {
    ble_core_reset();
}

// ---------------------------------------------------------------------------
// 128-bit UUID Parsing (Pure C, Little-Endian)
// ---------------------------------------------------------------------------
static uint8_t hex_char_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static void parse_uuid128_little_endian(const char *str, int len, uint8_t *out16) {
    uint8_t raw[16];
    int byte_idx = 0;
    for (int i = 0; i < len && byte_idx < 16; i++) {
        if (str[i] == '-') continue;
        if (i + 1 < len) {
            raw[byte_idx++] = (hex_char_val(str[i]) << 4) | hex_char_val(str[i + 1]);
            i++;
        }
    }
    // Reverse byte order for NimBLE Little-Endian representation
    for (int i = 0; i < 16; i++) {
        out16[i] = raw[15 - i];
    }
}

// uuid: <name> <uuid_str> -> allocates in uforth_ram and defines word <name>
static void fn_uuid_create() {
    char *name = uforth_next_word();
    int name_len = uforth_iram->tibwordlen;
    char name_buf[32];
    if (name_len >= (int)sizeof(name_buf)) name_len = sizeof(name_buf) - 1;
    memcpy(name_buf, name, name_len);
    name_buf[name_len] = '\0';

    char *text = uforth_next_word();
    int text_len = uforth_iram->tibwordlen;

    size_t bytes = sizeof(ble_uuid128_t);
    size_t cells = (bytes + sizeof(DCELL) - 1) / sizeof(DCELL);
    DCELL ram_idx = VAR_ALLOT(cells) - cells; // Real base index of allocated block

    ble_uuid128_t *u128 = (ble_uuid128_t *)&uforth_ram[ram_idx];
    memset(u128, 0, bytes);
    u128->u.type = BLE_UUID_TYPE_128;

    parse_uuid128_little_endian(text, text_len, u128->value);

    make_word(name_buf, name_len);
    dict_append(1); // LIT
    dict_append((CELL)ram_idx);
    dict_append(20); // EXIT
}

// ---------------------------------------------------------------------------
// GATT Table and Event Buffer Allocation in Forth RAM
// ---------------------------------------------------------------------------
// ( #srv #chr -- ) gatt-table: <name>
static void fn_table_create() {
    DCELL max_chrs = dpop();
    DCELL max_svcs = dpop();

    char *name = uforth_next_word();
    int name_len = uforth_iram->tibwordlen;
    char name_buf[32];
    if (name_len >= (int)sizeof(name_buf)) name_len = sizeof(name_buf) - 1;
    memcpy(name_buf, name, name_len);
    name_buf[name_len] = '\0';

    if (max_svcs <= 0) max_svcs = 1;
    if (max_chrs <= 0) max_chrs = 1;

    size_t sz_evt = max_chrs * 64;
    if (sz_evt < 128) sz_evt = 128;

    size_t sz_svcs = (max_svcs + 1) * sizeof(struct ble_gatt_svc_def);
    size_t sz_chrs = (max_chrs + 1) * sizeof(struct ble_gatt_chr_def);
    size_t sz_hdls = max_chrs * sizeof(uint16_t);

    size_t total_bytes = sz_svcs + sz_chrs + sz_hdls + sz_evt;
    size_t cells = (total_bytes + sizeof(DCELL) - 1) / sizeof(DCELL);

    DCELL ram_idx = VAR_ALLOT(cells) - cells; // Real base index of allocated block
    uint8_t *ptr = (uint8_t *)&uforth_ram[ram_idx];
    memset(ptr, 0, total_bytes); // Zero-fill ensures trailing {0} terminators

    _active_svcs        = (struct ble_gatt_svc_def *)ptr; ptr += sz_svcs;
    _active_chrs        = (struct ble_gatt_chr_def *)ptr; ptr += sz_chrs;
    _active_val_handles = (uint16_t *)ptr;                ptr += sz_hdls;

    _evt_buf      = ptr;
    _evt_buf_size = (uint16_t)sz_evt;
    _evt_head     = 0;
    _evt_tail     = 0;

    _max_svcs = (uint16_t)max_svcs;
    _max_chrs = (uint16_t)max_chrs;
    _cur_svc  = 0;
    _cur_chr  = 0;

    make_word(name_buf, name_len);
    dict_append(1); // LIT
    dict_append((CELL)ram_idx); // Pushes base address of services for gatt-reg
    dict_append(20); // EXIT
}

static void fn_gatt_use() {
    DCELL ram_idx = dpop();
    if (ram_idx == 0) return;
    _active_svcs = (struct ble_gatt_svc_def *)&uforth_ram[ram_idx];
}

static void fn_gatt_srv() {
    DCELL uuid_val = dpop();
    if (!_active_svcs || _cur_svc >= _max_svcs) return;

    uint16_t sidx = _cur_svc;
    _active_svcs[sidx].type = BLE_GATT_SVC_TYPE_PRIMARY;
    _active_svcs[sidx].uuid = (const ble_uuid_t *)&uforth_ram[uuid_val];
    _active_svcs[sidx].characteristics = &_active_chrs[_cur_chr];

    _cur_svc++;
}

static void fn_gatt_chr_add() {
    DCELL flags = dpop();
    DCELL uuid_val = dpop();
    if (!_active_chrs || _cur_chr >= _max_chrs) return;

    uint16_t cidx = _cur_chr;

    _active_chrs[cidx].uuid = (const ble_uuid_t *)&uforth_ram[uuid_val];

    ble_gatt_chr_flags nimble_flags = 0;
    if (flags & 1) nimble_flags |= BLE_GATT_CHR_F_READ;
    if (flags & 2) nimble_flags |= (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP);
    if (flags & 4) nimble_flags |= BLE_GATT_CHR_F_NOTIFY;

    _active_chrs[cidx].flags      = nimble_flags;
    _active_chrs[cidx].access_cb  = forth_gatt_access_cb;
    _active_chrs[cidx].arg        = (void *)(uintptr_t)cidx;
    _active_chrs[cidx].val_handle = &_active_val_handles[cidx];

    _cur_chr++;
}

static void fn_gatt_reg() {
    DCELL svcs_addr = dpop();
    if (svcs_addr == 0) return;

    ensure_stack_init();
    ble_core_reset();

    struct ble_gatt_svc_def *svcs = (struct ble_gatt_svc_def *)&uforth_ram[svcs_addr];

    int rc = ble_gatts_count_cfg(svcs);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(svcs);
    }
    if (rc == 0) {
        rc = ble_gatts_start();
    }
    dpush(rc == 0 ? 0 : 1);
}

// ---------------------------------------------------------------------------
// Native Forth Words: Radio GAP & Advertising
// ---------------------------------------------------------------------------
static void fn_ble_name() {
    DCELL str_addr = dpop();
    const char *s = (const char *)&uforth_ram[str_addr];
    strncpy(_device_name, s, sizeof(_device_name) - 1);
    _device_name[sizeof(_device_name) - 1] = '\0';
    if (NimBLEDevice::isInitialized()) {
        NimBLEDevice::setDeviceName(_device_name);
    }
}

static void fn_ble_srv() {
    DCELL uuid_val = dpop();
    const ble_uuid_t *u = (const ble_uuid_t *)&uforth_ram[uuid_val];
    if (u->type == BLE_UUID_TYPE_128) {
        memcpy(&_adv_svc_uuid128, u, sizeof(ble_uuid128_t));
        _has_adv_svc128 = true;
    } else if (u->type == BLE_UUID_TYPE_16) {
        _adv_svc_uuid16 = ((const ble_uuid16_t *)u)->value;
        _has_adv_svc128 = false;
    }
}

static void start_low_level_advertising() {
    ensure_stack_init();

    // 1. GAP Device Name
    NimBLEDevice::setDeviceName(_device_name);

    // 2. Primary Advertising Packet (Flags + primary Service UUID)
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    if (_active_svcs && _active_svcs[0].uuid) {
        fields.uuids128 = (const ble_uuid128_t *)_active_svcs[0].uuid;
        fields.num_uuids128 = 1;
        fields.uuids128_is_complete = 1;
    } else if (_has_adv_svc128) {
        fields.uuids128 = &_adv_svc_uuid128;
        fields.num_uuids128 = 1;
        fields.uuids128_is_complete = 1;
    } else if (_adv_svc_uuid16 != 0) {
        ble_uuid16_t u16;
        u16.u.type = BLE_UUID_TYPE_16;
        u16.value = _adv_svc_uuid16;
        fields.uuids16 = &u16;
        fields.num_uuids16 = 1;
        fields.uuids16_is_complete = 1;
    }
    ble_gap_adv_set_fields(&fields);

    // 3. Scan Response Packet (Full Device Name)
    struct ble_hs_adv_fields rsp_fields;
    memset(&rsp_fields, 0, sizeof(rsp_fields));
    rsp_fields.name = (const uint8_t *)_device_name;
    rsp_fields.name_len = strlen(_device_name);
    rsp_fields.name_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&rsp_fields);

    // 4. Start Advertising
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min  = BLE_GAP_ADV_ITVL_MS(20);
    adv_params.itvl_max  = BLE_GAP_ADV_ITVL_MS(40);

    uint8_t own_addr_type = 0;
    ble_hs_id_infer_auto(0, &own_addr_type);
    ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, forth_gap_event, NULL);
}

static void fn_ble_adv() {
    start_low_level_advertising();
}

static void fn_ble_adv_stop() {
    ble_gap_adv_stop();
}

static void fn_ble_conn_query() {
    dpush(_connected ? 1 : 0);
}

static void fn_ble_sub_query() {
    DCELL chr_idx = dpop();
    if (chr_idx >= 16) {
        dpush(0);
    } else {
        dpush((_subscribed_mask & (1 << chr_idx)) ? 1 : 0);
    }
}

static void fn_ble_notify() {
    DCELL chr_idx  = dpop();
    DCELL len      = dpop();
    DCELL buf_addr = dpop();

    if (!_connected || _conn_handle == BLE_HS_CONN_HANDLE_NONE || !_active_val_handles) return;
    if (chr_idx >= _cur_chr) return;

    uint16_t handle = _active_val_handles[chr_idx];
    if (handle == 0) return;

    struct os_mbuf *om = ble_hs_mbuf_from_flat((const void *)&uforth_ram[buf_addr], (uint16_t)len);
    if (om) {
        ble_gatts_notify_custom(_conn_handle, handle, om);
    }
}

// ---------------------------------------------------------------------------
// Forth Event Queue: ble-evt@ and ble-evt-pop
// ---------------------------------------------------------------------------
// ble-evt@ ( -- addr | 0 )
static void fn_ble_evt_fetch() {
    if (!_evt_buf || _evt_head == _evt_tail) {
        dpush(0);
        return;
    }
    // Return absolute byte offset into uforth_ram
    DCELL ram_byte_offset = (DCELL)((&_evt_buf[_evt_head] - (uint8_t *)uforth_ram));
    dpush(ram_byte_offset);
}

// ble-evt-pop ( -- )
static void fn_ble_evt_pop() {
    if (!_evt_buf || _evt_head == _evt_tail) return;

    uint8_t type = _evt_buf[_evt_head];
    uint16_t advance = 1;

    if (type == 1 || type == 2) {
        advance = 1; // CONN or DISC
    } else if (type == 4) {
        advance = 3; // SUB [ 4, chr_id, state ]
    } else if (type == 3) {
        // WRITE [ 3, chr_id, len, payload... ]
        uint16_t len_idx = (_evt_head + 2) % _evt_buf_size;
        uint8_t len = _evt_buf[len_idx];
        advance = 3 + len;
    }

    _evt_head = (_evt_head + advance) % _evt_buf_size;
}

// ---------------------------------------------------------------------------
// Forth Native Word Registration & Definitions
// ---------------------------------------------------------------------------
void ble_bindings() {
    _active_svcs        = nullptr;
    _active_chrs        = nullptr;
    _active_val_handles = nullptr;
    _evt_buf            = nullptr;
    _evt_buf_size       = 0;
    _evt_head           = 0;
    _evt_tail           = 0;
    _connected          = false;
    _conn_handle        = BLE_HS_CONN_HANDLE_NONE;
    _subscribed_mask    = 0;

    forth_register("uuid:",        fn_uuid_create);
    forth_register("gatt-table:",  fn_table_create);

    forth_register("gatt-use",     fn_gatt_use);
    forth_register("gatt-srv",     fn_gatt_srv);
    forth_register("gatt-chr+",    fn_gatt_chr_add);
    forth_register("gatt-reg",     fn_gatt_reg);

    forth_register("ble-name!",    fn_ble_name);
    forth_register("ble-srv!",     fn_ble_srv);
    forth_register("ble-adv",      fn_ble_adv);
    forth_register("ble-adv-stop", fn_ble_adv_stop);
    forth_register("ble-reset",    fn_ble_reset);
    forth_register("ble-conn?",    fn_ble_conn_query);
    forth_register("ble-sub?",     fn_ble_sub_query);
    forth_register("ble-notify",   fn_ble_notify);

    forth_register("ble-evt@",     fn_ble_evt_fetch);
    forth_register("ble-evt-pop",  fn_ble_evt_pop);
}

void ble_definitions() {
    uforth_interpret("1 constant BLE-R");
    uforth_interpret("2 constant BLE-W");
    uforth_interpret("4 constant BLE-N");

    uforth_interpret("uuid: midi-svc 03b80e5a-ede8-4b33-a751-6ce34ec4c700");
    uforth_interpret("uuid: midi-chr 7772e5db-3868-4112-a1a9-f2669d106bf3");

    uforth_interpret("defer on-ble-conn");
    uforth_interpret("defer on-ble-disc");
    uforth_interpret("defer on-chr-write");
    uforth_interpret("defer on-chr-sub");

    // Flat event dispatcher
    uforth_interpret(": _ble-dispatch");
    uforth_interpret("  ble-evt@ 0 +c@ 1 = if on-ble-conn ble-evt-pop exit then");
    uforth_interpret("  ble-evt@ 0 +c@ 2 = if on-ble-disc ble-evt-pop exit then");
    uforth_interpret("  ble-evt@ 0 +c@ 4 = if ble-evt@ 1 +c@ ble-evt@ 2 +c@ on-chr-sub ble-evt-pop exit then");
    uforth_interpret("  ble-evt@ 0 +c@ 3 = if ble-evt@ 1 +c@ ble-evt@ 3 + ble-evt@ 2 +c@ on-chr-write ble-evt-pop exit then");
    uforth_interpret("  ble-evt-pop ;");

    uforth_interpret(": ble-update begin ble-evt@ while _ble-dispatch repeat ;");
}

#else

static void fn_ble_dummy() {
    dpush(0);
}

void ble_bindings() {
    forth_register("uuid:",        fn_ble_dummy);
    forth_register("gatt-table:",  fn_ble_dummy);
    forth_register("gatt-use",     fn_ble_dummy);
    forth_register("gatt-srv",     fn_ble_dummy);
    forth_register("gatt-chr+",    fn_ble_dummy);
    forth_register("gatt-reg",     fn_ble_dummy);
    forth_register("ble-name!",    fn_ble_dummy);
    forth_register("ble-srv!",     fn_ble_dummy);
    forth_register("ble-adv",      fn_ble_dummy);
    forth_register("ble-adv-stop", fn_ble_dummy);
    forth_register("ble-reset",    fn_ble_dummy);
    forth_register("ble-conn?",    fn_ble_dummy);
    forth_register("ble-sub?",     fn_ble_dummy);
    forth_register("ble-notify",   fn_ble_dummy);
    forth_register("ble-evt@",     fn_ble_dummy);
    forth_register("ble-evt-pop",  fn_ble_dummy);
}

void ble_definitions() {
    uforth_interpret("1 constant BLE-R");
    uforth_interpret("2 constant BLE-W");
    uforth_interpret("4 constant BLE-N");
    uforth_interpret(": midi-svc 0 ;");
    uforth_interpret(": midi-chr 0 ;");
    uforth_interpret("defer on-ble-conn");
    uforth_interpret("defer on-ble-disc");
    uforth_interpret("defer on-chr-write");
    uforth_interpret("defer on-chr-sub");
    uforth_interpret(": ble-update ;");
}

#endif

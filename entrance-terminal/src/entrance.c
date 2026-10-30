/*
 * entrance.c  --  Member 1: Main entrance and access control
 * See entrance.h for the public interface.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "entrance.h"
#include "feedback.h"

static const char *TAG = "ENTRANCE";

// 1 = print [DBG]/[PROBE] lines about the RFID readers (set to 0 once scanning works)
#define RFID_DEBUG 0

// ---------------- Pins ----------------
#define PIN_LED_GREEN     26
#define PIN_LED_RED       27
#define PIN_BUZZER        25
#define PIN_SERVO1_PWM    13   // Entry gate
#define PIN_SERVO2_PWM    14   // Exit gate
#define PIN_SERVO3_PWM    12   // Admin door
#define PIN_EMERGENCY_BTN 32   // push button to GND (internal pull-up)

#define PIN_RFID1_CS      5    // Reader 1: ENTRY
#define PIN_RFID1_RST     4
#define PIN_RFID2_CS      2    // Reader 2: EXIT
#define PIN_RFID2_RST     16

// ---------------- Tag database ----------------
typedef struct {
    const char *card_uid;     // hex string, 8 chars (4-byte), 14 (7-byte) or 20 (10-byte)
    const char *user_name;
    UserRole role;
} RFIDTag;

static const RFIDTag rfid_database[] = {
    {"01020304", "Alice (Student)",   ROLE_STUDENT},
    {"11223344", "Bob (Student)",     ROLE_STUDENT},
    {"AABBCCDD", "Dr. Sarah (Admin)", ROLE_ADMIN}
};
#define DB_SIZE (sizeof(rfid_database) / sizeof(rfid_database[0]))

// ---------------- State ----------------
static bool user_inside[DB_SIZE];
static volatile int check_in_counter  = 0;
static volatile int check_out_counter = 0;
static volatile bool emergency_mode = false;
static volatile bool admin_busy     = false;

static SemaphoreHandle_t entrance_mutex;   // recursive: user_inside, counters, emergency
static SemaphoreHandle_t rfid_bus_mutex;   // serialises MFRC522 SPI transactions

// ====================================================================
// Small helpers
// ====================================================================
int entrance_find_card(const char *uid) {
    for (size_t i = 0; i < DB_SIZE; i++) {
        if (strcmp(uid, rfid_database[i].card_uid) == 0) return (int)i;
    }
    return -1;
}

const char *entrance_tag_name(int idx) {
    return (idx >= 0 && idx < (int)DB_SIZE) ? rfid_database[idx].user_name : "UNKNOWN_USER";
}

bool entrance_tag_is_admin(int idx) {
    return idx >= 0 && idx < (int)DB_SIZE && rfid_database[idx].role == ROLE_ADMIN;
}

int entrance_building_occupancy(void) {
    int n = 0;
    xSemaphoreTakeRecursive(entrance_mutex, portMAX_DELAY);
    for (size_t i = 0; i < DB_SIZE; i++) if (user_inside[i]) n++;
    xSemaphoreGiveRecursive(entrance_mutex);
    return n;
}

void entrance_print_inside(void) {
    xSemaphoreTakeRecursive(entrance_mutex, portMAX_DELAY);
    for (size_t i = 0; i < DB_SIZE; i++) {
        if (user_inside[i]) printf("     * %s\n", rfid_database[i].user_name);
    }
    xSemaphoreGiveRecursive(entrance_mutex);
}

int entrance_checkin_count(void)  { return check_in_counter; }
int entrance_checkout_count(void) { return check_out_counter; }
bool entrance_emergency_active(void) { return emergency_mode; }
void entrance_set_admin_busy(bool busy) { admin_busy = busy; }

static void uid_to_str(const uint8_t *uid, uint8_t len, char *out) {
    for (uint8_t i = 0; i < len; i++) sprintf(out + i * 2, "%02X", uid[i]);
    out[len * 2] = '\0';
}

// Cleans a typed UID: drops spaces/colons/dashes, upper-cases, and checks it is
// hex of a legal UID length (4, 7 or 10 bytes). Returns false if it is not valid.
bool entrance_normalize_uid(char *s) {
    size_t w = 0;
    for (size_t i = 0; s[i]; i++) {
        char ch = s[i];
        if (ch == ' ' || ch == ':' || ch == '-') continue;
        if (ch >= 'a' && ch <= 'f') ch = (char)(ch - 'a' + 'A');
        if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F'))) return false;
        s[w++] = ch;
    }
    s[w] = '\0';
    return (w == 8 || w == 14 || w == 20);
}

// Replay one line of the persistent log after a reboot (called by the storage module)
void entrance_restore_event(const char *uid, bool entered) {
    int idx = entrance_find_card(uid);
    xSemaphoreTakeRecursive(entrance_mutex, portMAX_DELAY);
    if (entered) { check_in_counter++;  if (idx >= 0) user_inside[idx] = true;  }
    else         { check_out_counter++; if (idx >= 0) user_inside[idx] = false; }
    xSemaphoreGiveRecursive(entrance_mutex);
}

void entrance_reset_occupancy(void) {
    xSemaphoreTakeRecursive(entrance_mutex, portMAX_DELAY);
    memset(user_inside, 0, sizeof(user_inside));
    xSemaphoreGiveRecursive(entrance_mutex);
}

void entrance_reset_all(void) {
    xSemaphoreTakeRecursive(entrance_mutex, portMAX_DELAY);
    memset(user_inside, 0, sizeof(user_inside));
    check_in_counter = 0;
    check_out_counter = 0;
    xSemaphoreGiveRecursive(entrance_mutex);
}

// ====================================================================
// Hardware helpers: servos, LEDs, buzzer
// ====================================================================
static void set_gate_angle(ledc_channel_t channel, uint32_t angle) {
    // 50 Hz, 13-bit: 1 ms = 410 counts, 2 ms = 819 counts
    uint32_t duty = 410 + (((819 - 410) * angle) / 180);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

void entrance_signal_denied(void) {
    gpio_set_level(PIN_LED_RED, 1);
    feedback_sound(SND_DENIED);
    vTaskDelay(pdMS_TO_TICKS(800));
    if (!emergency_mode) gpio_set_level(PIN_LED_RED, 0);
}

static void open_gate(ledc_channel_t ch, bool also_admin_door) {
    gpio_set_level(PIN_LED_GREEN, 1);
    set_gate_angle(ch, 90);
    if (also_admin_door) set_gate_angle(LEDC_CHANNEL_2, 90);
    feedback_sound(ch == LEDC_CHANNEL_1 ? SND_EXIT : SND_ENTRY);
    vTaskDelay(pdMS_TO_TICKS(3000));
    // In an emergency the exit gate must stay open
    if (!(emergency_mode && ch == LEDC_CHANNEL_1)) set_gate_angle(ch, 0);
    if (also_admin_door) set_gate_angle(LEDC_CHANNEL_2, 0);
    gpio_set_level(PIN_LED_GREEN, 0);
}

// ====================================================================
// Emergency override: exit gate held open, exits always allowed
// ====================================================================
void entrance_emergency_set(bool on) {
    if (emergency_mode == on) return;
    xSemaphoreTakeRecursive(entrance_mutex, portMAX_DELAY);
    emergency_mode = on;
    if (on) {
        set_gate_angle(LEDC_CHANNEL_1, 90);   // exit gate open
        log_event("SYSTEM", "SYSTEM", "EMERGENCY", "ACTIVATED");
        printf("\n!!! EMERGENCY MODE ACTIVE: exit gate OPEN, entry restricted to admins !!!\n");
    } else {
        set_gate_angle(LEDC_CHANNEL_1, 0);
        gpio_set_level(PIN_LED_RED, 0);
        feedback_tone(0);
        log_event("SYSTEM", "SYSTEM", "EMERGENCY", "CLEARED");
        printf("\n>>> Emergency cleared. Normal operation resumed.\n");
    }
    xSemaphoreGiveRecursive(entrance_mutex);
}

static void task_emergency(void *pv) {
    int pressed = 0;
    bool blink = false;
    while (1) {
        if (gpio_get_level(PIN_EMERGENCY_BTN) == 0) {
            if (++pressed >= 2) { entrance_emergency_set(true); pressed = 0; }
        } else {
            pressed = 0;
        }
        if (emergency_mode) {
            blink = !blink;
            gpio_set_level(PIN_LED_RED, blink);
            feedback_tone(blink ? 1500 : 900);
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

// ====================================================================
// MFRC522 RFID driver (minimal, SPI): reads 4 / 7 / 10 byte UIDs
// ====================================================================
static spi_device_handle_t rfid_dev[2];
static bool rfid_ok[2] = { false, false };
static const int rfid_cs_pin[2]  = { PIN_RFID1_CS,  PIN_RFID2_CS };
static const int rfid_rst_pin[2] = { PIN_RFID1_RST, PIN_RFID2_RST };

#define RC_CommandReg    0x01
#define RC_ComIrqReg     0x04
#define RC_DivIrqReg     0x05
#define RC_ErrorReg      0x06
#define RC_FIFODataReg   0x09
#define RC_FIFOLevelReg  0x0A
#define RC_BitFramingReg 0x0D
#define RC_CollReg       0x0E
#define RC_ModeReg       0x11
#define RC_TxModeReg     0x12
#define RC_RxModeReg     0x13
#define RC_TxControlReg  0x14
#define RC_TxASKReg      0x15
#define RC_CRCResultRegH 0x21
#define RC_CRCResultRegL 0x22
#define RC_ModWidthReg   0x24
#define RC_TModeReg      0x2A
#define RC_TPrescalerReg 0x2B
#define RC_TReloadRegH   0x2C
#define RC_TReloadRegL   0x2D
#define RC_VersionReg    0x37

static uint8_t rc_last_irq[2]  = { 0, 0 };   // last ComIrqReg value seen while waiting for a reply
static uint8_t rc_last_fifo[2] = { 0, 0 };   // FIFO level after the last failed attempt

static uint8_t rc_read(int r, uint8_t reg) {
    uint8_t tx[2] = { (uint8_t)(((reg << 1) & 0x7E) | 0x80), 0 };
    uint8_t rx[2] = { 0, 0 };
    spi_transaction_t t = { .length = 16, .tx_buffer = tx, .rx_buffer = rx };
    spi_device_polling_transmit(rfid_dev[r], &t);
    return rx[1];
}

static void rc_write(int r, uint8_t reg, uint8_t val) {
    uint8_t tx[2] = { (uint8_t)((reg << 1) & 0x7E), val };
    spi_transaction_t t = { .length = 16, .tx_buffer = tx };
    spi_device_polling_transmit(rfid_dev[r], &t);
}

static void rc_set_bits(int r, uint8_t reg, uint8_t mask)   { rc_write(r, reg, rc_read(r, reg) | mask); }
static void rc_clear_bits(int r, uint8_t reg, uint8_t mask) { rc_write(r, reg, rc_read(r, reg) & (uint8_t)~mask); }

// Burst write: one SPI frame with CS held low (same as the Arduino MFRC522 library does for the FIFO)
static void rc_write_buf(int r, uint8_t reg, const uint8_t *data, uint8_t len) {
    uint8_t tx[1 + 64];
    if (len > 64) len = 64;
    tx[0] = (uint8_t)((reg << 1) & 0x7E);
    memcpy(tx + 1, data, len);
    spi_transaction_t t = { .length = (size_t)(len + 1) * 8, .tx_buffer = tx };
    spi_device_polling_transmit(rfid_dev[r], &t);
}

// Burst read: address byte repeated for every byte, last byte 0 (as in the library)
static void rc_read_buf(int r, uint8_t reg, uint8_t *out, uint8_t len) {
    if (len == 0) return;
    if (len > 64) len = 64;
    uint8_t tx[1 + 64], rx[1 + 64];
    uint8_t addr = (uint8_t)(((reg << 1) & 0x7E) | 0x80);
    for (uint8_t i = 0; i < len; i++) tx[i] = addr;
    tx[len] = 0;
    spi_transaction_t t = { .length = (size_t)(len + 1) * 8, .tx_buffer = tx, .rx_buffer = rx };
    spi_device_polling_transmit(rfid_dev[r], &t);
    memcpy(out, rx + 1, len);
}

static void rfid_init_one(int r) {
    gpio_set_direction(rfid_rst_pin[r], GPIO_MODE_OUTPUT);
    gpio_set_level(rfid_rst_pin[r], 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(rfid_rst_pin[r], 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 1000000,
        .mode = 0,
        .spics_io_num = rfid_cs_pin[r],
        .queue_size = 2,
    };
    if (spi_bus_add_device(SPI2_HOST, &dev_cfg, &rfid_dev[r]) != ESP_OK) {
        ESP_LOGE(TAG, "RFID %d: could not add SPI device", r + 1);
        return;
    }

    rc_write(r, RC_CommandReg, 0x0F);            // soft reset
    vTaskDelay(pdMS_TO_TICKS(50));

    // Same register setup as the standard MFRC522 library (known to work with Wokwi's part)
    rc_write(r, RC_TxModeReg, 0x00);             // 106 kBd, no CRC
    rc_write(r, RC_RxModeReg, 0x00);
    rc_write(r, RC_ModWidthReg, 0x26);
    rc_write(r, RC_TModeReg, 0x80);              // timer auto-start
    rc_write(r, RC_TPrescalerReg, 0xA9);
    rc_write(r, RC_TReloadRegH, 0x03);
    rc_write(r, RC_TReloadRegL, 0xE8);           // ~25 ms timeout
    rc_write(r, RC_TxASKReg, 0x40);
    rc_write(r, RC_ModeReg, 0x3D);
    rc_set_bits(r, RC_TxControlReg, 0x03);       // antenna on

    uint8_t ver = rc_read(r, RC_VersionReg);
    rfid_ok[r] = (ver != 0x00 && ver != 0xFF);
    printf("[RFID %d] Version=0x%02X, %s\n", r + 1, ver, rfid_ok[r] ? "READY" : "SPI ERROR");
    ESP_LOGI(TAG, "RFID reader %d: version 0x%02X (%s)", r + 1, ver,
             rfid_ok[r] ? "OK" : "NOT RESPONDING - keyboard fallback only");
}

static bool rc_transceive(int r, const uint8_t *send, uint8_t slen,
                          uint8_t *recv, uint8_t *rlen, uint8_t last_bits) {
    rc_write(r, RC_CommandReg, 0x00);            // idle
    rc_write(r, RC_ComIrqReg, 0x7F);             // clear IRQs
    rc_write(r, RC_FIFOLevelReg, 0x80);          // flush FIFO
    rc_write_buf(r, RC_FIFODataReg, send, slen); // whole frame in one SPI transaction
    rc_write(r, RC_BitFramingReg, last_bits);
    rc_write(r, RC_CommandReg, 0x0C);            // transceive
    rc_set_bits(r, RC_BitFramingReg, 0x80);      // start send

    bool done = false;
    uint8_t n = 0;
    for (int i = 0; i < 500; i++) {
        n = rc_read(r, RC_ComIrqReg);
        rc_last_irq[r] = n;
        if (n & 0x30) { done = true; break; }    // RxIRq or IdleIRq = reply received
        if (n & 0x01) break;                     // timer timeout = no card
        esp_rom_delay_us(100);
    }
    rc_clear_bits(r, RC_BitFramingReg, 0x80);
    if (!done) {
        rc_last_fifo[r] = rc_read(r, RC_FIFOLevelReg) & 0x7F;
        return false;
    }
    if (rc_read(r, RC_ErrorReg) & 0x13) return false;

    uint8_t cnt = rc_read(r, RC_FIFOLevelReg) & 0x7F;
    if (cnt > *rlen) cnt = *rlen;
    rc_read_buf(r, RC_FIFODataReg, recv, cnt);
    *rlen = cnt;
    return true;
}

// CRC_A using the MFRC522 coprocessor (needed for the SELECT command)
static bool rc_crc(int r, const uint8_t *data, uint8_t len, uint8_t out[2]) {
    rc_write(r, RC_CommandReg, 0x00);
    rc_write(r, RC_DivIrqReg, 0x04);             // clear CRCIRq
    rc_write(r, RC_FIFOLevelReg, 0x80);
    rc_write_buf(r, RC_FIFODataReg, data, len);
    rc_write(r, RC_CommandReg, 0x03);            // CalcCRC
    for (int i = 0; i < 100; i++) {
        if (rc_read(r, RC_DivIrqReg) & 0x04) {
            rc_write(r, RC_CommandReg, 0x00);
            out[0] = rc_read(r, RC_CRCResultRegL);
            out[1] = rc_read(r, RC_CRCResultRegH);
            return true;
        }
        esp_rom_delay_us(100);
    }
    return false;
}

// Reads a card UID with cascade levels: 4-byte, 7-byte and 10-byte UIDs.
// uid must hold 10 bytes. Returns true and sets *uid_len if a card is present.
static bool rfid_read_uid(int r, uint8_t *uid, uint8_t *uid_len) {
    if (!rfid_ok[r]) return false;
    static const uint8_t cl_cmd[3] = { 0x93, 0x95, 0x97 };
    bool ok = false;
    uint8_t total = 0;

    xSemaphoreTake(rfid_bus_mutex, portMAX_DELAY);

    // Same preparation the Arduino library does in PICC_IsNewCardPresent()
    rc_write(r, RC_TxModeReg, 0x00);
    rc_write(r, RC_RxModeReg, 0x00);
    rc_write(r, RC_ModWidthReg, 0x26);
    rc_clear_bits(r, RC_CollReg, 0x80);

    uint8_t reqa = 0x26, atqa[2], alen = 2;
    bool got = rc_transceive(r, &reqa, 1, atqa, &alen, 7);
#if RFID_DEBUG
    static int idle_polls[2] = { 0, 0 };
    if (got) {
        printf("[DBG] R%d: card answered REQA (%d bytes)\n", r + 1, alen);
    } else if (++idle_polls[r] % 20 == 0) {      // about every 3 s per reader
        printf("[PROBE] R%d: no answer to REQA (irq=0x%02X fifo=%d) - no card seen\n",
               r + 1, rc_last_irq[r], rc_last_fifo[r]);
    }
#endif
    if (got && alen == 2) {
        for (int lvl = 0; lvl < 3; lvl++) {
            uint8_t ac[2] = { cl_cmd[lvl], 0x20 }, resp[5], rl = 5;
            if (!rc_transceive(r, ac, 2, resp, &rl, 0) || rl != 5) {
#if RFID_DEBUG
                printf("[DBG] R%d: anticollision failed (%d bytes)\n", r + 1, rl);
#endif
                break;
            }
            if ((resp[0] ^ resp[1] ^ resp[2] ^ resp[3]) != resp[4]) {
#if RFID_DEBUG
                printf("[DBG] R%d: anticollision BCC mismatch\n", r + 1);
#endif
                break;
            }
            bool cascade = (resp[0] == 0x88);        // 0x88 = more UID bytes follow

            // SELECT this level
            uint8_t sel[9] = { cl_cmd[lvl], 0x70, resp[0], resp[1], resp[2], resp[3], resp[4], 0, 0 };
            uint8_t crc[2], sak[3], sl = 3;
            bool sel_ok = rc_crc(r, sel, 7, crc);
            if (sel_ok) {
                sel[7] = crc[0]; sel[8] = crc[1];
                sel_ok = rc_transceive(r, sel, 9, sak, &sl, 0) && sl == 3;
            }
            if (cascade) {
                if (!sel_ok) break;                  // cannot continue to next level
                memcpy(uid + total, resp + 1, 3);
                total += 3;
            } else {
                memcpy(uid + total, resp, 4);
                total += 4;
                ok = true;                           // 4-byte level reached: UID complete
                break;
            }
        }
    }
    xSemaphoreGive(rfid_bus_mutex);
    if (ok) *uid_len = total;
    return ok;
}

// Admin login helper: wait for a card on reader `reader`, keyboard also works
bool entrance_acquire_uid(int reader, char *out, size_t len) {
    printf("(present card to RFID reader %d, or type a UID + Enter; 15 s timeout)\n", reader + 1);
    for (int ms = 0; ms < 15000; ms += 100) {
        uint8_t u[10], n = 0;
        if (rfid_read_uid(reader, u, &n)) {
            uid_to_str(u, n, out);
            printf("[RFID %d] Card detected, UID = %s\n", reader + 1, out);
            return true;
        }
        int c = getchar();
        if (c != EOF && c >= 32 && c < 127) {
            size_t idx = 0;
            out[idx++] = (char)c;
            printf("%c", c); fflush(stdout);
            while (1) {
                int d = getchar();
                if (d == EOF) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
                if (d == '\n' || d == '\r') break;
                if (d >= 32 && d < 127 && idx < len - 1) {
                    out[idx++] = (char)d;
                    printf("%c", d); fflush(stdout);
                }
            }
            out[idx] = '\0';
            printf("\n");
            if (!entrance_normalize_uid(out)) {
                printf("Invalid UID. Type only the card UID in hex (e.g. AABBCCDD). The PIN is asked next.\n");
                return false;
            }
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    printf("\nNo card presented (timeout).\n");
    return false;
}

// ====================================================================
// Access decision (reader 0 = ENTRY, reader 1 = EXIT)
//
// The decision and the log entry are made while holding the mutex.
// The slow part (3 s of servo/LED/buzzer action) happens AFTER the mutex is
// released, so the other reader, the emergency button and the menu are never
// blocked while a door is moving.
// ====================================================================
typedef enum { ACT_NONE, ACT_DENY, ACT_OPEN_ENTRY, ACT_OPEN_ENTRY_ADMIN, ACT_OPEN_EXIT } gate_action_t;

void entrance_process_card(int reader, const char *uid) {
    bool is_entry = (reader == 0);
    const char *action = is_entry ? "ENTRY" : "EXIT";
    gate_action_t act = ACT_NONE;

    xSemaphoreTakeRecursive(entrance_mutex, portMAX_DELAY);
    int idx = entrance_find_card(uid);

    if (idx < 0) {
        printf("[%s] DENIED: card %s not registered.\n", action, uid);
        log_event(uid, "UNKNOWN_USER", action, "DENIED (UNREGISTERED)");
        act = ACT_DENY;
    } else {
        const RFIDTag *tag = &rfid_database[idx];
        // Admins skip the schedule check when ADMIN_BYPASS_SCHEDULE is 1
        bool obeys_schedule = !(ADMIN_BYPASS_SCHEDULE && tag->role == ROLE_ADMIN);
        int now = clock_minutes();

        if (is_entry) {
            if (emergency_mode && tag->role != ROLE_ADMIN) {
                printf("[ENTRY] DENIED: emergency lockdown (%s).\n", tag->user_name);
                log_event(uid, tag->user_name, action, "DENIED (EMERGENCY)");
                act = ACT_DENY;
            } else if (user_inside[idx]) {
                printf("[ENTRY] DENIED: %s is already inside (scan EXIT first).\n", tag->user_name);
                log_event(uid, tag->user_name, action, "DENIED (ALREADY INSIDE)");
                act = ACT_DENY;
            } else if (obeys_schedule && !schedule_is_open()) {
                printf("[ENTRY] DENIED: outside access hours (clock %02d:%02d).\n", now / 60, now % 60);
                log_event(uid, tag->user_name, action, "DENIED (OUT OF HOURS)");
                act = ACT_DENY;
            } else {
                user_inside[idx] = true;
                check_in_counter++;
                int occ = 0;
                for (size_t i = 0; i < DB_SIZE; i++) if (user_inside[i]) occ++;
                printf("[ENTRY] GRANTED: welcome %s | Building occupancy: %d\n", tag->user_name, occ);
                log_event(uid, tag->user_name, action, "GRANTED");
                act = (tag->role == ROLE_ADMIN) ? ACT_OPEN_ENTRY_ADMIN : ACT_OPEN_ENTRY;
            }
        } else {
            if (emergency_mode) {
                // Exit is always allowed in an emergency; the gate is already held open
                if (user_inside[idx]) { user_inside[idx] = false; check_out_counter++; }
                int occ = 0;
                for (size_t i = 0; i < DB_SIZE; i++) if (user_inside[i]) occ++;
                printf("[EXIT] GRANTED (EMERGENCY): %s | Building occupancy: %d\n", tag->user_name, occ);
                log_event(uid, tag->user_name, action, "GRANTED (EMERGENCY)");
            } else if (!user_inside[idx]) {
                printf("[EXIT] DENIED: %s is not inside (no matching entry).\n", tag->user_name);
                log_event(uid, tag->user_name, action, "DENIED (NOT INSIDE)");
                act = ACT_DENY;
            } else if (obeys_schedule && !schedule_is_open()) {
                // Brief section 2.6: nobody may leave outside the authorised period
                printf("[EXIT] DENIED: outside access hours (clock %02d:%02d), %s must stay inside.\n",
                       now / 60, now % 60, tag->user_name);
                log_event(uid, tag->user_name, action, "DENIED (OUT OF HOURS)");
                act = ACT_DENY;
            } else {
                user_inside[idx] = false;
                check_out_counter++;
                int occ = 0;
                for (size_t i = 0; i < DB_SIZE; i++) if (user_inside[i]) occ++;
                printf("[EXIT] GRANTED: goodbye %s | Building occupancy: %d\n", tag->user_name, occ);
                log_event(uid, tag->user_name, action, "GRANTED");
                act = ACT_OPEN_EXIT;
            }
        }
    }
    xSemaphoreGiveRecursive(entrance_mutex);

    feedback_show_event(entrance_tag_name(idx), act != ACT_DENY, is_entry);

    switch (act) {
        case ACT_DENY:             entrance_signal_denied();          break;
        case ACT_OPEN_ENTRY:       open_gate(LEDC_CHANNEL_0, false);  break;
        case ACT_OPEN_ENTRY_ADMIN: open_gate(LEDC_CHANNEL_0, true);   break;
        case ACT_OPEN_EXIT:        open_gate(LEDC_CHANNEL_1, false);  break;
        default: break;
    }
}

// One task per reader: continuous card monitoring
static void task_reader(void *pv) {
    int r = (int)(intptr_t)pv;
    char last[24] = "";
    int misses = 0;
    while (1) {
        if (admin_busy || !rfid_ok[r]) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        uint8_t u[10], n = 0;
        if (rfid_read_uid(r, u, &n)) {
            char s[24];
            uid_to_str(u, n, s);
            misses = 0;
            if (strcmp(s, last) != 0) {          // ignore a card that is still held on the reader
                strncpy(last, s, sizeof(last) - 1);
                last[sizeof(last) - 1] = '\0';
                printf("\n[RFID %d] Card detected, UID = %s\n", r + 1, s);
                fflush(stdout);
                entrance_process_card(r, s);
            }
        } else if (misses < 3 && ++misses >= 3) {
            last[0] = '\0';                      // card removed -> can be scanned again
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

// ====================================================================
// Life cycle
// ====================================================================
void entrance_init(void) {
    entrance_mutex = xSemaphoreCreateRecursiveMutex();
    rfid_bus_mutex = xSemaphoreCreateMutex();

    // Keep both RFID chip-selects high before any SPI device is added
    gpio_config_t cs_cfg = {
        .pin_bit_mask = (1ULL << PIN_RFID1_CS) | (1ULL << PIN_RFID2_CS),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cs_cfg);
    gpio_set_level(PIN_RFID1_CS, 1);
    gpio_set_level(PIN_RFID2_CS, 1);

    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << PIN_LED_GREEN) | (1ULL << PIN_LED_RED),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out_cfg);

    // Emergency button: active low, internal pull-up (reads high if no button is wired)
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << PIN_EMERGENCY_BTN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&btn_cfg);

    // Servos via LEDC: 50 Hz, 13-bit
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    const int servo_pins[3] = { PIN_SERVO1_PWM, PIN_SERVO2_PWM, PIN_SERVO3_PWM };
    for (int i = 0; i < 3; i++) {
        ledc_channel_config_t ch = {
            .gpio_num = servo_pins[i],
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = (ledc_channel_t)i,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
            .hpoint = 0,
        };
        ledc_channel_config(&ch);
        set_gate_angle((ledc_channel_t)i, 0);
    }
}

void entrance_rfid_init(void) {
    rfid_init_one(0);   // reader 1 (entry)
    rfid_init_one(1);   // reader 2 (exit)
    printf("\n[RFID] Select a card in Wokwi, then press Tap or enable Hold.\n");
    printf("[RFID] Blue=Alice, Green=Bob, Red=Admin, Yellow=unregistered.\n");
}

void entrance_start(void) {
    xTaskCreate(task_emergency, "emergency", 2048, NULL, 6, NULL);
    xTaskCreate(task_reader,    "reader1",   4096, (void *)(intptr_t)0, 5, NULL);
    xTaskCreate(task_reader,    "reader2",   4096, (void *)(intptr_t)1, 5, NULL);
}

// ====================================================================
// Weak defaults: let this module build and run on its own.
// The real versions (Member 4: log + schedule, clock owner) override these.
// ====================================================================
__attribute__((weak)) void log_event(const char *uid, const char *name, const char *action, const char *status) {
    printf("[LOG] %s,%s,%s,%s\n", uid, name, action, status);
}

__attribute__((weak)) int clock_minutes(void) {
    // 1 real second = 1 simulated minute, starting at 10:00
    //return (10 * 60 + (int)(xTaskGetTickCount() / pdMS_TO_TICKS(1000))) % 1440;
    return (16 * 60 + 45 + (int)(xTaskGetTickCount() / pdMS_TO_TICKS(1000))) % 1440;
}

__attribute__((weak)) bool schedule_is_open(void) {
    int h = clock_minutes() / 60;
    return h >= 8 && h < 17;
}

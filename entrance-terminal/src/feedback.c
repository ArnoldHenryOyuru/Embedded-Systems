/*
 * feedback.c  --  Member 1 innovation module
 *   1. Sound signatures (LEDC PWM on the buzzer)
 *   2. LCD1602 (I2C, PCF8574 backpack) showing occupancy, clock, hours, last scan
 *   3. After-hours overstay alert (people still inside when the building closes)
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "entrance.h"
#include "feedback.h"

static const char *TAG = "FEEDBACK";

#define FB_BUZZER_CH      LEDC_CHANNEL_3     /* servos use timer 0 / channels 0-2 */
#define OVERSTAY_REMIND_TICKS  40            /* 40 x 500 ms = 20 s between reminders */

static SemaphoreHandle_t snd_mutex;          /* one signature at a time */
static SemaphoreHandle_t ev_mutex;           /* protects the LCD line-2 text */
static char ev_buf[24] = "Tap your card";
static bool ev_dirty = true;

/* ====================================================================
 * 1. Sound
 * ==================================================================== */
void feedback_tone(uint32_t hz) {
    if (hz == 0) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, FB_BUZZER_CH, 0);
    } else {
        ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_1, hz);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, FB_BUZZER_CH, 4096);   /* 50 % of 13 bit */
    }
    ledc_update_duty(LEDC_LOW_SPEED_MODE, FB_BUZZER_CH);
}

static void note(uint32_t hz, int ms) {
    feedback_tone(hz);
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void feedback_sound(sound_t s) {
    if (snd_mutex) xSemaphoreTake(snd_mutex, portMAX_DELAY);
    switch (s) {
    case SND_ENTRY:
#if FB_SOUND_LOG
        printf("[SOUND] entry chime (rising)\n");
#endif
        note(1000, 90); note(1600, 140);
        break;
    case SND_EXIT:
#if FB_SOUND_LOG
        printf("[SOUND] exit chime (falling)\n");
#endif
        note(1600, 90); note(1000, 140);
        break;
    case SND_DENIED:
#if FB_SOUND_LOG
        printf("[SOUND] denied (3 low buzzes)\n");
#endif
        for (int i = 0; i < 3; i++) { note(300, 150); note(0, 80); }
        break;
    case SND_ALERT:
#if FB_SOUND_LOG
        printf("[SOUND] overstay alert\n");
#endif
        for (int i = 0; i < 3; i++) { note(1800, 100); note(1200, 100); }
        break;
    }
    feedback_tone(0);
    if (snd_mutex) xSemaphoreGive(snd_mutex);
}

/* ====================================================================
 * 2. LCD1602 over I2C (PCF8574: P0=RS P1=RW P2=E P3=backlight P4-P7=D4-D7)
 * ==================================================================== */
#define LCD_RS  0x01
#define LCD_EN  0x04
#define LCD_BL  0x08

static i2c_master_bus_handle_t i2c_bus;
static i2c_master_dev_handle_t lcd_dev;
static bool lcd_ok = false;

static void lcd_raw(uint8_t v) {
    i2c_master_transmit(lcd_dev, &v, 1, 50);
}

static void lcd_nib(uint8_t nib, bool rs) {
    uint8_t b = (uint8_t)((nib << 4) | LCD_BL | (rs ? LCD_RS : 0));
    lcd_raw(b | LCD_EN);
    esp_rom_delay_us(2);
    lcd_raw(b);
    esp_rom_delay_us(60);
}

static void lcd_send(uint8_t v, bool rs) {
    lcd_nib(v >> 4, rs);
    lcd_nib(v & 0x0F, rs);
}

static void lcd_cmd(uint8_t c) {
    lcd_send(c, false);
    if (c <= 0x02) esp_rom_delay_us(2000);   /* clear / home are slow */
}

static void lcd_line(int row, const char *s) {
    lcd_cmd((uint8_t)(0x80 | (row ? 0x40 : 0x00)));
    for (int i = 0; i < 16; i++) {
        char ch = *s ? *s++ : ' ';
        lcd_send((uint8_t)ch, true);
    }
}

static void lcd_setup(void) {
    i2c_master_bus_config_t bc = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = FB_PIN_SDA,
        .scl_io_num = FB_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bc, &i2c_bus) != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed - display disabled");
        return;
    }
    if (i2c_master_probe(i2c_bus, FB_LCD_ADDR, 50) != ESP_OK) {
        ESP_LOGW(TAG, "no LCD at 0x%02X - display disabled (check wiring)", FB_LCD_ADDR);
        return;
    }
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = FB_LCD_ADDR,
        .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(i2c_bus, &dc, &lcd_dev) != ESP_OK) {
        ESP_LOGE(TAG, "could not add LCD device");
        return;
    }
    lcd_ok = true;

    vTaskDelay(pdMS_TO_TICKS(50));
    lcd_nib(0x3, false); esp_rom_delay_us(5000);
    lcd_nib(0x3, false); esp_rom_delay_us(200);
    lcd_nib(0x3, false); esp_rom_delay_us(200);
    lcd_nib(0x2, false);                         /* 4-bit mode */
    lcd_cmd(0x28);                               /* 2 lines, 5x8 font */
    lcd_cmd(0x0C);                               /* display on, no cursor */
    lcd_cmd(0x06);                               /* entry mode */
    lcd_cmd(0x01);                               /* clear */
    lcd_line(0, "CoCIS Entrance");
    lcd_line(1, "Starting...");
    ESP_LOGI(TAG, "LCD ready at 0x%02X", FB_LCD_ADDR);
}

/* Called by entrance_process_card() after every decision */
void feedback_show_event(const char *name, bool granted, bool is_entry) {
    if (!ev_mutex) return;
    char nm[16];
    size_t n = 0;
    while (name[n] && name[n] != '(' && n < sizeof(nm) - 1) { nm[n] = name[n]; n++; }
    while (n > 0 && nm[n - 1] == ' ') n--;       /* "Alice (Student)" -> "Alice" */
    nm[n] = '\0';

    xSemaphoreTake(ev_mutex, portMAX_DELAY);
    snprintf(ev_buf, sizeof(ev_buf), "%s %s %.9s", is_entry ? "IN" : "OUT",
             granted ? "OK" : "NO", nm);
    ev_dirty = true;
    xSemaphoreGive(ev_mutex);
}

/* ====================================================================
 * 3. Display + overstay task (the only code that writes to the LCD)
 * ==================================================================== */
static void task_feedback(void *pv) {
    char prev1[24] = "";
    bool prev_overstay = false;
    int remind = 0;
    bool phase = false;

    while (1) {
        bool open  = schedule_is_open();
        int  occ   = entrance_building_occupancy();
        int  now   = clock_minutes();
        bool emerg = entrance_emergency_active();
        bool overstay = !open && occ > 0;
        phase = !phase;

        /* ---- overstay alert ---- */
        if (overstay && !prev_overstay) {
            char st[40];
            printf("\n[OVERSTAY] Building closed with %d person(s) still inside:\n", occ);
            entrance_print_inside();
            snprintf(st, sizeof(st), "%d INSIDE AFTER HOURS", occ);
            log_event("SYSTEM", "SYSTEM", "OVERSTAY", st);
            feedback_sound(SND_ALERT);
            remind = 0;
        } else if (overstay && ++remind >= OVERSTAY_REMIND_TICKS) {
            printf("[OVERSTAY] Still %d inside after hours - admin attention needed.\n", occ);
            feedback_sound(SND_ALERT);
            remind = 0;
        } else if (!overstay && prev_overstay) {
            printf("[OVERSTAY] Cleared: nobody is inside.\n");
            log_event("SYSTEM", "SYSTEM", "OVERSTAY", "CLEARED");
        }
        prev_overstay = overstay;

        /* ---- LCD ---- */
        if (lcd_ok) {
            char l1[32];
            if (emerg)                     snprintf(l1, sizeof(l1), "EMERGENCY! Occ:%d", occ);
            else if (overstay && phase)    snprintf(l1, sizeof(l1), "AFTER HRS: %d IN", occ);
            else snprintf(l1, sizeof(l1), "Occ:%d %02d:%02d %s", occ, now / 60, now % 60,
                          open ? "OPEN" : "SHUT");
            if (strcmp(l1, prev1) != 0) {
                lcd_line(0, l1);
                strcpy(prev1, l1);
            }

            char l2[24];
            bool dirty = false;
            xSemaphoreTake(ev_mutex, portMAX_DELAY);
            if (ev_dirty) { strcpy(l2, ev_buf); ev_dirty = false; dirty = true; }
            xSemaphoreGive(ev_mutex);
            if (dirty) lcd_line(1, l2);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ====================================================================
 * Life cycle
 * ==================================================================== */
void feedback_init(void) {
    snd_mutex = xSemaphoreCreateMutex();
    ev_mutex  = xSemaphoreCreateMutex();

    ledc_timer_config_t bt = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_1,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = 2000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&bt);
    ledc_channel_config_t bch = {
        .gpio_num = FB_PIN_BUZZER,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = FB_BUZZER_CH,
        .timer_sel = LEDC_TIMER_1,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&bch);

    lcd_setup();
}

void feedback_start(void) {
    xTaskCreate(task_feedback, "feedback", 4096, NULL, 3, NULL);
}

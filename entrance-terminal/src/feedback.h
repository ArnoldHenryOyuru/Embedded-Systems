/*
 * feedback.h  --  Member 1 innovation module: sound signatures, LCD status
 *                 display and after-hours overstay alert.
 *
 * Start-up order (demo/main.c):
 *     entrance_init();
 *     feedback_init();     // buzzer PWM + I2C LCD (before the reader tasks start)
 *     ...
 *     entrance_start();
 *     feedback_start();    // display + overstay task
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Pins used by this module (tell the other members so nobody reuses them) */
#define FB_PIN_BUZZER   25
#define FB_PIN_SDA      21
#define FB_PIN_SCL      22
#define FB_LCD_ADDR     0x27

/* 1 = print a [SOUND] line each time a signature plays (useful in screen recordings) */
#define FB_SOUND_LOG    1

typedef enum {
    SND_ENTRY,      /* rising two-note chime  */
    SND_EXIT,       /* falling two-note chime */
    SND_DENIED,     /* three low buzzes       */
    SND_ALERT       /* overstay alert         */
} sound_t;

void feedback_init(void);
void feedback_start(void);

/* Plays a full signature (blocks until it finishes). */
void feedback_sound(sound_t s);

/* Raw tone: hz = 0 turns the buzzer off. Used for the emergency siren. */
void feedback_tone(uint32_t hz);

/* Shows the result of the last card scan on LCD line 2. */
void feedback_show_event(const char *name, bool granted, bool is_entry);

/*
 * entrance.h  --  Member 1: Main entrance and access control
 *
 * Owns: two MFRC522 readers (ENTRY / EXIT), three servo doors, green/red LEDs,
 *       buzzer, emergency button, the tag database and the occupancy state.
 *
 * Start-up order (see demo/main.c):
 *     entrance_init();        // GPIO, servos, mutexes   (no SPI needed yet)
 *     <SPI bus is initialised by the app / storage module>
 *     entrance_rfid_init();   // talks to the readers over SPI
 *     entrance_start();       // creates reader + emergency tasks
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Set to 0 if administrators must also obey the access schedule. */
#define ADMIN_BYPASS_SCHEDULE   1

typedef enum { ROLE_STUDENT, ROLE_ADMIN } UserRole;

/* ---------------- Life cycle ---------------- */
void entrance_init(void);
void entrance_rfid_init(void);
void entrance_start(void);

/* ---------------- Access decisions ---------------- */
/* reader 0 = ENTRY, reader 1 = EXIT. Also used for manual UID typing. */
void entrance_process_card(int reader, const char *uid);

/* ---------------- Occupancy (read by the serial menu) ---------------- */
int  entrance_building_occupancy(void);
void entrance_print_inside(void);          /* prints the names of people inside */
int  entrance_checkin_count(void);
int  entrance_checkout_count(void);

/* ---------------- Admin / storage helpers ---------------- */
int         entrance_find_card(const char *uid);   /* -1 if unknown */
const char *entrance_tag_name(int idx);
bool        entrance_tag_is_admin(int idx);
bool        entrance_normalize_uid(char *s);       /* clean + validate a typed UID */
bool        entrance_acquire_uid(int reader, char *out, size_t len); /* card or keyboard */
void        entrance_set_admin_busy(bool busy);    /* pauses reader tasks during admin login */

void entrance_restore_event(const char *uid, bool entered); /* replay one SD log line */
void entrance_reset_occupancy(void);                        /* everyone marked outside */
void entrance_reset_all(void);                              /* occupancy + counters */

/* ---------------- Emergency override ---------------- */
void entrance_emergency_set(bool on);
bool entrance_emergency_active(void);

/* ---------------- Feedback ---------------- */
void entrance_signal_denied(void);

/* ====================================================================
 * Functions this module CALLS but does not own.
 * Member 4 (storage / schedule) and the clock owner implement them.
 * entrance.c provides weak defaults so this module builds and runs alone.
 * ==================================================================== */
void log_event(const char *uid, const char *name, const char *action, const char *status);
bool schedule_is_open(void);     /* true if the current time is inside the access window */
int  clock_minutes(void);        /* minutes since midnight */

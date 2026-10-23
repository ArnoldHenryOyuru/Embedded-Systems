/*
 * cocis_protocol.h
 * Shared definitions for all three CoCIS terminals.
 * AGREE ON CHANGES HERE AS A GROUP: every module depends on this file.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define COCIS_PROTOCOL_VERSION  1
#define COCIS_UID_MAX_LEN       10      /* RFID UIDs are 4, 7 or 10 bytes */
#define COCIS_NAME_MAX_LEN      24

/* ---------- Terminals ---------- */
typedef enum {
    TERMINAL_ENTRANCE = 0,
    TERMINAL_ROOM_1   = 1,
    TERMINAL_ROOM_2   = 2,
    TERMINAL_COUNT
} terminal_id_t;

/* ---------- Message types sent between terminals ---------- */
typedef enum {
    MSG_HEARTBEAT = 0,       /* periodic "I am alive" */
    MSG_ACCESS_EVENT,        /* entrance -> others/logger: someone entered or left */
    MSG_OCCUPANCY_UPDATE,    /* room -> entrance/logger: room occupied or empty */
    MSG_LIGHT_STATE,         /* room -> logger: lights switched on or off */
    MSG_SCHEDULE_UPDATE,     /* authenticated change of the access window */
    MSG_SCHEDULE_REQUEST,    /* terminal asks for the current schedule */
    MSG_ACK                  /* acknowledgement of a received message */
} msg_type_t;

/* ---------- Access events ---------- */
typedef enum {
    ACCESS_ENTRY_GRANTED = 0,
    ACCESS_EXIT_GRANTED,
    ACCESS_ENTRY_DENIED_UNKNOWN_TAG,
    ACCESS_ENTRY_DENIED_OUTSIDE_HOURS,
    ACCESS_EXIT_DENIED_OUTSIDE_HOURS,   /* must stay inside until window reopens */
    ACCESS_ANOMALY                      /* e.g. repeated invalid scans, double entry */
} access_result_t;

/* One log entry. Written to the SD card and mirrored in RAM. */
typedef struct __attribute__((packed)) {
    uint32_t        seq;                      /* monotonically increasing record number */
    uint32_t        timestamp;                /* Unix time (seconds) */
    uint8_t         uid[COCIS_UID_MAX_LEN];
    uint8_t         uid_len;
    access_result_t result;
    terminal_id_t   source;
} access_record_t;

/* ---------- Occupancy / lighting ---------- */
typedef struct __attribute__((packed)) {
    terminal_id_t room;        /* TERMINAL_ROOM_1 or TERMINAL_ROOM_2 */
    bool          occupied;
    bool          lights_on;
    uint32_t      timestamp;
} room_status_t;

/* ---------- Configurable access schedule ---------- */
typedef struct __attribute__((packed)) {
    uint16_t open_minute;      /* minutes after midnight, e.g. 07:00 -> 420 */
    uint16_t close_minute;     /* e.g. 22:00 -> 1320 */
    uint8_t  days_mask;        /* bit0 = Monday ... bit6 = Sunday */
    uint32_t version;          /* incremented on every change (conflict detection) */
} access_schedule_t;

/* ---------- The packet actually sent over the wireless link ---------- */
typedef struct __attribute__((packed)) {
    uint8_t       version;     /* COCIS_PROTOCOL_VERSION */
    msg_type_t    type;
    terminal_id_t sender;
    uint32_t      msg_id;      /* for ACKs and duplicate detection */
    union {
        access_record_t   access;
        room_status_t     room;
        access_schedule_t schedule;
    } payload;
    uint8_t       mac[8];      /* truncated authentication tag (optional security feature) */
} cocis_msg_t;

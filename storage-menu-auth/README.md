# Member 4: Data management and admin interface

**Owner:** Member 4  
**Branch:** `feature/<name>-storage-menu-auth`

## Wokwi parts
SD card module (SPI), serial monitor

## Responsibilities
- Write access records, timestamps and occupancy data to the SD card
- Serial CLI with custom commands for monitoring
- Configurable access schedule with PIN/password protection

## Video demo target
Type commands in the serial monitor to pull up SD logs, change the gate schedule and show the password lock working.

## Interfaces
Uses `common/cocis_protocol.h`. Document the functions this module exposes here.

## Status
- [ ] Design agreed
- [ ] Implemented
- [ ] Tested in Wokwi (see `docs/test-plan.md`)
- [ ] Explained and demonstrated in video

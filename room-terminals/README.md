# Member 2: Lecture rooms and automation

**Owner:** Member 2  
**Branch:** `feature/<name>-room-terminals`

## Wokwi parts
ESP32 #2 and #3, PIR sensors, relay modules, LEDs as lights

## Responsibilities
- Occupancy detection for Lecture Room 1 and Lecture Room 2
- Lighting control from sensor state and timers
- Edge cases: lights must not switch off instantly if someone stands still

## Video demo target
Trigger the PIR sensors and show the automated response of the room lights.

## Interfaces
Uses `common/cocis_protocol.h`. Document the functions this module exposes here.

## Status
- [ ] Design agreed
- [ ] Implemented
- [ ] Tested in Wokwi (see `docs/test-plan.md`)
- [ ] Explained and demonstrated in video

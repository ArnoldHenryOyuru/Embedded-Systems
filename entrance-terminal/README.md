# Member 1: Main entrance and access control

**Owner:** Member 1  
**Branch:** `feature/<name>-entrance-terminal`

## Wokwi parts
ESP32 #1, RFID reader (simulated), door actuator (servo or LED), RTC module

## Responsibilities
- RFID tag scanning, validation and ID verification
- Separate logic for the entry gate and the exit gate
- Building occupancy counter (+1 on entry, -1 on exit)
- Read the RTC for timestamps and for enforcing the schedule

## Video demo target
Scan a valid vs an invalid card and show how the occupancy counter changes.

## Interfaces
Uses `common/cocis_protocol.h`. Document the functions this module exposes here.

## Status
- [ ] Design agreed
- [ ] Implemented
- [ ] Tested in Wokwi (see `docs/test-plan.md`)
- [ ] Explained and demonstrated in video

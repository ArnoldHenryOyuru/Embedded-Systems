# Member 3: Network integration and synchronization

**Owner:** Member 3  
**Branch:** `feature/<name>-comms-sync`

## Wokwi parts
All three ESP32s (software only)

## Responsibilities
- Communication architecture between the three ESP32s (ESP-NOW, MQTT or UART)
- Mutexes/semaphores/queues in ESP-IDF FreeRTOS to prevent race conditions
- Reliable delivery: ACKs, retries, duplicate detection

## Video demo target
Explain the protocol and prove that data from Room 1 or the entrance reaches the central log without crashing.

## Interfaces
Uses `common/cocis_protocol.h`. Document the functions this module exposes here.

## Status
- [ ] Design agreed
- [ ] Implemented
- [ ] Tested in Wokwi (see `docs/test-plan.md`)
- [ ] Explained and demonstrated in video

# Smart Control and Monitoring System, CoCIS (Makerere University)

Coursework for **CSC 2118 Embedded and Real-time Systems**, 2026/2027.
ESP32 + ESP-IDF, **simulated in Wokwi only (no physical build)**.
**Deadline: 2 November 2026** (submit on Muele). Testing is shown through screen recordings.

- **Wokwi simulation:** _add link_
- **Report:** `docs/`
- **Test plan:** `docs/test-plan.md`

## Group members and roles

| Member | Name | Student no. | Role | Video link |
|---|---|---|---|---|
| 1 | Oyuru Arnold Henry (confirm) | _add_ | Main entrance and access control (ESP32 #1, RFID, door actuator, RTC) | _add_ |
| 2 | _add_ | _add_ | Lecture rooms and automation (ESP32 #2 and #3, PIR, relays, lights) | _add_ |
| 3 | _add_ | _add_ | Network integration and synchronization | _add_ |
| 4 | _add_ | _add_ | Data management and admin interface (SD card, serial CLI, schedule, PIN) | _add_ |

## Architecture

```
                      +-----------------------+
                      |  ENTRANCE TERMINAL    |
   RFID (entry) ----> |  ESP32 #1 + RTC       | ----> door (entry)
   RFID (exit)  ----> |  access + counter     | ----> door (exit)
                      +-----------+-----------+
                                  |   inter-ESP32 link (ESP-NOW / MQTT / UART)
              +-------------------+-------------------+
              |                                       |
   +----------v-----------+              +------------v---------+
   |  ROOM 1  (ESP32 #2)  |              |  ROOM 2  (ESP32 #3)  |
   |  PIR -> relay/LED    |              |  PIR -> relay/LED    |
   +----------------------+              +----------------------+

   Central log: SD card + serial CLI + PIN-protected schedule changes
```

## Repository layout

| Folder | Contents |
|---|---|
| `common/` | Shared message and record formats (`cocis_protocol.h`) |
| `entrance-terminal/` | Member 1 |
| `room-terminals/` | Member 2 |
| `comms-sync/` | Member 3 |
| `storage-menu-auth/` | Member 4 |
| `wokwi/` | `diagram.json`, `wokwi.toml`, simulation notes |
| `docs/` | Report drafts, test plan, contribution log, innovation tracker |

## Requirements checklist (from the brief)

- [ ] 2.1 RFID entry and exit, door actuation, entry/exit records, who-is-inside
- [ ] 2.2 Occupancy detection and automatic lighting in two rooms
- [ ] 2.3 Three coordinated ESP32 terminals
- [ ] 2.4 SD card storage
- [ ] 2.5 Serial menus and commands
- [ ] 2.6 Configurable access schedule (entry **and** exit enforced), authenticated changes
- [ ] 2.7 Synchronization (mutexes/queues), consistent records
- [ ] Innovation features (see `docs/innovation.md`)
- [ ] Every test in `docs/test-plan.md` recorded

## Working rules

1. One branch per member (`feature/<name>-<module>`). Merge to `main` by pull request.
2. Do not edit `common/cocis_protocol.h` without telling the group.
3. Commit often with clear messages. The history shows who wrote what.
4. Every member must be able to explain every line they claim.

## Timeline

- 23-24 Oct: agree interfaces and the message format
- 24-27 Oct: each member builds and tests their module in Wokwi
- 28-29 Oct: integration
- 30 Oct: innovation features, run every test in the test plan
- 31 Oct: record videos, finish the report
- 1 Nov: buffer and presentation rehearsal
- 2 Nov: submit on Muele

## Build (ESP-IDF)

```
cd entrance-terminal
idf.py set-target esp32
idf.py build
```

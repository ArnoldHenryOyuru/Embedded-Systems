# Smart Control and Monitoring System, CoCIS (Makerere University)

Coursework for **CSC 2118 Embedded and Real-time Systems**, 2026/2027.
ESP32 + ESP-IDF, simulated in Wokwi. **Deadline: 2 November 2026** (submit on Muele).

- **Wokwi simulation:** _add link_
- **Report:** `docs/`

## Group members

| Name | Student no. | Module | Video link |
|---|---|---|---|
| Oyuru Arnold Henry | _add_ | _assign_ | _add_ |
| _add_ | _add_ | _assign_ | _add_ |
| _add_ | _add_ | _assign_ | _add_ |
| _add_ | _add_ | _assign_ | _add_ |

## Architecture

```
                      +-----------------------+
                      |  ENTRANCE TERMINAL    |
   RFID (entry) ----> |  ESP32                | ----> door (entry)
   RFID (exit)  ----> |  access + schedule    | ----> door (exit)
                      +-----------+-----------+
                                  |   wireless link (ESP-NOW / MQTT)
              +-------------------+-------------------+
              |                                       |
   +----------v-----------+              +------------v---------+
   |  ROOM 1 TERMINAL     |              |  ROOM 2 TERMINAL     |
   |  PIR -> lights       |              |  PIR -> lights       |
   +----------------------+              +----------------------+

   SD card (logs) + serial menu (UART) + authenticated schedule changes
```

## Repository layout

| Folder | Contents |
|---|---|
| `common/` | Shared message and record formats (`cocis_protocol.h`) |
| `entrance-terminal/` | RFID entry/exit readers, doors, access decisions |
| `room-terminals/` | PIR occupancy detection and lighting control (Room 1 and 2) |
| `comms-sync/` | Inter-ESP32 communication, mutexes, queues |
| `storage-menu-auth/` | SD logging, serial menu, access schedule, authentication |
| `wokwi/` | `diagram.json`, `wokwi.toml`, simulation notes |
| `docs/` | Report drafts, diagrams, contribution log |

## Requirements checklist (from the brief)

- [ ] 2.1 RFID entry and exit, door actuation, entry/exit records, who-is-inside
- [ ] 2.2 Occupancy detection and automatic lighting in two rooms
- [ ] 2.3 Three coordinated ESP32 terminals
- [ ] 2.4 SD card storage
- [ ] 2.5 Serial menus and commands
- [ ] 2.6 Configurable access schedule (entry **and** exit enforced), authenticated changes
- [ ] 2.7 Synchronization (mutexes/queues), consistent records
- [ ] Innovation features (see `docs/innovation.md`)

## Working rules

1. One branch per member (`feature/<name>-<module>`). Merge to `main` by pull request.
2. Do not edit `common/cocis_protocol.h` without telling the group.
3. Commit often with clear messages. The history shows who wrote what.
4. Every member must be able to explain every line they claim.

## Build (ESP-IDF)

```
cd entrance-terminal
idf.py set-target esp32
idf.py build
```

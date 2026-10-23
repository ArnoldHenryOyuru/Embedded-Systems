# Test plan (simulation + screen recordings)

All testing is done in Wokwi and **recorded on screen**. Run every scenario on each build.
Each test must show: the input, the serial log output, and the visible result (LED, servo, counter).
Tick "Recorded" once the clip exists and note the video timestamp.

| ID | Scenario | Steps | Expected result | Owner | Recorded |
|---|---|---|---|---|---|
| T1 | Valid card, entry | Scan registered tag at entry reader | Door opens, occupancy +1, `ENTRY GRANTED` logged | M1 | [ ] |
| T2 | Invalid card | Scan unregistered tag | Door stays shut, `DENIED unknown tag` logged | M1 | [ ] |
| T3 | Valid card, exit | Scan tag at exit reader | Door opens, occupancy -1, `EXIT GRANTED` logged | M1 | [ ] |
| T4 | Exit with nobody inside / double entry | Scan exit for tag not inside, or entry twice | Anomaly flagged, counter never goes negative | M1 | [ ] |
| T5 | Entry outside hours | Set clock/schedule so gate is closed, scan valid tag | Denied, `OUTSIDE HOURS` logged | M1 + M4 | [ ] |
| T6 | Exit outside hours | Person inside, window closed, scan at exit | Exit blocked, person stays counted as inside | M1 + M4 | [ ] |
| T7 | Room occupied | Trigger PIR in Room 1 | Lights switch on, status sent to log | M2 | [ ] |
| T8 | Stationary person | PIR stops triggering for a short time | Lights stay on (no instant switch-off) | M2 | [ ] |
| T9 | Room empty | No motion past the timeout | Lights switch off, event logged | M2 | [ ] |
| T10 | Two rooms independent | Trigger Room 1 only, then Room 2 only | Each room controls only its own lights | M2 | [ ] |
| T11 | Message delivery | Trigger a room event and an entrance event | Both arrive at the central log in order | M3 | [ ] |
| T12 | Concurrent events | Fire entrance and room events at the same time | No corrupted or missing records, no crash | M3 | [ ] |
| T13 | SD logging | Run T1 to T9, then read the card | All events present with timestamps | M4 | [ ] |
| T14 | Serial menu | Run `help`, `status`, `occupancy`, `logs` | Correct, readable output | M4 | [ ] |
| T15 | Wrong PIN | Try to change schedule with wrong PIN | Rejected, attempt logged | M4 | [ ] |
| T16 | Correct PIN | Change schedule with right PIN | Applied, entrance terminal uses the new window | M4 + M3 | [ ] |
| T17 | Innovation feature(s) | One test per feature chosen | Add rows here | All | [ ] |

## Recording checklist
- Show the Wokwi window and serial monitor together
- Say the test ID and what you are about to do
- Keep each clip short and labelled with the ID
- Each member records their own module's tests, then walks through the code

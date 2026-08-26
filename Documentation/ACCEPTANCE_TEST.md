# Controlled Acceptance Test

1. Leave `AUTO_UNICOM_MODE=off` and set the exact `ALTITUDE_CALLSIGN`.
2. Start X-Plane, YAL and IVAO Altitude on Windows.
3. Run `Discover Altitude UI`; require `DISCOVERY_OK` in the helper log.
4. Set `AUTO_UNICOM_MODE=dry_run`, reload config and verify YAL receives
   `PREVIEW_READY` without any Altitude UI change.
5. Select `LOCAL READBACK`, run `Voice Audio Test` and verify no PTT occurs.
6. If the audio guard will be used, configure unique input/output matches,
   enable it, reload config and verify the expected endpoint IDs under `[AUDIO]`
   in `IVAO_Pilot_Client.conf` without changing unrelated sections.
7. For productive text, connect online, select the transmit COM and tune
   122.800 before setting `AUTO_UNICOM_MODE=send`.
8. Submit one controlled YAL request and require the exact text to appear in
   Altitude history with result `SUBMITTED_VISIBLE`.
9. Configure and test radio routing only after all previous cases pass. Verify
   RX activity delays PTT and that changing frequency or going offline prevents
   transmission.

Never repeat a request whose terminal result is `UNCERTAIN_AFTER_SUBMIT` or
whose voice result is `UNCERTAIN_AFTER_PTT`.

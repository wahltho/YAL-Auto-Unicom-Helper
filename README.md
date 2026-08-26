# YAL Auto-Unicom Helper

YAL Auto-Unicom Helper is a Windows X-Plane 12 companion plugin for YAL. YAL
prepares complete Auto-Unicom text and voice payloads; this helper validates the
live IVAO context and submits them through IVAO Altitude's normal message UI.

The helper is not an IVAO product and is not affiliated with or endorsed by
IVAO. It does not connect to the IVAO network itself and has no standalone
message generator.

Current plugin version: 0.1.0

## Safety model

Productive sending is disabled by default. A message can be submitted only
when all of these conditions are true:

- the helper and IVAO Altitude are available;
- Altitude reports the pilot online;
- the selected transmit COM is tuned exactly to the configured UNICOM frequency;
- the transport and Altitude message composer are idle;
- the gate is sampled again immediately before invoking `SEND`.

Text is written into Altitude's normal message composer and accepted only after
the same text becomes visible in Altitude's active-frequency history. Voice is
additive to text and runs only after visible text submission. Radio voice waits
for the selected Altitude receiver to remain quiet before pressing PTT.

## Menu

`Plugins > YAL Auto-Unicom Helper` provides:

- `AUTO UNICOM > TEXT`
- `AUTO UNICOM > VOICE > OFF | LOCAL READBACK | RADIO`
- `Voice Audio Test`
- `Chime Test`
- `Discover Altitude UI`
- `Reload Config`

Turning text off also turns voice off. Selecting local or radio voice enables
text because voice-only requests are forbidden. Changes are persistent and are
deferred until the active request has completed.

## Configuration

The plugin creates `Output/preferences/YAL_AutoUnicomHelper.prf` with inert
defaults on first start. Set `ALTITUDE_CALLSIGN` to the exact callsign used for
the Altitude connection. If it is empty, the API publishes an empty effective
callsign and YAL must not submit Auto-Unicom messages.

Use `AUTO_UNICOM_MODE=dry_run` for mailbox integration without changing
Altitude. Use `send` only after the discovery and controlled acceptance tests
pass. `AUTO_UNICOM_VOICE_MODE=local` speaks the voice phrase locally without
PTT. `radio` requires an output endpoint routed into Altitude's microphone
input.

See `YAL_AutoUnicomHelper.prf.example` for every supported setting.

## API

The public API is version 3 under `wahltho/autounicom/*`. It preserves the
existing Auto-Unicom text/voice mailbox semantics. YAL should discover this
provider first and fall back to `ivao_monitor/autounicom/*` for private
installations.

Full protocol: `Documentation/YAL_Auto_Unicom_Helper_API.md`.

## Installation and builds

See `INSTALL.md` and `BUILD.md`.

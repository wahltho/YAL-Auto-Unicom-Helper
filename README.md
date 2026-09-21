# YAL Auto-Unicom Helper

**IVAO only:** This plugin requires IVAO Altitude and does not support VATSIM,
PilotEdge, or other online networks.

YAL Auto-Unicom Helper is a Windows X-Plane 12 companion plugin for YAL. YAL
prepares complete Auto-Unicom text and voice payloads; this helper validates the
live IVAO context and submits them through IVAO Altitude's normal message UI.
It supports the Zibo Mod 737-800 and compatible LevelUp 737NG variants supported
by the installed YAL release.

The helper is not an IVAO product and is not affiliated with or endorsed by
IVAO. It does not connect to the IVAO network itself and has no standalone
message generator. A compatible YAL installation is mandatory; without YAL the
helper cannot create or deliver Auto-Unicom messages.

**Compatibility requirement:** YAL 4.8b2 or newer is required. Earlier YAL
versions do not support the standalone YAL Auto-Unicom Helper provider.

Current plugin version: 0.1.0b2

## Required and related projects

A compatible official YAL release is mandatory. Download YAL from the
[official YAL releases](https://github.com/wahltho/YAL/releases); the complete
project is available at the [YAL repository](https://github.com/wahltho/YAL).

[YAL Hoppie Helper](https://github.com/wahltho/YAL-Hoppie-Helper) is a separate
optional companion for Hoppie ACARS/CPDLC. It is not required for Auto-Unicom.
Packaged versions are available from the
[YAL Hoppie Helper releases](https://github.com/wahltho/YAL-Hoppie-Helper/releases).

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

The helper tracks a composer draft that it owns from the first write attempt.
If an interrupted transaction leaves that exact draft, or a partial prefix
left by an interrupted write, it is cleared after
`AUTO_UNICOM_COMPOSER_STALE_MS` (15 seconds by default) before another request
can start. The ownership lease remains active across flight changes while the
helper stays loaded. Focused, changing or foreign/manual composer text is never
cleared, and a request that may already have invoked `SEND` is never retried.

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
the Altitude connection. If it is empty, YAL does not submit Auto-Unicom
messages.

Use `AUTO_UNICOM_MODE=dry_run` to verify the YAL-to-Helper path without changing
Altitude. Use `send` only after completing the discovery and text-only
commissioning steps in the user manual. `AUTO_UNICOM_VOICE_MODE=local` speaks
the voice phrase locally without PTT. `radio` requires an output endpoint
routed into Altitude's microphone input.

See `YAL_AutoUnicomHelper.prf.example` for every supported setting.

## Altitude audio guard

The optional `ALTITUDE_AUDIO_GUARD` keeps the persisted `INPUT` and `OUTPUT`
entries in X-Plane's `IVAO_Pilot_Client.conf` on explicitly configured Windows
audio endpoints. Friendly-name matches must resolve to exactly one active
capture or render device; missing and ambiguous matches fail without changing
the Altitude file. The guard is disabled by default and checks every 60 seconds
after it is enabled.

For the proven VoiceMeeter route, Altitude input matches `Voicemeeter Out B1`
and radio TTS output matches `Voicemeeter Input`. The complete setup and local
proof sequence are documented in
`Documentation/Auto-UNICOM-Voice-Setup-Guide.md`.

## User documentation

Start with the complete user workflow:

- [YAL Auto-Unicom Helper User Manual](Documentation/USER_MANUAL.md)

It covers the mandatory YAL relationship, installation, first configuration,
safe text commissioning, voice modes, normal operation, troubleshooting,
updates and uninstall. The detailed Windows audio route is kept separately in:

- [Auto-UNICOM Voice Setup with YAL, IVAO Altitude, and VoiceMeeter](Documentation/Auto-UNICOM-Voice-Setup-Guide.md)

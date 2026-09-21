# Installation

This is the short installation reference. The mandatory YAL setup, safe
commissioning sequence, text and voice modes, operation and troubleshooting are
documented in the
[YAL Auto-Unicom Helper User Manual](Documentation/USER_MANUAL.md).

## Requirements

- X-Plane 12 on Windows
- IVAO Altitude
- [YAL 4.8b1 or newer](https://github.com/wahltho/YAL/releases); earlier YAL
  versions do not support the standalone Helper provider
- the Zibo Mod 737-800, or a compatible LevelUp 737NG variant supported by YAL
- X-Plane and Altitude running in the same Windows user session

## Install

Copy the packaged folder into the X-Plane installation so the result is:

```text
X-Plane 12/
  Resources/
    plugins/
      YAL_AutoUnicomHelper/
        64/
          win.xpl
        Documentation/
          USER_MANUAL.md
          Auto-UNICOM-Voice-Setup-Guide.md
        resources/
          auto_unicom_chime.wav
```

Start X-Plane once. The helper creates:

```text
Output/preferences/YAL_AutoUnicomHelper.prf
Output/preferences/YAL_AutoUnicomHelper.log
```

Set `ALTITUDE_CALLSIGN` to the exact callsign used in Altitude. Leave
`AUTO_UNICOM_MODE=off` until `Discover Altitude UI` succeeds. For voice, test
`LOCAL READBACK` before configuring `RADIO` and its virtual audio route.

The Helper has no standalone message generator. Enable `IVAOAUTOUNICOM` in YAL
and keep either YAL Auto Functions or Voice Advice Only active as described in
the user manual.

After editing the preference file, choose `Reload Config` from the plugin menu.

When using radio voice, configure Altitude's microphone route manually first.
The optional `ALTITUDE_AUDIO_GUARD` can then preserve the verified endpoint IDs;
it is intentionally disabled in a fresh installation.

## Uninstall

Remove `Resources/plugins/YAL_AutoUnicomHelper`. The preference and log files
under `Output/preferences` can be retained or removed separately.

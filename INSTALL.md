# Installation

## Requirements

- X-Plane 12 on Windows
- IVAO Altitude
- YAL with YAL Auto-Unicom Helper provider support
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

After editing the preference file, choose `Reload Config` from the plugin menu.

## Uninstall

Remove `Resources/plugins/YAL_AutoUnicomHelper`. The preference and log files
under `Output/preferences` can be retained or removed separately.

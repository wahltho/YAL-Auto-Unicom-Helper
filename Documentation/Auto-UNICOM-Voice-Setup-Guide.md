# Auto-UNICOM Voice Setup with YAL, IVAO Altitude, and VoiceMeeter

## Purpose

This guide configures a safe audio path for sending YAL Auto-UNICOM messages
as both text and speech through IVAO Altitude. The normal headset microphone
remains available for manual radio communication.

YAL 4.8b2 or newer is required. Earlier YAL versions do not support the
standalone YAL Auto-Unicom Helper provider.

This is the detailed audio companion to `USER_MANUAL.md`. Complete the YAL and
Helper installation and text-only commissioning in that manual first. The
Helper does not operate without YAL.

The setup follows one strict rule: do not enable automatic radio voice until
the complete audio path has been proven locally.

The intended responsibilities are:

- YAL produces the final text and a TTS-optimized voice phrase.
- YAL Auto-Unicom Helper submits the text through Altitude and produces TTS.
- VoiceMeeter mixes only the hardware microphone and Helper TTS.
- Altitude uses that mix as its microphone input.
- Altitude sends received audio directly to the physical headset.
- Desktop audio and Altitude output never enter the transmit mix.
- There is no permanent sidetone.

## Safety baseline

Use these modes during setup and troubleshooting:

```ini
AUTO_UNICOM_MODE=off
AUTO_UNICOM_VOICE_MODE=local
```

`local` never presses PTT. Text and voice are controlled separately, so keep
`AUTO_UNICOM_MODE=off` while testing only the audio path. Use the dedicated
audio-test command rather than forcing a YAL flight event.

Only after every local test passes should the modes become:

```ini
AUTO_UNICOM_MODE=send
AUTO_UNICOM_VOICE_MODE=radio
```

## Signal path

```text
Hardware microphone --------------------------------+
                                                     |
                                                     v
                                             VoiceMeeter B1
                                                     |
YAL -> Auto-Unicom Helper -> TTS -> VoiceMeeter Input+
                                                     |
                                                     v
                                  Voicemeeter Out B1 (capture)
                                                     |
                                                     v
                                            Altitude Input

Altitude Output ---------------------------------> physical headset
Desktop audio -----------------------------------> normal output device
```

The input and output directions must remain separate:

- Only VoiceMeeter B1 feeds Altitude input.
- Altitude output goes directly to the headset, not back through VoiceMeeter.
- `Voicemeeter Input` is not the global Windows playback device.

## VoiceMeeter endpoint names

Windows names endpoints from an application's perspective. The names can look
reversed when compared with the mixer surface.

| Windows endpoint | Type | Purpose |
|---|---|---|
| `Voicemeeter Input (VB-Audio Voicemeeter VAIO)` | Playback/render | The Helper plays TTS here. It appears on VoiceMeeter's `VIRTUAL INPUT` strip. |
| `Voicemeeter Out B1 (VB-Audio Voicemeeter VAIO)` | Recording/capture | The completed B1 mix used by Altitude as its microphone. |
| `Headset Microphone (TCA YOKE BOEING)` | Recording/capture | Example physical microphone for manual radio. |
| `Headset Earphone (TCA YOKE BOEING)` | Playback/render | Example physical Altitude output. |

In short: Helper TTS enters **Voicemeeter Input**, while Altitude records
**Voicemeeter Out B1**.

## Requirements

- Windows 10 or Windows 11
- X-Plane 12 with YAL and YAL Auto-Unicom Helper
- IVAO Altitude
- A working headset with identifiable capture and playback endpoints
- An installed Windows SAPI voice, for example `Microsoft David Desktop`
- Administrator rights for the initial virtual-audio driver installation

Example paths used by this guide:

```text
X-Plane:           D:\X-Plane 12
YAL preferences:   D:\X-Plane 12\Output\preferences\YAL.prf
Helper preferences:D:\X-Plane 12\Output\preferences\YAL_AutoUnicomHelper.prf
Altitude audio:    D:\X-Plane 12\IVAO_Pilot_Client.conf
Helper log:        D:\X-Plane 12\Output\preferences\YAL_AutoUnicomHelper.log
YAL/SASL log:      D:\X-Plane 12\Resources\plugins\YAL\data\output\SASLLog.txt
VoiceMeeter setup: C:\Users\<user>\AppData\Roaming\VoiceMeeterDefault.xml
```

Configuration files may contain personal information. Do not publish complete
files without reviewing their contents.

## 1. Inspect Windows audio endpoints

List all endpoints, including disconnected devices, from PowerShell:

```powershell
Get-PnpDevice -Class AudioEndpoint -PresentOnly:$false |
    Sort-Object Status, FriendlyName |
    Format-Table Status, FriendlyName, InstanceId -AutoSize
```

If another virtual mixer is already installed, use it only when all of these
conditions can be demonstrated:

- Both render and capture endpoints exist and remain stable.
- It can mix the hardware microphone and one selected TTS render stream.
- Desktop audio is not automatically included.
- Altitude output cannot loop back into the capture path.

## 2. Install VoiceMeeter Standard

Obtain VoiceMeeter Standard directly from VB-Audio:

<https://vb-audio.com/Voicemeeter/>

Close X-Plane and Altitude before installing the driver. Restart Windows once
after installation. Later routing and fader changes do not require a PC reboot.

The tested setup used VoiceMeeter Standard 1.1.2.2. Newer versions may use
slightly different menu labels.

## 3. Use 48 kHz throughout

Set the complete radio path to 48,000 Hz where Windows and VoiceMeeter expose a
format setting:

- Physical headset microphone
- Physical headset playback
- VoiceMeeter VAIO playback endpoint
- VoiceMeeter B1 capture endpoint
- VoiceMeeter system settings

Mixed 44.1 and 48 kHz paths can cause resampling, clicks, delayed starts, or
endpoints that fail to open. Do not change exclusive-mode settings without
evidence of an exclusive-access problem.

## 4. Configure VoiceMeeter

### Hardware Input 1

Select the physical headset microphone, for example:

```text
WDM: Headset Microphone (TCA YOKE BOEING)
```

Start with:

- Fader at `0.0 dB`
- `A` off
- `B` on
- Not muted

`A` off prevents permanent microphone sidetone. `B` on routes the microphone
to the B1 capture endpoint used by Altitude.

### Virtual Input

The Helper targets:

```text
Voicemeeter Input (VB-Audio Voicemeeter VAIO)
```

Configure the `VIRTUAL INPUT` strip with:

- `A` off
- `B` on
- Start near `-9 dB`
- Not muted

Adjust only this fader when balancing TTS against the hardware microphone.

### B1 bus

Configure B1 with:

- Master at `0.0 dB`
- Not muted
- Mono initially off

Do not use the B master for TTS volume because it changes both microphone and
TTS levels.

### A1 and hardware output

This architecture does not require VoiceMeeter to play audio to the headset.
Altitude handles headset output directly. Leave A routing off for both input
strips. A1 may remain empty or show `Internal Master CLOCK`.

Opening the same headset from both VoiceMeeter A1 and Altitude can cause device
conflicts, including SAPI error `SPERR_DEVICE_BUSY` or `0x80045006`.

A moving B meter with no direct headset sound is normal. B1 is a capture bus,
not a monitoring output.

## 5. Persist VoiceMeeter settings

From the VoiceMeeter menu:

1. Choose `Save Settings...` and save `VoiceMeeterDefault.xml`.
2. Enable `Load Settings on Startup`.
3. Enable `System Tray (Run at Startup)`.
4. Optionally enable `Auto Restart Audio Engine`.
5. Disable `Show App On Startup` if VoiceMeeter should start minimized.

After the next Windows login, verify that VoiceMeeter is running, Hardware
Input 1 still contains the microphone, and both required B routes are enabled.

## 6. Keep normal Windows defaults

Do not make VoiceMeeter the global Windows playback or recording default for
this setup.

Recommended separation:

- Normal Windows playback: physical headset or the existing output
- Normal Windows recording: physical headset microphone
- Helper TTS: explicit `Voicemeeter Input` endpoint
- Altitude input: explicit `Voicemeeter Out B1` endpoint
- Altitude output: explicit physical headset endpoint

Making `Voicemeeter Input` the global playback default can place all desktop
audio on B1 and therefore into the radio transmit path.

## 7. Configure the Altitude audio guard

The guard is disabled in a fresh Helper installation. First select and verify
the correct devices manually in Altitude. Then configure:

```ini
ALTITUDE_AUDIO_GUARD=1
ALTITUDE_AUDIO_INPUT=
ALTITUDE_AUDIO_OUTPUT=
ALTITUDE_AUDIO_INPUT_MATCH=Voicemeeter Out B1
ALTITUDE_AUDIO_OUTPUT_MATCH=TCA YOKE BOEING
```

The guard resolves active Windows endpoints by friendly-name match and writes
their endpoint IDs into:

```text
D:\X-Plane 12\IVAO_Pilot_Client.conf
```

It runs at Helper startup, after `Reload Config`, and every 60 seconds. Altitude
must be restarted after an endpoint ID changes because the guard updates the
persistent file, not an already-open Altitude audio stream.

A match is accepted only when exactly one active endpoint matches. With zero
or multiple matches, the Altitude file remains unchanged. Use a longer, unique
match and inspect the Helper log.

Expected log lines include:

```text
Altitude audio guard: input match 'Voicemeeter Out B1' -> Voicemeeter Out B1 (VB-Audio Voicemeeter VAIO)
Altitude audio guard: output match 'TCA YOKE BOEING' -> Headset Earphone (TCA YOKE BOEING)
Altitude audio guard: OK (...\IVAO_Pilot_Client.conf)
```

## 8. Configure the Helper for local proof

Use an inert initial configuration. Replace the example callsign with the exact
callsign used for the current Altitude connection.

```ini
AUTO_UNICOM_MODE=off
ALTITUDE_CALLSIGN=DLH3210
AUTO_UNICOM_FREQUENCY_KHZ=122800
AUTO_UNICOM_CONFIRM_TIMEOUT_MS=5000
AUTO_UNICOM_COMPOSER_STALE_MS=15000
AUTO_UNICOM_GATE_MAX_AGE_MS=2500
AUTO_UNICOM_FINAL_GATE_TIMEOUT_MS=2500
AUTO_UNICOM_MESSAGE_FIELD_NAME=Message
AUTO_UNICOM_SEND_BUTTON_TEXT=SEND

AUTO_UNICOM_VOICE_MODE=local
AUTO_UNICOM_VOICE_OUTPUT=
AUTO_UNICOM_VOICE_OUTPUT_MATCH=VoiceMeeter Input
AUTO_UNICOM_VOICE_LOCAL_OUTPUT=
AUTO_UNICOM_VOICE_LOCAL_OUTPUT_MATCH=VoiceMeeter Input
AUTO_UNICOM_VOICE_SAPI_VOICE=Microsoft David Desktop
AUTO_UNICOM_VOICE_SAPI_RATE=0
AUTO_UNICOM_VOICE_VOLUME=100
AUTO_UNICOM_VOICE_PTT_LEAD_MS=250
AUTO_UNICOM_VOICE_PTT_TAIL_MS=250
AUTO_UNICOM_VOICE_RECEIVE_WAIT_MS=5000
AUTO_UNICOM_VOICE_RECEIVE_QUIET_MS=1000
AUTO_UNICOM_VOICE_PTT_CONFIRM_MS=2500
AUTO_UNICOM_VOICE_TEST_TEXT=Auto Unicom voice audio test
```

Important distinctions:

| Setting | Meaning |
|---|---|
| `AUTO_UNICOM_MODE=off` | No YAL event can submit text during audio setup. |
| `ALTITUDE_CALLSIGN` | Must equal the current Altitude network callsign. |
| `AUTO_UNICOM_VOICE_MODE=local` | Automatic voice cannot press PTT. |
| `AUTO_UNICOM_VOICE_OUTPUT_MATCH` | Radio TTS output used later in `radio` mode. |
| `AUTO_UNICOM_VOICE_LOCAL_OUTPUT_MATCH` | Output used by local readback and the audio-test command. |

Apply changes with:

```text
Plugins > YAL Auto-Unicom Helper > Reload Config
```

Confirm the reload in `YAL_AutoUnicomHelper.log`.

## 9. Enable YAL integration

YAL requires:

```text
IVAOAUTOUNICOM 1
```

At least one of these operating modes must also be active:

```text
AUTOFUNCTIONS 1
```

or:

```text
VOICEADVICEONLY 1
```

A conservative setup that avoids other automatic aircraft actions is:

```text
IVAOAUTOUNICOM 1
AUTOFUNCTIONS 0
VOICEADVICEONLY 1
```

YAL must report that the Auto-Unicom transport is connected, and the Helper
callsign must exactly match the current Altitude callsign.

## 10. Configure Altitude

Altitude should display:

```text
Input:  Voicemeeter Out B1 (VB-Audio Voicemeeter VAIO)
Output: Headset Earphone (TCA YOKE BOEING)
```

The actual endpoint names will vary. Confirm that `[AUDIO]` in
`IVAO_Pilot_Client.conf` contains the resolved B1 input ID. If Altitude is
already running when the file changes, close and restart Altitude.

## 11. Startup order

Use this order:

1. Start Windows and wait for VoiceMeeter to appear in the system tray.
2. Verify the microphone, B routes, and disabled A routes.
3. Start X-Plane.
4. Wait for YAL Auto-Unicom Helper and YAL to load completely.
5. Start Altitude or connect it to the simulator.
6. Verify Altitude input and output.
7. Connect to the IVAO network only after the audio devices are correct.

If VoiceMeeter starts after Altitude, Altitude may fail to open B1 or may fall
back to another input.

## 12. Local audio tests without radio transmission

### Test A: Helper TTS reaches B1

1. Confirm `AUTO_UNICOM_MODE=off` and `AUTO_UNICOM_VOICE_MODE=local`.
2. Run `Reload Config`.
3. Select this Helper menu item:

```text
Plugins > YAL Auto-Unicom Helper > Voice Audio Test
```

4. Verify that VoiceMeeter `VIRTUAL INPUT` and B1 meters move while A remains
   off.

Expected Helper log output contains:

```text
WASAPI_READY:Voicemeeter Input (VB-Audio Voicemeeter VAIO)
```

No direct headset sound is expected because A monitoring is disabled.

### Test B: Helper TTS reaches Altitude input

1. Enable Altitude's local audio test.
2. Do not press PTT.
3. Run the Helper audio-test command again.
4. Confirm that TTS returns through Altitude's local test to the headset.

The local test has noticeable loopback delay. Adjust TTS level with the
VoiceMeeter Virtual Input fader, not the B1 master.

### Test C: Hardware microphone reaches the same input

1. Leave Altitude's local audio test enabled.
2. Speak into the physical microphone without pressing PTT.
3. Verify movement on Hardware Input 1 and B1.
4. Confirm that the microphone returns through Altitude's local test.

Tests B and C must both work through the same Altitude input.

### Test D: Exclude desktop and Altitude output

1. Keep the microphone quiet.
2. Play a normal Windows system or desktop sound.
3. Confirm that VoiceMeeter Virtual Input and B1 do not move.
4. Play an Altitude login sound or monitor received radio audio.
5. Confirm that this audio also does not appear on B1.

Any desktop or Altitude output on B1 is a transmit-loop risk. Correct the
routing before continuing.

## 13. Prove Helper readiness

Before a productive YAL request, confirm all of these conditions:

- the current official YAL release includes Auto-Unicom Helper support;
- YAL Auto-Unicom is enabled;
- YAL Auto Functions or Voice Advice Only is active;
- `Discover Altitude UI` reports `DISCOVERY_OK`;
- `ALTITUDE_CALLSIGN` exactly matches the active Altitude callsign;
- Altitude is online;
- the selected transmit COM is tuned to `122.800` MHz;
- the Helper is in `send` mode;
- the Altitude message composer is empty.

The text-only controlled test in `USER_MANUAL.md` must already have produced
`SUBMITTED_VISIBLE` before radio voice is enabled.

## 14. Enable radio mode

Only after Tests B, C, and D pass and the callsign is verified, set:

```ini
AUTO_UNICOM_MODE=send
AUTO_UNICOM_VOICE_MODE=radio
```

Run `Reload Config`. This permits radio transmission but does not prove it.

## 15. Controlled live test

Run a real network test only when:

- Altitude is online.
- The selected transmit COM is tuned to `122.800` UNICOM.
- No manual radio or PTT action is in progress.
- The receiver has been quiet.
- YAL and the Helper are unambiguously ready.
- The shared B1/Altitude input path passed all local tests.

During transmission verify:

- Altitude's TX/PTT indication becomes active.
- PTT releases after audio finishes.
- VoiceMeeter Virtual Input and B1 show TTS activity.
- No feedback or loop occurs.

## 16. Success evidence

Successful text submission:

```text
result_code=21
SUBMITTED_VISIBLE
```

Successful radio voice:

```text
voice_result_code=20
TRANSMITTED
```

Typical Helper log evidence:

```text
Auto UNICOM: ... result=SUBMITTED_VISIBLE detail=SUBMITTED_VISIBLE
Auto UNICOM voice: prepared ... PCM bytes WASAPI_READY:Voicemeeter Input (VB-Audio Voicemeeter VAIO)
Auto UNICOM voice: ... mode=radio result=TRANSMITTED detail=VOICE_TRANSMITTED
```

`TRANSMITTED` proves the Helper's PTT/playback transaction. The Altitude local
test and VoiceMeeter meters are still required to prove the physical route.

## 17. Result codes

### Text channel

| Code | Name | Meaning |
|---:|---|---|
| 10 | `ACCEPTED` | Request accepted but not complete. |
| 20 | `PREVIEW_READY` | Preview completed without Altitude submission. |
| 21 | `SUBMITTED_VISIBLE` | Text submitted and found in visible Altitude history. |
| 30 | `REJECTED_TEXT` | Text payload rejected. |
| 31 | `REJECTED_POLICY` | Operating policy rejected the request. |
| 32 | `REJECTED_CONTEXT` | Live Altitude context was not eligible. |
| 40 | `FAILED_BEFORE_SUBMIT` | Failure before invoking SEND. |
| 41 | `UNCERTAIN_AFTER_SUBMIT` | Submission outcome is uncertain. Do not retry blindly. |
| 42 | `CANCELLED` | Transaction cancelled. |

### Voice channel

| Code | Name | Meaning |
|---:|---|---|
| 1 | `NOT_REQUESTED` | Voice was not requested. |
| 10 | `ACCEPTED` | Voice request accepted. |
| 20 | `TRANSMITTED` | PTT and audio transaction completed. |
| 30 | `REJECTED_TEXT` | Voice phrase rejected. |
| 31 | `DISABLED` | Voice is disabled or local readback completed. |
| 32 | `REJECTED_CONTEXT` | Radio context was not eligible. |
| 40 | `FAILED_BEFORE_PTT` | Failure before PTT activation. |
| 41 | `UNCERTAIN_AFTER_PTT` | PTT outcome is uncertain. Verify release before continuing. |
| 42 | `CANCELLED` | Voice transaction cancelled. |

## 18. Troubleshooting

### Altitude returns to the physical microphone after restart

Check that the Helper does not still contain an old match:

```ini
ALTITUDE_AUDIO_INPUT_MATCH=Voicemeeter Out B1
```

Run `Reload Config`, confirm the guard log, and restart Altitude.

### VoiceMeeter receives the microphone but Altitude local test does not

Check:

1. Hardware Input 1 has B enabled.
2. B1 master is not muted.
3. Altitude input is `Voicemeeter Out B1`.
4. VoiceMeeter started before Altitude.
5. Altitude was restarted after changing devices.
6. The Helper audio guard did not restore an old microphone endpoint.

### Virtual Input and B1 move but nothing is heard

This is normal without A monitoring. Listen through Altitude's local test. Do
not enable A as a shortcut because it adds an unrelated direct monitoring path.

### Altitude login sound works but the local input test does not

The login sound proves only Altitude output. Verify Altitude input separately.

### Hardware microphone works but Helper TTS does not

Check:

- `AUTO_UNICOM_VOICE_LOCAL_OUTPUT_MATCH=VoiceMeeter Input` for the local test
- `AUTO_UNICOM_VOICE_OUTPUT_MATCH=VoiceMeeter Input` for radio mode
- Helper log contains `WASAPI_READY:Voicemeeter Input ...`
- Virtual Input has B enabled and is not muted
- The configured SAPI voice is installed

### Helper TTS works but the hardware microphone does not

Check that Hardware Input 1 uses the physical microphone, is not muted, has B
enabled, and reaches the same Altitude B1 input.

### TTS is too loud or unclear

Lower the Virtual Input fader in small steps, for example from `-9 dB` toward
`-12 dB`. Keep the hardware microphone and B1 master near `0 dB` initially.

### Altitude local test has delay

Some loopback delay is normal. Clicks, dropouts, or multi-second stalls suggest
device conflicts or mismatched sample rates.

### `SPERR_DEVICE_BUSY` or `0x80045006`

VoiceMeeter A1 and Altitude may both be opening the physical headset. Leave A1
empty and let Altitude drive the headset directly.

### Desktop audio is transmitted

Immediately set `AUTO_UNICOM_VOICE_MODE=local` and reload. Confirm that
`Voicemeeter Input` is not the global Windows playback device and that no
desktop application or loopback channel is routed to B1.

### Altitude output loops into the microphone

Immediately use `local` mode. Altitude output must be the physical headset, not
VoiceMeeter or any B-routed endpoint.

### YAL does not produce requests

Check:

- `IVAOAUTOUNICOM 1`
- `AUTOFUNCTIONS 1` or `VOICEADVICEONLY 1`
- a current YAL release with Auto-Unicom Helper support
- the Helper callsign matches the Altitude callsign
- No open request or session-level transport block remains

### Text may have been sent but confirmation is missing

For `UNCERTAIN_AFTER_SUBMIT`, inspect Altitude history and logs before doing
anything else. Never immediately repeat the same message.

### PTT may have activated but the outcome is unclear

For `UNCERTAIN_AFTER_PTT`, first verify that PTT has released. Return to
`local` mode if release cannot be proven.

## 19. Maintenance

Repeat the local proof after:

- VoiceMeeter installation or update
- Altitude update
- YAL Auto-Unicom Helper update
- YAL Auto-Unicom integration update
- Windows feature update
- Headset USB-port or device-name change
- Sample-rate change
- Installation of another virtual audio driver

Windows may generate new endpoint IDs after a USB or driver change. The Helper
must resolve each configured match uniquely again.

## 20. In-flight rollback

If manual radio must be restored immediately and the mix path is uncertain:

1. Set `AUTO_UNICOM_MODE=off`, `AUTO_UNICOM_VOICE_MODE=local`, and
   `ALTITUDE_AUDIO_GUARD=0`.
2. Run `Reload Config`.
3. Set Altitude input directly to the physical headset microphone.
4. Keep Altitude output on the physical headset.
5. Restart Altitude and run its local test.

This restores direct manual radio but removes automatic TTS from Altitude. It
is a safe fallback, not a completed Auto-UNICOM configuration.

## 21. Proven reference configuration

### VoiceMeeter

```text
Hardware Input 1: Headset Microphone (TCA YOKE BOEING)
Hardware Input 1: A off, B on, 0.0 dB
Virtual Input:     A off, B on, approximately -9 dB
B master:          0.0 dB, not muted, mono off
A1:                no TCA headset; empty/Internal Master CLOCK
Sample rate:       48 kHz
Autostart:         on
System tray:       on
Show on startup:   off
```

### Altitude

```text
Input:  Voicemeeter Out B1 (VB-Audio Voicemeeter VAIO)
Output: Headset Earphone (TCA YOKE BOEING)
```

### YAL Auto-Unicom Helper

```ini
ALTITUDE_AUDIO_GUARD=1
ALTITUDE_AUDIO_INPUT=
ALTITUDE_AUDIO_OUTPUT=
ALTITUDE_AUDIO_INPUT_MATCH=Voicemeeter Out B1
ALTITUDE_AUDIO_OUTPUT_MATCH=TCA YOKE BOEING

AUTO_UNICOM_MODE=send
ALTITUDE_CALLSIGN=DLH3210
AUTO_UNICOM_FREQUENCY_KHZ=122800
AUTO_UNICOM_COMPOSER_STALE_MS=15000
AUTO_UNICOM_VOICE_MODE=radio
AUTO_UNICOM_VOICE_OUTPUT=
AUTO_UNICOM_VOICE_OUTPUT_MATCH=VoiceMeeter Input
AUTO_UNICOM_VOICE_LOCAL_OUTPUT=
AUTO_UNICOM_VOICE_LOCAL_OUTPUT_MATCH=Voicemeeter Input
AUTO_UNICOM_VOICE_SAPI_VOICE=Microsoft David Desktop
AUTO_UNICOM_VOICE_SAPI_RATE=0
AUTO_UNICOM_VOICE_VOLUME=100
AUTO_UNICOM_VOICE_PTT_LEAD_MS=250
AUTO_UNICOM_VOICE_PTT_TAIL_MS=250
AUTO_UNICOM_VOICE_RECEIVE_WAIT_MS=5000
AUTO_UNICOM_VOICE_RECEIVE_QUIET_MS=1000
AUTO_UNICOM_VOICE_PTT_CONFIRM_MS=2500
AUTO_UNICOM_VOICE_TEST_TEXT=Auto Unicom voice audio test
```

Replace device and callsign examples with values from the actual installation.

### YAL

```text
IVAOAUTOUNICOM 1
AUTOFUNCTIONS 0
VOICEADVICEONLY 1
```

## 22. Completion checklist

The setup is complete only when every item is true:

- VoiceMeeter starts automatically with the saved configuration.
- The hardware microphone appears on Hardware Input 1 with A off and B on.
- Helper TTS appears on Virtual Input with A off and B on.
- Desktop and Altitude output do not move B1.
- Altitude input is `Voicemeeter Out B1`.
- Altitude output is the physical headset.
- The audio guard confirms both unique endpoint matches.
- TTS and the hardware microphone both pass Altitude's local test.
- No PTT occurs during local tests.
- The text-only controlled test reports `SUBMITTED_VISIBLE` with the correct
  Altitude callsign.
- A controlled live test returns `result_code=21 SUBMITTED_VISIBLE`.
- Voice returns `voice_result_code=20 TRANSMITTED`.
- PTT releases after every transmission.
- Manual microphone radio still works.
- There is no feedback or permanent sidetone.

Setting `AUTO_UNICOM_VOICE_MODE=radio` alone is not proof. Completion requires
demonstrating the entire microphone and TTS path through the same Altitude input
without transmitting any unrelated audio.

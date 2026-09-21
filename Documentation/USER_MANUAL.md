# YAL Auto-Unicom Helper User Manual

## 1. About this plugin

YAL Auto-Unicom Helper is a Windows companion plugin for Yet Another Linda
(YAL). It allows YAL to send selected flight-status messages through IVAO
Altitude as normal UNICOM text messages and, optionally, as speech.

The Helper is designed for X-Plane 12 and supports both the Zibo Mod 737-800
and compatible LevelUp 737NG variants supported by the installed YAL release.

The Helper does not work by itself. YAL is mandatory:

- YAL detects the flight situation and creates the complete text and voice
  phrase.
- YAL Auto-Unicom Helper validates the live radio context and handles delivery.
- IVAO Altitude displays and sends the text through its normal message window.
- For radio voice, the Helper produces speech and operates the configured
  Altitude push-to-talk key.
- VoiceMeeter is needed only when automatic speech should be transmitted over
  IVAO radio.

The Helper does not generate messages, inspect flight phases independently or
connect directly to the IVAO network. Without a compatible YAL installation it
has no productive function.

## 2. Supported environment

The required environment is:

- Windows 10 or Windows 11
- X-Plane 12
- YAL 4.8b1 or newer; earlier YAL versions do not support the standalone
  Helper provider
- the Zibo Mod 737-800, or a compatible LevelUp 737NG variant supported by the
  installed YAL release
- IVAO Altitude
- X-Plane and Altitude running in the same Windows user session

The Helper is Windows-only because text submission, speech output and
push-to-talk use Windows services and the Altitude desktop interface.

For text-only operation, no virtual audio software is required. Radio voice
requires a tested audio route that combines the physical microphone and Helper
speech without feeding desktop or received radio audio back into Altitude.

## 3. How a message is delivered

The normal text path is:

```text
Flight situation -> YAL -> YAL Auto-Unicom Helper -> Altitude message window
```

YAL decides whether a message is relevant and prepares its final wording. The
Helper then checks that Altitude is available, the pilot is online, the
selected transmit COM is tuned to the configured UNICOM frequency, no other
request is active and the Altitude composer is available. It writes the text
into Altitude and invokes the normal `SEND` action.

A text transaction is considered successful only after the same text is
visible in Altitude's active-frequency history. This ensures that sent
messages remain visible to the pilot.

Voice is always additional to text. It never creates or transmits a voice-only
message. Automatic voice starts only after visible text submission succeeds.

## 4. Install YAL

Install YAL 4.8b1 or newer first. Earlier YAL versions do not discover the
standalone Helper provider and therefore cannot submit requests to this plugin.
Obtain YAL only from its official project and release pages:

- [YAL project](https://github.com/wahltho/YAL)
- [Official YAL downloads](https://github.com/wahltho/YAL/releases)

Download the packaged release asset rather than installing a source-code
archive. Install it as:

```text
X-Plane 12/
  Resources/
    plugins/
      YAL/
```

Start X-Plane once and verify that this menu is available:

```text
Plugins > Yet Another Linda
```

Complete the normal YAL setup for the aircraft before enabling Auto-Unicom.
The full YAL product manual remains the reference for YAL installation,
aircraft support and general operation.

### Related YAL extension: YAL Hoppie Helper

[YAL Hoppie Helper](https://github.com/wahltho/YAL-Hoppie-Helper) is the other
public YAL companion plugin. It provides the Hoppie ACARS/CPDLC HTTP bridge for
supported aircraft and can optionally integrate with YAL. It is a separate
product and is not required for YAL Auto-Unicom Helper.

Obtain its packaged releases from the
[official YAL Hoppie Helper downloads](https://github.com/wahltho/YAL-Hoppie-Helper/releases).
Do not copy files between the two Helper plugin folders; install and configure
each product according to its own documentation.

## 5. Install YAL Auto-Unicom Helper

Close X-Plane before installing or updating the Helper. Copy the complete
packaged `YAL_AutoUnicomHelper` folder into the X-Plane plugin directory so the
result is:

```text
X-Plane 12/
  Resources/
    plugins/
      YAL_AutoUnicomHelper/
        64/
          win.xpl
        resources/
          auto_unicom_chime.wav
        Documentation/
          USER_MANUAL.md
          Auto-UNICOM-Voice-Setup-Guide.md
```

Start X-Plane and verify that this menu is available:

```text
Plugins > YAL Auto-Unicom Helper
```

On first start, the Helper creates:

```text
X-Plane 12/Output/preferences/YAL_AutoUnicomHelper.prf
X-Plane 12/Output/preferences/YAL_AutoUnicomHelper.log
```

The first configuration is intentionally inactive. No automatic message can
be sent until both YAL and the Helper are explicitly enabled.

## 6. Enable Auto-Unicom in YAL

Close X-Plane before changing the YAL preference file. In:

```text
X-Plane 12/Output/preferences/YAL.prf
```

enable Auto-Unicom:

```text
IVAOAUTOUNICOM 1
```

YAL also requires one of its two operating modes:

```text
AUTOFUNCTIONS 1
```

or:

```text
VOICEADVICEONLY 1
```

For Auto-Unicom without enabling other automatic cockpit actions, use:

```text
IVAOAUTOUNICOM 1
AUTOFUNCTIONS 0
VOICEADVICEONLY 1
```

Restart X-Plane after changing the YAL preference file. YAL will then create
and queue Auto-Unicom messages when its supported flight events occur. The
Helper still decides whether the live IVAO context permits delivery.

Typical messages cover departure preparation and taxi, runway operations,
climb and cruise progress, holding, descent and approach, final, runway
vacating and arrival at parking. YAL may suppress, replace or expire a queued
message when a newer flight event makes it obsolete.

## 7. Configure the Helper safely

Open:

```text
X-Plane 12/Output/preferences/YAL_AutoUnicomHelper.prf
```

Use this safe starting point:

```ini
AUTO_UNICOM_MODE=off
ALTITUDE_CALLSIGN=DLH3210
AUTO_UNICOM_FREQUENCY_KHZ=122800
AUTO_UNICOM_VOICE_MODE=off
ALTITUDE_AUDIO_GUARD=0
```

Replace `DLH3210` with the exact callsign used for the current Altitude
connection. The Helper intentionally cannot obtain the callsign from IVAO. An
empty callsign prevents YAL from submitting messages; an incorrect callsign
would produce incorrect message content.

Keep `AUTO_UNICOM_MODE=off` during initial setup. Leave
`ALTITUDE_AUDIO_GUARD=0` until Altitude's audio devices have been selected and
verified manually.

After editing the Helper preference file, use:

```text
Plugins > YAL Auto-Unicom Helper > Reload Config
```

The Helper log records whether the configuration was accepted. Invalid
configuration is rejected as a whole, leaving the previous active settings in
place.

## 8. Helper menu

The Helper menu contains:

- `AUTO UNICOM > TEXT`: enables or disables productive text transport.
- `AUTO UNICOM > VOICE > OFF`: disables automatic voice.
- `AUTO UNICOM > VOICE > LOCAL READBACK`: speaks the YAL voice phrase locally
  after successful text submission and never presses PTT.
- `AUTO UNICOM > VOICE > RADIO`: routes speech to the radio output and permits
  automatic PTT after successful text submission.
- `Voice Audio Test`: plays a fixed test phrase without requiring a YAL flight
  event and without pressing PTT.
- `Chime Test`: tests the successful-text notification sound.
- `Discover Altitude UI`: performs a read-only check of Altitude's message
  controls.
- `Reload Config`: reloads supported Helper settings while X-Plane is running.

Menu changes are persistent. Turning text off also turns voice off. Selecting
local or radio voice enables text because voice-only operation is not allowed.
Changes wait until an active request has completed.

## 9. Verify Altitude before sending

Start IVAO Altitude in the same Windows session as X-Plane. Its main window may
be visible or minimized, but Altitude must remain running. Then select:

```text
Plugins > YAL Auto-Unicom Helper > Discover Altitude UI
```

The operation is read-only. Require `DISCOVERY_OK` in:

```text
X-Plane 12/Output/preferences/YAL_AutoUnicomHelper.log
```

If discovery fails, do not enable productive sending. Confirm that exactly one
Altitude window is open and that the Altitude message composer and `SEND`
button are available.

## 10. Test text without sending

Set:

```ini
AUTO_UNICOM_MODE=dry_run
AUTO_UNICOM_VOICE_MODE=off
```

Reload the Helper configuration. During the next suitable YAL flight event,
YAL prepares the real message and the Helper completes it as a preview. Nothing
is written into Altitude and nothing is sent to IVAO.

The Helper log should show `PREVIEW_READY`. This proves the YAL-to-Helper path,
but it does not prove Altitude submission.

## 11. Enable text-only operation

Before the first live test:

1. Confirm the exact Altitude callsign in the Helper configuration.
2. Confirm `Discover Altitude UI` succeeds.
3. Connect Altitude to IVAO.
4. Select the transmit COM used for UNICOM.
5. Tune that active COM exactly to `122.800` MHz.
6. Ensure the Altitude message composer is empty.
7. Set `AUTO_UNICOM_MODE=send` and `AUTO_UNICOM_VOICE_MODE=off`.
8. Reload the configuration.

Use the next appropriate YAL flight event for a controlled test. Success
requires the exact text to appear in Altitude's active-frequency history. The
Helper log then reports `SUBMITTED_VISIBLE`, and the success chime plays when
enabled.

The Helper does not send merely because it is enabled. It remains blocked when
Altitude is offline, the selected transmit COM is not on the configured UNICOM
frequency, another request is active or the Altitude composer contains other
text.

## 12. Voice modes

### Off

`OFF` is the normal starting mode. YAL may still provide a voice phrase with a
request, but the Helper does not play or transmit it.

### Local readback

`LOCAL READBACK` speaks the YAL voice phrase through the configured local
output only after its text message has been submitted visibly. It never presses
PTT and cannot transmit speech to IVAO.

The separate `Voice Audio Test` command is safer for initial setup because it
tests speech without waiting for a YAL event or sending text.

### Radio

`RADIO` sends speech through the configured radio audio endpoint. Before PTT,
the Helper waits for the selected Altitude receiver to remain quiet. Any
received transmission restarts the quiet period. The Helper checks the online
and frequency context again before and during transmission and releases its
owned PTT key when the operation ends. `PTT_KEY` must match the key configured
in Altitude. Altitude normally does not play the pilot's own network
transmission back, and radio mode adds no permanent sidetone.

Radio voice must not be enabled until the full microphone path has passed local
testing. Follow the dedicated guide:

[Auto-UNICOM Voice Setup with YAL, IVAO Altitude, and VoiceMeeter](Auto-UNICOM-Voice-Setup-Guide.md)

That guide covers VoiceMeeter installation, the B1 microphone mix, Altitude
input and output selection, SAPI voice selection, PTT configuration, local
proof, controlled radio testing and feedback prevention.

## 13. Normal operation

For each flight:

1. Start VoiceMeeter first when radio voice is used.
2. Start X-Plane and wait for YAL and the Helper to load.
3. Start or connect Altitude.
4. Confirm the Altitude callsign matches `ALTITUDE_CALLSIGN`.
5. Confirm the intended transmit COM and frequency before each expected
   message.
6. Keep the Altitude composer clear unless entering a manual message.
7. Watch Altitude history to see every successfully submitted text.
8. Monitor PTT release after every automatic radio transmission.

YAL owns message timing and content. The Helper does not bypass YAL and offers
no manual message-composition interface. YAL's `Repeat Last IVAO Auto-Unicom
Message` command can request the last valid message again, but it should be used
only after confirming that the previous attempt was not sent.

## 14. IVAO operational use and responsibility

YAL Auto-Unicom Helper is an independent third-party utility. It is not an
IVAO product and is not affiliated with or endorsed by IVAO. IVAO Altitude
remains the only software that connects this installation to the IVAO network.
The Helper does not replace or modify the pilot's obligations under the current
IVAO rules and local procedures.

Use the Helper within these operational limits:

- Send automatic messages only when no applicable ATC is available and the
  selected transmit COM is tuned to UNICOM `122.800`.
- Limit messages to concise operational position and intention reports that
  assist nearby traffic with conflict avoidance and sequencing. Do not use
  Auto-Unicom for chat, advertising or unrelated content.
- Use appropriate aviation English for air-to-air communication.
- Keep text enabled alongside radio voice where practical so text-only pilots
  can receive the same operational report.
- Continue monitoring the frequency and nearby traffic. Detected RX activity
  delays automatic PTT, but this protection does not replace active listening
  or the pilot's decision not to transmit.
- Remain present and able to respond to ATC, force-act messages and network
  supervisors. Automatic reporting does not permit an unattended connection.
- Disable or override automation whenever the current traffic situation, ATC
  instruction or local procedure makes an automatic report inappropriate.

The Helper can verify that Altitude reports online, that the selected transmit
COM is tuned to the configured UNICOM frequency and that the selected receiver
is quiet before automatic PTT. It cannot determine complete ATC coverage,
interpret the traffic situation or decide whether a message is operationally
appropriate. Those decisions remain the pilot's responsibility.

Consult the current official rules before use:

- [IVAO Rules](https://wiki.ivao.aero/en/home/ivao/rules)
- [IVAO Regulations](https://wiki.ivao.aero/en/home/ivao/regulations)

## 15. Safety behavior

The Helper uses these protections:

- Productive text and voice are disabled in a fresh installation.
- Text is sent only while Altitude is online and the selected transmit COM is
  exactly on the configured UNICOM frequency.
- The live context is checked again immediately before `SEND` and before PTT.
- Text success requires visible confirmation in Altitude history.
- Voice cannot run before successful text submission.
- Radio voice waits for a quiet receiver and does not interrupt received audio.
- Only one request can run at a time.
- Manual or focused composer text is preserved.
- A stale Helper-owned composer draft can be cleared after the configured
  timeout so a later YAL message is not blocked indefinitely.
- A result that may have reached `SEND` or PTT is never retried automatically.

If the log reports `UNCERTAIN_AFTER_SUBMIT`, inspect Altitude history before
doing anything else. Never immediately repeat the same message. If it reports
`UNCERTAIN_AFTER_PTT`, first verify that PTT has released.

## 16. Troubleshooting

### The Helper menu is missing

Confirm that `win.xpl` is located under
`Resources/plugins/YAL_AutoUnicomHelper/64/`. Check `X-Plane 12/Log.txt` for a
plugin loading error. The Helper cannot run on macOS or Linux.

### YAL does not create messages

Check that:

- the installed YAL release includes Auto-Unicom Helper support;
- `IVAOAUTOUNICOM 1` is present in `YAL.prf`;
- either `AUTOFUNCTIONS 1` or `VOICEADVICEONLY 1` is active;
- a supported YAL aircraft is loaded;
- the current flight situation provides enough data for the message;
- X-Plane was restarted after changing `YAL.prf`.

Inspect YAL's `SASLLog.txt` for Auto-Unicom messages when the feature remains
inactive.

### A YAL message is not accepted by the Helper

Check that:

- `ALTITUDE_CALLSIGN` exactly matches the current Altitude callsign;
- the Helper is in `send` or `dry_run` mode as intended;
- Altitude is running and online;
- the selected transmit COM is tuned to `122.800` MHz;
- no earlier request is active;
- the Altitude composer does not contain a manual draft.

### Altitude discovery fails

Close duplicate Altitude windows, reopen the normal message window and run
`Discover Altitude UI` again. Keep the configured Altitude window title,
message-field name and send-button text at their defaults unless an Altitude
version requires different visible labels.

### A composer draft blocks later messages

The Helper clears only text that it can prove it owns. It deliberately preserves
manual, edited or keyboard-focused content. Send or clear a manual draft in
Altitude yourself. An unchanged stale Helper-owned draft is recovered after
`AUTO_UNICOM_COMPOSER_STALE_MS`, which defaults to 15 seconds.

### Text appears but voice does not

Confirm the selected voice mode and check the Helper log. Use `Voice Audio
Test` in local mode first. For radio mode, follow the VoiceMeeter guide and
confirm that the selected receiver is not continuously active.

### The wrong audio device returns after restart

Verify the device manually in Altitude first. Then configure and enable the
optional audio guard only with unique endpoint matches. The audio guard updates
Altitude's persisted configuration; an already-open Altitude session may need
to be restarted.

### Emergency rollback during a flight

1. Set text off and voice to local or off from the Helper menu.
2. Disable `ALTITUDE_AUDIO_GUARD` and reload the Helper configuration.
3. Select the physical headset microphone directly in Altitude.
4. Keep Altitude output on the physical headset.
5. Restart Altitude if its audio stream does not change.

This restores manual radio operation without automatic voice.

## 17. Logs and evidence

The main diagnostic files are:

```text
X-Plane 12/Output/preferences/YAL_AutoUnicomHelper.log
X-Plane 12/Resources/plugins/YAL/data/output/SASLLog.txt
X-Plane 12/Log.txt
```

Useful Helper outcomes include:

- `PREVIEW_READY`: YAL and Helper completed a dry-run request.
- `SUBMITTED_VISIBLE`: text was sent and found in Altitude history.
- `FAILED_BEFORE_SUBMIT`: no `SEND` action occurred.
- `UNCERTAIN_AFTER_SUBMIT`: the outcome must be checked manually.
- `VOICE_LOCAL_READBACK_PLAYED`: local speech completed without PTT.
- `VOICE_TRANSMITTED`: the Helper completed its radio voice transaction.
- `FAILED_BEFORE_PTT`: voice did not reach PTT activation.
- `UNCERTAIN_AFTER_PTT`: verify PTT release before continuing.

Logs prove software decisions, not the physical audio route. VoiceMeeter meters,
Altitude's local audio test and a controlled live test are still required for
radio voice acceptance.

## 18. Updates and maintenance

Install Helper updates only while X-Plane is closed. The preferences and logs
under `Output/preferences` are outside the plugin folder and remain in place
when the plugin folder is replaced.

After updating YAL, the Helper, Altitude, VoiceMeeter, Windows or an audio
driver:

1. Run `Discover Altitude UI` again.
2. Confirm the callsign and text-only path.
3. Repeat `Voice Audio Test` when voice is enabled.
4. Repeat the VoiceMeeter local tests before returning to radio mode after any
   audio-related change.

Do not assume that saved Windows endpoint IDs remain valid after a driver,
device or USB-port change.

## 19. Uninstall

Close X-Plane and remove:

```text
X-Plane 12/Resources/plugins/YAL_AutoUnicomHelper
```

The following files may be retained for a later reinstall or deleted manually:

```text
X-Plane 12/Output/preferences/YAL_AutoUnicomHelper.prf
X-Plane 12/Output/preferences/YAL_AutoUnicomHelper.log
```

Remove or disable `IVAOAUTOUNICOM` in `YAL.prf` when YAL should no longer
produce Auto-Unicom events.

## 20. Completion checklist

Text operation is ready only when:

- YAL and the Helper both load without errors.
- YAL Auto-Unicom is enabled together with Auto Functions or Voice Advice Only.
- The Helper callsign exactly matches the current Altitude callsign.
- `Discover Altitude UI` succeeds.
- Dry run returns `PREVIEW_READY`.
- A controlled online test appears in Altitude history as
  `SUBMITTED_VISIBLE`.
- Sending is blocked when offline or tuned away from UNICOM.

Radio voice is ready only when the additional VoiceMeeter guide checklist is
complete, the physical microphone still works, no desktop or received audio
enters the transmit mix, RX activity delays PTT and every automatic PTT action
releases correctly.

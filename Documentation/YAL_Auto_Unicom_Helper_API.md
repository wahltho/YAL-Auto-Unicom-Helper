# YAL Auto-Unicom Helper API v3

YAL 4.8b2 or newer is required for discovery of this standalone provider.

## Scope

YAL owns message generation, phase logic, phraseology and telephony. YAL
Auto-Unicom Helper owns transport validation and submission through IVAO
Altitude's normal message UI. The helper performs no message generation and no
telephony resolution.

## Provider discovery

YAL should use this provider order:

1. `wahltho/autounicom/api_version`
2. `ivao_monitor/autounicom/api_version`

Require API version 3, `ready=1` and a non-empty `effective_callsign`. Never
write one request to more than one provider.

## DataRefs

| DataRef | Type | Access | Meaning |
|---|---|---|---|
| `wahltho/autounicom/api_version` | int | read-only | Major API version, `3`. |
| `wahltho/autounicom/ready` | int | read-only | All API DataRefs registered. |
| `wahltho/autounicom/mode` | int | read-only | `0=off`, `1=dry_run`, `2=send`. |
| `wahltho/autounicom/transport_state` | int | read-only | Current generic eligibility. |
| `wahltho/autounicom/effective_callsign` | data | read-only | Normalized configured `ALTITUDE_CALLSIGN`; empty when unset. |
| `wahltho/autounicom/request_text` | data | read-write | Complete final Altitude message. |
| `wahltho/autounicom/request_seq` | int | read-write | Commit marker; strictly increasing and nonzero. |
| `wahltho/autounicom/result_seq` | int | read-only | Sequence owning the current text result. |
| `wahltho/autounicom/result_code` | int | read-only | Text result code. |
| `wahltho/autounicom/result_detail` | data | read-only | Stable text result detail. |
| `wahltho/autounicom/request_channels` | int | read-write | `1=text`, `3=text+voice`; voice-only is forbidden. |
| `wahltho/autounicom/request_voice_text` | data | read-write | Optional speakable phrase. |
| `wahltho/autounicom/voice_state` | int | read-only | Current voice worker state. |
| `wahltho/autounicom/voice_result_seq` | int | read-only | Sequence owning the voice result. |
| `wahltho/autounicom/voice_result_code` | int | read-only | Independent voice result. |
| `wahltho/autounicom/voice_result_detail` | data | read-only | Stable voice result detail. |

No trailing NUL is preferred for strings. Exactly one trailing NUL is tolerated
for SASL compatibility. Embedded or multiple NUL bytes are rejected.

## Transaction

For each request:

1. Wait for `transport_state=5` (`READY`).
2. Write `request_text`.
3. Write `request_channels`.
4. If channels are `3`, write `request_voice_text`.
5. Commit by writing a new positive `request_seq` last.
6. Wait until `result_seq` equals the submitted sequence and `result_code` is terminal.
7. If voice was requested, independently wait for matching `voice_result_seq`.

The sequence, text, channels and voice text form one immutable request snapshot.
Only one request may be active. YAL must not automatically retry
`UNCERTAIN_AFTER_SUBMIT` or `UNCERTAIN_AFTER_PTT`.

## Transport states

| Value | Name |
|---:|---|
| 0 | `UNAVAILABLE` |
| 1 | `OFFLINE` |
| 2 | `NOT_UNICOM` |
| 3 | `BLOCKED_ATC` reserved, never emitted |
| 4 | `BUSY` |
| 5 | `READY` |
| 6 | `ERROR` |

Eligibility depends on helper readiness, Windows support, Altitude presence,
Altitude online state and the selected transmit COM being tuned exactly to
`AUTO_UNICOM_FREQUENCY_KHZ`. It does not depend on an ATC feed.

## Text result codes

| Value | Name |
|---:|---|
| 0 | `IDLE` |
| 10 | `ACCEPTED` |
| 20 | `PREVIEW_READY` |
| 21 | `SUBMITTED_VISIBLE` |
| 30 | `REJECTED_TEXT` |
| 31 | `REJECTED_POLICY` |
| 32 | `REJECTED_CONTEXT` |
| 40 | `FAILED_BEFORE_SUBMIT` |
| 41 | `UNCERTAIN_AFTER_SUBMIT` |
| 42 | `CANCELLED` |

`SUBMITTED_VISIBLE` means the text was invoked through Altitude's normal SEND
path and subsequently found in visible active-frequency history. A
`COMPOSER_AMBIGUOUS` failure before submit is retried internally exactly once
after 500 ms with the same request. Post-submit uncertainty is never retried.

The helper marks the Altitude composer as owned before its first write. While
that ownership is active, `transport_state` is `BUSY` and later mailbox
requests wait. After `AUTO_UNICOM_COMPOSER_STALE_MS` (default 15000 ms, allowed
range 5000-300000 ms), the helper may clear an unchanged exact owned draft or a
partial prefix left before the complete write was verified. It reads the value
twice and requires the composer not to have keyboard focus. Empty, changing,
focused and foreign/manual content is not cleared. A complete draft modified by
the pilot is foreign content. This cleanup does not retry the old request,
including after any path on which `SEND` may have been invoked. The timeout is
reloadable through `Reload Config`; no API version change is required because
the v3 mailbox contract is unchanged.

## Voice

Voice modes are persistent:

- `off`: no voice delivery;
- `local`: SAPI readback through the local output, never PTT;
- `radio`: SAPI audio routed to Altitude's microphone path with confirmed PTT.

Radio voice starts only after visible text submission. It waits up to
`AUTO_UNICOM_VOICE_RECEIVE_WAIT_MS` for the selected Altitude receiver to be
idle continuously for `AUTO_UNICOM_VOICE_RECEIVE_QUIET_MS`. Any RX activity
restarts the quiet window. Context is checked again before and while PTT is
held.

## Callsign contract

The public helper does not use IVAO OAuth or fetch flight plans. Its
`effective_callsign` is the normalized `ALTITUDE_CALLSIGN` setting. The
operator must keep this setting equal to the callsign used for the current
Altitude connection. An empty setting intentionally prevents YAL from sending.

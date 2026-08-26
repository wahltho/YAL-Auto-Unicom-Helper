# Auto-UNICOM Voice mit YAL, YAL Auto-Unicom Helper, Altitude und VoiceMeeter

## Zweck dieser Anleitung

Diese Anleitung beschreibt einen sicheren Audiopfad, bei dem automatisch von YAL erzeugte Auto-UNICOM-Meldungen als Text und Sprache über IVAO Altitude übertragen werden können. Gleichzeitig bleibt das normale Headset-Mikrofon für manuellen ATC-Funk nutzbar.

Die Anleitung basiert auf einer praktisch erprobten Windows-/X-Plane-Konfiguration. Sie ist bewusst so aufgebaut, dass kein produktiver Sprachfunk aktiviert wird, bevor der vollständige Audiopfad lokal nachgewiesen wurde.

Das Ziel ist:

- YAL erzeugt den Meldungstext sowie eine für TTS optimierte Sprechfassung.
- YAL Auto-Unicom Helper übermittelt den Text an Altitude und erzeugt die TTS-Wiedergabe.
- VoiceMeeter mischt ausschließlich das Hardware-Mikrofon und die Helper-TTS.
- Altitude verwendet diesen Mix als Eingang.
- Altitude gibt empfangenen Funk direkt auf das physische Headset aus.
- Desktop-Audio und Altitude-Ausgabe gelangen nicht in den Sendemix.
- Es entsteht kein permanenter Sidetone.

## Sicherheitsgrundsatz

Während Einrichtung und Fehlersuche muss zunächst Folgendes gelten:

```ini
AUTO_UNICOM_VOICE_MODE=local
```

Erst nach erfolgreichen lokalen Tests wird auf Folgendes umgestellt:

```ini
AUTO_UNICOM_VOICE_MODE=radio
```

Wichtig: `AUTO_UNICOM_VOICE_MODE=local` verhindert die automatische PTT-Sprachübertragung. Es deaktiviert nicht zwingend die Textübermittlung, denn diese wird separat über `AUTO_UNICOM_MODE` gesteuert. Für reine Audiotests deshalb ausschließlich den vorgesehenen Audio-Testbefehl verwenden und keine echte YAL-Flugphase künstlich auslösen.

## Signalweg

```text
Hardware-Mikrofon -----------------------------------+
                                                     |
                                                     v
                                             VoiceMeeter B1
                                                     |
YAL -> YAL Auto-Unicom Helper -> TTS -> VoiceMeeter Input -----+
                                                     |
                                                     v
                                  Voicemeeter Out B1 (Capture)
                                                     |
                                                     v
                                            Altitude Input

Altitude Output ---------------------------------> TCA-Headset
Desktop-Audio -----------------------------------> normales Ausgabegerät
```

Der entscheidende Punkt ist die Trennung der beiden Richtungen:

- In den Altitude-Eingang geht nur VoiceMeeter B1.
- Aus Altitude heraus geht der Ton direkt auf das Headset, nicht zurück durch VoiceMeeter.
- Der Windows-Desktop wird nicht auf `Voicemeeter Input` umgestellt.

## Die verwirrenden VoiceMeeter-Gerätenamen

Windows benennt die Endpunkte aus Sicht der Programme, nicht aus Sicht des virtuellen Mischpults. Dadurch wirken die Namen zunächst vertauscht.

| Windows-Endpunkt | Typ | Aufgabe |
|---|---|---|
| `Voicemeeter Input (VB-Audio Voicemeeter VAIO)` | Wiedergabe/Render | Hierhin spielt YAL Auto-Unicom Helper die TTS. Das Signal erscheint im VoiceMeeter-Kanal `VIRTUAL INPUT`. |
| `Voicemeeter Out B1 (VB-Audio Voicemeeter VAIO)` | Aufnahme/Capture | Dies ist der fertige B1-Mix, den Altitude als Mikrofoneingang verwendet. |
| `Headset Microphone (TCA YOKE BOEING)` | Aufnahme/Capture | Physisches Mikrofon für manuellen Funk. |
| `Headset Earphone (TCA YOKE BOEING)` | Wiedergabe/Render | Physischer Kopfhörerausgang für Altitude. |

Merksatz: Helper-TTS geht in **VoiceMeeter Input** hinein; Altitude nimmt **Voicemeeter Out B1** auf.

## Voraussetzungen

- Windows 10 oder Windows 11
- X-Plane mit YAL und YAL Auto-Unicom Helper
- IVAO Altitude/Pilot Client
- DataRefTool zum Ausführen des lokalen Audio-Testbefehls
- ein funktionierendes Headset mit getrennt erkennbarem Mikrofon- und Wiedergabeendpunkt
- eine installierte Windows-SAPI-Stimme, zum Beispiel `Microsoft David Desktop`
- Administratorrechte nur für die erstmalige Treiberinstallation

Beispielpfade dieser Installation:

```text
X-Plane:              D:\X-Plane 12
YAL-Konfiguration:    D:\X-Plane 12\Output\preferences\YAL.prf
Helper-Konfiguration:D:\X-Plane 12\Output\preferences\YAL_AutoUnicomHelper.prf
Altitude-Audiodatei:  D:\X-Plane 12\IVAO_Pilot_Client.conf
Helper-Log:          D:\X-Plane 12\Output\preferences\YAL_AutoUnicomHelper.log
YAL-/SASL-Log:        D:\X-Plane 12\Resources\plugins\YAL\data\output\SASLLog.txt
VoiceMeeter-Settings: C:\Users\<Benutzer>\AppData\Roaming\VoiceMeeterDefault.xml
```

Die Konfigurationsdateien können persönliche Zugangsdaten enthalten. Sie niemals vollständig veröffentlichen oder ungeprüft an andere Personen weitergeben.

## 1. Vorhandene Windows-Audioendpunkte prüfen

Zunächst alle vorhandenen, auch derzeit nicht verbundenen Audioendpunkte erfassen. In einer normalen PowerShell kann beispielsweise Folgendes verwendet werden:

```powershell
Get-PnpDevice -Class AudioEndpoint -PresentOnly:$false |
    Sort-Object Status, FriendlyName |
    Format-Table Status, FriendlyName, InstanceId -AutoSize
```

Besonders auf virtuelle Geräte achten. Falls früher `Sonic Studio Virtual Mixer` vorhanden war, darf er nur benutzt werden, wenn:

- eine Render-Seite und eine Capture-Seite vorhanden sind,
- beide Seiten aktiv und stabil sind,
- das Hardware-Mikrofon und ein gezielt angewählter TTS-Renderstream gemeinsam gemischt werden können,
- Desktop-Audio nicht automatisch Bestandteil dieses Mixes wird,
- Altitude-Ausgabe nicht zurück in den Capture-Pfad gelangt.

Fehlt eine Seite oder ist die Mischung nicht eindeutig kontrollierbar, den Sonic-Studio-Endpunkt nicht verwenden.

## 2. VoiceMeeter Standard installieren

Wenn kein geeigneter virtueller Mixer vorhanden ist, VoiceMeeter Standard ausschließlich von VB-Audio beziehen:

<https://vb-audio.com/Voicemeeter/>

Keine Download-Portale oder neu verpackten Installer verwenden.

Nach der Treiberinstallation ist normalerweise ein Windows-Neustart erforderlich. Dieser Neustart erfolgt einmal nach der Installation, bevor die Endpunkte konfiguriert werden. X-Plane und Altitude vorher beenden. Spätere Änderungen an Fadern und Routing benötigen keinen PC-Neustart.

Die erprobte Installation verwendete VoiceMeeter Standard 1.1.2.2. Neuere Versionen können Menüpunkte leicht anders benennen.

## 3. Einheitlich 48 kHz verwenden

Für IVAO-Sprachfunk und VoiceMeeter sollte der gesamte Pfad auf 48 kHz eingestellt sein.

Zu prüfen sind mindestens:

- Hardware-Mikrofon
- VoiceMeeter-VAIO-Wiedergabeendpunkt
- VoiceMeeter-B1-Aufnahmeendpunkt
- physischer Headset-Ausgang
- VoiceMeeter-Systemeinstellungen

In den klassischen Windows-Soundeinstellungen befindet sich die Abtastrate unter den Geräteeigenschaften auf der Registerkarte `Erweitert`. Wenn mehrere Formate angeboten werden, 48.000 Hz wählen. Eine Mischung aus 44,1 und 48 kHz kann zu Resampling, Knacken, verzögertem Start oder nicht geöffneten Endpunkten führen.

Nicht vorsorglich die Exklusivmodus-Optionen aller Geräte verändern. Solche Änderungen sind erst eine gezielte Fehlerbehebung, wenn ein konkreter Beleg für einen Exklusivzugriff vorliegt.

## 4. VoiceMeeter konfigurieren

### Hardware Input 1

Als `Hardware Input 1` auswählen:

```text
Headset Microphone (TCA YOKE BOEING)
```

Wenn mehrere Treibermodelle angeboten werden, ist WDM normalerweise die erste Wahl. Danach prüfen, ob beim Sprechen der Pegel dieses Kanals ausschlägt.

Routing des Hardware-Mikrofons:

- `A` aus
- `B` an
- Fader zunächst `0,0 dB`
- Mute aus
- Mono nicht ohne konkreten Grund umschalten

`A aus` verhindert, dass man sich permanent selbst im Headset hört. `B an` legt das Mikrofon auf den virtuellen B1-Ausgang für Altitude.

### Virtual Input

Der Kanal `VIRTUAL INPUT` entspricht dem Windows-Renderendpunkt:

```text
Voicemeeter Input (VB-Audio Voicemeeter VAIO)
```

Routing des Virtual Input:

- `A` aus
- `B` an
- Fader als Startwert etwa `-9 dB`
- Mute aus

Die Absenkung betrifft nur die TTS-Lautstärke. Sie verhindert, dass die synthetische Stimme das Hardware-Mikrofon im Mix deutlich übertönt. Die Feineinstellung erfolgt später während des Altitude-Local-Tests.

### B1-Bus

Für den B-Bus:

- Fader `0,0 dB`
- Mute aus
- Mono zunächst aus

Den B-Master nicht zur TTS-Lautstärkeregelung verwenden, weil er Mikrofon und TTS gemeinsam verändert. Für die TTS-Lautstärke ausschließlich den Fader des Virtual Input verwenden.

### A1 und Hardware Out

In dieser Architektur wird VoiceMeeter nicht für die Wiedergabe zum Headset benötigt. Altitude gibt direkt auf das Headset aus. Deshalb:

- A1 nicht auf das TCA-Headset setzen
- A-Routing für Mikrofon und TTS ausgeschaltet lassen
- `Internal Master CLOCK` beziehungsweise ein leerer A1-Zustand ist zulässig

Das ist kein Fehler. Wird dasselbe TCA-Headset gleichzeitig von VoiceMeeter A1 und Altitude geöffnet, kann ein Gerätekonflikt entstehen. Ein beobachtetes Symptom war der SAPI-/Audiogerätefehler:

```text
0x80045006 SPERR_DEVICE_BUSY
```

Ein hoher B-Pegel bei gleichzeitig fehlendem Ton im Headset ist im normalen Betrieb ebenfalls kein Fehler: B1 ist ein virtueller Aufnahmebus und soll nicht direkt hörbar sein.

## 5. VoiceMeeter-Einstellungen dauerhaft speichern

Im VoiceMeeter-Menü:

1. `Save Settings...` wählen und als `VoiceMeeterDefault.xml` speichern.
2. `Load Settings on Startup` auf diese Datei setzen.
3. `Run on Windows Startup` aktivieren.
4. `System Tray` aktivieren.
5. `Show App On Startup` deaktivieren, wenn VoiceMeeter minimiert starten soll.

VoiceMeeter startet seine Audio-Engine beim Programmstart. `Auto Restart Audio Engine` ist eine zusätzliche Wiederanlaufoption bei Geräteausfällen, nicht die Voraussetzung für den normalen Start.

Nach einem Windows-Neustart kontrollieren:

- VoiceMeeter läuft im Infobereich.
- Hardware Input 1 enthält noch das Headset-Mikrofon.
- Hardware Input 1: A aus, B an.
- Virtual Input: A aus, B an.
- Der TTS-Fader steht wieder auf dem gespeicherten Wert.

## 6. Windows-Standardgeräte nicht unnötig verändern

Für diese Lösung müssen die globalen Windows-Standardgeräte nicht auf VoiceMeeter umgestellt werden.

Empfohlene Trennung:

- normales Windows-Wiedergabegerät: physisches Headset oder bisheriges Gerät
- normales Windows-Aufnahmegerät: physisches Headset-Mikrofon
- YAL Auto-Unicom Helper TTS: explizit `Voicemeeter Input`
- Altitude Input: explizit `Voicemeeter Out B1`
- Altitude Output: explizit physisches Headset

Würde `Voicemeeter Input` zum globalen Windows-Wiedergabestandard, könnte sämtliches Desktop-Audio auf dem B-Bus und damit im Funk landen. Genau das soll verhindert werden.

## 7. YAL-Auto-Unicom-Helper-Audio-Guard konfigurieren

Der Guard ist in einer frischen Installation deaktiviert. Erst nach manueller
Prüfung der richtigen Altitude-Geräte wird er bewusst aktiviert.

Die folgenden Einstellungen gehören in:

```text
D:\X-Plane 12\Output\preferences\YAL_AutoUnicomHelper.prf
```

```ini
ALTITUDE_AUDIO_GUARD=1
ALTITUDE_AUDIO_INPUT=
ALTITUDE_AUDIO_OUTPUT=
ALTITUDE_AUDIO_INPUT_MATCH=Voicemeeter Out B1
ALTITUDE_AUDIO_OUTPUT_MATCH=TCA YOKE BOEING
```

Der Audio-Guard sucht aktive Endpunkte über die Match-Texte und schreibt die gefundenen Geräte in:

```text
D:\X-Plane 12\IVAO_Pilot_Client.conf
```

Er prüft beim Pluginstart, nach `Reload Config` und anschließend alle 60
Sekunden. Altitude muss nach einer geänderten Endpoint-ID neu gestartet werden,
weil der Guard die persistente Datei und nicht Altitudes bereits geöffneten
Audiostream ändert.

Das ist ein wichtiger Fallstrick: Eine manuelle Geräteauswahl in Altitude kann später wieder überschrieben werden, wenn `ALTITUDE_AUDIO_INPUT_MATCH` noch auf das alte Hardware-Mikrofon zeigt. Der Match-Wert im Helper muss deshalb auf B1 zeigen.

Erwartete Logzeilen:

```text
Altitude audio guard: input match 'Voicemeeter Out B1' -> Voicemeeter Out B1 (VB-Audio Voicemeeter VAIO)
Altitude audio guard: output match 'TCA YOKE BOEING' -> Headset Earphone (TCA YOKE BOEING)
Altitude audio guard: OK (...\IVAO_Pilot_Client.conf)
```

Der Helper akzeptiert einen Match nur, wenn genau ein aktiver Endpunkt passt. Bei keinem oder mehreren Treffern bleibt `IVAO_Pilot_Client.conf` unverändert. Dann einen längeren eindeutigen Namen verwenden und nach `Reload Config` das Helper-Log kontrollieren.

## 8. YAL-Auto-Unicom-Helper-TTS zunächst lokal konfigurieren

Die relevante Startkonfiguration lautet:

```ini
AUTO_UNICOM_MODE=off
ALTITUDE_CALLSIGN=DLH3210
AUTO_UNICOM_FREQUENCY_KHZ=122800
AUTO_UNICOM_CONFIRM_TIMEOUT_MS=5000
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

Erklärung wichtiger Felder:

| Feld | Bedeutung |
|---|---|
| `AUTO_UNICOM_MODE=off` | Während der Audioeinrichtung kann kein YAL-Ereignis Text senden. |
| `ALTITUDE_CALLSIGN` | Muss exakt dem aktuell in Altitude verwendeten Network-Callsign entsprechen. |
| `AUTO_UNICOM_VOICE_MODE=local` | Keine automatische PTT-Sprachübertragung während der Einrichtung. |
| `AUTO_UNICOM_VOICE_OUTPUT_MATCH=VoiceMeeter Input` | Radio-TTS wird später gezielt auf den VoiceMeeter-Renderendpunkt gespielt. |
| `AUTO_UNICOM_VOICE_LOCAL_OUTPUT_MATCH=VoiceMeeter Input` | Der lokale Audio-Test wird ohne PTT in denselben geprüften VoiceMeeter-Pfad gespielt. |
| `AUTO_UNICOM_VOICE_SAPI_VOICE` | Zu verwendende installierte Windows-Stimme. |
| `PTT_LEAD_MS` | Wartezeit zwischen bestätigtem PTT-Start und Audiobeginn. |
| `PTT_TAIL_MS` | Wartezeit nach Audioende, bevor PTT freigegeben wird. |
| `RECEIVE_WAIT_MS` und `RECEIVE_QUIET_MS` | Schutz gegen Senden bei laufendem Empfang. |

Nach einer Änderung in X-Plane ausführen:

```text
Plugins > YAL Auto-Unicom Helper > Reload Config
```

Im Helper-Log muss die Änderung bestätigt werden. Falls Altitude bereits läuft, kann es die alte Geräteauswahl noch im Speicher halten. Altitude danach vollständig schließen und neu starten. Wenn der in X-Plane laufende Altitude-Pluginteil ebenfalls noch alte Audiowerte hält, ist ein vollständiger X-Plane-Neustart erforderlich. Ein PC-Neustart ist dafür nicht nötig.

## 9. YAL aktivieren

In der YAL-Konfiguration müssen folgende Voraussetzungen erfüllt sein:

```text
IVAOAUTOUNICOM 1
```

Zusätzlich muss mindestens eine der beiden Betriebsarten aktiv sein:

```text
AUTOFUNCTIONS 1
```

oder:

```text
VOICEADVICEONLY 1
```

Eine zurückhaltende Konfiguration, bei der YAL keine zusätzlichen Automatikhandlungen ausführen soll, ist beispielsweise:

```text
IVAOAUTOUNICOM 1
AUTOFUNCTIONS 0
VOICEADVICEONLY 1
```

YAL bindet die Helper-API nur, wenn YAL Auto-Unicom Helper verfügbar und kompatibel ist. Erwartete YAL-/SASL-Logzeilen sind:

```text
IVAO Auto-Unicom DataRefs bound version=3
IVAO Auto-Unicom API connected version=3 mode=2
IVAO Auto-Unicom enabled; waiting for YAL runtime events
```

Typische YAL-Fehlertexte:

```text
IVAO Auto-Unicom enabled but YAL Auto-Unicom Helper is not available
IVAO Auto-Unicom enabled but API DataRefs are not ready
IVAO Auto-Unicom enabled but API version or DataRefs are not supported
IVAO Auto-Unicom inactive: enable Auto Functions or Voice Advice Only
IVAO Auto-Unicom API ready but effective callsign is unavailable
```

Die jeweiligen Meldungen bedeuten:

- Helper-Plugin fehlt oder wurde nicht geladen.
- API-DataRefs sind noch nicht registriert.
- YAL und Helper verwenden inkompatible API-Versionen.
- Weder `AUTOFUNCTIONS` noch `VOICEADVICEONLY` ist aktiv.
- `ALTITUDE_CALLSIGN` fehlt oder entspricht nicht dem verwendeten Network-Callsign.

## 10. Altitude konfigurieren

In Altitude sollen folgende Geräte angezeigt werden:

```text
Input:  Voicemeeter Out B1 (VB-Audio Voicemeeter VAIO)
Output: Headset Earphone (TCA YOKE BOEING)
```

Wenn Altitude nach einem Neustart wieder das TCA-Mikrofon als Eingang anzeigt, nicht dauerhaft gegen den Helper-Guard ankämpfen. Zuerst kontrollieren:

```ini
ALTITUDE_AUDIO_INPUT_MATCH=Voicemeeter Out B1
```

Danach `Reload Config`, Altitude neu starten und im Helper-Log die Audio-Guard-Zeilen prüfen.

Die tatsächlich geschriebene Altitude-Datei muss unter `[AUDIO]` beim `INPUT` die Endpoint-ID von B1 enthalten. Endpoint-IDs können sich nach Treiberneuinstallation oder USB-Neuerkennung ändern; deshalb ist der eindeutige Namens-Match im Helper robuster als das manuelle Kopieren alter IDs.

## 11. Startreihenfolge

Eine robuste Reihenfolge ist:

1. Windows starten und warten, bis VoiceMeeter im Infobereich läuft.
2. VoiceMeeter kurz kontrollieren: Mikrofon auf Hardware Input 1, beide benötigten B-Schalter an, A-Schalter aus.
3. X-Plane starten.
4. YAL Auto-Unicom Helper und YAL vollständig laden lassen.
5. Altitude starten beziehungsweise mit dem Simulator verbinden.
6. Altitude Input/Output kontrollieren.
7. Erst danach IVAO online verbinden.

Wenn VoiceMeeter erst nach Altitude gestartet wird, kann Altitude den B1-Endpunkt beim Initialisieren nicht öffnen oder auf ein anderes Gerät zurückfallen.

## 12. Lokaler Audiotest ohne Funk

### Test A: Helper-TTS bis B1

1. `AUTO_UNICOM_VOICE_MODE=local` kontrollieren.
2. `Reload Config` ausführen.
3. In DataRefTool den X-Plane-Befehl ausführen:

```text
yal_autounicomhelper/autounicom_voice_audio_test
```

4. In VoiceMeeter beobachten:
   - Der Pegel von `VIRTUAL INPUT` muss ausschlagen.
   - Der B-Bus muss ausschlagen.
   - A bleibt aus.

Erwartete Helper-Logaussage enthält den ausgewählten Endpunkt:

```text
WASAPI_READY:Voicemeeter Input (VB-Audio Voicemeeter VAIO)
```

Ein Pegelausschlag ohne direkt hörbaren Headset-Ton ist hierbei normal, weil das A-Routing absichtlich deaktiviert ist.

### Test B: TTS am Altitude-Eingang

1. In Altitude den lokalen Audio-Test aktivieren.
2. Keine PTT-Taste drücken.
3. Den Helper-Audio-Test erneut ausführen.
4. Die TTS muss über den Altitude-Local-Test im Headset hörbar werden.

Der Altitude-Local-Test besitzt eine hörbare Rundlaufverzögerung. Das ist für diesen Test normal. Ist die TTS zu laut, den VoiceMeeter-Fader des Virtual Input schrittweise absenken, nicht den B-Master.

### Test C: Hardware-Mikrofon am gleichen Altitude-Eingang

1. Altitude-Local-Test aktiviert lassen.
2. Ohne PTT in das physische Mikrofon sprechen.
3. In VoiceMeeter müssen Hardware Input 1 und B ausschlagen.
4. Die eigene Sprache muss verzögert im Headset hörbar werden.

Nur wenn Test B und Test C über denselben Altitude-Input funktionieren, ist der gemeinsame Pfad nachgewiesen.

### Test D: Ausschluss von Desktop- und Altitude-Rückführung

1. Das Mikrofon vorübergehend stumm halten oder sehr ruhig sein.
2. Einen normalen Windows-Systemton oder Desktop-Ton abspielen.
3. Der VoiceMeeter-Virtual-Input und B dürfen dadurch nicht ausschlagen.
4. Einen Altitude-Login-/Logout-Ton oder empfangenen Funk prüfen.
5. Auch dieser Ton darf nicht auf B erscheinen.

Wenn Desktop-Audio auf B erscheint, ist meistens `Voicemeeter Input` fälschlich das globale Windows-Wiedergabegerät oder eine Desktop-Anwendung wurde gezielt dorthin geroutet.

Wenn Altitude-Ausgabe auf B erscheint, ist Altitudes Output fälschlich auf VoiceMeeter statt auf das physische Headset gestellt.

## 13. READY-Zustand mit DataRefTool nachweisen

Für API-Version 3 gelten vor einer produktiven YAL-Anforderung mindestens folgende Werte:

```text
wahltho/autounicom/api_version       = 3
wahltho/autounicom/ready             = 1
wahltho/autounicom/mode              = 2
wahltho/autounicom/transport_state   = 5
wahltho/autounicom/effective_callsign ist nicht leer
```

`ready=1` allein genügt nicht. YAL übergibt erst dann eine Nachricht, wenn die API kompatibel, der Sendemodus aktiv und der Transportzustand bereit ist.

## 14. Erst jetzt auf Funkbetrieb umstellen

Nur nach bestandenen Tests B, C und D sowie kontrolliertem Callsign:

```ini
AUTO_UNICOM_MODE=send
AUTO_UNICOM_VOICE_MODE=radio
```

Danach:

```text
Plugins > YAL Auto-Unicom Helper > Reload Config
```

Der Moduswechsel allein ist kein Erfolgsnachweis. Er erlaubt nur, dass der Helper bei einer zulässigen Anforderung PTT verwendet.

## 15. Sicherer echter Funktest

Ein echter Test darf nur stattfinden, wenn:

- Altitude online ist,
- die aktive TX-Frequenz `122.800` UNICOM ist,
- die ausgewählte TX-COM tatsächlich auf UNICOM steht und kein laufender manueller Funkvorgang besteht,
- der Empfang ruhig ist,
- keine fremde oder manuelle PTT-Aktivität läuft,
- Helper und YAL eindeutig READY sind,
- Mikrofon und TTS vorher lokal über denselben B1-/Altitude-Eingang nachgewiesen wurden.

Während der Übertragung beobachten:

- Altitudes TX-/PTT-Anzeige wird aktiv.
- PTT wird nach dem Audioende immer wieder freigegeben.
- Der VoiceMeeter-Virtual-Input und B zeigen TTS-Pegel.
- Es entsteht keine Rückkopplung.

## 16. Erfolgsnachweis in Log und DataRefs

Erfolgreiche Textübermittlung:

```text
result_code=21
SUBMITTED_VISIBLE
```

Erfolgreiche Sprachübermittlung:

```text
voice_result_code=20
TRANSMITTED
```

Typische erfolgreiche Helper-Logsequenz:

```text
Auto UNICOM: ... result=SUBMITTED_VISIBLE detail=SUBMITTED_VISIBLE
Auto UNICOM voice: prepared ... PCM bytes WASAPI_READY:Voicemeeter Input (VB-Audio Voicemeeter VAIO)
Auto UNICOM voice: ... mode=radio result=TRANSMITTED detail=VOICE_TRANSMITTED
```

`TRANSMITTED` beweist die erfolgreiche PTT-/Playback-Transaktion des Helpers. Der lokale Altitude-Test bleibt trotzdem wichtig, weil nur er zusammen mit den VoiceMeeter-Pegeln belegt, dass Mikrofon und TTS tatsächlich denselben Altitude-Capture-Eingang erreichen.

## 17. Ergebnis- und Fehlercodes

### Textkanal

| Code | Name | Bedeutung |
|---:|---|---|
| 10 | `ACCEPTED` | Anfrage angenommen, noch nicht abgeschlossen. |
| 20 | `PREVIEW_READY` | Vorschau erzeugt, nicht sichtbar gesendet. |
| 21 | `SUBMITTED_VISIBLE` | Text sichtbar übermittelt. |
| 30 | `REJECTED_TEXT` | Textinhalt abgelehnt. |
| 31 | `REJECTED_POLICY` | Sicherheits-/Betriebsregel blockiert. |
| 32 | `REJECTED_CONTEXT` | Flug-/Altitude-Kontext ungeeignet. |
| 40 | `FAILED_BEFORE_SUBMIT` | Fehler vor dem Absenden. |
| 41 | `UNCERTAIN_AFTER_SUBMIT` | Zustand nach dem Absenden nicht eindeutig. Nicht blind wiederholen. |
| 42 | `CANCELLED` | Vorgang abgebrochen. |

### Sprachkanal

| Code | Name | Bedeutung |
|---:|---|---|
| 1 | `NOT_REQUESTED` | Für diese Nachricht wurde keine Sprache angefordert. |
| 10 | `ACCEPTED` | Sprachanfrage angenommen. |
| 20 | `TRANSMITTED` | PTT-/Audiotransaktion abgeschlossen. |
| 30 | `REJECTED_TEXT` | Sprechtext abgelehnt. |
| 31 | `DISABLED` | Sprachmodus deaktiviert oder lokal. |
| 32 | `REJECTED_CONTEXT` | Funkkontext nicht zulässig. |
| 40 | `FAILED_BEFORE_PTT` | Fehler vor PTT-Aktivierung. |
| 41 | `UNCERTAIN_AFTER_PTT` | Zustand nach PTT nicht eindeutig. Erst Logs und PTT-Freigabe prüfen. |
| 42 | `CANCELLED` | Sprachvorgang abgebrochen. |

## 18. Typische Fehlerbilder und Lösungen

### Altitude zeigt nach jedem Start wieder das TCA-Mikrofon

Wahrscheinliche Ursache:

```ini
ALTITUDE_AUDIO_INPUT_MATCH=TCA YOKE BOEING
```

Der Helper-Audio-Guard überschreibt damit die Altitude-Datei regelmäßig.

Lösung:

```ini
ALTITUDE_AUDIO_INPUT_MATCH=Voicemeeter Out B1
```

Danach Reload Config, Log prüfen und Altitude neu starten.

### Mikrofonpegel in VoiceMeeter, aber nichts im Altitude-Test

Prüfreihenfolge:

1. Hardware Input 1: B an?
2. B-Master nicht stumm?
3. Altitude Input wirklich `Voicemeeter Out B1`?
4. VoiceMeeter bereits vor Altitude gestartet?
5. Altitude nach der Geräteänderung vollständig neu gestartet?
6. Helper-Audio-Guard nachträglich wieder auf TCA zurückgeschrieben?

Ein bewegter Hardware-Input-Pegel allein beweist nur, dass VoiceMeeter das Mikrofon erhält. Erst der B-Pegel plus erfolgreicher Altitude-Local-Test beweist den vollständigen Pfad.

### Virtual Input und B schlagen aus, aber im Headset ist nichts zu hören

Im normalen Betrieb ist das beabsichtigt. A ist ausgeschaltet, damit kein Sidetone entsteht. Hörbar wird das Signal während des Tests über Altitudes lokalen Audio-Rundlauf oder während einer echten Übertragung bei den Empfängern.

Nicht als Schnelllösung A einschalten. Das würde einen separaten direkten Hörweg erzeugen und kann die Beurteilung des Altitude-Capture-Pfads verfälschen.

### Altitude-Login-/Logout-Ton funktioniert, der Local-Test aber nicht

Der Login-Ton prüft nur den Altitude-Output zum Headset. Er sagt nichts über den Altitude-Input aus. Input separat prüfen, Altitude nach Gerätewechsel neu starten und Audio-Guard kontrollieren.

### Selbstgesprochene Stimme funktioniert, Helper-TTS nicht

Prüfen:

- `AUTO_UNICOM_VOICE_OUTPUT_MATCH=VoiceMeeter Input`
- Helper-Log enthält `WASAPI_READY:Voicemeeter Input ...`
- Virtual Input zeigt beim Test Pegel
- Virtual Input hat B an
- TTS-Fader ist nicht ganz unten oder stumm
- verwendete SAPI-Stimme ist installiert

### Helper-TTS funktioniert, Hardware-Mikrofon nicht

Prüfen:

- Hardware Input 1 zeigt das richtige physische Mikrofon
- das Mikrofon ist nicht durch Hardwaretaste stumm
- Hardware Input 1 hat B an
- VoiceMeeter wurde vor Altitude gestartet
- Altitude verwendet B1 und nicht einen Stereo-/anderen VoiceMeeter-Endpunkt

### TTS ist sehr laut oder schlecht verständlich

Den Virtual-Input-Fader in kleinen Schritten absenken, beispielsweise von `-9 dB` Richtung `-12 dB`. Den Hardware-Mikrofonfader zunächst bei `0 dB` und den B-Master bei `0 dB` lassen. Die Verständlichkeit über den Altitude-Local-Test beurteilen, nicht über einen zusätzlich eingeschalteten A-Monitorweg.

### Deutliche Verzögerung im Altitude-Local-Test

Eine hörbare Verzögerung ist normal, weil Aufnahme, Altitude-Testverarbeitung und Wiedergabe durchlaufen werden. Sie ist kein Beweis für eine spätere Funkverzögerung. Knacken, Aussetzer oder mehrere Sekunden Stillstand sind dagegen Hinweise auf Geräte-/Sampleratenprobleme.

### Headset plötzlich komplett ohne Altitude-Ton

Prüfen:

- Altitude Output bleibt physisches TCA-Headset.
- VoiceMeeter A1 greift nicht gleichzeitig auf dasselbe Headset zu.
- A1 gegebenenfalls wieder freigeben/leer lassen und Altitude neu starten.
- Altitude-Login-/Logout-Ton erneut prüfen.

### `SPERR_DEVICE_BUSY` oder `0x80045006`

Ein Programm kann den Wiedergabeendpunkt nicht öffnen. Häufige Ursache in dieser Architektur: VoiceMeeter A1 und Altitude versuchen dasselbe Headset zu verwenden. A1 leer lassen und Altitude direkt auf das Headset ausgeben lassen.

### Desktop-Audio wird mitgesendet

Sofort Sprachmodus auf `local` stellen und Reload Config ausführen. Danach prüfen:

- Ist `Voicemeeter Input` globales Windows-Wiedergabegerät? Falls ja, auf das normale Gerät zurückstellen.
- Ist eine Anwendung in Windows gezielt auf VoiceMeeter geroutet?
- Ist ein zusätzlicher Loopback-/Stereo-Mix-Kanal auf B aktiv?

### Altitude-Ausgabe wird zurück ins Mikrofon gespeist

Sofort Sprachmodus auf `local` stellen. Altitude Output muss das physische Headset sein. Kein Altitude-Ausgabestream darf auf `Voicemeeter Input` oder einen anderen B-gerouteten Kanal zeigen.

### YAL erzeugt keine Meldungen

Prüfen:

- `IVAOAUTOUNICOM 1`
- `AUTOFUNCTIONS 1` oder `VOICEADVICEONLY 1`
- Helper-API Version 3 und `ready=1`
- `wahltho/autounicom/effective_callsign` entspricht dem Altitude-Callsign
- keine Meldung `transport blocked for session`
- keine noch offene Anfrage

Nach einem Transport-Timeout blockiert YAL weitere Anforderungen absichtlich für die laufende Sitzung. Nicht durch wiederholtes Auslösen übergehen; zuerst die Ursache in Helper- und SASL-Log klären.

### Text wurde möglicherweise gesendet, Bestätigung fehlt

Bei `UNCERTAIN_AFTER_SUBMIT` nicht sofort dieselbe Meldung erneut senden. Zuerst Altitude-Nachrichtenfenster und Log prüfen, um Doppelmeldungen zu vermeiden.

### PTT wurde möglicherweise betätigt, Ergebnis unklar

Bei `UNCERTAIN_AFTER_PTT` zuerst prüfen, ob PTT wieder freigegeben wurde. Den Funkmodus auf `local` zurückstellen, wenn die Freigabe nicht eindeutig ist.

## 19. Wartung nach Updates oder Gerätewechsel

Nach folgenden Ereignissen muss der Audiopfad erneut lokal getestet werden:

- VoiceMeeter-Update oder Neuinstallation
- Altitude-Update
- YAL-Auto-Unicom-Helper-Update
- YAL-Update mit Auto-UNICOM-API-Änderung
- Windows-Funktionsupdate
- USB-Portwechsel des Headsets
- Änderung des Headset-Namens
- Änderung der Abtastrate
- neue virtuelle Audiotreiber

Nach einem USB- oder Treiberwechsel können Endpoint-IDs neu erzeugt werden. Der Helper-Match muss dann den neuen Namen eindeutig finden. Das Helper-Log ist die maßgebliche Kontrolle.

## 20. Schnell-Rollback für einen laufenden Flug

Wenn manueller ATC-Funk sofort wieder zuverlässig benötigt wird und der Mixpfad ungeklärt ist:

1. `AUTO_UNICOM_MODE=off`, `AUTO_UNICOM_VOICE_MODE=local` und
   `ALTITUDE_AUDIO_GUARD=0` setzen.
2. `Reload Config` ausführen.
3. Altitude Input vorübergehend direkt auf das physische Headset-Mikrofon setzen.
4. Altitude Output auf dem physischen Headset belassen.
5. Altitude neu starten und den manuellen Local-Test durchführen.

Damit funktioniert manueller Funk wieder direkt, aber automatische TTS erreicht Altitude nicht mehr. Das ist ein sicherer Notbetrieb, keine abgeschlossene Auto-UNICOM-Konfiguration.

## 21. Referenzkonfiguration des erprobten Aufbaus

### VoiceMeeter

```text
Hardware Input 1: Headset Microphone (TCA YOKE BOEING)
Hardware Input 1: A aus, B an, 0,0 dB
Virtual Input:     A aus, B an, etwa -9,1 dB
B-Master:          0,0 dB, nicht stumm, Mono aus
A1:                kein TCA-Headset; Internal Master CLOCK/leer
Samplerate:        48 kHz
Autostart:         an
System Tray:       an
Show App On Startup: aus
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

Der obige Referenzblock zeigt den finalen Funkbetrieb. Bei einer Neuinstallation muss `AUTO_UNICOM_VOICE_MODE` zunächst `local` bleiben.

### YAL

```text
IVAOAUTOUNICOM 1
AUTOFUNCTIONS 0
VOICEADVICEONLY 1
```

## 22. Abschlusscheckliste

Die Einrichtung ist erst abgeschlossen, wenn alle Punkte mit Ja beantwortet werden können:

- VoiceMeeter startet automatisch und lädt die richtige XML-Datei.
- Hardware-Mikrofon erscheint auf Hardware Input 1.
- Mikrofon hat A aus und B an.
- Helper-TTS erscheint auf Virtual Input.
- Virtual Input hat A aus und B an.
- Desktop-Audio bewegt den B-Bus nicht.
- Altitude-Ausgabe bewegt den B-Bus nicht.
- Altitude Input ist `Voicemeeter Out B1`.
- Altitude Output ist das physische Headset.
- Helper-Audio-Guard bestätigt beide richtigen Endpunkte.
- TTS ist im Altitude-Local-Test hörbar.
- Hardware-Mikrofon ist im selben Altitude-Local-Test hörbar.
- PTT wird während lokaler Tests nicht gedrückt.
- API meldet Version 3, READY und Transportzustand 5.
- Ein sicherer echter Test liefert `result_code=21 SUBMITTED_VISIBLE`.
- Derselbe Test liefert `voice_result_code=20 TRANSMITTED`.
- PTT wird nach jeder Übertragung wieder freigegeben.
- Manueller Mikrofonfunk funktioniert weiterhin.
- Es gibt keine Rückkopplung und keinen Sidetone im Normalbetrieb.

Nur `AUTO_UNICOM_VOICE_MODE=radio` zu setzen ist kein Fertigstellungsnachweis. Fertig bedeutet, dass Hardware-Mikrofon und Helper-TTS nachweislich denselben Altitude-Eingang erreichen und kein anderer Ton in diesen Sendepfad gelangt.

# AmisReader

ESPHome-Komponente speziell für den **Siemens TD-3511 AMIS Smart Meter** des Netzbetreibers Netz OÖ.
Sie ersetzt meine ursprüngliche Lösung auf Raspberry-Pi-Basis und lässt sich in Home Assistant sowie in bestehende
MQTT-Infrastrukturen einbinden.

Hintergrund (Österreich / ElWG & SNE-G-V): Mit der Reform der Netzentgelte in Österreich (geregelt durch das ElWG und die Systemnutzungsentgelte-Grundsatzverordnung SNE-G-V der E-Control) wird zwischen 2027 und 2028 schrittweise ein Leistungspreis für Haushalte eingeführt. Bemessungsgrundlage für den monatlichen Leistungspreis ist der höchste durchschnittliche 15-Minuten-Leistungswert (Viertelstundenmaximum) eines Kalendermonats. Diese ESPHome-Komponente berechnet genau diese Intervalle lokal in Echtzeit, um Lastspitzen rechtzeitig zu erkennen und Verbraucher (wie Wallbox oder Wärmepumpe) im Smart Home gezielt zu steuern.

> **Hinweis:** Dies ist ein privates Hobbyprojekt und **keine offizielle Lösung** des Netzbetreibers oder des
> Zähler-Herstellers. Nutzung, Nachbau und der Anschluss an den Zähler erfolgen **auf eigene Verantwortung**.
> Siehe auch [Lizenz und Haftung](#lizenz-und-haftung).

## Features

- **Hardware-Schnittstelle:** UART-Anbindung über einen optischen Auslesekopf an die Kundenschnittstelle des AMIS-Zählers.
- **Verschlüsselung:** AES-128-Entschlüsselung. Der Schlüssel wird über `secrets.yaml` verwaltet.
- **Telegrammprüfung:** Rahmenformat, M-Bus-Prüfsumme und Datenlänge werden geprüft. Defekte Telegramme oder ein
  falscher Schlüssel führen zu einer Log-Meldung, aber nie zu falschen oder alten Werten in Home Assistant.
- **Home Assistant API:** Leistung Wirk +/-/Saldo, Blindleistung +/-, Energie Wirk +/-, Blindenergie +/-, Inkasso-Energie.
- **Viertelstunden-Maximum (berechnet):** Leistung der laufenden und der letzten Viertelstunde sowie das Monatsmaximum, berechnet
  aus dem Zählerstand und der Zählerzeit. Das Maximum bleibt auch nach einem Neustart erhalten. Siehe
  [Viertelstunden-Maximum](#viertelstunden-maximum-berechnet).
- **Zählerzeit:** Der Sensor `date_time` zeigt die Uhrzeit des Zählers aus dem M-Bus-Telegramm.
- **MQTT (optional, auch ohne Home Assistant):** JSON im Format des „Österreichs Energie (SMA-Adapter)“, damit bestehende
  Datenbank-Historien (InfluxDB/Grafana/Home Assistant) unterbrechungsfrei weitergeführt werden können.

## Hardware-Empfehlung und Verkabelung

Um den ESP32 vor Beschädigungen durch 5-V-Signale zu schützen, empfiehlt sich ein Aufbau, der vollständig mit
**3,3 V Logikspannung** arbeitet. Mein Aufbau besteht aus:

1. **Optischer Auslesekopf:** MY002-D (Modell mit RS485-Schnittstelle / offenen Kabelenden)
2. **RS485-Pegelwandler:** HW-519 (mit automatischer Flusssteuerung)
3. **Mikrocontroller:** ESP32 (z. B. ESP32-WROOM-32 Dev Kit)

### Spannungsversorgung

Die MY002-D ist offiziell ab 5 V spezifiziert. In meinem Aufbau lief sie zuverlässig direkt an der **3,3-V-Schiene** des
ESP32. So arbeitet der gesamte Bus mit 3,3-V-Pegel. Das ist keine Freigabe des Herstellers, sondern meine Erfahrung mit
einem einzelnen Aufbau.

### Anschluss-Schema

| ESP32 | HW-519 Pegelwandler | MY002-D Optical Probe |
| :--- | :--- | :--- |
| **3.3V** | VCC | VCC (Stromversorgung) |
| **GND** | GND | GND (Masse) |
| **GPIO15 (RX)** | TXD | — |
| **GPIO16 (TX)** | RXD | — |
| — | **A** | A+ (Signalleitung) |
| — | **B** | B- (Signalleitung) |

*TX:* Der ESP32 quittiert jedes gültige Telegramm mit einem ACK-Byte (0xE5) über TX. Die UART-Prüfung beim Kompilieren
verlangt nur `rx_pin`; ob dein Zähler die Quittierung benötigt, habe ich nicht untersucht.

*Polarität:* Die Leitungen A und B auf der RS485-Seite nicht vertauschen (A auf A, B auf B). Dann werden die
Push-Telegramme des Zählers im 1-Sekunden-Takt empfangen. GPIO15 ist ein Strapping-Pin des ESP32. Falls der ESP32
nicht sauber bootet, einen anderen Pin verwenden und `rx_pin`/`tx_pin` in der YAML anpassen.

## Voraussetzungen

| | |
| :--- | :--- |
| Plattform | **ESP32** (ESP-IDF oder Arduino). ESP8266 wird nicht unterstützt. |
| ESPHome | getestet mit **2026.10.2** |
| UART | **9600 Baud, 8E1** (wird beim Kompilieren geprüft) |
| AES-Key | 32 Hex-Zeichen, vom Netzbetreiber zu beziehen |

Den Schlüssel stellt der Netzbetreiber auf Anfrage zur Verfügung. Er gehört **nicht** ins Repository und sollte auch
nicht in Logs, Screenshots oder Forenbeiträgen auftauchen.

## Installation

### 1. Secrets konfigurieren

Kopiere `secrets.yaml.example` nach `secrets.yaml` und trage deine Werte ein, insbesondere den AES-Key:

```yaml
amis_aes_key: "DEIN_32_STELLIGER_HEX_KEY"
```

### 2. YAML-Konfiguration

Vollständige Beispiele: [`example_esp32.yaml`](example_esp32.yaml) (Home Assistant + MQTT) und
[`example_mqtt_only.yaml`](example_mqtt_only.yaml) (nur MQTT, ohne Home Assistant).

```yaml
external_components:
  - source: github://hgaderGit/amisreader@main
    components: [amisreader]

uart:
  id: uart_bus
  tx_pin: GPIO16
  rx_pin: GPIO15
  baud_rate: 9600
  data_bits: 8
  parity: EVEN
  stop_bits: 1

sensor:
  - platform: amisreader
    aes_key: !secret amis_aes_key

    power:
      name: "Amis Power A+-"
    power_forward:
      name: "Amis Power A+"
    power_reverse:
      name: "Amis Power A-"
    reactive_power_import:
      name: "Amis Power R+"
    reactive_power_export:
      name: "Amis Power R-"

    energy_forward:
      name: "Amis Energy A+"
    energy_reverse:
      name: "Amis Energy A-"
    reactive_energy_import:
      name: "Amis Energy R+"
    reactive_energy_export:
      name: "Amis Energy R-"

    energy_inkasso:
      name: "Amis Energy Inkasso"

    # Berechnete Viertelstunden-Werte (siehe unten)
    power_quarter_hour:
      name: "Amis Power Quarter Hour"
    power_last_quarter_hour:
      name: "Amis Power Last Quarter Hour"
    power_max_month:
      name: "Amis Power Max Month"

    # Zählerzeit aus dem Telegramm
    date_time:
      name: "Amis Date Time"
```

### Optionen

| Option | Pflicht | Beschreibung |
| :--- | :---: | :--- |
| `aes_key` | ja | AES-128-Key des Zählers, genau 32 Hex-Zeichen |
| `mqtt_json_topic` | nein | MQTT-Topic für das JSON (Standard: `amisreader/json_sma`) |
| `power` | nein | **berechnet:** Wirkleistung gesamt (A+ − A−),  W (1-0:15.7.0) |
| `power_forward` / `power_reverse` | nein | Wirkleistung Bezug / Lieferung, W (1-0:1.7.0 / 1-0:2.7.0) |
| `reactive_power_import` / `reactive_power_export` | nein | Blindleistung +/-, var (1-0:3.7.0 / 1-0:4.7.0) |
| `energy_forward` / `energy_reverse` | nein | Wirkenergie Bezug / Lieferung, kWh (1-0:1.8.0 / 1-0:2.8.0) |
| `reactive_energy_import` / `reactive_energy_export` | nein | Blindenergie +/-, kvarh (1-0:3.8.1 / 1-0:4.8.1) |
| `energy_inkasso` | nein | Inkasso-Energie, kWh (0-0:0.128.0) |
| `power_quarter_hour` | nein | **berechnet:** Leistung der laufenden Viertelstunde, W (1-0:1.4.0) |
| `power_last_quarter_hour` | nein | **berechnet:** Leistung der letzten Viertelstunde, W (1-0:1.5.0) |
| `power_max_month` | nein | **berechnet:** Monatsmaximum der Viertelstunden-Leistung, W (1-0:1.6.0) |
| `date_time` | nein | Zählerzeit aus dem Telegramm als Text, `JJJJ-MM-TT hh:mm:ss` |

Alle Sensoren sind normale ESPHome-Sensoren und unterstützen z. B. `filters:`. Da der Zähler jede Sekunde sendet, kann
ein Filter wie `throttle: 5s` die Datenmenge in Home Assistant verringern.

## MQTT / JSON

Durch Hinzufügen einer `mqtt:`-Konfiguration wird nach jedem gültigen Telegramm zusätzlich ein JSON gesendet. Es geht
standardmäßig an das Topic `amisreader/json_sma`. Das Topic lässt sich mit `mqtt_json_topic` in der Sektion `sensor:`
ändern.

Wenn du die Sensoren zusätzlich über die `sensor:`-Sektion nach Home Assistant bringst, setze in der `mqtt:`-Sektion
`discovery: false`, um doppelte Entitäten zu vermeiden.

Ohne Home Assistant genügt `mqtt:` (ohne `api:`). Das JSON wird auch gesendet, wenn keine Sensoren eingetragen sind
(siehe `example_mqtt_only.yaml`).

```yaml
mqtt:
  broker: !secret mqtt_broker
  username: !secret mqtt_user
  password: !secret mqtt_password
  discovery: false  # <--- verhindert doppelte Entitäten

sensor:
  - platform: amisreader
    aes_key: !secret amis_aes_key
    mqtt_json_topic: "AMIS"
```

### Datenformat (JSON im Österreichs-Energie-SMA-Format)

```json
{"0-0:96.1.0":{"value":"00000000"},
 "0-0:0.9.1":{"value":"23:35:13","time":1776807313},
 "0-0:0.9.2":{"value":"21.04.2026","time":1776807313},
 "1-0:1.8.0":{"value":"10423292","time":1776807313},
 "1-0:2.8.0":{"value":"903707","time":1776807313},
 "1-0:3.8.1":{"value":"9522","time":1776807313},
 "1-0:4.8.1":{"value":"301096","time":1776807313},
 "1-0:1.7.0":{"value":"0","time":1776807313},
 "1-0:2.7.0":{"value":"5","time":1776807313},
 "1-0:3.7.0":{"value":"0","time":1776807313},
 "1-0:4.7.0":{"value":"300","time":1776807313},
 "0-0:0.128.0":{"value":"0","time":1776807313},
 "1-0:15.7.0":{"value":"-5","time":1776807313},
 "1-0:1.4.0":{"value":"1860","time":0},
 "1-0:1.5.0":{"value":"1842","time":1776807000},
 "1-0:1.6.0":{"value":"4120","time":1776806100},
 "api_version":"v1.0","name":"AmisReader","sma_time":44}
```

Es werden nur Werte aufgenommen, die im jeweils aktuellen Telegramm enthalten waren. `1-0:1.4.0`, `1-0:1.5.0` und
`1-0:1.6.0` sind berechnete Werte, siehe nächster Abschnitt. `sma_time` ist die Laufzeit des ESP
in Sekunden. Für `1-0:15.7.0` gilt das Gleiche wie bei „Amis Power A+-“.

## Viertelstunden-Maximum (berechnet)

Der Zähler sendet kein Viertelstunden-Maximum. Die Komponente berechnet es deshalb selbst aus dem Zählerstand
Wirkenergie Bezug (1-0:1.8.0). Als Zeitgrundlage dient ausschließlich die **Zählerzeit** aus dem Telegramm, damit die
Messperioden zu denen des Zählers passen (die Abrechnung gilt nach Zählerzeit).

**Verfahren**

- Die Viertelstunden liegen auf hh:00, hh:15, hh:30 und hh:45 der Zählerzeit.
- Der Zählerstand an einer Grenze wird zwischen dem letzten Telegramm davor und dem ersten danach interpoliert. Einzelne
  verlorene Telegramme stören die Berechnung dadurch nicht.
- Leistung der Viertelstunde = Energie der Viertelstunde × 4.
- Das Monatsmaximum ist der größte Wert aller abgeschlossenen Viertelstunden des Monats (Zählerzeit). Eine Viertelstunde
  gehört zu dem Monat, in dem sie beginnt. Die Viertelstunde 23:45 bis 00:00 am Monatsletzten zählt also noch zum alten
  Monat. Mit dem ersten Telegramm danach wird das Maximum auf 0 zurückgesetzt.

**Werte**

| Sensor | OBIS | `time` im JSON | Beschreibung |
| :--- | :--- | :--- | :--- |
| `power_quarter_hour` | 1-0:1.4.0 | `0` | Mittlere Leistung seit Beginn der laufenden Viertelstunde. Erst ab 60 s nach Beginn der Viertelstunde, weil der Zählerstand nur auf 1 Wh genau ist. |
| `power_last_quarter_hour` | 1-0:1.5.0 | Ende der Viertelstunde | Leistung der zuletzt abgeschlossenen Viertelstunde. Wird einmal pro Viertelstunde gemeldet. |
| `power_max_month` | 1-0:1.6.0 | Ende der Viertelstunde des Maximums | Monatsmaximum. Der Zeitstempel ist die Zählerzeit am Ende der Viertelstunde, in der das Maximum entstand. |

Die `time`-Werte im JSON sind wie bei allen anderen Einträgen Unix-Zeit der Zählerzeit.

**Verhalten im Betrieb**

- Das Monatsmaximum wird im Flash gespeichert (nur bei neuem Maximum oder Monatswechsel) und überlebt einen Neustart.
  Mit dem ersten Telegramm nach dem Start wird es einmal gemeldet.
- Nach einem Neustart oder einer Lücke fehlt die Viertelstunde, in der der Ausfall lag. Ab der nächsten vollständigen
  Viertelstunde wird wieder gerechnet.
- Es wird nicht über Lücken von mehr als 60 s zwischen zwei Telegrammen hinweg interpoliert. Das Gleiche gilt für
  Zeitsprünge (z. B. Zeitsynchronisation des Zählers, Zeitumstellung), rückwärts laufende Zählerstände und eine Zählerzeit,
  die der Zähler selbst als ungültig kennzeichnet. Die betroffene Viertelstunde wird verworfen.
- Die Genauigkeit wird von der Auflösung des Zählerstands begrenzt: 1 Wh entspricht 4 W pro Viertelstunde. In meinen
  Simulationen mit wechselnder Last und bis zu 20 % verlorenen Telegrammen lag die Abweichung bei höchstens etwa 3 W.
  Gegenüber dem vom Zähler selbst gebildeten Wert (falls vorhanden) sind daher Unterschiede von einigen Watt normal.
- Die Berechnung wurde bisher nur mit simulierten Zählerdaten geprüft, nicht gegen die Werte eines echten Zählers.

### Zählerzeit (`date_time`)

Der Text-Sensor zeigt die Zählerzeit des letzten Telegramms im Format `2026-04-21 23:35:13`. Er wird **bei jedem
Telegramm, also im Sekundentakt**, aktualisiert. So lässt sich auch ein Zeitversatz der Zählerzeit gegenüber der echten
Zeit erkennen.

**Hinweis für Home Assistant:** Ein Wert, der sich jede Sekunde ändert, belastet die Datenbank und bringt in der Historie
nichts. Schließe den Sensor deshalb in der `configuration.yaml` vom Recorder aus. Die Entitäts-ID hängt vom Gerätenamen
ab und muss angepasst werden (Text-Sensoren erscheinen in Home Assistant in der Domäne `sensor`):

```yaml
recorder:
  purge_keep_days: 90
  include:
    domains:
      - sensor
  exclude:
    entities:
      - sensor.amisreader_amis_date_time   # <--- an deine Entitäts-ID anpassen
```

Danach Home Assistant neu starten. Der aktuelle Wert bleibt in Home Assistant sichtbar, es wird nur nicht mehr in die
Datenbank geschrieben.

## Technische Details

- Beim Sensor „Amis Power A+-“ handelt es sich um einen berechneten Wert, der nicht vom Zähler geliefert wird:
  `"Amis Power A+-" = "Amis Power A+" - "Amis Power A-"`.
- Ein Telegramm wird nur ausgewertet, wenn Start-/Stoppbytes, die doppelte Längenangabe, die M-Bus-Prüfsumme und die im
  Header angegebene Datenlänge stimmen und die Entschlüsselung die erwartete Kennung (`2F 2F`) ergibt. Sonst werden
  weder Sensoren noch JSON aktualisiert.
- Ein unvollständiges Telegramm wird nach einer Pause von 200 ms verworfen, danach synchronisiert sich der Empfang
  automatisch auf das nächste Telegramm.

## Fehlersuche

| Meldung im Log | Mögliche Ursache |
| :--- | :--- |
| *(gar keine Meldung)* | RX/TX vertauscht, A/B vertauscht, Auslesekopf sitzt schlecht, falsche UART-Einstellung |
| `Telegramm verworfen: Prüfsumme falsch` | Übertragungsstörungen, Auslesekopf verrutscht oder Kabel zu lang/ungeschirmt |
| `Telegramm verworfen: Rahmenformat ungültig` | wie oben, gelegentliche Einzelfälle sind unkritisch |
| `Telegramm nicht auswertbar: Entschlüsselung fehlgeschlagen - aes_key prüfen` | falscher AES-Key |
| `Telegramm nicht auswertbar: Datenlänge passt nicht zum Header` | anderes Telegrammformat (z. B. anderer Netzbetreiber) |
| `Unhandled: dif: ...` (Debug) | Der Zähler sendet einen Wert, den die Komponente nicht kennt (wird nur beim ersten Auftreten gemeldet) |

Für eine genauere Analyse `logger: level: VERBOSE` (und `tx_buffer_size: 1024`) aktivieren. **Achtung:** VERBOSE-Logs
enthalten Zählernummer und Verbrauchsdaten. Vor dem Veröffentlichen in einem Forum bitte entfernen.

## Einschränkungen

- **Blindenergie** und **Blindleistung** konnten von mir **nicht verifiziert** werden.
- **Inkasso-Energie** konnte von mir **nicht verifiziert** werden.
- Die Datentelegramme des AMIS Smart Meter können bei anderen Netzbetreibern anders aufgebaut sein.
- Pro ESP32 wird ein Zähler unterstützt.
- Das Viertelstunden-Maximum ist ein berechneter Wert und kann von den Abrechnungswerten des Netzbetreibers abweichen.

## Lizenz und Haftung

MIT-Lizenz, siehe [`LICENSE`](LICENSE). Die Software wird ohne jegliche Gewährleistung bereitgestellt. Für Schäden am
Zähler, an der Hardware oder durch falsche Messwerte wird nicht gehaftet. Der Zugriff auf die Kundenschnittstelle des
Zählers und die Verwendung des AES-Keys unterliegen den Bedingungen deines Netzbetreibers.

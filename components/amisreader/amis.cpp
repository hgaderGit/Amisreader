#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_timer.h"
#include "amis.h"
#include "esphome/core/hal.h"

#ifdef USE_MQTT
#include "esphome/components/mqtt/mqtt_client.h"
#endif

namespace esphome {
namespace amis {

static const char *const TAG = "amis";

// Zwischen zwei Telegrammen liegt eine Pause (Sendeintervall 1 s, Telegramm ca. 0,2 s).
// Kommt laenger als diese Zeit kein Byte mehr, wird ein unvollstaendiges Telegramm verworfen.
static const uint32_t FRAME_TIMEOUT_MS = 200;
// Groesster moeglicher M-Bus Langframe: 255 Nutzbytes + 6 Bytes Rahmen
static const size_t MAX_FRAME_SIZE = 261;

void Amis::setup() {
  static char tz_string[] = "TZ=CET-1CEST,M3.5.0,M10.5.0/3"; /* Österreich */

  this->pAmis_ = AmisInitDevice(this->aes_key_);
  this->rx_buffer_.reserve(MAX_FRAME_SIZE + 8);

  putenv(tz_string);
  tzset();

  // Monatsmaximum aus dem Flash wiederherstellen
  this->pref_ = global_preferences->make_preference<QuarterPersist>(fnv1_hash("amisreader_quarter_max_v1"));
  QuarterPersist restored{};
  if (this->pref_.load(&restored) && restored.month_id > 0 && restored.max_w >= 0 && restored.max_w < 1e8) {
    this->qh_.restore(restored);
    ESP_LOGD(TAG, "Monatsmaximum wiederhergestellt: %.0f W", restored.max_w);
  }

  ESP_LOGD(TAG, "AMIS Komponente gestartet");
}

void Amis::dump_config() {
  ESP_LOGCONFIG(TAG, "AMIS Reader:");
#ifdef USE_MQTT
  ESP_LOGCONFIG(TAG, "  MQTT JSON Topic: %s", this->json_topic_.c_str());
#endif
  LOG_SENSOR("  ", "Power A+-", this->power_sensor_);
  LOG_SENSOR("  ", "Power A+", this->power_forward_sensor_);
  LOG_SENSOR("  ", "Power A-", this->power_reverse_sensor_);
  LOG_SENSOR("  ", "Power R+", this->reactive_power_import_sensor_);
  LOG_SENSOR("  ", "Power R-", this->reactive_power_export_sensor_);
  LOG_SENSOR("  ", "Energy A+", this->energy_forward_sensor_);
  LOG_SENSOR("  ", "Energy A-", this->energy_reverse_sensor_);
  LOG_SENSOR("  ", "Energy R+", this->reactive_energy_import_sensor_);
  LOG_SENSOR("  ", "Energy R-", this->reactive_energy_export_sensor_);
  LOG_SENSOR("  ", "Energy Inkasso", this->energy_inkasso_sensor_);
  LOG_SENSOR("  ", "Power Quarter Hour", this->power_quarter_hour_sensor_);
  LOG_SENSOR("  ", "Power Last Quarter Hour", this->power_last_quarter_hour_sensor_);
  LOG_SENSOR("  ", "Power Max Month", this->power_max_month_sensor_);
  LOG_TEXT_SENSOR("  ", "Date Time", this->date_time_sensor_);
}

void Amis::loop() {
  const uint32_t now = millis();

  if (!this->available()) {
    // Nur wenn nichts mehr ansteht und die Pause lang genug ist, wird ein Rest verworfen.
    // (Bei einem kurzen Haenger der Hauptschleife liegen die Bytes noch im UART-Puffer.)
    if (!this->rx_buffer_.empty() && (now - this->last_rx_ms_) > FRAME_TIMEOUT_MS) {
      ESP_LOGD(TAG, "Pause im Telegramm, %u Byte verworfen", (unsigned) this->rx_buffer_.size());
      this->rx_buffer_.clear();
    }
    return;
  }

  uint8_t c;
  while (this->available() && this->read_byte(&c)) {
    this->rx_buffer_.push_back(c);
    this->last_rx_ms_ = now;
    this->process_rx_();
  }
}

// Sucht im Empfangspuffer nach vollstaendigen, gueltigen Telegrammen.
void Amis::process_rx_() {
  while (!this->rx_buffer_.empty()) {
    const uint8_t first = this->rx_buffer_[0];

    // Weder Kurz- (0x10) noch Langframe (0x68): Byte verwerfen
    if (first != 0x10 && first != 0x68) {
      this->rx_buffer_.erase(this->rx_buffer_.begin());
      continue;
    }

    if (first == 0x10) {
      // Kurzframe "10 40 F0 30 16" (SND_NKE) wird mit ACK bestaetigt
      if (this->rx_buffer_.size() < 5)
        return;
      if (this->rx_buffer_[1] == 0x40 && this->rx_buffer_[2] == 0xF0 && this->rx_buffer_[3] == 0x30 &&
          this->rx_buffer_[4] == 0x16) {
        ESP_LOGV(TAG, "<== %s", format_hex_pretty(this->rx_buffer_.data(), 5).c_str());
        ESP_LOGV(TAG, "==> ACK 0xE5");
        this->write_byte(0xE5);
        this->flush();  // Sofort senden!
        this->rx_buffer_.erase(this->rx_buffer_.begin(), this->rx_buffer_.begin() + 5);
      } else {
        // Zufaelliges 0x10: nur dieses Byte verwerfen und weitersuchen
        this->rx_buffer_.erase(this->rx_buffer_.begin());
      }
      continue;
    }

    // Datentelegramm "68 L L 68 ... CS 16"
    if (this->rx_buffer_.size() < 4)
      return;
    if (this->rx_buffer_[3] != 0x68 || this->rx_buffer_[1] != this->rx_buffer_[2]) {
      this->rx_buffer_.erase(this->rx_buffer_.begin());
      continue;
    }

    const size_t total = (size_t) this->rx_buffer_[1] + 6;
    if (this->rx_buffer_.size() < total)
      return;  // Telegramm noch nicht komplett

    const AmisResult res = AmisCheckFrame(this->rx_buffer_.data(), total);
    if (res != AMIS_OK) {
      // Nicht ausgerichtet oder gestoert: nur das erste Byte verwerfen und neu suchen
      if ((this->frame_errors_++ % 30) == 0) {
        ESP_LOGW(TAG, "Telegramm verworfen: %s (%u Byte, bisher %u Fehler)", AmisResultText(res), (unsigned) total,
                 (unsigned) this->frame_errors_);
      }
      this->rx_buffer_.erase(this->rx_buffer_.begin());
      continue;
    }

    this->handle_frame_(total);
    this->rx_buffer_.erase(this->rx_buffer_.begin(), this->rx_buffer_.begin() + total);
  }
}

void Amis::handle_frame_(size_t frame_size) {
  (void) frame_size;  // nur fuer das Verbose-Log
  ESP_LOGV(TAG, "<== %s", format_hex_pretty(this->rx_buffer_.data(), frame_size).c_str());
  this->write_byte(0xE5);
  this->flush();  // Sofort senden!
  ESP_LOGV(TAG, "==> ACK 0xE5");

  const AmisResult res = AmisParseTelegram(&this->rx_buffer_[4], this->rx_buffer_[1]);
  if (res != AMIS_OK) {
    // Nichts veroeffentlichen: sonst wuerden leere bzw. alte Werte als aktuell erscheinen.
    if ((this->parse_errors_++ % 60) == 0) {
      ESP_LOGW(TAG, "Telegramm nicht auswertbar: %s (bisher %u Fehler)", AmisResultText(res),
               (unsigned) this->parse_errors_);
    }
    return;
  }

  if (this->pAmis_->unknownDifVif && !this->unknown_logged_) {
    // Nur beim ersten Auftreten melden
    this->unknown_logged_ = true;
    ESP_LOGD(TAG, "Unhandled:");
    ESP_LOGD(TAG, "dif: 0x%02x", this->pAmis_->unknown.mbus.dif);
    ESP_LOGD(TAG, "vifPrimary: 0x%02x", this->pAmis_->unknown.mbus.vifPrimary);
    ESP_LOGD(TAG, "vifExtension: 0x%02x", this->pAmis_->unknown.mbus.vifExtension);
    ESP_LOGD(TAG, "vifCombinable: 0x%02x", this->pAmis_->unknown.mbus.vifCombinable);
    ESP_LOGD(TAG, "vifCombinableExtension: 0x%02x", this->pAmis_->unknown.mbus.vifCombinableExtension);
    ESP_LOGD(TAG, "tarif: 0x%02x", this->pAmis_->unknown.mbus.tarif);
    ESP_LOGD(TAG, "subunit: 0x%02x", this->pAmis_->unknown.mbus.subunit);
    ESP_LOGD(TAG, "storage: 0x%02x", this->pAmis_->unknown.mbus.storage);
  }

  ESP_LOGVV(TAG, "iv: %s", format_hex_pretty(this->pAmis_->device.iv, 16).c_str());
  ESP_LOGVV(TAG, "DataSize: %d  Configuration: 0x%02x", this->pAmis_->device.dataSizeAes,
            this->pAmis_->device.configuration);
  ESP_LOGV(TAG, "decoded: %s",
           format_hex_pretty((uint8_t *) this->pAmis_->device.decodedData, this->pAmis_->device.dataSizeAes, ' ')
               .c_str());
  ESP_LOGV(TAG, "Handled Mbus DIF/VIF: %d", this->pAmis_->countHandled);

  this->update_quarter_hour_();

#ifdef USE_MQTT
  if (mqtt::global_mqtt_client != nullptr && mqtt::global_mqtt_client->is_connected()) {
    if (this->build_sma_json_()) {
      mqtt::global_mqtt_client->publish(this->json_topic_, this->pAmis_->device.output);
      ESP_LOGD(TAG, "==> JSON MQTT");
    }
  } else {
    ESP_LOGW(TAG, "MQTT nicht verbunden, JSON verworfen");
  }
#endif

  this->publish_sensors_();
}

void Amis::publish_value_(sensor::Sensor *s, const DeviceDataPoint &dp, double divisor) {
  // Nur Werte aus dem aktuellen, gueltigen Telegramm veroeffentlichen
  if (s == nullptr || !dp.valid)
    return;
  s->publish_state(strtod(dp.value, nullptr) / divisor);
}

void Amis::publish_sensors_() {
  AmisDevice *a = this->pAmis_;

  publish_value_(this->power_sensor_, a->m1570, 1.0);
  publish_value_(this->power_forward_sensor_, a->m170, 1.0);
  publish_value_(this->power_reverse_sensor_, a->m270, 1.0);
  publish_value_(this->reactive_power_import_sensor_, a->m370, 1.0);
  publish_value_(this->reactive_power_export_sensor_, a->m470, 1.0);

  publish_value_(this->energy_forward_sensor_, a->m18x[0], 1000.0);
  publish_value_(this->energy_reverse_sensor_, a->m28x[0], 1000.0);
  publish_value_(this->reactive_energy_import_sensor_, a->m38x[1], 1000.0);
  publish_value_(this->reactive_energy_export_sensor_, a->m48x[1], 1000.0);
  publish_value_(this->energy_inkasso_sensor_, a->m01280, 1000.0);

  // Laufende Viertelstunde: bei jedem Telegramm (nach Ablauf der Mindestzeit)
  if (this->power_quarter_hour_sensor_ != nullptr && this->qh_.current_valid())
    this->power_quarter_hour_sensor_->publish_state(this->qh_.current_w());

  // Letzte Viertelstunde: einmal pro abgeschlossener Viertelstunde
  if (this->power_last_quarter_hour_sensor_ != nullptr && (this->quarter_changed_ & QuarterHour::CHANGED_QUARTER) &&
      this->qh_.last_valid())
    this->power_last_quarter_hour_sensor_->publish_state(this->qh_.last_w());

  // Monatsmaximum: bei Aenderung (neues Maximum, Monatswechsel) und einmal nach dem Start
  if (this->qh_.max_known() && ((this->quarter_changed_ & QuarterHour::CHANGED_MAX) || !this->max_published_)) {
    if (this->power_max_month_sensor_ != nullptr)
      this->power_max_month_sensor_->publish_state(this->qh_.max_w());
    this->max_published_ = true;
  }

  // Zaehlerzeit: bei jedem Telegramm (im Sekundentakt). In Home Assistant sollte der Sensor vom Recorder
  // ausgeschlossen werden, siehe README.
  if (this->date_time_sensor_ != nullptr && a->device.meterTimeValid)
    this->date_time_sensor_->publish_state(a->device.dateTime);
}

// Füttert die Viertelstunden-Berechnung mit Zaehlerzeit und Zaehlerstand des aktuellen Telegramms.
void Amis::update_quarter_hour_() {
  AmisDevice *a = this->pAmis_;
  this->quarter_changed_ = 0;

  // Ohne gueltige Zaehlerzeit oder Zaehlerstand ist die Kette der Telegramme unterbrochen.
  if (!a->device.meterTimeValid || a->device.meterTimeFlagInvalid || !a->m18x[0].valid) {
    this->qh_.invalidate();
    return;
  }

  const double energy_wh = strtod(a->m18x[0].value, nullptr);
  this->quarter_changed_ =
      this->qh_.update(a->device.localSeconds, (int64_t) a->device.timeStamp, a->device.monthId, energy_wh);

  if (this->quarter_changed_ & QuarterHour::CHANGED_MAX) {
    // Wird nur bei neuem Maximum oder Monatswechsel gespeichert (ESPHome schreibt gesammelt in den Flash).
    QuarterPersist p = this->qh_.persist();
    this->pref_.save(&p);
    ESP_LOGD(TAG, "Monatsmaximum: %.0f W", p.max_w);
  }
}

void Amis::set_aes_key(const std::vector<uint8_t> &key) {
  memset(this->aes_key_, 0, sizeof(this->aes_key_));
  for (size_t i = 0; i < sizeof(this->aes_key_) && i < key.size(); i++) {
    this->aes_key_[i] = key[i];
  }
}

#ifdef USE_MQTT
// Traegt einen OBIS-Wert nur ein, wenn er im aktuellen Telegramm enthalten war.
static void add_obis(JsonDocument &doc, const char *obis, const DeviceDataPoint &dp, time_t timestamp) {
  if (!dp.valid)
    return;
  JsonObject o = doc[obis].to<JsonObject>();
  o["value"] = dp.value;
  o["time"] = timestamp;
}

// Wie add_obis(), aber fuer berechnete Werte (value muss bis zum Serialisieren gueltig bleiben).
static void add_value(JsonDocument &doc, const char *obis, const char *value, int64_t timestamp) {
  JsonObject o = doc[obis].to<JsonObject>();
  o["value"] = value;
  o["time"] = timestamp;
}

bool Amis::build_sma_json_() {
  // 1. Dokument initialisieren
  JsonDocument doc;
  AmisDevice *a = this->pAmis_;
  const time_t ts = a->device.timeStamp;

  uint32_t espSystemTime = esp_timer_get_time() / 1000000;

  // 2. OBIS-Werte mit Unter-Objekten (value/time) erstellen
  JsonObject ident = doc["0-0:96.1.0"].to<JsonObject>();
  ident["value"] = a->device.identNr;

  add_obis(doc, "0-0:0.9.1", a->time, ts);
  add_obis(doc, "0-0:0.9.2", a->date, ts);
  add_obis(doc, "1-0:1.8.0", a->m18x[0], ts);
  add_obis(doc, "1-0:2.8.0", a->m28x[0], ts);
  add_obis(doc, "1-0:3.8.1", a->m38x[1], ts);
  add_obis(doc, "1-0:4.8.1", a->m48x[1], ts);
  add_obis(doc, "1-0:1.7.0", a->m170, ts);
  add_obis(doc, "1-0:2.7.0", a->m270, ts);
  add_obis(doc, "1-0:3.7.0", a->m370, ts);
  add_obis(doc, "1-0:4.7.0", a->m470, ts);
  add_obis(doc, "0-0:0.128.0", a->m01280, ts);
  add_obis(doc, "1-0:15.7.0", a->m1570, ts);

  // Berechnete Viertelstunden-Werte. 1-0:1.4.0 (laufende Viertelstunde) hat wie ueblich time = 0,
  // 1-0:1.5.0 und 1-0:1.6.0 tragen die Zaehlerzeit am Ende der jeweiligen Viertelstunde.
  char current[16], last[16], maximum[16];
  if (this->qh_.current_valid()) {
    snprintf(current, sizeof(current), "%.0f", this->qh_.current_w());
    add_value(doc, "1-0:1.4.0", current, 0);
  }
  if (this->qh_.last_valid()) {
    snprintf(last, sizeof(last), "%.0f", this->qh_.last_w());
    add_value(doc, "1-0:1.5.0", last, this->qh_.last_end_epoch());
  }
  if (this->qh_.max_known()) {
    snprintf(maximum, sizeof(maximum), "%.0f", this->qh_.max_w());
    add_value(doc, "1-0:1.6.0", maximum, this->qh_.max_end_epoch());
  }

  // 3. Root-Elemente am Ende hinzufügen
  doc["api_version"] = "v1.0";
  doc["name"] = "AmisReader";
  doc["sma_time"] = espSystemTime;

  // 4. In String umwandeln (abgeschnittenes JSON wird nicht gesendet)
  if (measureJson(doc) >= sizeof(a->device.output)) {
    ESP_LOGE(TAG, "JSON zu groß für den Ausgabepuffer (%u Byte)", (unsigned) sizeof(a->device.output));
    return false;
  }
  serializeJson(doc, a->device.output, sizeof(a->device.output));

  ESP_LOGV(TAG, "JSON: %s", a->device.output);
  return true;
}
#endif

}  // namespace amis
}  // namespace esphome

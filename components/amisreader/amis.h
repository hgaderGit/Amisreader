#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/preferences.h"
#include <string>
#include <vector>

extern "C" {
#include "parser.h"
}

#include "quarter_hour.h"

namespace esphome {
namespace amis {

class Amis : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_mqtt_topic(const std::string &topic) { this->json_topic_ = topic; }

  //  AES Key (genau 16 Byte)
  void set_aes_key(const std::vector<uint8_t> &key);

  //  Sensoren
  void set_power_sensor(sensor::Sensor *s) { this->power_sensor_ = s; }
  void set_power_forward_sensor(sensor::Sensor *s) { this->power_forward_sensor_ = s; }
  void set_power_reverse_sensor(sensor::Sensor *s) { this->power_reverse_sensor_ = s; }
  void set_reactive_power_import_sensor(sensor::Sensor *s) { this->reactive_power_import_sensor_ = s; }
  void set_reactive_power_export_sensor(sensor::Sensor *s) { this->reactive_power_export_sensor_ = s; }
  void set_energy_forward_sensor(sensor::Sensor *s) { this->energy_forward_sensor_ = s; }
  void set_energy_reverse_sensor(sensor::Sensor *s) { this->energy_reverse_sensor_ = s; }
  void set_reactive_energy_import_sensor(sensor::Sensor *s) { this->reactive_energy_import_sensor_ = s; }
  void set_reactive_energy_export_sensor(sensor::Sensor *s) { this->reactive_energy_export_sensor_ = s; }
  void set_energy_inkasso_sensor(sensor::Sensor *s) { this->energy_inkasso_sensor_ = s; }

  //  Berechnete Viertelstunden-Werte (aus dem Zaehlerstand 1-0:1.8.0 und der Zaehlerzeit)
  void set_power_quarter_hour_sensor(sensor::Sensor *s) { this->power_quarter_hour_sensor_ = s; }
  void set_power_last_quarter_hour_sensor(sensor::Sensor *s) { this->power_last_quarter_hour_sensor_ = s; }
  void set_power_max_month_sensor(sensor::Sensor *s) { this->power_max_month_sensor_ = s; }

  //  Zaehlerzeit aus dem Telegramm
  void set_date_time_sensor(text_sensor::TextSensor *s) { this->date_time_sensor_ = s; }

 protected:
  void process_rx_();
  void handle_frame_(size_t frame_size);
  void publish_sensors_();
  void update_quarter_hour_();
  static void publish_value_(sensor::Sensor *s, const DeviceDataPoint &dp, double divisor);

#ifdef USE_MQTT
  bool build_sma_json_();
#endif

  std::vector<uint8_t> rx_buffer_;
  std::string json_topic_{"amisreader/json_sma"};  // Default-Wert, falls nichts in der YAML steht

  AmisDevice *pAmis_{nullptr};
  uint8_t aes_key_[16]{};
  QuarterHour qh_;
  ESPPreferenceObject pref_;
  uint8_t quarter_changed_{0};
  bool max_published_{false};

  uint32_t last_rx_ms_{0};
  uint32_t frame_errors_{0};
  uint32_t parse_errors_{0};
  bool unknown_logged_{false};

  sensor::Sensor *power_sensor_{nullptr};
  sensor::Sensor *power_forward_sensor_{nullptr};
  sensor::Sensor *power_reverse_sensor_{nullptr};
  sensor::Sensor *reactive_power_import_sensor_{nullptr};
  sensor::Sensor *reactive_power_export_sensor_{nullptr};
  sensor::Sensor *energy_forward_sensor_{nullptr};
  sensor::Sensor *energy_reverse_sensor_{nullptr};
  sensor::Sensor *reactive_energy_import_sensor_{nullptr};
  sensor::Sensor *reactive_energy_export_sensor_{nullptr};
  sensor::Sensor *energy_inkasso_sensor_{nullptr};
  sensor::Sensor *power_quarter_hour_sensor_{nullptr};
  sensor::Sensor *power_last_quarter_hour_sensor_{nullptr};
  sensor::Sensor *power_max_month_sensor_{nullptr};
  text_sensor::TextSensor *date_time_sensor_{nullptr};
};

}  // namespace amis
}  // namespace esphome

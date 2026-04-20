#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace esp32_two_way_audio {

class ESP32TwoWayAudio : public PollingComponent {
 public:
  ESP32TwoWayAudio() : PollingComponent(10) {}

  // ── Pin setters (called from generated code) ──────────────────────────
  void set_bck_pin(int pin)      { bck_pin_ = pin; }
  void set_ws_pin(int pin)       { ws_pin_ = pin; }
  void set_data_out_pin(int pin) { data_out_pin_ = pin; }
  void set_data_in_pin(int pin)  { data_in_pin_ = pin; }

  // ── Sensor / switch setters ───────────────────────────────────────────
  void set_microphone_switch(binary_sensor::BinarySensor *s) { microphone_switch = s; }
  void set_speaker_switch(binary_sensor::BinarySensor *s)    { speaker_switch = s; }
  void set_call_tone_switch(binary_sensor::BinarySensor *s)  { call_tone_switch = s; }
  void set_partner_ip_sensor(text_sensor::TextSensor *s)     { partner_ip_sensor = s; }
  void set_com_port(uint16_t port)                           { com_port = port; }

  void setup() override;
  void update() override;

  static void transmit_task(void *arg);
  static void receive_task(void *arg);
  void play_call_tone();
  static void play_call_tone_task(void *arg);

  // Public so static tasks can access them directly
  binary_sensor::BinarySensor *microphone_switch  = nullptr;
  binary_sensor::BinarySensor *speaker_switch     = nullptr;
  binary_sensor::BinarySensor *call_tone_switch   = nullptr;
  text_sensor::TextSensor     *partner_ip_sensor  = nullptr;
  uint16_t                     com_port           = 8000;
  int                          udp_tx_socket      = -1;
  int                          udp_rx_socket      = -1;
  bool                         is_tone_task_running_ = false;

 protected:
  bool is_microphone_enabled() { return microphone_switch && microphone_switch->state; }
  bool is_speaker_enabled()    { return speaker_switch    && speaker_switch->state; }
  bool is_call_tone_enabled()  { return call_tone_switch  && call_tone_switch->state; }

  void set_partner_ip(const char *ip) {
    if (partner_ip_sensor != nullptr)
      partner_ip_sensor->publish_state(ip != nullptr ? ip : "");
  }
  const char *get_partner_ip() {
    if (partner_ip_sensor != nullptr)
      return partner_ip_sensor->state.c_str();
    return nullptr;
  }

  // I2S pins — configured from YAML
  int bck_pin_      = -1;
  int ws_pin_       = -1;
  int data_out_pin_ = -1;
  int data_in_pin_  = -1;

  bool sockets_created_ = false;
};

}  // namespace esp32_two_way_audio
}  // namespace esphome

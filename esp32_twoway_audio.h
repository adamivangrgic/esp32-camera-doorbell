#pragma once
#include "esphome.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/log.h"

class ESP32TwoWayAudio : public esphome::PollingComponent {
 public:
  ESP32TwoWayAudio(uint32_t update_interval = 10) : esphome::PollingComponent(update_interval) {}

  void set_microphone_switch(esphome::binary_sensor::BinarySensor *microphone_switch) {
    this->microphone_switch = microphone_switch;
  }
  void set_speaker_switch(esphome::binary_sensor::BinarySensor *speaker_switch) {
    this->speaker_switch = speaker_switch;
  }
  
  void set_call_tone_switch(esphome::binary_sensor::BinarySensor *call_tone_switch) {
    this->call_tone_switch = call_tone_switch;
  }

  // void set_remote_address(const char *remote_ip, uint16_t remote_port) {
  //   strncpy(this->remote_ip, remote_ip, sizeof(this->remote_ip));
  //   this->remote_port = remote_port;
  // }

  void set_partner_ip_sensor(esphome::text_sensor::TextSensor *sensor) {
    this->partner_ip_sensor = sensor;
  }

  void set_com_port(uint16_t port) { com_port = port; }

  void setup() override;
  void update() override;

  static void transmit_task(void *arg);
  static void receive_task(void *arg);

  void play_call_tone();
  static void play_call_tone_task(void *arg);

  esphome::binary_sensor::BinarySensor *microphone_switch = nullptr;
  esphome::binary_sensor::BinarySensor *speaker_switch = nullptr;
  esphome::binary_sensor::BinarySensor *call_tone_switch = nullptr;
  esphome::text_sensor::TextSensor *partner_ip_sensor = nullptr;

 protected:
  bool is_microphone_enabled() { return microphone_switch && microphone_switch->state; }
  bool is_speaker_enabled() { return speaker_switch && speaker_switch->state; }
  bool is_call_tone_enabled() { return call_tone_switch && call_tone_switch->state; }

  void set_partner_ip(const char* ip_address) {
    if (this->partner_ip_sensor != nullptr) {
      this->partner_ip_sensor->publish_state(ip_address);
    }
  }

  const char* get_partner_ip() {
    if (this->partner_ip_sensor != nullptr) {
      return this->partner_ip_sensor->state.c_str();
    }
    return nullptr;
  }

  // char remote_ip[16] = "192.168.1.100";  // Default IP; override in YAML.
  // uint16_t remote_port = 8000;
  uint16_t com_port = 8000;

  // UDP sockets
  int udp_tx_socket = -1;
  int udp_rx_socket = -1;

  // Flag to ensure sockets/tasks are created only once.
  bool sockets_created_ = false;

  // Flag to track if the tone task is running
  bool is_tone_task_running_ = false;
};
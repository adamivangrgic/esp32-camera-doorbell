#include "esp32_two_way_audio.h"
#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "esphome/components/network/util.h"

namespace esphome {
namespace esp32_two_way_audio {

#define I2S_NUM         I2S_NUM_1
#define SAMPLE_RATE     16000
#define I2S_READ_LEN    (1024)
#define I2S_BUFFER_LEN  (1024)

static const i2s_config_t i2s_config = {
  .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX),
  .sample_rate          = SAMPLE_RATE,
  .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
  .communication_format = I2S_COMM_FORMAT_STAND_I2S,
  .intr_alloc_flags     = 0,
  .dma_buf_count        = 8,
  .dma_buf_len          = 64,
  .use_apll             = false,
  .tx_desc_auto_clear   = true,
  .fixed_mclk           = 0
};

void ESP32TwoWayAudio::setup() {
  // Build pin config from YAML-supplied values
  const i2s_pin_config_t pin_config = {
    .bck_io_num   = bck_pin_,
    .ws_io_num    = ws_pin_,
    .data_out_num = data_out_pin_,
    .data_in_num  = data_in_pin_
  };

  i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM, &pin_config);
  ESP_LOGI("ESP32TwoWayAudio", "I2S driver installed (bck=%d ws=%d out=%d in=%d)",
           bck_pin_, ws_pin_, data_out_pin_, data_in_pin_);
}

void ESP32TwoWayAudio::update() {
  if (!sockets_created_) {
    // Works for WiFi *and* Ethernet — no dependency on either component
    if (network::is_connected()) {

      udp_tx_socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (udp_tx_socket < 0) {
        ESP_LOGE("ESP32TwoWayAudio", "Failed to create UDP TX socket");
      }

      udp_rx_socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (udp_rx_socket < 0) {
        ESP_LOGE("ESP32TwoWayAudio", "Failed to create UDP RX socket");
      } else {
        struct sockaddr_in local_addr;
        local_addr.sin_family      = AF_INET;
        local_addr.sin_port        = htons(com_port);
        local_addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(udp_rx_socket, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
          ESP_LOGE("ESP32TwoWayAudio", "Failed to bind UDP RX socket");
        } else {
          ESP_LOGI("ESP32TwoWayAudio", "UDP RX socket bound to port %d", com_port);
        }
      }

      xTaskCreate(transmit_task, "audio_transmit_task", 4096, this, 1, NULL);
      xTaskCreate(receive_task,  "audio_receive_task",  4096, this, 1, NULL);

      sockets_created_ = true;
    }
  }

  if (is_call_tone_enabled()) {
    play_call_tone();
  }
}

void ESP32TwoWayAudio::transmit_task(void *arg) {
  ESP32TwoWayAudio *audio = static_cast<ESP32TwoWayAudio *>(arg);
  uint8_t buffer[I2S_BUFFER_LEN];
  while (true) {
    if (audio->is_microphone_enabled() && audio->get_partner_ip()) {
      size_t bytes_read = 0;
      if (i2s_read(I2S_NUM, (void *)buffer, I2S_BUFFER_LEN, &bytes_read, 10) == ESP_OK && bytes_read > 0) {
        struct sockaddr_in remote_addr;
        remote_addr.sin_family      = AF_INET;
        remote_addr.sin_port        = htons(audio->com_port);
        remote_addr.sin_addr.s_addr = inet_addr(audio->get_partner_ip());
        sendto(audio->udp_tx_socket, buffer, bytes_read, 0,
               (struct sockaddr *)&remote_addr, sizeof(remote_addr));
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void ESP32TwoWayAudio::receive_task(void *arg) {
  ESP32TwoWayAudio *audio = static_cast<ESP32TwoWayAudio *>(arg);
  uint8_t buffer[I2S_BUFFER_LEN];

  bool     is_microphone_off = false;
  uint32_t quiet_duration    = 0;
  constexpr uint32_t QUIET_TIME_THRESHOLD = 250;
  constexpr int      AUDIO_THRESHOLD      = 12000;

  bool     sender_locked    = false;
  uint32_t last_packet_time = 0;

  while (true) {
    if (audio->is_speaker_enabled()) {
      struct sockaddr_in sender_addr;
      socklen_t addr_len = sizeof(sender_addr);

      int len = recvfrom(audio->udp_rx_socket, buffer, I2S_BUFFER_LEN, 0,
                         (struct sockaddr *)&sender_addr, &addr_len);
      if (len > 0) {
        last_packet_time = xTaskGetTickCount();

        if (!sender_locked) {
          const char *sender_ip = inet_ntoa(sender_addr.sin_addr);
          audio->set_partner_ip(sender_ip);
          sender_locked = true;
          ESP_LOGI("ESP32TwoWayAudio", "Sender locked: %s", sender_ip);
        }

        // RMS-based hysteresis for half-duplex mic muting
        int64_t sum = 0;
        for (int i = 0; i < len / 2; i++) {
          int16_t sample = (buffer[i * 2] << 8) | buffer[i * 2 + 1];
          sum += static_cast<int64_t>(sample) * sample;
        }
        int rms = sqrt(sum / (len / 2));

        if (rms > AUDIO_THRESHOLD && !is_microphone_off) {
          audio->microphone_switch->publish_state(false);
          is_microphone_off = true;
          quiet_duration    = 0;
        } else if (rms < AUDIO_THRESHOLD / 2 && is_microphone_off) {
          quiet_duration += 10;
          if (quiet_duration >= QUIET_TIME_THRESHOLD) {
            audio->microphone_switch->publish_state(true);
            is_microphone_off = false;
            quiet_duration    = 0;
          }
        } else {
          quiet_duration = 0;
        }

        size_t bytes_written = 0;
        i2s_write(I2S_NUM, (const char *)buffer, len, &bytes_written, 10);
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Connection timeout
    uint32_t current_time = xTaskGetTickCount();
    if (sender_locked && (current_time - last_packet_time > pdMS_TO_TICKS(5000))) {
      sender_locked = false;
      audio->set_partner_ip(nullptr);
      ESP_LOGI("ESP32TwoWayAudio", "Sender connection timeout, unlocked");
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void ESP32TwoWayAudio::play_call_tone_task(void *arg) {
  ESP32TwoWayAudio *audio = static_cast<ESP32TwoWayAudio *>(arg);

  constexpr int   tone_frequency      = 440;
  constexpr int   call_beep_duration  = 50;
  constexpr int   call_silence_duration = 50;
  float           volume              = 0.05f;

  int16_t tone_buffer[I2S_BUFFER_LEN];
  int samples_per_cycle = SAMPLE_RATE / tone_frequency;
  bool is_high = true;

  for (int i = 0; i < I2S_BUFFER_LEN; i++) {
    if (i % samples_per_cycle == 0) is_high = !is_high;
    int16_t sample  = is_high ? 32767 : -32768;
    tone_buffer[i]  = static_cast<int16_t>(sample * volume);
  }

  int beep_samples = (call_beep_duration * SAMPLE_RATE) / 1000;

  while (audio->is_call_tone_enabled()) {
    int samples_played = 0;
    while (samples_played < beep_samples) {
      if (!audio->is_call_tone_enabled()) break;
      size_t bytes_written   = 0;
      int    samples_to_write = std::min(I2S_BUFFER_LEN, beep_samples - samples_played);
      i2s_write(I2S_NUM, (const char *)tone_buffer,
                samples_to_write * sizeof(int16_t), &bytes_written, portMAX_DELAY);
      samples_played += samples_to_write;
    }
    vTaskDelay(pdMS_TO_TICKS(call_silence_duration));
  }

  if (audio->call_tone_switch) {
    audio->call_tone_switch->publish_state(false);
    ESP_LOGI("ESP32TwoWayAudio", "Call tone switch reverted to off");
  }

  audio->is_tone_task_running_ = false;
  vTaskDelete(NULL);
}

void ESP32TwoWayAudio::play_call_tone() {
  if (is_tone_task_running_) return;
  is_tone_task_running_ = true;
  if (xTaskCreate(play_call_tone_task, "tone_task", 4096, this, 1, nullptr) != pdPASS) {
    ESP_LOGE("ESP32TwoWayAudio", "Failed to create tone task");
    is_tone_task_running_ = false;
  }
}

}  // namespace esp32_two_way_audio
}  // namespace esphome
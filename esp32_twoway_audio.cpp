#include "esp32_twoway_audio.h"
#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

// Include WiFi component header to check connectivity.
#include "esphome/components/wifi/wifi_component.h"

using namespace esphome;

#define I2S_NUM         I2S_NUM_1
#define SAMPLE_RATE     16000
#define I2S_READ_LEN    (1024)
#define I2S_BUFFER_LEN  (1024)

static const i2s_config_t i2s_config = {
  .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
  .communication_format = I2S_COMM_FORMAT_STAND_I2S,
  .intr_alloc_flags = 0, // (ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_SHARED | ESP_INTR_FLAG_INTRDISABLED),
  .dma_buf_count = 8,
  .dma_buf_len = 64,
  .use_apll = false,
  .tx_desc_auto_clear = true,
  .fixed_mclk = 0
};

static const i2s_pin_config_t pin_config = {
  .bck_io_num = 15,    // sck
  .ws_io_num = 13,     // lrc
  .data_out_num = 14,  // Speaker output.
  .data_in_num = 12    // Microphone input.
};

void ESP32TwoWayAudio::setup() {
  // Initialize I2S driver.
  i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM, &pin_config);
  ESP_LOGI("ESP32TwoWayAudio", "I2S driver installed");
  // Note: UDP sockets and tasks will be created in update() after WiFi is connected.
}

void ESP32TwoWayAudio::update() {
  // Only create sockets and tasks once.
  if (!sockets_created_) {
    // Check if WiFi is connected.
    if (esphome::wifi::global_wifi_component != nullptr &&
        esphome::wifi::global_wifi_component->is_connected()) {

      // Create UDP transmit socket.
      udp_tx_socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (udp_tx_socket < 0) {
        ESP_LOGE("ESP32TwoWayAudio", "Failed to create UDP TX socket");
      }

      // Create UDP receive socket.
      udp_rx_socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (udp_rx_socket < 0) {
        ESP_LOGE("ESP32TwoWayAudio", "Failed to create UDP RX socket");
      }
      struct sockaddr_in local_addr;
      local_addr.sin_family = AF_INET;
      local_addr.sin_port = htons(com_port);
      local_addr.sin_addr.s_addr = INADDR_ANY;
      if (bind(udp_rx_socket, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        ESP_LOGE("ESP32TwoWayAudio", "Failed to bind UDP RX socket");
      } else {
        ESP_LOGI("ESP32TwoWayAudio", "UDP RX socket bound to port %d", com_port);
      }

      // Launch tasks for audio transmit and receive.
      xTaskCreate(transmit_task, "audio_transmit_task", 4096, this, 1, NULL);
      xTaskCreate(receive_task, "audio_receive_task", 4096, this, 1, NULL);

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
        remote_addr.sin_family = AF_INET;
        remote_addr.sin_port = htons(audio->com_port);
        remote_addr.sin_addr.s_addr = inet_addr(audio->get_partner_ip());
        sendto(audio->udp_tx_socket, buffer, bytes_read, 0, (struct sockaddr *)&remote_addr, sizeof(remote_addr));
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
  bool is_microphone_off = false;                     // Track if the microphone was turned off
  uint32_t quiet_duration = 0;                        // Track how long the audio level has been below the threshold
  constexpr uint32_t QUIET_TIME_THRESHOLD = 250;
  constexpr static int AUDIO_THRESHOLD = 12000;
  
  bool sender_locked = false;
  uint32_t last_packet_time = 0;

  while (true) {
    if (audio->is_speaker_enabled()) {
      struct sockaddr_in sender_addr;
      socklen_t addr_len = sizeof(sender_addr);

      int len = recvfrom(audio->udp_rx_socket, buffer, I2S_BUFFER_LEN, 0, (struct sockaddr *)&sender_addr, &addr_len);
      if (len > 0) {
        last_packet_time = xTaskGetTickCount();
        
        if (!sender_locked) {
          const char* sender_ip = inet_ntoa(sender_addr.sin_addr);
          audio->set_partner_ip(sender_ip);
          sender_locked = true;
          ESP_LOGI("ESP32TwoWayAudio", "Sender locked: %s", sender_ip);
        }

        // Measure the audio level (RMS)
        int64_t sum = 0; // Use 64-bit integer to avoid overflow
        for (int i = 0; i < len / 2; i++) {
          int16_t sample = (buffer[i * 2] << 8) | buffer[i * 2 + 1];
          sum += static_cast<int64_t>(sample) * sample; // Cast to int64_t before squaring
        }
        int rms = sqrt(sum / (len / 2));

        // Log the RMS value for debugging
        // ESP_LOGI("ESP32TwoWayAudio", "RMS value: %d", rms);

        // Hysteresis logic
        if (rms > AUDIO_THRESHOLD && !is_microphone_off) {
          audio->microphone_switch->publish_state(false);
          is_microphone_off = true;
          quiet_duration = 0; // Reset the quiet duration timer
        } else if (rms < AUDIO_THRESHOLD / 2 && is_microphone_off) {
          // Increment the quiet duration timer
          quiet_duration += 10; // Add the delay time (10 ms)

          // If the audio level has been below the threshold for the specified duration, turn the microphone back on
          if (quiet_duration >= QUIET_TIME_THRESHOLD) {
            audio->microphone_switch->publish_state(true);
            is_microphone_off = false;
            quiet_duration = 0; // Reset the quiet duration timer
          }
        } else {
          quiet_duration = 0; // Reset the quiet duration timer if the audio level is above the threshold
        }

        // Play the received audio
        size_t bytes_written = 0;
        i2s_write(I2S_NUM, (const char *)buffer, len, &bytes_written, 10);
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Check for connection timeout every loop iteration
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

  constexpr int tone_frequency = 440;                  // Frequency of the tone (440 Hz)
  constexpr int call_beep_duration = 50;               // Duration of each beep in milliseconds
  constexpr int call_silence_duration = 50;            // Duration of silence between beeps
  float volume = 0.05f;                                // Volume level (0.0 to 1.0)

  int16_t tone_buffer[I2S_BUFFER_LEN];

  // Calculate samples per cycle
  int samples_per_cycle = SAMPLE_RATE / tone_frequency;

  // Track the phase of the square wave
  bool is_high = true; // Start with the high state of the square wave

  // Generate a square wave for the tone, scaled by the volume level
  for (int i = 0; i < I2S_BUFFER_LEN; i++) {
    // Toggle the square wave state at the halfway point of each cycle
    if (i % samples_per_cycle == 0) {
      is_high = !is_high;
    }

    // Generate the square wave sample
    int16_t sample = is_high ? 32767 : -32768;
    tone_buffer[i] = static_cast<int16_t>(sample * volume); // Apply volume
  }

  // Calculate the number of samples for the beep duration
  int beep_samples = (call_beep_duration * SAMPLE_RATE) / 1000;

  // Play the call tone
  while (audio->is_call_tone_enabled()) {
    int samples_played = 0;
    while (samples_played < beep_samples) {
      // Check if the binary sensor is still enabled
      if (!audio->is_call_tone_enabled()) {
        ESP_LOGI("ESP32TwoWayAudio", "Call tone switch disabled, stopping playback immediately");
        break; // Stop playback immediately if the switch is disabled
      }

      // Play a chunk of the tone
      size_t bytes_written = 0;
      int samples_to_write = std::min(I2S_BUFFER_LEN, beep_samples - samples_played);
      i2s_write(I2S_NUM, (const char *)tone_buffer, samples_to_write * sizeof(int16_t), &bytes_written, portMAX_DELAY);
      samples_played += samples_to_write;
    }

    // Add silence between beeps
    vTaskDelay(pdMS_TO_TICKS(call_silence_duration));
  }

  // Revert the binary sensor state after the tone sequence is complete
  if (audio->call_tone_switch) {
    audio->call_tone_switch->publish_state(false); // Revert the switch to "off"
    ESP_LOGI("ESP32TwoWayAudio", "Call tone switch reverted to off");
  }

  // Reset the flag to indicate the task is no longer running
  audio->is_tone_task_running_ = false;

  // Delete the task
  vTaskDelete(NULL);
}

void ESP32TwoWayAudio::play_call_tone() {
  // Check if the tone task is already running
  if (is_tone_task_running_) {
    // ESP_LOGI("ESP32TwoWayAudio", "Tone task is already running, skipping new task creation");
    return;
  }

  // Set the flag to indicate the task is running
  is_tone_task_running_ = true;

  // Create the task
  if (xTaskCreate(
          play_call_tone_task, // Task function
          "tone_task",                // Task name
          4096,                       // Stack size
          this,                       // Task parameter
          1,                          // Task priority
          nullptr                     // Task handle (not used here)
      ) != pdPASS) {
    ESP_LOGE("ESP32TwoWayAudio", "Failed to create tone task");
    is_tone_task_running_ = false; // Reset the flag if task creation fails
  }
}
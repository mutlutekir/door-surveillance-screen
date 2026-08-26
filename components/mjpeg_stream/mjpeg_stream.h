#pragma once

#include "esphome/core/component.h"

#include <lvgl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_http_client.h"

#include <string>

namespace esphome {
namespace mjpeg_stream {

class MjpegStream : public Component {
 public:
  ~MjpegStream(); // Bellek sızıntısını önlemek için yıkıcı eklendi

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_url(const std::string &url) { this->url_ = url; }
  void set_size(int width, int height) {
    this->width_ = width;
    this->height_ = height;
  }
  void set_stack_size(uint32_t stack_size) { this->stack_size_ = stack_size; }
  void set_jpeg_buffer_size(uint32_t size) { this->jpeg_buffer_size_ = size; }

  void set_canvas(lv_obj_t *canvas) { this->canvas_ = canvas; }
  
  // Ekran kapandığında yayını durdurmak için yeni metod
  void set_stream_active(bool active) { this->stream_active_ = active; }

  uint32_t get_frame_count() const { return this->frame_count_; }
  uint32_t get_error_count() const { return this->error_count_; }
  uint32_t get_last_frame_ms() const { return this->last_frame_ms_; }

 protected:
  static void stream_task_trampoline(void *param);
  void stream_task_();
  void run_connection_();
  size_t read_one_frame_(esp_http_client_handle_t client);
  bool decode_jpeg_(size_t jpeg_len);

  std::string url_;
  int width_{0};
  int height_{0};
  uint32_t stack_size_{8192};
  uint32_t jpeg_buffer_size_{131072};

  lv_obj_t *canvas_{nullptr};

  uint8_t *jpeg_buf_{nullptr};     
  uint16_t *decode_buf_{nullptr};  

  SemaphoreHandle_t frame_mutex_{nullptr};
  volatile bool new_frame_ready_{false};
  volatile bool stream_active_{true}; // Akış durumu takibi

  TaskHandle_t task_handle_{nullptr};

  uint32_t frame_count_{0};
  uint32_t error_count_{0};
  uint32_t last_frame_ms_{0};
};

}  // namespace mjpeg_stream
}  // namespace esphome

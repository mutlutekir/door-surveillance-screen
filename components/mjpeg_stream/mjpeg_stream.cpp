#include "mjpeg_stream.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include "esp_heap_caps.h"
#include "rom/tjpgd.h"

#include <cstring>

namespace esphome {
namespace mjpeg_stream {

static const char *const TAG = "mjpeg_stream";

namespace {

struct JpegCtx {
  const uint8_t *src;
  size_t src_size;
  size_t src_pos;

  uint16_t *dst;   
  int dst_w;
  int dst_h;
  int src_w;  
  int src_h;  
};

inline uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

size_t jd_input_cb(JDEC *jd, uint8_t *buf, size_t nbyte) {
  auto *ctx = static_cast<JpegCtx *>(jd->device);
  size_t avail = ctx->src_size - ctx->src_pos;
  size_t n = nbyte < avail ? nbyte : avail;
  if (buf != nullptr && n > 0) {
    memcpy(buf, ctx->src + ctx->src_pos, n);
  }
  ctx->src_pos += n;
  return n;
}

UINT jd_output_cb(JDEC *jd, void *bitmap, JRECT *rect) {
  auto *ctx = static_cast<JpegCtx *>(jd->device);
  const uint8_t *src = static_cast<const uint8_t *>(bitmap);

  int tile_w = rect->right - rect->left + 1;
  int tile_h = rect->bottom - rect->top + 1;

  for (int ty = 0; ty < tile_h; ty++) {
    int y = rect->top + ty;
    if (y < 0 || y >= ctx->dst_h) continue;

    for (int tx = 0; tx < tile_w; tx++) {
      int x = rect->left + tx;
      if (x < 0 || x >= ctx->dst_w) continue;

      const uint8_t *px = src + (ty * tile_w + tx) * 3;  
      ctx->dst[y * ctx->dst_w + x] = rgb888_to_rgb565(px[0], px[1], px[2]);
    }
  }
  return 1;  
}

}  // namespace

// Bellek temizliği için yıkıcı (Destructor)
MjpegStream::~MjpegStream() {
  if (this->jpeg_buf_ != nullptr) heap_caps_free(this->jpeg_buf_);
  if (this->decode_buf_ != nullptr) heap_caps_free(this->decode_buf_);
}

void MjpegStream::setup() {
  ESP_LOGCONFIG(TAG, "MJPEG Stream:");
  ESP_LOGCONFIG(TAG, "  URL: %s", this->url_.c_str());
  ESP_LOGCONFIG(TAG, "  Size: %dx%d", this->width_, this->height_);

  this->jpeg_buf_ = static_cast<uint8_t *>(
      heap_caps_malloc(this->jpeg_buffer_size_, MALLOC_CAP_SPIRAM));
  this->decode_buf_ = static_cast<uint16_t *>(heap_caps_malloc(
      static_cast<size_t>(this->width_) * this->height_ * 2, MALLOC_CAP_SPIRAM));

  if (this->jpeg_buf_ == nullptr || this->decode_buf_ == nullptr) {
    ESP_LOGE(TAG, "PSRAM allocation failed - not starting stream task");
    this->mark_failed();
    return;
  }

  this->frame_mutex_ = xSemaphoreCreateMutex();

  // İşlemci rahatlasın diye priority 5'ten 2'ye düşürüldü
  BaseType_t ok = xTaskCreatePinnedToCore(&MjpegStream::stream_task_trampoline, "mjpeg_stream", this->stack_size_,
                                            this, 2 /* priority */, &this->task_handle_, 0 /* core */);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to create stream task");
    this->mark_failed();
  }
}

void MjpegStream::loop() {
  if (this->canvas_ == nullptr || !this->new_frame_ready_) return;
  if (xSemaphoreTake(this->frame_mutex_, 0) != pdTRUE) return;  

  void *canvas_buf = const_cast<void *>(lv_canvas_get_buf(this->canvas_));
  if (canvas_buf != nullptr) {
    memcpy(canvas_buf, this->decode_buf_, static_cast<size_t>(this->width_) * this->height_ * 2);
    lv_obj_invalidate(this->canvas_);
  }

  this->new_frame_ready_ = false;
  xSemaphoreGive(this->frame_mutex_);
}

void MjpegStream::stream_task_trampoline(void *param) {
  static_cast<MjpegStream *>(param)->stream_task_();
}

void MjpegStream::stream_task_() {
  uint32_t backoff_ms = 1000;
  const uint32_t max_backoff_ms = 15000;

  while (true) {
    ESP_LOGI(TAG, "Connecting to %s", this->url_.c_str());
    this->run_connection_();
    
    ESP_LOGW(TAG, "Stream disconnected, retrying in %lu ms", (unsigned long) backoff_ms);
    vTaskDelay(pdMS_TO_TICKS(backoff_ms));
    backoff_ms = std::min(backoff_ms * 2, max_backoff_ms);
  }
}

void MjpegStream::run_connection_() {
  esp_http_client_config_t config = {};
  config.url = this->url_.c_str();
  config.timeout_ms = 3000; 
  config.keep_alive_enable = true;
  config.buffer_size = 4096;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) return;

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    esp_http_client_cleanup(client);
    return;
  }

  esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
  
  if (status != 200) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return;
  }

  while (esp_http_client_is_complete_data_received(client) == false) {
    
    // YENİ MANTIK (SÜPÜRGE): Ekran kapalıysa bağlantıyı KESME!
    // FFMPEG'in uyumasını engellemek için veriyi emip anında çöpe atıyoruz.
    if (!this->stream_active_) {
      static uint8_t trash[4096];
      int r = esp_http_client_read(client, reinterpret_cast<char *>(trash), sizeof(trash));
      if (r <= 0) break; // Sunucu gerçekten çökerse döngüyü kır ve baştan bağlan
      
      // Çekirdeği kitlememek için 1 milisaniyelik mini nefes payı
      vTaskDelay(pdMS_TO_TICKS(1)); 
      continue; 
    }

    // Ekran açıksa normal şekilde yeni kareyi bekle ve oku
    uint32_t read_start = millis();
    size_t frame_len = this->read_one_frame_(client);
    if (frame_len == 0) break;

    uint32_t read_time = millis() - read_start;

    // 150ms Gecikme Filtresi (TCP kuyruğunda birikmiş bayat kareleri atla)
    if (read_time < 150 || this->new_frame_ready_) {
      continue; 
    }

    // Görüntü taze ise çöz (decode) ve ekrana bas
    if (xSemaphoreTake(this->frame_mutex_, pdMS_TO_TICKS(200)) == pdTRUE) {
      if (this->decode_jpeg_(frame_len)) {
        this->new_frame_ready_ = true;
        this->frame_count_++;
        this->last_frame_ms_ = millis();
      } else {
        this->error_count_++;
      }
      xSemaphoreGive(this->frame_mutex_);
    }
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
}

size_t MjpegStream::read_one_frame_(esp_http_client_handle_t client) {
  static uint8_t chunk[4096];

  size_t len = 0;
  bool found_soi = false;
  uint8_t prev_byte = 0;

  while (true) {
    int r = esp_http_client_read(client, reinterpret_cast<char *>(chunk), sizeof(chunk));
    if (r <= 0) return 0; 

    for (int i = 0; i < r; i++) {
      uint8_t byte = chunk[i];

      if (!found_soi) {
        if (prev_byte == 0xFF && byte == 0xD8) {
          this->jpeg_buf_[0] = 0xFF;
          this->jpeg_buf_[1] = 0xD8;
          len = 2;
          found_soi = true;
        }
        prev_byte = byte;
        continue;
      }

      if (len >= this->jpeg_buffer_size_) return 0;
      
      this->jpeg_buf_[len++] = byte;

      if (len >= 2 && this->jpeg_buf_[len - 2] == 0xFF && this->jpeg_buf_[len - 1] == 0xD9) {
        return len;  
      }
      prev_byte = byte;
    }
  }
}

bool MjpegStream::decode_jpeg_(size_t jpeg_len) {
  JpegCtx ctx{};
  ctx.src = this->jpeg_buf_;
  ctx.src_size = jpeg_len;
  ctx.src_pos = 0;
  ctx.dst = this->decode_buf_;
  ctx.dst_w = this->width_;
  ctx.dst_h = this->height_;

  static uint8_t work_buf[3100];  
  JDEC jd;

  JRESULT res = jd_prepare(&jd, jd_input_cb, work_buf, sizeof(work_buf), &ctx);
  if (res != JDR_OK) return false;

  ctx.src_w = jd.width;
  ctx.src_h = jd.height;

  res = jd_decomp(&jd, jd_output_cb, 0);
  if (res != JDR_OK) return false;

  return true;
}

}  // namespace mjpeg_stream
}  // namespace esphome

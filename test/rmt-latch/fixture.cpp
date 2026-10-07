// Host-only RMT/SDK models for the actual NeoPixelBus encoder-creation function.
// This measures the explicit encoded reset symbol, not a physical GPIO waveform.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>
using esp_err_t = int;
constexpr esp_err_t ESP_OK=0, ESP_ERR_INVALID_ARG=0x102, ESP_ERR_NO_MEM=0x101;
constexpr const char* TAG="test";
struct rmt_encoder_t {
  void (*encode)() = nullptr;
  esp_err_t (*del)(rmt_encoder_t*) = nullptr;
  esp_err_t (*reset)(rmt_encoder_t*) = nullptr;
};
using rmt_encoder_handle_t=rmt_encoder_t*;
struct rmt_symbol_word_t {
  uint32_t val=0;
  uint16_t duration0=0, duration1=0;
  uint8_t level0=0, level1=0;
};
struct rmt_bytes_encoder_config_t {
  rmt_symbol_word_t bit0, bit1;
  struct { bool msb_first=false; } flags;
};
struct rmt_copy_encoder_config_t {};
static esp_err_t rmt_new_bytes_encoder(const rmt_bytes_encoder_config_t*, rmt_encoder_handle_t* out) { *out=new rmt_encoder_t(); return ESP_OK; }
static esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t*, rmt_encoder_handle_t* out) { *out=new rmt_encoder_t(); return ESP_OK; }
static esp_err_t rmt_del_encoder(rmt_encoder_handle_t encoder) { delete encoder; return ESP_OK; }
#define __containerof(ptr,type,member) reinterpret_cast<type*>(reinterpret_cast<char*>(ptr)-offsetof(type,member))
#define ESP_GOTO_ON_FALSE(condition,code,label,...) do { if (!(condition)) { ret=(code); goto label; } } while (0)
#define ESP_GOTO_ON_ERROR(expression,label,...) do { ret=(expression); if (ret!=ESP_OK) goto label; } while (0)
/* SPEED_CLASSES */
/* ENCODER_STRUCTS */
template<class T_SPEED> struct Probe {
  static void rmt_encode_led_strip() {}
  static esp_err_t rmt_led_strip_encoder_reset(rmt_encoder_t*) { return ESP_OK; }
  /* DELETE_ENCODER */
  /* CREATE_ENCODER */
};
template<class Speed> static bool check(const char* name) {
  led_strip_encoder_config_t config{Speed::RmtTicksPerSecond};
  rmt_encoder_handle_t encoder=nullptr;
  if (Probe<Speed>::rmt_new_led_strip_encoder(&config,&encoder,Speed::RmtBit0,Speed::RmtBit1)!=ESP_OK) throw std::runtime_error("Encoder creation failed");
  auto* strip=__containerof(encoder,rmt_led_strip_encoder_t,base);
  const unsigned actualTicks=strip->reset_code.duration0+strip->reset_code.duration1;
  const unsigned expectedTicks=Speed::RmtDurationReset;
  const bool pass=actualTicks==expectedTicks && strip->reset_code.level0==0 && strip->reset_code.level1==0;
  std::cout<<(pass ? "PASS " : "FAIL ")<<name<<" reset: "<<actualTicks*1000000ULL/Speed::RmtTicksPerSecond
           <<"us, expected "<<expectedTicks*1000000ULL/Speed::RmtTicksPerSecond<<"us\n";
  encoder->del(encoder);
  return pass;
}
int main() {
  unsigned passed=0;
  passed+=check<NeoEsp32RmtSpeedWs2811>("WS2811");
  passed+=check<NeoEsp32RmtSpeedWs2812x>("WS2812x");
  passed+=check<NeoEsp32RmtSpeedWs2805>("WS2805");
  passed+=check<NeoEsp32RmtSpeedSk6812>("SK6812");
  passed+=check<NeoEsp32RmtSpeedTm1814>("TM1814");
  passed+=check<NeoEsp32RmtSpeedTm1829>("TM1829");
  passed+=check<NeoEsp32RmtSpeedTm1914>("TM1914");
  passed+=check<NeoEsp32RmtSpeed800Kbps>("800Kbps");
  passed+=check<NeoEsp32RmtSpeed400Kbps>("400Kbps");
  passed+=check<NeoEsp32RmtSpeedApa106>("APA106");
  passed+=check<NeoEsp32RmtSpeedTx1812>("TX1812");
  passed+=check<NeoEsp32RmtSpeedGs1903>("GS1903");
  std::cout<<passed<<"/12 encoded-reset checks passed\n";
  return passed==12 ? 0 : 1;
}

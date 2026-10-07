// Compile the production config blocks with the bundled ArduinoJson parser.
// Bus/task/GPIO dependencies are modeled; no controller is contacted.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include CONFIG_JSON_HEADER
#define F(s) s
#define CJSON(a,b) a = b | a
/* CONSTANTS */
/* FRAME_DELAY */

static size_t testStrlcpy(char* dest, const char* src, size_t size) {
  const size_t length = std::strlen(src);
  if (size) {
    const size_t count = std::min(length, size - 1);
    std::memcpy(dest, src, count);
    dest[count] = '\0';
  }
  return length;
}
#define strlcpy testStrlcpy

struct Bus {
  inline static uint8_t _gAWM=AW_GLOBAL_DISABLED;
  inline static int8_t _cctBlend=0;
  /* BUS_STATE */
};
namespace BusManager {
  uint16_t maxCurrent=0;
  uint16_t ablMilliampsMax() { return maxCurrent; }
  void setMilliampsMax(uint16_t value) { maxCurrent=value; }
}
struct WS2812FX {
  bool correctWB=false, cctFromRgb=false, autoSegments=false;
  uint8_t _targetFps=WLED_FPS;
  uint16_t _frametime=1000/WLED_FPS;
  unsigned getLengthTotal() const { return 165; }
  uint8_t getTargetFps() const { return _targetFps; }
  void setTargetFps(unsigned);
} strip;
bool cctICused=false, gammaCorrectBri=DEFAULT_BRIGHTNESS_GAMMA, gammaCorrectCol=DEFAULT_COLOR_GAMMA;
float gammaCorrectVal=DEFAULT_GAMMA;
uint8_t briMultiplier=100, paletteBlend=0;
struct NeoGammaWLEDMethod {
  inline static float tableGamma=DEFAULT_GAMMA;
  static void calcGammaTable(float value) { tableGamma=value; }
};
bool enableESPNow=false;
std::vector<std::array<char,13>> linked_remotes;
void readLedConfig(JsonObject);
void readLighting(JsonObject);
void readRemotes(JsonObject);

static void applyConfig(const char* json) {
  DynamicJsonDocument document(2048);
  if (deserializeJson(document, json)) throw std::runtime_error("Invalid test JSON");
  JsonObject root=document.as<JsonObject>();
  readLedConfig(root);
  readLighting(root);
  readRemotes(root);
}

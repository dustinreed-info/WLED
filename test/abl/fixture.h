// Host-only fixture. Production function bodies are inserted by tools/abl-test.js.
// The driver records native channel values and models NeoPixelBus's constructors,
// including RgbwColor(uint8_t), which must never receive a packed RGBW integer.
// Constructor reference: Makuna/NeoPixelBus, src/internal/colors/RgbwColor.h
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using byte = uint8_t;
#define ARDUINO_ARCH_ESP32
#define WLED_HAS_PARALLEL_I2S
#define IRAM_ATTR
#define BLACK 0
#define DEBUGBUS_PRINTF_P(...)
inline uint8_t R(uint32_t c) { return c >> 16; }
inline uint8_t G(uint32_t c) { return c >> 8; }
inline uint8_t B(uint32_t c) { return c; }
inline uint8_t W(uint32_t c) { return c >> 24; }
#define RGBW32(r,g,b,w) ((uint32_t(uint8_t(w)) << 24) | (uint32_t(uint8_t(r)) << 16) | (uint32_t(uint8_t(g)) << 8) | uint8_t(b))
#define NUM_ICS_WS2812_1CH_3X(len) (((len) + 2) / 3)
#define IC_INDEX_WS2812_1CH_3X(i) ((i) / 3)
using std::max;
/* CONSTANTS */

struct RgbwColor;
struct RgbColor {
  uint8_t R, G, B;
  RgbColor(uint8_t r=0, uint8_t g=0, uint8_t b=0): R(r), G(g), B(b) {}
  RgbColor(const RgbwColor&);
};
struct RgbwColor {
  uint8_t R, G, B, W;
  RgbwColor(uint8_t brightness=0): R(0), G(0), B(0), W(brightness) {}
  RgbwColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w=0): R(r), G(g), B(b), W(w) {}
  RgbwColor(const RgbColor& c): R(c.R), G(c.G), B(c.B), W(0) {}
};
RgbColor::RgbColor(const RgbwColor& c): R(c.R), G(c.G), B(c.B) {}
struct RgbwwColor {
  uint8_t R, G, B, WW, CW;
  RgbwwColor(uint8_t r=0, uint8_t g=0, uint8_t b=0, uint8_t ww=0, uint8_t cw=0): R(r), G(g), B(b), WW(ww), CW(cw) {}
};
struct Rgb48Color {
  uint16_t R, G, B;
  Rgb48Color(uint16_t r=0, uint16_t g=0, uint16_t b=0): R(r), G(g), B(b) {}
  Rgb48Color(const RgbwColor& c): R(c.R*257), G(c.G*257), B(c.B*257) {}
};
struct Rgbw64Color {
  uint16_t R, G, B, W;
  Rgbw64Color(uint16_t r=0, uint16_t g=0, uint16_t b=0, uint16_t w=0): R(r), G(g), B(b), W(w) {}
  Rgbw64Color(const RgbwColor& c): R(c.R*257), G(c.G*257), B(c.B*257), W(c.W*257) {}
};
struct Rgbww80Color {
  uint16_t R, G, B, WW, CW;
  Rgbww80Color(uint16_t r=0, uint16_t g=0, uint16_t b=0, uint16_t ww=0, uint16_t cw=0): R(r), G(g), B(b), WW(ww), CW(cw) {}
};
struct CRGBW {
  uint8_t r, g, b, w;
  CRGBW(uint32_t c): r(R(c)), g(G(c)), b(B(c)), w(W(c)) {}
  operator uint32_t() const { return RGBW32(r,g,b,w); }
};

struct FakePixel { uint16_t R=0, G=0, B=0, W=0, WW=0, CW=0; };
struct FakeRaw {
  std::vector<FakePixel> pixels;
  explicit FakeRaw(unsigned count): pixels(count) {}
  virtual ~FakeRaw() = default;
};
template <class Color> struct FakeNeoBus : FakeRaw {
  explicit FakeNeoBus(unsigned count): FakeRaw(count) {}
  Color GetPixelColor(unsigned i) const {
    const auto& p = pixels.at(i);
    if constexpr (std::is_same_v<Color,RgbwwColor> || std::is_same_v<Color,Rgbww80Color>) return Color(p.R,p.G,p.B,p.WW,p.CW);
    else if constexpr (std::is_same_v<Color,RgbwColor> || std::is_same_v<Color,Rgbw64Color>) return Color(p.R,p.G,p.B,p.W);
    else return Color(p.R,p.G,p.B);
  }
  void SetPixelColor(unsigned i, Color c) {
    auto& p = pixels.at(i);
    p.R=c.R; p.G=c.G; p.B=c.B;
    if constexpr (std::is_same_v<Color,RgbwwColor> || std::is_same_v<Color,Rgbww80Color>) { p.WW=c.WW; p.CW=c.CW; }
    else if constexpr (std::is_same_v<Color,RgbwColor> || std::is_same_v<Color,Rgbw64Color>) p.W=c.W;
  }
};
/* DRIVER_ALIASES */
/* POLYBUS */

class Bus {
public:
  static int16_t _cct;
  static int8_t _cctBlend;
  static uint8_t _gAWM;
  uint8_t _type=TYPE_WS2805, _autoWhiteMode=RGBW_MODE_MANUAL_ONLY;
  bool _hasCCT=true;
  virtual ~Bus() = default;
  /* CAPABILITIES */
  bool hasRGB() const { return hasRGB(_type); }
  bool hasWhite() const { return hasWhite(_type); }
  bool hasCCT() const { return hasCCT(_type); }
  unsigned getNumberOfChannels() const { return 3*hasRGB() + hasWhite() + hasCCT(); }
  bool isDigital() const { return true; }
  bool isOk() const { return true; }
  virtual uint16_t getLEDCurrent() const = 0;
  virtual uint16_t getMaxCurrent() const = 0;
  virtual CURRENT_TYPE getUsedCurrent() const = 0;
  virtual unsigned getLength() const = 0;
  static void calculateCCT(uint32_t, uint8_t&, uint8_t&);
  uint32_t autoWhiteCalc(uint32_t, uint8_t&, uint8_t&) const;
};
int16_t Bus::_cct=-1;
int8_t Bus::_cctBlend=0;
uint8_t Bus::_gAWM=255;
namespace BusManager {
  bool _useABL=true;
  uint16_t _gMilliAmpsMax=0;
  GLOBAL_CURRENT_TYPE _gMilliAmpsUsed=0;
  std::vector<std::unique_ptr<Bus>> busses;
  void initializeABL();
  void applyABL();
}
uint16_t approximateKelvinFromRGB(uint32_t);
uint32_t color_fade(uint32_t, uint8_t, bool);
uint32_t colorBalanceFromKelvin(int, uint32_t c) { return c; }
struct FakeColorMap { uint8_t getPixelColorOrder(unsigned, uint8_t co) { return co; } } _colorOrderMap;

class BusDigital : public Bus {
public:
  bool _valid=true, _reversed=false;
  uint8_t _NPBbri=255, _bri=255, _milliAmpsPerLed=55, _colorOrder=0, _iType;
  unsigned _len, _skip, _start=0;
  uint16_t _milliAmpsMax=2000, _milliAmpsLimit=0;
  uint32_t _colorSum=0;
  /* CURRENT_MEMBER */
  std::unique_ptr<FakeRaw> raw;
  void* _busPtr;
  BusDigital(unsigned count, unsigned skip=0, uint8_t type=TYPE_WS2805): _len(count), _skip(skip) {
    _type=type; _hasCCT=hasCCT(); _milliAmpsTotal=0;
    unsigned hwCount=(_type==TYPE_WS2812_1CH_X3 ? NUM_ICS_WS2812_1CH_3X(count) : count) + skip;
    if (type==TYPE_WS2805 || type==TYPE_FW1906) {
      _iType=type==TYPE_WS2805 ? I_32_RN_2805_5 : I_32_RN_FW6_5;
      raw=std::make_unique<FakeNeoBus<RgbwwColor>>(hwCount);
    } else if (type==TYPE_SM16825) {
      _iType=I_32_RN_SM16825_5; raw=std::make_unique<FakeNeoBus<Rgbww80Color>>(hwCount);
    } else if (type==TYPE_UCS8903) {
      _iType=I_32_RN_UCS_3; raw=std::make_unique<FakeNeoBus<Rgb48Color>>(hwCount);
    } else if (type==TYPE_UCS8904) {
      _iType=I_32_RN_UCS_4; raw=std::make_unique<FakeNeoBus<Rgbw64Color>>(hwCount);
    } else if (type==TYPE_SK6812_RGBW) {
      _iType=I_32_RN_NEO_4; raw=std::make_unique<FakeNeoBus<RgbwColor>>(hwCount);
    } else {
      _iType=I_32_RN_NEO_3; raw=std::make_unique<FakeNeoBus<RgbColor>>(hwCount);
    }
    _busPtr=raw.get();
  }
  unsigned getLength() const override { return _len; }
  uint16_t getLEDCurrent() const override { return _milliAmpsPerLed; }
  uint16_t getMaxCurrent() const override { return _milliAmpsMax; }
  CURRENT_TYPE getUsedCurrent() const override { return _milliAmpsTotal; }
  void setCurrentLimit(uint16_t ma) { _milliAmpsLimit=ma; }
  void estimateCurrent();
  void applyBriLimit(uint8_t);
  void setPixelColor(unsigned, uint32_t);
};

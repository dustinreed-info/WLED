// Host fixtures for the actual realtime ingress functions, without an ESP SDK.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <deque>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <arpa/inet.h>
using byte=uint8_t;
#define F(s) s
#define PSTR(s) s
#define BLACK 0
#define DEBUG_PRINTLN(...)
#define DEBUG_PRINTF_P(...)
#define VERSION 2607201
#define JSON_LOCK_SERIAL 1
#define ERR_NOBUF 3
#define RGBW32(r,g,b,w) ((uint32_t(w)<<24)|(uint32_t(r)<<16)|(uint32_t(g)<<8)|uint32_t(b))
inline uint8_t R(uint32_t c) { return c >> 16; }
inline uint8_t G(uint32_t c) { return c >> 8; }
inline uint8_t B(uint32_t c) { return c; }
inline uint8_t W(uint32_t c) { return c >> 24; }
/* CONSTANTS */
uint32_t fakeTime=100;
uint32_t millis() { return fakeTime; }
void yield() {}
bool serialCanRX=true, serialCanTX=true, useMainSegmentOnly=false, realtimeRespectLedMaps=true;
bool e131SkipOutOfSequence=true, arlsForceMaxBri=false, e131NewData=false;
byte realtimeOverride=0, realtimeMode=0, bri=77, briT=77, briLast=77;
uint32_t realtimeTimeout=0, realtimeTimeoutMs=2500;
std::array<byte,4> realtimeIP{};
void updateInterfaces(byte) {}
int arlsOffset=0;
uint16_t DMXAddress=1;
byte e131LastSequenceNumber[16]={};
byte errorFlag=0;
bool failSerialAllocation=false;
unsigned serialAllocations=0, serialFrees=0;
size_t lastSerialAllocationSize=0;
void* allocate_buffer(size_t size, uint32_t) {
  serialAllocations++; lastSerialAllocationSize=size;
  return failSerialAllocation ? nullptr : std::malloc(size);
}
void p_free(void* p) { if (p) { serialFrees++; std::free(p); } }

struct Segment {
  size_t count=8;
  bool freeze=false;
  bool storageAvailable=true;
  mutable std::vector<uint32_t> colors=std::vector<uint32_t>(8,0);
  unsigned length() const { return count; }
  bool isActive() const { return count>0 && storageAvailable; }
  void clear() { std::fill(colors.begin(),colors.end(),0); }
  void setPixelColorRaw(unsigned n, uint32_t c) const { colors.at(n)=c; }
  uint32_t getPixelColorRaw(unsigned n) const { return n<colors.size() ? colors[n] : 0; }
};
struct WS2812FX {
  std::vector<uint32_t> colors=std::vector<uint32_t>(8,0), shown=colors;
  uint32_t* _pixels=colors.data();
  uint16_t customMappingSize=0;
  uint16_t* customMappingTable=nullptr;
  Segment main;
  byte mainSegmentId=0;
  unsigned segmentCount=1;
  unsigned shows=0;
  unsigned getLengthTotal() const { return colors.size(); }
  Segment& getMainSegment() { return main; }
  const Segment& getMainSegment() const { return main; }
  byte getMainSegmentId() const { return mainSegmentId; }
  unsigned getSegmentsNum() const { return segmentCount; }
  Segment& getSegment(unsigned) { return main; }
  void fill(uint32_t c) const;
  void resizePixels(size_t count) { colors.resize(count); shown.resize(count); _pixels=colors.data(); }
  void setPixelColor(unsigned n, uint32_t c) const;
  uint16_t getMappedPixelIndex(uint16_t index) const;
  void setBrightness(uint8_t, bool) {}
  void trigger() {}
  void setRealtimePixelColor(unsigned i, uint32_t c);
  void show() { shown=useMainSegmentOnly ? main.colors : colors; shows++; }
  uint32_t getLastShow() const { return shows; }
  uint32_t getPixelColor(unsigned n) const;
  uint32_t getPixelColorNoMap(unsigned n) const;
  uint32_t getRealtimePixelColor(unsigned n) const;
} strip;
using Strip=WS2812FX;

struct e131_packet_t {
  uint8_t flags=DDP_FLAGS_VER1, sequenceNum=0, dataType=DDP_TYPE_RGB24, destination=DDP_ID_DISPLAY;
  uint32_t channelOffset=0;
  uint16_t dataLen=0;
  uint8_t data[1500]={};
};

struct FakeSerial {
  std::deque<byte> input;
  std::vector<byte> output;
  bool connected=true;
  unsigned printCalls=0, printfCalls=0;
  explicit operator bool() const { return connected; }
  unsigned available() const { return input.size(); }
  byte peek() const { return input.front(); }
  byte read() { byte b=input.front(); input.pop_front(); return b; }
  void write(byte b) { output.push_back(b); }
  template<class T> void print(const T&) { printCalls++; }
  template<class T> void println(const T&) { printCalls++; }
  void println() { printCalls++; }
  template<class... T> void printf_P(const char*, T...) { printfCalls++; }
  void setTimeout(unsigned) {}
} Serial;

struct JsonObject {};
struct Document {
  void clear() {}
  template<class T> T as() { return {}; }
  JsonObject createNestedObject(const char*) { return {}; }
} doc;
Document* pDoc=&doc;
struct DeserializationError { explicit operator bool() const { return true; } };
bool jsonLockAvailable=true;
bool requestJSONBufferLock(byte) { return jsonLockAvailable; }
void releaseJSONBufferLock() {}
bool deserializeState(JsonObject) { return false; }
DeserializationError deserializeJson(Document&, FakeSerial&) { return {}; }
void serializeState(JsonObject) {}
void serializeInfo(JsonObject) {}
void serializeJson(Document&, FakeSerial&) {}
void handleImprovPacket() {}
void updateBaudRate(uint32_t) {}
static void sendJSON() {}
static void sendBytes();

inline byte highByte(unsigned value) { return value >> 8; }
inline byte lowByte(unsigned value) { return value; }
inline byte qadd8(byte a, byte b) { return std::min(unsigned(a)+b,255u); }

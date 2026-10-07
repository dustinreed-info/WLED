// Host fixtures for the actual realtime ingress functions, without an ESP SDK.
#include <algorithm>
#include <array>
#include <cstdint>
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
/* CONSTANTS */
uint32_t fakeTime=100;
uint32_t millis() { return fakeTime; }
void yield() {}
bool serialCanRX=true, serialCanTX=true, useMainSegmentOnly=false;
bool e131SkipOutOfSequence=true, arlsForceMaxBri=false, e131NewData=false;
byte realtimeOverride=0, realtimeMode=0, bri=77, briT=77, briLast=77;
uint32_t realtimeTimeout=0, realtimeTimeoutMs=2500;
int arlsOffset=0;
uint16_t DMXAddress=1;
byte e131LastSequenceNumber[16]={};

struct Segment {
  size_t count=8;
  bool freeze=false;
  std::vector<uint32_t> colors=std::vector<uint32_t>(8,0);
  unsigned length() const { return count; }
  void clear() { std::fill(colors.begin(),colors.end(),0); }
};
struct Strip {
  std::vector<uint32_t> colors=std::vector<uint32_t>(8,0), shown=colors;
  Segment main;
  unsigned shows=0;
  unsigned getLengthTotal() const { return colors.size(); }
  Segment& getMainSegment() { return main; }
  unsigned getSegmentsNum() const { return 1; }
  Segment& getSegment(unsigned) { return main; }
  void fill(uint32_t c) { std::fill(colors.begin(),colors.end(),c); }
  void setBrightness(uint8_t, bool) {}
  void setRealtimePixelColor(unsigned i, uint32_t c) {
    auto& target=useMainSegmentOnly ? main.colors : colors;
    if (i<target.size()) target[i]=c;
  }
  void show() { shown=useMainSegmentOnly ? main.colors : colors; shows++; }
  uint32_t getLastShow() const { return shows; }
} strip;

struct e131_packet_t {
  uint8_t flags=DDP_FLAGS_VER1, sequenceNum=0, dataType=DDP_TYPE_RGB24, destination=DDP_ID_DISPLAY;
  uint32_t channelOffset=0;
  uint16_t dataLen=0;
  uint8_t data[1500]={};
};

struct FakeSerial {
  std::deque<byte> input;
  explicit operator bool() const { return true; }
  unsigned available() const { return input.size(); }
  byte peek() const { return input.front(); }
  byte read() { byte b=input.front(); input.pop_front(); return b; }
  void write(byte) {}
  template<class T> void print(const T&) {}
  template<class T> void println(const T&) {}
  void println() {}
  template<class... T> void printf_P(const char*, T...) {}
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
bool requestJSONBufferLock(byte) { return true; }
void releaseJSONBufferLock() {}
bool deserializeState(JsonObject) { return false; }
DeserializationError deserializeJson(Document&, FakeSerial&) { return {}; }
void serializeState(JsonObject) {}
void serializeInfo(JsonObject) {}
void serializeJson(Document&, FakeSerial&) {}
void handleImprovPacket() {}
void updateBaudRate(uint32_t) {}
static void sendJSON() {}
static void sendBytes() {}

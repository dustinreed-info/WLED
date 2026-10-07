#include "wled.h"

// forward declarations
static void sendBytes();
static constexpr byte TPM2_FRAME_END = 0x36;
static constexpr uint16_t TPM2_MAX_LEDS = UINT16_MAX / 3;

/*
 * Adalight and TPM2 handler
 */

enum class AdaState {
  Header_A,
  Header_d,
  Header_a,
  Header_CountHi,
  Header_CountLo,
  Header_CountCheck,
  Data_Red,
  Data_Green,
  Data_Blue,
  TPM2_Header_Type,
  TPM2_Header_CountHi,
  TPM2_Header_CountLo,
  TPM2_Footer,
};

// Recognize a fresh frame prefix after an invalid or overlapping header.
static AdaState getSerialHeaderState(byte next) {
  if (next == 'A') return AdaState::Header_d;
  if (next == 0xC9) return AdaState::TPM2_Header_Type;
  return AdaState::Header_A;
}

// Cache a candidate RGB frame separately from the live buffers used by rendering.
// Only its in-bounds, offset-clipped span consumes RAM; all serial bytes are consumed.
class SerialFrameBuffer {
  public:
    ~SerialFrameBuffer() { clear(); }

    // Capture stream geometry once so configuration changes cannot move a partial frame.
    void begin(uint32_t pixelCount) {
      _valid = false;
      _mainOnly = useMainSegmentOnly;
      _mainSegmentId = strip.getMainSegmentId();
      _offset = arlsOffset;
      if (_mainOnly && _mainSegmentId >= strip.getSegmentsNum()) {
        errorFlag = ERR_NORAM_PX;
        return;
      }
      if (_mainOnly && !strip.getMainSegment().isActive()) return;
      _length = _mainOnly ? strip.getMainSegment().length() : strip.getLengthTotal();
      int64_t first = std::max(int64_t(0), int64_t(_offset));
      int64_t end = std::min(int64_t(_length), int64_t(pixelCount) + _offset);
      _start = unsigned(std::min(first, int64_t(_length)));
      _span = end > first ? unsigned(end - first) : 0;
      if (_span > _capacity) {
        p_free(_pixels);
        _pixels = nullptr;
        _capacity = 0;
        _pixels = static_cast<uint32_t*>(allocate_buffer(size_t(_span) * sizeof(uint32_t), BFRALLOC_PREFER_PSRAM | BFRALLOC_NOBYTEACCESS));
        if (!_pixels) {
          errorFlag = ERR_NORAM_PX;
          return;
        }
        _capacity = _span;
      }
      _valid = true;
    }

    // RGB serial data has no white byte, leaving the top bit available for an override marker.
    void setPixel(uint32_t streamPixel, uint32_t color) {
      if (!_valid) return;
      int64_t pixel = int64_t(streamPixel) + _offset - _start;
      if (pixel < 0 || uint64_t(pixel) >= _span) return;
      _pixels[unsigned(pixel)] = realtimeOverride ? SKIPPED_PIXEL : color;
    }

    // Initialize takeover before applying a validated frame, never after writing its pixels.
    void commit() {
      if (!_valid) return;
      _valid = false;
      if (_mainOnly != useMainSegmentOnly || _offset != arlsOffset) return;
      if (_mainOnly && (_mainSegmentId != strip.getMainSegmentId() || _mainSegmentId >= strip.getSegmentsNum())) return;
      if (_mainOnly && !strip.getMainSegment().isActive()) return;
      unsigned length = _mainOnly ? strip.getMainSegment().length() : strip.getLengthTotal();
      if (_length != length) return;
      if (!realtimeOverride) {
        // Save overridden pixels before takeover clears the old scene. These are full RGBW
        // values, so do not interpret their top bit as a marker again after this pass.
        for (unsigned i = 0; i < _span; i++) {
          if (_pixels[i] == SKIPPED_PIXEL) _pixels[i] = strip.getRealtimePixelColor(_start + i);
        }
      }
      realtimeLock(realtimeTimeoutMs, REALTIME_MODE_ADALIGHT);
      if (realtimeOverride) return;
      for (unsigned i = 0; i < _span; i++) strip.setRealtimePixelColor(_start + i, _pixels[i]);
      strip.show();
    }

    // Keep an existing serial stream alive while a long candidate frame arrives.
    void refreshTimeout() const {
      if (_valid && realtimeMode == REALTIME_MODE_ADALIGHT) realtimeLock(realtimeTimeoutMs, REALTIME_MODE_ADALIGHT);
    }

    // Release cached storage on inactivity/disconnection; the displayed pixels are independent.
    void clear() {
      p_free(_pixels);
      _pixels = nullptr;
      _capacity = 0;
      _valid = false;
    }

  private:
    static constexpr uint32_t SKIPPED_PIXEL = 0x80000000;
    uint32_t *_pixels = nullptr;
    unsigned _capacity = 0, _span = 0, _start = 0, _length = 0;
    int _offset = 0;
    byte _mainSegmentId = 0;
    bool _mainOnly = false, _valid = false;
};
static SerialFrameBuffer serialFrame;

static uint16_t currentBaud = 1152; //default baudrate 115200 (divided by 100)
static bool continuousSendLED = false;
static uint32_t lastUpdate = 0;

void updateBaudRate(uint32_t rate){
  unsigned rate100 = rate/100;
  if (rate100 == currentBaud || rate100 < 96) return;
  currentBaud = rate100;

  if (serialCanTX){
    Serial.print(F("Baud is now ")); Serial.println(rate);
  }

  Serial.flush();
  Serial.begin(rate);
}

// RGB LED data return as JSON array. Slow, but easy to use on the other end.
static inline void sendJSON(){
  if (serialCanTX) {
    unsigned used = strip.getLengthTotal();
    Serial.write('[');
    for (unsigned i=0; i<used; i++) {
      Serial.print(strip.getPixelColor(i));
      if (i != used-1) Serial.write(',');
    }
    Serial.println("]");
  }
}

// RGB LED data returned as bytes in TPM2 format. Faster, and slightly less easy to use on the other end.
static void sendBytes(){
  if (serialCanTX) {
    Serial.write(0xC9); Serial.write(0xDA);
    // A TPM2 packet has a 16-bit byte length. JSON queries can return the full canvas.
    unsigned used = std::min<unsigned>(strip.getLengthTotal(), TPM2_MAX_LEDS);
    unsigned len = used*3;
    Serial.write(highByte(len));
    Serial.write(lowByte(len));
    for (unsigned i=0; i < used; i++) {
      uint32_t c = strip.getPixelColor(i);
      Serial.write(qadd8(W(c), R(c))); //R, add white channel to RGB channels as a simple RGBW -> RGB map
      Serial.write(qadd8(W(c), G(c))); //G
      Serial.write(qadd8(W(c), B(c))); //B
    }
    Serial.write(TPM2_FRAME_END); Serial.write('\n');
  }
}

void handleSerial()
{
  static auto state = AdaState::Header_A;
  static uint32_t count = 0; // Adalight encodes count-1, so 0xFFFF represents 65536 pixels
  static uint32_t pixel = 0;
  static byte check = 0x00;
  static byte red   = 0x00;
  static byte green = 0x00;
  static uint32_t lastByteTime = 0;
  static bool tpm2Frame = false;
  constexpr uint32_t SERIAL_FRAME_IDLE_TIMEOUT_MS = 1000;

  if (!(serialCanRX && Serial)) { // USB CDC can disconnect; non-USB ports always evaluate true
    serialFrame.clear();
    state = AdaState::Header_A;
    count = 0;
    pixel = 0;
    return;
  }

  // An interrupted frame must not consume the next connection's header as RGB.
  // Use inactivity rather than total duration so large slow frames remain valid.
  if (millis() - lastByteTime > SERIAL_FRAME_IDLE_TIMEOUT_MS) {
    serialFrame.clear();
    state = AdaState::Header_A;
    count = 0;
    pixel = 0;
  }

  while (Serial.available() > 0)
  {
    yield();
    byte next = Serial.peek();
    lastByteTime = millis();
    switch (state) {
      case AdaState::Header_A:
        if      (next == 'A')  { state = AdaState::Header_d; }
        else if (next == 0xC9) { state = AdaState::TPM2_Header_Type; } //TPM2 start byte
        else if (next == 'I')  { handleImprovPacket(); return; }
        else if (next == 'v')  { if (serialCanTX) { Serial.print("WLED"); Serial.write(' '); Serial.println(VERSION); } }
        else if (next == 0xB0) { updateBaudRate( 115200); }
        else if (next == 0xB1) { updateBaudRate( 230400); }
        else if (next == 0xB2) { updateBaudRate( 460800); }
        else if (next == 0xB3) { updateBaudRate( 500000); }
        else if (next == 0xB4) { updateBaudRate( 576000); }
        else if (next == 0xB5) { updateBaudRate( 921600); }
        else if (next == 0xB6) { updateBaudRate(1000000); }
        else if (next == 0xB7) { updateBaudRate(1500000); }
        else if (next == 'l')  { sendJSON(); } // Send LED data as JSON Array
        else if (next == 'L')  { sendBytes(); } // Send LED data as TPM2 Data Packet
        else if (next == 'o')  { continuousSendLED = false; } // Disable Continuous Serial Streaming
        else if (next == 'O')  { continuousSendLED = true; } // Enable Continuous Serial Streaming
        else if (next == '{')  { //JSON API
          bool verboseResponse = false;
          if (!requestJSONBufferLock(JSON_LOCK_SERIAL)) {
            if (serialCanTX) Serial.printf_P(PSTR("{\"error\":%d}\n"), ERR_NOBUF);
            return;
          }
          Serial.setTimeout(100);
          DeserializationError error = deserializeJson(*pDoc, Serial);
          if (!error) {
            verboseResponse = deserializeState(pDoc->as<JsonObject>());
            //only send response if TX pin is unused for other purposes
            if (verboseResponse && serialCanTX) {
              pDoc->clear();
              JsonObject stateDoc = pDoc->createNestedObject("state");
              serializeState(stateDoc);
              JsonObject info  = pDoc->createNestedObject("info");
              serializeInfo(info);

              serializeJson(*pDoc, Serial);
              Serial.println();
            }
          }
          releaseJSONBufferLock();
        }
        break;
      case AdaState::Header_d:
        if (next == 'd') state = AdaState::Header_a;
        else             state = getSerialHeaderState(next);
        break;
      case AdaState::Header_a:
        if (next == 'a') state = AdaState::Header_CountHi;
        else             state = getSerialHeaderState(next);
        break;
      case AdaState::Header_CountHi:
        pixel = 0;
        count = next * 0x100;
        check = next;
        state = AdaState::Header_CountLo;
        break;
      case AdaState::Header_CountLo:
        count += next + 1;
        check = check ^ next ^ 0x55;
        state = AdaState::Header_CountCheck;
        break;
      case AdaState::Header_CountCheck:
        if (check == next) {
          serialFrame.begin(count);
          tpm2Frame = false;
          state = AdaState::Data_Red;
        }
        else               state = getSerialHeaderState(next);
        break;
      case AdaState::TPM2_Header_Type:
        state = getSerialHeaderState(next); // recover a fresh prefix after an unsupported type
        if (next == 0xDA) state = AdaState::TPM2_Header_CountHi; //TPM2 data
        else if (next == 0xAA && serialCanTX) Serial.write(0xAC); // TPM2 ping
        break;
      case AdaState::TPM2_Header_CountHi:
        pixel = 0;
        count = next * 0x100;
        state = AdaState::TPM2_Header_CountLo;
        break;
      case AdaState::TPM2_Header_CountLo:
        count += next;
        if (count == 0 || count % 3 != 0) {
          state = AdaState::Header_A; // only complete 24-bit RGB pixels are supported
        } else {
          count /= 3;
          serialFrame.begin(count);
          tpm2Frame = true;
          state = AdaState::Data_Red;
        }
        break;
      case AdaState::Data_Red:
        red   = next;
        state = AdaState::Data_Green;
        break;
      case AdaState::Data_Green:
        green = next;
        state = AdaState::Data_Blue;
        break;
      case AdaState::Data_Blue: {
        byte blue  = next;
        serialFrame.setPixel(pixel, RGBW32(red, green, blue, 0));
        pixel++; // consume positions even while realtime output is overridden
        if (--count > 0) state = AdaState::Data_Red;
        else if (tpm2Frame) state = AdaState::TPM2_Footer;
        else {
          serialFrame.commit();
          state = AdaState::Header_A;
        }
        break;
      }
      case AdaState::TPM2_Footer:
        if (next == TPM2_FRAME_END) serialFrame.commit();
        else serialFrame.clear();
        state = getSerialHeaderState(next);
        break;
    }

    // All other received bytes will disable Continuous Serial Streaming
    if (continuousSendLED && next != 'O'){
      continuousSendLED = false;
    }

    Serial.read(); //discard the byte
  }

  serialFrame.refreshTimeout();

  // If Continuous Serial Streaming is enabled, send new LED data as bytes
  if (continuousSendLED && (lastUpdate != strip.getLastShow())){
    sendBytes();
    lastUpdate = strip.getLastShow();
  }
}

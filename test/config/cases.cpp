#define CHECK(c) do { if (!(c)) throw std::runtime_error(#c); } while (0)

static void resetState() {
  Bus::_gAWM=AW_GLOBAL_DISABLED; Bus::_cctBlend=0; BusManager::maxCurrent=0;
  strip=WS2812FX{}; cctICused=false;
  gammaCorrectVal=DEFAULT_GAMMA;
  gammaCorrectCol=DEFAULT_COLOR_GAMMA; gammaCorrectBri=DEFAULT_BRIGHTNESS_GAMMA;
  briMultiplier=100; paletteBlend=0; enableESPNow=false; linked_remotes.clear();
}
static void pair(const char* mac) {
  std::array<char,13> entry{};
  testStrlcpy(entry.data(),mac,entry.size());
  linked_remotes.push_back(entry);
}

int main() {
  std::vector<std::pair<const char*,std::function<void()>>> cases = {
    {"unrelated config writes retain white mode and frame rate", [] {
      for (uint8_t mode : {0,1,2,3,4,255}) for (unsigned fps : {0,1,42,120,250}) {
        Bus::setGlobalAWMode(mode); strip.setTargetFps(fps);
        applyConfig("{\"hw\":{\"led\":{\"maxpwr\":5000}}}");
        CHECK(Bus::getGlobalAWMode()==mode && strip.getTargetFps()==fps);
        CHECK(BusManager::maxCurrent==5000);
      }
    }},
    {"explicit white mode and frame rate updates still apply", [] {
      Bus::setGlobalAWMode(2); strip.setTargetFps(0);
      applyConfig("{\"hw\":{\"led\":{\"rgbwm\":3,\"fps\":60}}}");
      CHECK(Bus::getGlobalAWMode()==3 && strip.getTargetFps()==60);
      applyConfig("{\"hw\":{\"led\":{\"rgbwm\":255,\"fps\":0}}}");
      CHECK(Bus::getGlobalAWMode()==255 && strip.getTargetFps()==0);
    }},
    {"an omitted frame rate preserves unlimited mode independently", [] {
      Bus::setGlobalAWMode(AW_GLOBAL_DISABLED); strip.setTargetFps(0);
      applyConfig("{\"hw\":{\"led\":{\"maxpwr\":5000}}}");
      CHECK(strip.getTargetFps()==0);
    }},
    {"null LED settings retain current values", [] {
      Bus::setGlobalAWMode(2); strip.setTargetFps(70);
      applyConfig("{\"hw\":{\"led\":{\"rgbwm\":null,\"fps\":null}}}");
      CHECK(Bus::getGlobalAWMode()==2 && strip.getTargetFps()==70);
    }},
    {"unrelated config writes retain paired remotes", [] {
      pair("001122334455"); pair("AABBCCDDEEFF"); enableESPNow=true;
      applyConfig("{\"hw\":{\"led\":{\"maxpwr\":5000}}}");
      CHECK(linked_remotes.size()==2 && std::string(linked_remotes[0].data())=="001122334455");
      CHECK(enableESPNow);
    }},
    {"null remote field does not clear pairing", [] {
      pair("001122334455");
      applyConfig("{\"nw\":{\"linked_remote\":null}}");
      CHECK(linked_remotes.size()==1);
    }},
    {"explicit empty array clears paired remotes", [] {
      pair("001122334455"); applyConfig("{\"nw\":{\"linked_remote\":[]}}");
      CHECK(linked_remotes.empty());
    }},
    {"remote array replaces the existing list", [] {
      pair("001122334455");
      applyConfig("{\"nw\":{\"linked_remote\":[\"112233445566\",\"AABBCCDDEEFF\"]}}");
      CHECK(linked_remotes.size()==2 && std::string(linked_remotes[0].data())=="112233445566");
      CHECK(std::string(linked_remotes[1].data())=="AABBCCDDEEFF");
    }},
    {"legacy single remote address remains supported", [] {
      pair("001122334455"); applyConfig("{\"nw\":{\"linked_remote\":\"112233445566\"}}");
      CHECK(linked_remotes.size()==1 && std::string(linked_remotes[0].data())=="112233445566");
    }},
    {"omitted gamma keys retain both flags even with exponent one", [] {
      for (bool brightness : {false,true}) for (bool color : {false,true}) for (float exponent : {1.0f,2.2f}) {
        gammaCorrectBri=brightness; gammaCorrectCol=color; gammaCorrectVal=exponent;
        applyConfig("{\"id\":{\"name\":\"example\"}}");
        CHECK(gammaCorrectBri==brightness && gammaCorrectCol==color);
        CHECK(gammaCorrectVal==exponent && NeoGammaWLEDMethod::tableGamma==exponent);
      }
    }},
    {"changing the exponent preserves omitted enable flags", [] {
      gammaCorrectBri=false; gammaCorrectCol=false;
      applyConfig("{\"light\":{\"gc\":{\"val\":2.8}}}");
      CHECK(!gammaCorrectBri && !gammaCorrectCol && gammaCorrectVal==2.8f);
    }},
    {"explicit gamma switches still update independently", [] {
      gammaCorrectBri=false; gammaCorrectCol=true;
      applyConfig("{\"light\":{\"gc\":{\"bri\":2.2,\"col\":1.0}}}");
      CHECK(gammaCorrectBri && !gammaCorrectCol);
      applyConfig("{\"light\":{\"gc\":{\"bri\":1.0,\"col\":2.2}}}");
      CHECK(!gammaCorrectBri && gammaCorrectCol);
    }},
    {"null gamma flags retain their previous state", [] {
      gammaCorrectBri=true; gammaCorrectCol=false;
      applyConfig("{\"light\":{\"gc\":{\"bri\":null,\"col\":null}}}");
      CHECK(gammaCorrectBri && !gammaCorrectCol);
    }},
    {"invalid gamma exponent still disables correction", [] {
      gammaCorrectBri=true; gammaCorrectCol=true;
      applyConfig("{\"light\":{\"gc\":{\"val\":4.0}}}");
      CHECK(!gammaCorrectBri && !gammaCorrectCol && gammaCorrectVal==1.0f);
      CHECK(NeoGammaWLEDMethod::tableGamma==1.0f);
    }},
    {"an empty boot configuration retains compiled defaults", [] {
      applyConfig("{}");
      CHECK(Bus::getGlobalAWMode()==AW_GLOBAL_DISABLED && strip.getTargetFps()==WLED_FPS);
      CHECK(gammaCorrectCol==DEFAULT_COLOR_GAMMA && gammaCorrectBri==DEFAULT_BRIGHTNESS_GAMMA);
      CHECK(gammaCorrectVal==DEFAULT_GAMMA);
    }}
  };
  unsigned failures=0;
  for (const auto& test : cases) {
    resetState();
    try { test.second(); std::cout << "PASS " << test.first << '\n'; }
    catch (const std::exception& error) { failures++; std::cout << "FAIL " << test.first << ": " << error.what() << '\n'; }
  }
  std::cout << cases.size()-failures << '/' << cases.size() << " configuration checks passed\n";
  return failures ? 1 : 0;
}

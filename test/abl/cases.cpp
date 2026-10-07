// Behavioral checks for source extracted from the pixel driver and ABL pipeline.
// Failures are reported individually so an upstream snapshot can demonstrate regressions.
#define CHECK(c) do { if (!(c)) throw std::runtime_error(#c); } while (0)

static void resetState() {
  Bus::_cct=-1; Bus::_cctBlend=0; Bus::_gAWM=255;
  BusManager::busses.clear(); BusManager::_useABL=true;
  BusManager::_gMilliAmpsMax=0; BusManager::_gMilliAmpsUsed=0;
  PolyBus::_useParallelI2S=false;
}

static void checkColorReadback(uint8_t type) {
  BusDigital bus(1,0,type);
  Bus::_cct=0;
  uint32_t color=RGBW32(15,76,28,200);
  bus.setPixelColor(0,color);
  uint32_t got=PolyBus::getPixelColor(bus._busPtr,bus._iType,0,0);
  CHECK(R(got)==15 && G(got)==76 && B(got)==28);
  CHECK(W(got)==(bus.hasWhite() ? 200 : 0));
}

static void checkCCTLimit(uint8_t type, bool accurate, bool swapWhite) {
  BusDigital bus(2,0,type);
  bus._autoWhiteMode=accurate ? RGBW_MODE_AUTO_ACCURATE : RGBW_MODE_MANUAL_ONLY;
  bus._colorOrder=swapWhite ? 0x40 : 0;
  const uint32_t color=accurate ? RGBW32(255,214,177,0) : RGBW32(0,0,0,255);
  Bus::_cct=0; bus.setPixelColor(0,color);
  Bus::_cct=255; bus.setPixelColor(1,color);
  const auto before0=bus.raw->pixels[0], before1=bus.raw->pixels[1];
  Bus::_cct=-1;
  bus.applyBriLimit(128);
  const auto after0=bus.raw->pixels[0], after1=bus.raw->pixels[1];
  CHECK((after0.WW==0)==(before0.WW==0));
  CHECK((after0.CW==0)==(before0.CW==0));
  CHECK((after1.WW==0)==(before1.WW==0));
  CHECK((after1.CW==0)==(before1.CW==0));
  CHECK(after0.WW+after0.CW>0 && after0.WW+after0.CW<before0.WW+before0.CW);
  CHECK(after1.WW+after1.CW>0 && after1.WW+after1.CW<before1.WW+before1.CW);
}

int main() {
  std::vector<std::pair<std::string,std::function<void()>>> checks;
  for (auto type : {TYPE_WS2805,TYPE_FW1906,TYPE_SM16825,TYPE_UCS8903,TYPE_UCS8904})
    checks.push_back({"readback type "+std::to_string(type), [type] { checkColorReadback(type); }});
  for (auto type : {TYPE_WS2805,TYPE_FW1906,TYPE_SM16825}) {
    checks.push_back({"white temperature under ABL type "+std::to_string(type), [type] { checkCCTLimit(type,false,false); }});
    checks.push_back({"auto-white and swapped whites under ABL type "+std::to_string(type), [type] { checkCCTLimit(type,true,true); }});
  }
  checks.push_back({"I2S single and parallel WS2805 readback", [] {
    for (bool parallel : {false,true}) {
      BusDigital bus(1); bus._iType=I_32_I2_2805_5; PolyBus::_useParallelI2S=parallel;
      Bus::_cct=0; bus.setPixelColor(0,RGBW32(17,38,59,255));
      CHECK(PolyBus::getPixelColor(bus._busPtr,bus._iType,0,0)==RGBW32(17,38,59,255));
    }
  }});
  checks.push_back({"color ordering remains intact during limiting", [] {
    for (uint8_t order=0;order<6;order++) {
      BusDigital bus(1); bus._colorOrder=order|0x40;
      Bus::_cct=0; bus.setPixelColor(0,RGBW32(200,100,20,255));
      bus.applyBriLimit(128);
      uint32_t got=PolyBus::getPixelColor(bus._busPtr,bus._iType,0,bus._colorOrder);
      uint32_t expected=color_fade(RGBW32(200,100,20,255),128,true);
      CHECK(R(got)==R(expected) && G(got)==G(expected) && B(got)==B(expected));
      CHECK(bus.raw->pixels[0].WW==0 && bus.raw->pixels[0].CW==128);
    }
  }});
  checks.push_back({"skipped pixels do not leave a bright tail", [] {
    for (uint8_t type : {TYPE_WS2812_RGB,TYPE_SK6812_RGBW,TYPE_WS2805}) {
      for (unsigned skip : {1u,3u}) {
        BusDigital bus(4,skip,type); bus._reversed=true;
        Bus::_cct=0;
        for (unsigned i=0;i<4;i++) bus.setPixelColor(i,RGBW32(255,0,0,0));
        bus.applyBriLimit(128);
        for (unsigned i=0;i<skip;i++) CHECK(bus.raw->pixels[i].R==0);
        for (unsigned i=skip;i<skip+4;i++) CHECK(bus.raw->pixels[i].R==127);
      }
    }
  }});
  checks.push_back({"white-only IC grouping and skipped pixels", [] {
    BusDigital bus(7,2,TYPE_WS2812_1CH_X3);
    for (unsigned i=0;i<7;i++) bus.setPixelColor(i,RGBW32(0,0,0,255));
    bus.estimateCurrent(); CHECK(bus.getUsedCurrent()==7*55+7);
    bus.applyBriLimit(128);
    for (const auto& p : bus.raw->pixels) CHECK(p.R<255 && p.G<255 && p.B<255);
  }});
  checks.push_back({"CCT blending changes estimated output current", [] {
    BusDigital bus(100); Bus::_cct=127;
    for (unsigned i=0;i<100;i++) bus.setPixelColor(i,RGBW32(0,0,0,255));
    bus.estimateCurrent(); unsigned before=bus.getUsedCurrent();
    bus.applyBriLimit(255); Bus::_cctBlend=127;
    for (unsigned i=0;i<100;i++) bus.setPixelColor(i,RGBW32(0,0,0,255));
    bus.estimateCurrent();
    CHECK(bus.getUsedCurrent()>before*18/10);
  }});
  checks.push_back({"RGB-only full-scale current remains unchanged", [] {
    BusDigital bus(100,0,TYPE_WS2812_RGB);
    for (unsigned i=0;i<100;i++) bus.setPixelColor(i,RGBW32(255,255,255,0));
    bus.estimateCurrent(); CHECK(bus.getUsedCurrent()==5600);
  }});
  checks.push_back({"WS2815 special current model remains unchanged", [] {
    BusDigital bus(100,0,TYPE_WS2812_RGB); bus._milliAmpsPerLed=255;
    for (unsigned i=0;i<100;i++) bus.setPixelColor(i,RGBW32(255,128,32,0));
    bus.estimateCurrent(); CHECK(bus.getUsedCurrent()==1300);
  }});
  checks.push_back({"white-only WWA is not counted twice", [] {
    BusDigital bus(100,0,TYPE_WS2812_WWA); Bus::_cct=0;
    for (unsigned i=0;i<100;i++) bus.setPixelColor(i,RGBW32(0,0,0,255));
    bus.estimateCurrent(); CHECK(bus.getUsedCurrent()==2850);
    bus.applyBriLimit(128); CHECK(bus.raw->pixels[0].R==127 && bus.raw->pixels[0].G==0);
  }});
  checks.push_back({"current estimates above 65535 do not wrap", [] {
    BusDigital bus(1300,0,TYPE_WS2812_RGB);
    for (unsigned i=0;i<1300;i++) bus.setPixelColor(i,RGBW32(255,255,255,0));
    bus.estimateCurrent(); CHECK(bus.getUsedCurrent()==72800);
    bus.setCurrentLimit(10000); bus.applyBriLimit(0);
    CHECK(bus.getUsedCurrent()==10000 && bus.raw->pixels[0].R<64);
  }});
  checks.push_back({"disabled per-output limit remains disabled", [] {
    auto free=std::make_unique<BusDigital>(3,0,TYPE_WS2812_RGB);
    auto limited=std::make_unique<BusDigital>(3,0,TYPE_WS2812_RGB);
    auto* freePtr=free.get(); auto* limitPtr=limited.get();
    free->_milliAmpsMax=0; limited->_milliAmpsMax=150;
    BusManager::busses.push_back(std::move(free)); BusManager::busses.push_back(std::move(limited));
    BusManager::initializeABL(); CHECK(freePtr->_milliAmpsLimit==0 && limitPtr->_milliAmpsLimit>0);
    for (unsigned frame=0;frame<2;frame++) {
      for (unsigned i=0;i<3;i++) {
        freePtr->setPixelColor(i,RGBW32(255,255,255,0));
        limitPtr->setPixelColor(i,RGBW32(255,255,255,0));
      }
      BusManager::applyABL();
      CHECK(freePtr->raw->pixels[2].R==255 && freePtr->getUsedCurrent()==168);
      CHECK(limitPtr->raw->pixels[2].R<255 && limitPtr->getUsedCurrent()==150-MA_FOR_ESP);
      CHECK(freePtr->_colorSum==0);
    }
  }});
  checks.push_back({"global limiting preserves CCT and scales a skipped tail", [] {
    auto bus=std::make_unique<BusDigital>(100,1); auto* ptr=bus.get();
    BusManager::busses.push_back(std::move(bus)); BusManager::_gMilliAmpsMax=500;
    BusManager::initializeABL(); Bus::_cct=0;
    for (unsigned i=0;i<100;i++) ptr->setPixelColor(i,RGBW32(0,0,0,255));
    Bus::_cct=-1; BusManager::applyABL();
    for (unsigned i=1;i<=100;i++) CHECK(ptr->raw->pixels[i].WW>0 && ptr->raw->pixels[i].WW<255 && ptr->raw->pixels[i].CW==0);
  }});
  unsigned failures=0;
  for (const auto& check : checks) {
    resetState();
    try { check.second(); std::cout << "PASS " << check.first << '\n'; }
    catch (const std::exception& error) { failures++; std::cout << "FAIL " << check.first << ": " << error.what() << '\n'; }
  }
  std::cout << checks.size()-failures << '/' << checks.size() << " regression checks passed\n";
  return failures ? 1 : 0;
}

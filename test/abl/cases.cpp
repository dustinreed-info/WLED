// Behavioral checks for source extracted from the pixel driver and ABL pipeline.
// Failures are reported individually so an upstream snapshot can demonstrate regressions.
#define CHECK(c) do { if (!(c)) throw std::runtime_error(#c); } while (0)

static void resetState() {
  strip.releaseCCT();
  strip.length=8; errorFlag=0; cctAllocations=0; cctFrees=0; failCCTAllocation=false;
  strip._length=8; strip._segments.clear();
  strip._hasWhiteChannel=false; strip._isOffRefreshRequired=false;
  Bus::_cct=-1; Bus::_cctBlend=0; Bus::_gAWM=255;
  BusManager::busses.clear(); BusManager::_useABL=true;
  BusManager::_gMilliAmpsMax=0; BusManager::_gMilliAmpsUsed=0;
  PolyBus::resetChannelTracking();
  strip.cctFromRgb=false; strip.correctWB=false;
  realtimeMode=REALTIME_MODE_INACTIVE; realtimeOverride=REALTIME_OVERRIDE_NONE;
  useMainSegmentOnly=false; strip._mainSegment=0;
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
  checks.push_back({"mixed I2S protocols are rejected without consuming channels", [] {
    uint8_t pins[2]={16,17};
    CHECK(PolyBus::getI(TYPE_WS2812_RGB,pins,1)==I_32_I2_NEO_3);
    CHECK(PolyBus::getI(TYPE_WS2805,pins,1)==I_NONE);
    CHECK(PolyBus::_i2sChannelsAssigned==1 && !PolyBus::_useParallelI2S);
    CHECK(PolyBus::getI(TYPE_WS2812_RGB,pins,1)==I_32_I2_NEO_3);
    CHECK(PolyBus::_i2sChannelsAssigned==2 && PolyBus::_useParallelI2S);
  }});
  checks.push_back({"RMT exhaustion does not substitute the wrong I2S protocol", [] {
    uint8_t pins[2]={16,17};
    CHECK(PolyBus::getI(TYPE_WS2805,pins,1)==I_32_I2_2805_5);
    CHECK(PolyBus::getI(TYPE_WS2812_RGB,pins,0)==I_32_RN_NEO_3);
    CHECK(PolyBus::getI(TYPE_WS2812_RGB,pins,0)==I_32_RN_NEO_3);
    CHECK(PolyBus::getI(TYPE_WS2812_RGB,pins,0)==I_NONE);
    CHECK(PolyBus::getI(TYPE_WS2805,pins,0)==I_32_I2_2805_5);
  }});
  checks.push_back({"unsupported digital types do not reserve driver channels", [] {
    uint8_t pins[2]={16,17};
    CHECK(PolyBus::getI(17,pins,0)==I_NONE);
    CHECK(PolyBus::_rmtChannelsAssigned==0);
    CHECK(PolyBus::getI(17,pins,1)==I_NONE);
    CHECK(PolyBus::_i2sChannelsAssigned==0);
  }});
  checks.push_back({"invalid outputs do not hide later WS2805 capabilities", [] {
    auto bad=std::make_unique<BusDigital>(1,0,TYPE_WS2812_RGB); bad->_valid=false;
    BusManager::busses.push_back(std::move(bad));
    BusManager::busses.push_back(std::make_unique<BusDigital>(1));
    CHECK(strip.hasCCTBus() && strip.hasRGBWBus());
    Segment segment; segment.refreshLightCapabilities();
    CHECK(segment._capabilities==(SEG_CAPABILITY_RGB|SEG_CAPABILITY_W|SEG_CAPABILITY_CCT));
  }});
  checks.push_back({"CCT storage is reused across frames and clears stale temperatures", [] {
    BusManager::busses.push_back(std::make_unique<BusDigital>(8));
    for (unsigned frame=0;frame<100;frame++) {
      CHECK(strip.updateCCTBuffer());
      CHECK(strip._pixelCCT && strip._pixelCCT[0]==127 && strip._pixelCCT[7]==127);
      strip._pixelCCT[0]=0; strip._pixelCCT[7]=255;
      strip.finishFrame();
    }
    CHECK(cctAllocations==1 && cctFrees==0);
  }});
  checks.push_back({"CCT storage follows canvas resizing", [] {
    BusManager::busses.push_back(std::make_unique<BusDigital>(8));
    CHECK(strip.updateCCTBuffer()); strip.finishFrame(); strip.length=12;
    CHECK(strip.updateCCTBuffer() && strip._pixelCCT[11]==127);
    CHECK(cctAllocations==2 && cctFrees==1);
    strip.finishFrame(); strip.length=0;
    CHECK(strip.updateCCTBuffer() && !strip._pixelCCT);
    CHECK(cctAllocations==2 && cctFrees==2);
  }});
  checks.push_back({"RGB-derived temperature releases cached CCT storage", [] {
    BusManager::busses.push_back(std::make_unique<BusDigital>(8));
    CHECK(strip.updateCCTBuffer()); strip.finishFrame(); strip.cctFromRgb=true;
    CHECK(strip.updateCCTBuffer() && !strip._pixelCCT);
    CHECK(cctFrees==1);
    strip.cctFromRgb=false;
    CHECK(strip.updateCCTBuffer() && cctAllocations==2);
  }});
  checks.push_back({"CCT allocation failure rejects the frame and can recover", [] {
    BusManager::busses.push_back(std::make_unique<BusDigital>(8));
    failCCTAllocation=true;
    CHECK(!strip.updateCCTBuffer() && !strip._pixelCCT && errorFlag==ERR_NORAM_PX);
    failCCTAllocation=false;
    CHECK(strip.updateCCTBuffer() && strip._pixelCCT[0]==127);
    strip.length=16; failCCTAllocation=true;
    CHECK(!strip.updateCCTBuffer() && !strip._pixelCCT);
  }});
  checks.push_back({"RGB outputs only allocate CCT data for white balance correction", [] {
    BusManager::busses.push_back(std::make_unique<BusDigital>(8,0,TYPE_WS2812_RGB));
    CHECK(strip.updateCCTBuffer() && !strip._pixelCCT && cctAllocations==0);
    strip.correctWB=true;
    CHECK(strip.updateCCTBuffer() && strip._pixelCCT);
    strip.correctWB=false;
    CHECK(strip.updateCCTBuffer() && !strip._pixelCCT);
  }});
  checks.push_back({"bottom blend mode preserves the underlying white temperature", [] {
    BusManager::busses.push_back(std::make_unique<BusDigital>(8));
    CHECK(strip.updateCCTBuffer()); strip._pixelCCT[0]=0;
    strip.blendPixelCCT(0,RGBW32(0,0,0,255),255,1,255);
    CHECK(strip._pixelCCT[0]==0);
  }});
  checks.push_back({"transparent stencil pixels preserve the underlying temperature", [] {
    BusManager::busses.push_back(std::make_unique<BusDigital>(8));
    CHECK(strip.updateCCTBuffer()); strip._pixelCCT[7]=0;
    strip.blendPixelCCT(7,BLACK,255,16,255); CHECK(strip._pixelCCT[7]==0);
    strip.blendPixelCCT(7,RGBW32(0,0,0,255),255,16,255); CHECK(strip._pixelCCT[7]==255);
  }});
  checks.push_back({"zero-opacity layers do not change white temperature", [] {
    BusManager::busses.push_back(std::make_unique<BusDigital>(8));
    CHECK(strip.updateCCTBuffer()); strip._pixelCCT[0]=255;
    strip.blendPixelCCT(0,RGBW32(0,0,0,255),0,0,0); CHECK(strip._pixelCCT[0]==255);
    strip.blendPixelCCT(0,RGBW32(0,0,0,255),255,0,0); CHECK(strip._pixelCCT[0]==0);
  }});
  checks.push_back({"CCT blending tolerates RGB-only frames without a CCT buffer", [] {
    strip.blendPixelCCT(0,RGBW32(255,0,0,0),255,0,255);
    CHECK(strip._pixelCCT==nullptr);
  }});
  checks.push_back({"custom overlapping segments are not mistaken for automatic segments", [] {
    BusManager::busses.push_back(std::make_unique<BusDigital>(8));
    Segment full; full.stop=8;
    Segment custom; custom.start=1; custom.stop=6;
    strip._segments={full,custom}; CHECK(!strip.checkSegmentAlignment());
    strip._segments={custom,full}; CHECK(!strip.checkSegmentAlignment());
  }});
  checks.push_back({"all aligned segments remain eligible for automatic resizing", [] {
    BusManager::busses.push_back(std::make_unique<BusDigital>(4));
    auto second=std::make_unique<BusDigital>(4); second->_start=4;
    BusManager::busses.push_back(std::move(second));
    Segment left; left.stop=4;
    Segment right; right.start=4; right.stop=8;
    strip._segments={left,right}; CHECK(strip.checkSegmentAlignment());
  }});
  checks.push_back({"later valid outputs initialize despite invalid earlier outputs", [] {
    auto bad=std::make_unique<BusDigital>(1,0,TYPE_WS2812_RGB); bad->_valid=false;
    auto* badPtr=bad.get(); BusManager::busses.push_back(std::move(bad));
    auto over=std::make_unique<BusDigital>(1,0,TYPE_WS2812_RGB); over->_start=MAX_LEDS;
    auto* overPtr=over.get(); BusManager::busses.push_back(std::move(over));
    auto valid=std::make_unique<BusDigital>(8); valid->_start=16;
    auto* validPtr=valid.get(); BusManager::busses.push_back(std::move(valid));
    strip.initializeOutputs();
    CHECK(strip._length==24 && strip._hasWhiteChannel);
    CHECK(validPtr->begun && validPtr->_bri==bri);
    CHECK(!badPtr->begun && !overPtr->begun);
  }});
  checks.push_back({"full-strip realtime uses the chosen main segment temperature", [] {
    BusManager::busses.push_back(std::make_unique<BusDigital>(8));
    strip._segments.resize(2); strip._mainSegment=1;
    strip._segments[0].cct=127;
    for (byte mode : {REALTIME_MODE_DDP, REALTIME_MODE_ADALIGHT, REALTIME_MODE_HYPERION, REALTIME_MODE_E131}) {
      realtimeMode=mode;
      for (uint8_t cct : {0, 64, 255}) {
        strip._segments[1].cct=cct;
        CHECK(strip.updateCCTBuffer());
        for (size_t i=0;i<strip.length;i++) CHECK(strip._pixelCCT[i]==cct);
        auto* bus=static_cast<BusDigital*>(BusManager::getBus(0));
        Bus::_cct=strip._pixelCCT[0]; bus->setPixelColor(0,RGBW32(0,0,0,255));
        CHECK(bus->raw->pixels[0].WW==255-cct && bus->raw->pixels[0].CW==cct);
      }
    }
    CHECK(cctAllocations==1);
  }});
  checks.push_back({"effects and overridden streams reset their CCT before blending", [] {
    BusManager::busses.push_back(std::make_unique<BusDigital>(8));
    strip._segments.resize(1); strip._segments[0].cct=0;
    realtimeMode=REALTIME_MODE_DDP;
    CHECK(strip.updateCCTBuffer() && strip._pixelCCT[0]==0);
    useMainSegmentOnly=true;
    CHECK(strip.updateCCTBuffer() && strip._pixelCCT[0]==127);
    useMainSegmentOnly=false;
    for (byte overrideMode : {REALTIME_OVERRIDE_ONCE, REALTIME_OVERRIDE_ALWAYS}) {
      realtimeOverride=overrideMode;
      CHECK(strip.updateCCTBuffer() && strip._pixelCCT[0]==127);
    }
    realtimeOverride=REALTIME_OVERRIDE_NONE; realtimeMode=REALTIME_MODE_INACTIVE;
    CHECK(strip.updateCCTBuffer() && strip._pixelCCT[0]==127);
  }});
  checks.push_back({"full-strip white balance uses the configured CCT on RGB outputs", [] {
    BusManager::busses.push_back(std::make_unique<BusDigital>(8,0,TYPE_SK6812_RGBW));
    strip.correctWB=true; strip._segments.resize(1); strip._segments[0].cct=64;
    realtimeMode=REALTIME_MODE_DDP;
    CHECK(strip.updateCCTBuffer() && strip._pixelCCT[7]==64);
    strip.cctFromRgb=true;
    CHECK(strip.updateCCTBuffer() && !strip._pixelCCT);
  }});
  checks.push_back({"full-strip realtime tolerates an absent segment list", [] {
    BusManager::busses.push_back(std::make_unique<BusDigital>(8));
    realtimeMode=REALTIME_MODE_DDP;
    CHECK(strip.updateCCTBuffer() && strip._pixelCCT[0]==127);
  }});
  checks.push_back({"SK6812 RGBW ordering and white swaps survive current limiting", [] {
    for (bool parallel : {false,true}) {
      for (uint8_t order=0;order<6;order++) {
        for (uint8_t swap=0;swap<4;swap++) {
          BusDigital bus(3,2,TYPE_SK6812_RGBW);
          bus._iType=parallel ? I_32_I2_NEO_4 : I_32_RN_NEO_4;
          PolyBus::_useParallelI2S=parallel; bus._colorOrder=order|(swap<<4); bus._reversed=true;
          for (unsigned i=0;i<3;i++) bus.setPixelColor(i,RGBW32(200,0,0,180));
          for (unsigned i=2;i<5;i++) CHECK(PolyBus::getPixelColor(bus._busPtr,bus._iType,i,bus._colorOrder)==RGBW32(200,0,0,180));
          bus.applyBriLimit(128);
          for (unsigned i=2;i<5;i++) {
            uint32_t c=PolyBus::getPixelColor(bus._busPtr,bus._iType,i,bus._colorOrder);
            CHECK(R(c)>0 && R(c)<200 && G(c)==0 && B(c)==0 && W(c)>0 && W(c)<180);
          }
          for (unsigned i=0;i<2;i++) CHECK(PolyBus::getPixelColor(bus._busPtr,bus._iType,i,bus._colorOrder)==0);
        }
      }
    }
  }});
  checks.push_back({"SK6812 auto-white modes preserve their documented channel behavior", [] {
    for (uint8_t mode : {RGBW_MODE_MANUAL_ONLY, RGBW_MODE_AUTO_BRIGHTER, RGBW_MODE_AUTO_ACCURATE, RGBW_MODE_DUAL, RGBW_MODE_MAX}) {
      BusDigital bus(1,0,TYPE_SK6812_RGBW); bus._autoWhiteMode=mode;
      bus.setPixelColor(0,RGBW32(100,60,20,80));
      const auto before=bus.raw->pixels[0];
      if (mode==RGBW_MODE_MANUAL_ONLY || mode==RGBW_MODE_DUAL) CHECK(before.R==100 && before.G==60 && before.B==20 && before.W==80);
      else if (mode==RGBW_MODE_AUTO_ACCURATE) CHECK(before.R==80 && before.G==40 && before.B==0 && before.W==20);
      else if (mode==RGBW_MODE_MAX) CHECK(before.R==100 && before.G==60 && before.B==20 && before.W==100);
      else CHECK(before.R==100 && before.G==60 && before.B==20 && before.W==20);
      bus.applyBriLimit(128); const auto after=bus.raw->pixels[0];
      CHECK(after.W>0 && after.W<before.W);
      CHECK((after.R==0)==(before.R==0) && (after.G==0)==(before.G==0) && (after.B==0)==(before.B==0));
    }
    BusDigital bus(1,0,TYPE_SK6812_RGBW); bus._autoWhiteMode=RGBW_MODE_DUAL;
    bus.setPixelColor(0,RGBW32(100,60,20,0));
    CHECK(bus.raw->pixels[0].W==20);
    Bus::_gAWM=RGBW_MODE_MANUAL_ONLY;
    bus.setPixelColor(0,RGBW32(100,60,20,80));
    CHECK(bus.raw->pixels[0].W==80);
  }});
  checks.push_back({"SK6812 current accounts for its actual RGBW output channels", [] {
    BusDigital bus(100,0,TYPE_SK6812_RGBW);
    for (unsigned i=0;i<100;i++) bus.setPixelColor(i,RGBW32(0,0,0,255));
    bus.estimateCurrent(); CHECK(bus.getUsedCurrent()==1475);
    bus.applyBriLimit(255);
    for (unsigned i=0;i<100;i++) bus.setPixelColor(i,RGBW32(255,255,255,255));
    bus.estimateCurrent(); CHECK(bus.getUsedCurrent()==5600);
  }});
  checks.push_back({"digital RGBW white balance matches virtual RGBW output", [] {
    for (uint8_t mode : {RGBW_MODE_MANUAL_ONLY, RGBW_MODE_AUTO_BRIGHTER, RGBW_MODE_AUTO_ACCURATE, RGBW_MODE_DUAL, RGBW_MODE_MAX}) {
      BusDigital digital(1,0,TYPE_SK6812_RGBW); digital._autoWhiteMode=mode;
      BusNetwork network; network._autoWhiteMode=mode;
      for (uint16_t kelvin : {1900,2700,6500,10000}) {
        Bus::_cct=kelvin;
        for (uint32_t c : {RGBW32(100,60,20,0),RGBW32(100,100,100,0),RGBW32(100,60,20,80),RGBW32(0,0,255,0)}) {
          digital.setPixelColor(0,c); network.setPixelColor(0,c);
          const auto& p=digital.raw->pixels[0];
          CHECK(p.R==network._data[0] && p.G==network._data[1] && p.B==network._data[2] && p.W==network._data[3]);
        }
      }
    }
  }});
  checks.push_back({"PWM RGBW white balance matches virtual RGBW output", [] {
    for (uint8_t mode : {RGBW_MODE_MANUAL_ONLY, RGBW_MODE_AUTO_BRIGHTER, RGBW_MODE_AUTO_ACCURATE, RGBW_MODE_DUAL, RGBW_MODE_MAX}) {
      BusPwm pwm; pwm._autoWhiteMode=mode;
      BusNetwork network; network._autoWhiteMode=mode;
      for (uint16_t kelvin : {1900,2700,6500,10000}) {
        Bus::_cct=kelvin;
        for (uint32_t c : {RGBW32(100,60,20,0),RGBW32(100,100,100,0),RGBW32(100,60,20,80),RGBW32(0,0,255,0)}) {
          pwm.setPixelColor(0,c); network.setPixelColor(0,c);
          for (unsigned channel=0;channel<4;channel++) CHECK(pwm._data[channel]==network._data[channel]);
        }
      }
    }
  }});
  checks.push_back({"WS2805 white extraction is independent of RGB white balance", [] {
    for (uint8_t mode : {RGBW_MODE_AUTO_BRIGHTER,RGBW_MODE_AUTO_ACCURATE,RGBW_MODE_DUAL}) {
      BusDigital bus(1); bus._autoWhiteMode=mode;
      for (uint16_t kelvin : {1900,2700,6500,10000}) {
        Bus::_cct=kelvin; bus.setPixelColor(0,RGBW32(100,100,100,0));
        const auto& p=bus.raw->pixels[0];
        CHECK(p.WW+p.CW>=99 && p.WW+p.CW<=100);
        if (mode==RGBW_MODE_AUTO_ACCURATE) CHECK(p.R==0 && p.G==0 && p.B==0);
      }
    }
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

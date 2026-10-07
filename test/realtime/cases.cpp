#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while(0)
static void reset() {
  strip=Strip{}; Serial.input.clear(); realtimeRespectLedMaps=true;
  realtimeOverride=0; realtimeMode=0; realtimeTimeout=0;
  useMainSegmentOnly=false; arlsOffset=0; DMXAddress=1;
  e131NewData=false; std::fill(std::begin(e131LastSequenceNumber),std::end(e131LastSequenceNumber),0);
}
static void feed(const std::vector<byte>& bytes) {
  Serial.input.insert(Serial.input.end(),bytes.begin(),bytes.end());
  handleSerial();
}
static e131_packet_t packet(uint32_t offset, bool white, const std::vector<byte>& bytes) {
  e131_packet_t p;
  p.flags|=DDP_FLAGS_PUSH;
  p.channelOffset=htonl(offset); p.dataType=white ? DDP_TYPE_RGBW32 : DDP_TYPE_RGB24;
  p.dataLen=htons(bytes.size()); std::copy(bytes.begin(),bytes.end(),p.data);
  return p;
}
int main() {
  std::cout<<std::unitbuf;
  std::vector<std::pair<std::string,std::function<void()>>> cases;
  cases.push_back({"ordinary DDP RGBW retains all four channels", [] {
    auto p=packet(0,true,{11,22,33,44,55,66,77,88});
    handleDDPPacket(&p,DDP_HEADER_LEN+8);
    CHECK(strip.colors[0]==RGBW32(11,22,33,44) && strip.colors[1]==RGBW32(55,66,77,88));
  }});
  cases.push_back({"DDP RGB clears white from a previous RGBW stream", [] {
    realtimeMode=REALTIME_MODE_DDP; strip.colors[0]=RGBW32(0,0,0,255);
    auto p=packet(0,false,{11,22,33}); handleDDPPacket(&p,DDP_HEADER_LEN+3);
    CHECK(strip.colors[0]==RGBW32(11,22,33,0));
  }});
  cases.push_back({"DDP offsets beyond 65535 pixels do not wrap into the strip", [] {
    realtimeMode=REALTIME_MODE_DDP;
    auto p=packet(65536u*4,true,{11,22,33,44}); handleDDPPacket(&p,DDP_HEADER_LEN+4);
    CHECK(strip.colors[0]==0);
  }});
  cases.push_back({"large realtime indices do not wrap when an offset is added", [] {
    arlsOffset=2; setRealtimePixel(UINT32_MAX-1,11,22,33,44);
    CHECK(strip.colors[0]==0);
  }});
  cases.push_back({"negative realtime offsets clip before the first pixel", [] {
    arlsOffset=-2; setRealtimePixel(1,11,22,33,44); CHECK(strip.colors[0]==0);
    setRealtimePixel(2,11,22,33,44); CHECK(strip.colors[0]==RGBW32(11,22,33,44));
  }});
  cases.push_back({"main-segment realtime offset respects segment length", [] {
    useMainSegmentOnly=true; strip.main.count=3; strip.main.colors.resize(3);
    setRealtimePixel(2,11,22,33,44); setRealtimePixel(3,99,99,99,99);
    CHECK(strip.main.colors[2]==RGBW32(11,22,33,44));
  }});
  cases.push_back({"first Adalight frame survives realtime takeover", [] {
    feed({'A','d','a',0,0,0x55,11,22,33});
    CHECK(strip.shows==1 && strip.shown[0]==RGBW32(11,22,33,0));
  }});
  cases.push_back({"TPM2 byte length carries correctly across the high byte", [] {
    realtimeMode=REALTIME_MODE_ADALIGHT; strip.resizePixels(100);
    std::vector<byte> bytes={0xC9,0xDA,0x01,0x2C};
    for (unsigned i=0;i<100;i++) { bytes.push_back(i+1); bytes.push_back(22); bytes.push_back(33); }
    bytes.push_back(0x36); feed(bytes);
    CHECK(strip.shows==1 && strip.shown[99]==RGBW32(100,22,33,0));
  }});
  cases.push_back({"large realtime index plus positive offset never overflows", [] {
    arlsOffset=2; setRealtimePixel(UINT32_MAX,11,22,33,44);
    CHECK(strip.colors[1]==0);
  }});
  cases.push_back({"DDP RGB high offsets cannot alias an active LED", [] {
    realtimeMode=REALTIME_MODE_DDP;
    auto p=packet(65537u*3,false,{11,22,33}); handleDDPPacket(&p,DDP_HEADER_LEN+3);
    CHECK(strip.colors[1]==0);
  }});
  cases.push_back({"incomplete DDP payload does not acquire realtime mode", [] {
    auto p=packet(0,true,{11,22,33,44}); handleDDPPacket(&p,DDP_HEADER_LEN+3);
    CHECK(realtimeMode==0 && !e131NewData && strip.colors[0]==0);
  }});
  cases.push_back({"DDP timestamp bytes are skipped before RGBW payload", [] {
    auto p=packet(0,true,{11,22,33,44}); p.flags|=DDP_FLAGS_TIME;
    std::copy_backward(p.data,p.data+4,p.data+8);
    std::fill(p.data,p.data+4,255); handleDDPPacket(&p,DDP_HEADER_LEN+8);
    CHECK(strip.colors[0]==RGBW32(11,22,33,44));
  }});
  cases.push_back({"empty DDP push updates frame readiness without changing channels", [] {
    realtimeMode=REALTIME_MODE_DDP; strip.colors[0]=RGBW32(11,22,33,44);
    auto p=packet(0,true,{}); handleDDPPacket(&p,DDP_HEADER_LEN);
    CHECK(e131NewData && strip.colors[0]==RGBW32(11,22,33,44));
  }});
  cases.push_back({"fragmented Adalight input completes the original frame", [] {
    feed({'A','d'}); CHECK(strip.shows==0);
    feed({'a',0,1,0x54,11,22}); CHECK(strip.shows==0);
    feed({33,44,55,66});
    CHECK(strip.shows==1 && strip.shown[0]==RGBW32(11,22,33,0) && strip.shown[1]==RGBW32(44,55,66,0));
  }});
  cases.push_back({"bad Adalight checksum cannot clear the current frame", [] {
    strip.colors[0]=RGBW32(11,22,33,44);
    feed({'A','d','a',0,0,0x00});
    CHECK(realtimeMode==0 && strip.colors[0]==RGBW32(11,22,33,44) && strip.shows==0);
  }});
  cases.push_back({"TPM2 empty or non-RGB lengths recover at the next header", [] {
    feed({0xC9,0xDA,0,0,0x36}); CHECK(strip.shows==0);
    feed({0xC9,0xDA,0,1,0x36}); CHECK(strip.shows==0);
    feed({'A','d','a',0,0,0x55,11,22,33});
    CHECK(strip.shows==1 && strip.shown[0]==RGBW32(11,22,33,0));
  }});
  cases.push_back({"temporarily overridden serial pixels still consume their positions", [] {
    realtimeOverride=REALTIME_OVERRIDE_ALWAYS;
    feed({'A','d','a',0,1,0x54,11,22,33});
    realtimeOverride=REALTIME_OVERRIDE_NONE; feed({44,55,66});
    CHECK(strip.shown[0]==0 && strip.shown[1]==RGBW32(44,55,66,0));
  }});
  cases.push_back({"Adalight maximum encoded count does not wrap to zero", [] {
    std::vector<byte> bytes={'A','d','a',255,255,0x55};
    for (unsigned i=0;i<65536;i++) { bytes.push_back(11); bytes.push_back(22); bytes.push_back(33); }
    feed(bytes); CHECK(strip.shows==1 && strip.shown[0]==RGBW32(11,22,33,0));
  }});
  cases.push_back({"DDP single white-byte update preserves existing RGB", [] {
    realtimeMode=REALTIME_MODE_DDP; strip.colors[0]=RGBW32(11,22,33,44);
    auto p=packet(3,true,{99}); handleDDPPacket(&p,DDP_HEADER_LEN+1);
    CHECK(strip.colors[0]==RGBW32(11,22,33,99));
  }});
  cases.push_back({"DDP split RGBW pixels reconstruct across datagrams", [] {
    auto first=packet(0,true,{11,22}); first.flags&=~DDP_FLAGS_PUSH;
    handleDDPPacket(&first,DDP_HEADER_LEN+2);
    auto second=packet(2,true,{33,44,55,66,77,88}); handleDDPPacket(&second,DDP_HEADER_LEN+6);
    CHECK(strip.colors[0]==RGBW32(11,22,33,44) && strip.colors[1]==RGBW32(55,66,77,88));
  }});
  cases.push_back({"DDP partial RGB update preserves other RGB channels and clears white", [] {
    realtimeMode=REALTIME_MODE_DDP; strip.colors[0]=RGBW32(11,22,33,44);
    auto p=packet(1,false,{99}); handleDDPPacket(&p,DDP_HEADER_LEN+1);
    CHECK(strip.colors[0]==RGBW32(11,99,33,0));
  }});
  cases.push_back({"serial partial header recovers after inactivity", [] {
    feed({'A','d'}); fakeTime+=1001;
    feed({'A','d','a',0,0,0x55,11,22,33});
    CHECK(strip.shows==1 && strip.shown[0]==RGBW32(11,22,33,0));
  }});
  cases.push_back({"truncated serial frame recovers after inactivity", [] {
    feed({'A','d','a',0,1,0x54,11,22}); fakeTime+=1001;
    feed({'A','d','a',0,0,0x55,44,55,66});
    CHECK(strip.shows==1 && strip.shown[0]==RGBW32(44,55,66,0));
  }});
  cases.push_back({"DDP reconstructs RGB and RGBW at every byte split boundary", [] {
    for (bool white : {false,true}) {
      unsigned channels=white ? 4 : 3;
      std::vector<byte> frame;
      for (unsigned i=0;i<8*channels;i++) frame.push_back(i+1);
      for (unsigned split=1;split<frame.size();split++) {
        strip=Strip{}; realtimeMode=0; e131LastSequenceNumber[0]=0;
        auto a=packet(0,white,std::vector<byte>(frame.begin(),frame.begin()+split)); a.flags&=~DDP_FLAGS_PUSH;
        auto b=packet(split,white,std::vector<byte>(frame.begin()+split,frame.end()));
        handleDDPPacket(&a,DDP_HEADER_LEN+split);
        handleDDPPacket(&b,DDP_HEADER_LEN+frame.size()-split);
        for (unsigned i=0;i<8;i++) CHECK(strip.colors[i]==RGBW32(frame[i*channels],frame[i*channels+1],frame[i*channels+2],white ? frame[i*channels+3] : 0));
      }
    }
  }});
  cases.push_back({"partial DDP update uses main-segment and negative-offset coordinates", [] {
    useMainSegmentOnly=true; arlsOffset=-1; realtimeMode=REALTIME_MODE_DDP;
    strip.main.colors[0]=RGBW32(11,22,33,44);
    auto p=packet(7,true,{99}); handleDDPPacket(&p,DDP_HEADER_LEN+1);
    CHECK(strip.main.colors[0]==RGBW32(11,22,33,99) && strip.colors[0]==0);
  }});
  cases.push_back({"DDP partial update at the final LED clips the following pixel", [] {
    realtimeMode=REALTIME_MODE_DDP; strip.colors[7]=RGBW32(11,22,33,44);
    auto p=packet(31,true,{99,1,2,3,4}); handleDDPPacket(&p,DDP_HEADER_LEN+5);
    CHECK(strip.colors[7]==RGBW32(11,22,33,99) && strip.colors[0]==0);
  }});
  cases.push_back({"serial frame duration may exceed the inactivity timeout", [] {
    feed({'A','d','a',0,1,0x54,11,22});
    fakeTime+=700; feed({33,44}); CHECK(strip.shows==0);
    fakeTime+=700; feed({55,66});
    CHECK(strip.shows==1 && strip.shown[1]==RGBW32(44,55,66,0));
  }});
  cases.push_back({"serial inactivity recovery survives millis wrap", [] {
    fakeTime=UINT32_MAX-500; feed({'A','d'});
    fakeTime+=1001; feed({'A','d','a',0,0,0x55,44,55,66});
    CHECK(strip.shows==1 && strip.shown[0]==RGBW32(44,55,66,0));
  }});
  cases.push_back({"raw realtime read handles absent storage and inactive main segment", [] {
    strip._pixels=nullptr; CHECK(getRealtimePixel(0)==0);
    useMainSegmentOnly=true; strip.main.count=0;
    CHECK(getRealtimePixel(0)==0 && strip.getRealtimePixelColor(0)==0);
  }});
  cases.push_back({"finite realtime timeout expires after its ordinary deadline", [] {
    fakeTime=100; realtimeLock(2500,REALTIME_MODE_DDP);
    fakeTime=2600; checkRealtimeMaintenance(); CHECK(realtimeMode==REALTIME_MODE_DDP);
    fakeTime=2601; checkRealtimeMaintenance(); CHECK(realtimeMode==REALTIME_MODE_INACTIVE);
  }});
  cases.push_back({"finite realtime deadline survives millis rollover", [] {
    fakeTime=UINT32_MAX-9; realtimeLock(25,REALTIME_MODE_DDP);
    checkRealtimeMaintenance(); CHECK(realtimeMode==REALTIME_MODE_DDP);
    fakeTime=8; checkRealtimeMaintenance(); CHECK(realtimeMode==REALTIME_MODE_DDP);
    fakeTime=16; checkRealtimeMaintenance(); CHECK(realtimeMode==REALTIME_MODE_INACTIVE);
  }});
  cases.push_back({"finite timeout cannot collide with the forever sentinel", [] {
    fakeTime=UINT32_MAX-25; realtimeLock(25,REALTIME_MODE_DDP);
    CHECK(realtimeTimeout!=UINT32_MAX);
    checkRealtimeMaintenance(); CHECK(realtimeMode==REALTIME_MODE_DDP);
    fakeTime=100; checkRealtimeMaintenance(); CHECK(realtimeMode==REALTIME_MODE_INACTIVE);
  }});
  cases.push_back({"finite timeout cannot collide with the cancellation sentinel", [] {
    fakeTime=UINT32_MAX-24; realtimeLock(25,REALTIME_MODE_DDP);
    CHECK(realtimeTimeout!=0);
    checkRealtimeMaintenance(); CHECK(realtimeMode==REALTIME_MODE_DDP);
    fakeTime=100; checkRealtimeMaintenance(); CHECK(realtimeMode==REALTIME_MODE_INACTIVE);
  }});
  cases.push_back({"indefinite realtime holds remain indefinite across rollover", [] {
    for (uint32_t hold : {65000u,255001u}) {
      realtimeTimeout=0; fakeTime=UINT32_MAX-10; realtimeLock(hold,REALTIME_MODE_DDP);
      CHECK(realtimeTimeout==UINT32_MAX);
      checkRealtimeMaintenance(); CHECK(realtimeMode==REALTIME_MODE_DDP);
      fakeTime=100; checkRealtimeMaintenance(); CHECK(realtimeMode==REALTIME_MODE_DDP);
      realtimeLock(2500,REALTIME_MODE_DDP); CHECK(realtimeTimeout==UINT32_MAX);
    }
  }});
  cases.push_back({"explicit realtime cancellation works immediately around rollover", [] {
    for (uint32_t now : {UINT32_MAX-10,0u,100u}) {
      realtimeMode=REALTIME_MODE_DDP; realtimeTimeout=0; fakeTime=now;
      checkRealtimeMaintenance(); CHECK(realtimeMode==REALTIME_MODE_INACTIVE);
    }
  }});
  cases.push_back({"mapped pixel readback preserves logical coordinates and masking", [] {
    strip.colors[0]=RGBW32(11,22,33,44); strip.colors[1]=RGBW32(55,66,77,88);
    uint16_t mapping[]={7,UINT16_MAX};
    strip.customMappingSize=2; strip.customMappingTable=mapping;
    CHECK(strip.getPixelColor(0)==strip.colors[0] && strip.getPixelColor(1)==0);
    CHECK(strip.getPixelColorNoMap(1)==strip.colors[1]);
    realtimeMode=REALTIME_MODE_DDP; realtimeRespectLedMaps=false;
    CHECK(strip.getPixelColor(1)==strip.colors[1]);
  }});
  cases.push_back({"pixel readback clips before narrowing mapped indices", [] {
    CHECK(strip.getPixelColor(65536)==0 && strip.getPixelColor(UINT32_MAX)==0);
    CHECK(strip.getPixelColorNoMap(65536)==0);
  }});
  cases.push_back({"missing canvas storage cannot crash realtime takeover and writes", [] {
    std::cout<<"CHECK missing canvas storage\n";
    strip._pixels=nullptr;
    CHECK(strip.getPixelColor(0)==0 && strip.getPixelColorNoMap(0)==0);
    strip.fill(BLACK); setRealtimePixel(0,11,22,33,44);
    auto p=packet(0,true,{11,22,33,44}); handleDDPPacket(&p,DDP_HEADER_LEN+4);
    CHECK(realtimeMode==REALTIME_MODE_DDP);
    p=packet(3,true,{99}); handleDDPPacket(&p,DDP_HEADER_LEN+1);
    CHECK(strip.colors[0]==0);
  }});
  unsigned failures=0;
  for (const auto& entry : cases) {
    reset();
    try { entry.second(); std::cout<<"PASS "<<entry.first<<'\n'; }
    catch(const std::exception& e) { failures++; std::cout<<"FAIL "<<entry.first<<": "<<e.what()<<'\n'; }
  }
  std::cout<<cases.size()-failures<<'/'<<cases.size()<<" realtime cases passed\n";
  return failures ? 1 : 0;
}

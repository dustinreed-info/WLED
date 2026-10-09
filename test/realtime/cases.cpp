#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while(0)
static void reset() {
  serialFrame.clear();
  failSerialAllocation=false; serialAllocations=0; serialFrees=0; lastSerialAllocationSize=0; errorFlag=0;
  serialCanRX=true; serialCanTX=true; Serial.connected=true;
  Serial.printCalls=0; Serial.printfCalls=0; jsonLockAvailable=true;
  strip=Strip{}; Serial.input.clear(); Serial.output.clear(); realtimeRespectLedMaps=true; e131SkipOutOfSequence=true;
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
  cases.push_back({"DDP sequence numbers restart after leaving realtime mode", [] {
    auto p=packet(0,true,{11,22,33,44}); p.sequenceNum=4;
    handleDDPPacket(&p,DDP_HEADER_LEN+4); exitRealtime();
    p=packet(0,true,{55,66,77,88}); p.sequenceNum=1;
    handleDDPPacket(&p,DDP_HEADER_LEN+4);
    CHECK(realtimeMode==REALTIME_MODE_DDP && strip.colors[0]==RGBW32(55,66,77,88));
    CHECK(e131LastSequenceNumber[0]==1);
  }});
  cases.push_back({"DDP does not inherit sequence numbers from another realtime protocol", [] {
    auto p=packet(0,true,{11,22,33,44}); p.sequenceNum=4;
    handleDDPPacket(&p,DDP_HEADER_LEN+4);
    realtimeLock(2500,REALTIME_MODE_ADALIGHT);
    p=packet(0,true,{55,66,77,88}); p.sequenceNum=1;
    handleDDPPacket(&p,DDP_HEADER_LEN+4);
    CHECK(realtimeMode==REALTIME_MODE_DDP && strip.colors[0]==RGBW32(55,66,77,88));
  }});
  cases.push_back({"unsequenced first DDP packet clears the old stream's sequence", [] {
    auto p=packet(0,true,{11,22,33,44}); p.sequenceNum=4;
    handleDDPPacket(&p,DDP_HEADER_LEN+4); exitRealtime();
    p=packet(0,true,{55,66,77,88}); p.sequenceNum=0;
    handleDDPPacket(&p,DDP_HEADER_LEN+4);
    CHECK(e131LastSequenceNumber[0]==0);
    p.sequenceNum=1; p.data[0]=99;
    handleDDPPacket(&p,DDP_HEADER_LEN+4); CHECK(R(strip.colors[0])==99);
  }});
  cases.push_back({"late DDP packets remain rejected within the same stream", [] {
    auto p=packet(0,true,{11,22,33,44}); p.sequenceNum=4;
    handleDDPPacket(&p,DDP_HEADER_LEN+4);
    p.sequenceNum=3; p.data[0]=99; e131NewData=false;
    handleDDPPacket(&p,DDP_HEADER_LEN+4);
    CHECK(!e131NewData && R(strip.colors[0])==11 && e131LastSequenceNumber[0]==4);
  }});
  cases.push_back({"DDP sequence wrap accepts the new frame and rejects an old one", [] {
    auto p=packet(0,true,{11,22,33,44}); p.sequenceNum=15;
    handleDDPPacket(&p,DDP_HEADER_LEN+4);
    p.sequenceNum=1; p.data[0]=55; handleDDPPacket(&p,DDP_HEADER_LEN+4);
    CHECK(R(strip.colors[0])==55 && e131LastSequenceNumber[0]==1);
    p.sequenceNum=15; p.data[0]=99; handleDDPPacket(&p,DDP_HEADER_LEN+4);
    CHECK(R(strip.colors[0])==55 && e131LastSequenceNumber[0]==1);
  }});
  cases.push_back({"truncated DDP cannot reset sequence state before validation", [] {
    auto p=packet(0,true,{11,22,33,44}); p.sequenceNum=4;
    handleDDPPacket(&p,DDP_HEADER_LEN+4); exitRealtime(); e131NewData=false;
    p.sequenceNum=1; handleDDPPacket(&p,DDP_HEADER_LEN+3);
    CHECK(realtimeMode==REALTIME_MODE_INACTIVE && !e131NewData && e131LastSequenceNumber[0]==4);
  }});
  cases.push_back({"DDP late-packet skipping can still be explicitly disabled", [] {
    auto p=packet(0,true,{11,22,33,44}); p.sequenceNum=4;
    handleDDPPacket(&p,DDP_HEADER_LEN+4); e131SkipOutOfSequence=false;
    p.sequenceNum=3; p.data[0]=99; handleDDPPacket(&p,DDP_HEADER_LEN+4);
    CHECK(R(strip.colors[0])==99 && e131LastSequenceNumber[0]==3);
  }});
  cases.push_back({"restarted legacy DDP streams do not inherit push gating", [] {
    auto p=packet(0,true,{11,22,33,44}); p.sequenceNum=4;
    handleDDPPacket(&p,DDP_HEADER_LEN+4); exitRealtime(); e131NewData=false;
    p.sequenceNum=1; p.flags&=~DDP_FLAGS_PUSH; p.data[0]=99;
    handleDDPPacket(&p,DDP_HEADER_LEN+4);
    CHECK(e131NewData && R(strip.colors[0])==99);
  }});
  cases.push_back({"repeated Adalight first header byte recovers the overlapping prefix", [] {
    feed({'A','A','d','a',0,0,0x55,11,22,33});
    CHECK(strip.shows==1 && strip.shown[0]==RGBW32(11,22,33,0));
  }});
  cases.push_back({"partial Adalight prefix can restart at its next first byte", [] {
    feed({'A','d','A','d','a',0,0,0x55,11,22,33});
    CHECK(strip.shows==1 && strip.shown[0]==RGBW32(11,22,33,0));
  }});
  cases.push_back({"TPM2 can follow an interrupted Adalight prefix", [] {
    feed({'A',0xC9,0xDA,0,3,11,22,33,0x36});
    CHECK(strip.shows==1 && strip.shown[0]==RGBW32(11,22,33,0));
  }});
  cases.push_back({"repeated TPM2 start bytes recover the overlapping prefix", [] {
    feed({0xC9,0xC9,0xDA,0,3,11,22,33,0x36});
    CHECK(strip.shows==1 && strip.shown[0]==RGBW32(11,22,33,0));
  }});
  cases.push_back({"Adalight can follow an interrupted TPM2 prefix", [] {
    feed({0xC9,'A','d','a',0,0,0x55,11,22,33});
    CHECK(strip.shows==1 && strip.shown[0]==RGBW32(11,22,33,0));
  }});
  cases.push_back({"failed Adalight checksum can contain the next valid frame prefix", [] {
    feed({'A','d','a',0,0,'A','d','a',0,0,0x55,11,22,33});
    CHECK(strip.shows==1 && strip.shown[0]==RGBW32(11,22,33,0));
  }});
  cases.push_back({"valid Adalight checksum equal to A is still treated as a checksum", [] {
    std::vector<byte> bytes={'A','d','a',0,20,0x41};
    for (unsigned i=0;i<21;i++) bytes.insert(bytes.end(),{11,22,33});
    feed(bytes); CHECK(strip.shows==1 && strip.shown[0]==RGBW32(11,22,33,0));
  }});
  cases.push_back({"TPM2 ping acknowledgement remains intact during header recovery", [] {
    feed({0xC9,0xAA,0xC9,0xDA,0,3,11,22,33,0x36});
    CHECK(Serial.output==std::vector<byte>{0xAC});
    CHECK(strip.shows==1 && strip.shown[0]==RGBW32(11,22,33,0));
  }});
  cases.push_back({"first main-segment serial frame preserves the previous scene until complete", [] {
    useMainSegmentOnly=true; std::fill(strip.main.colors.begin(),strip.main.colors.end(),RGBW32(100,100,0,0));
    auto before=strip.main.colors;
    feed({'A','d','a',0,1,0x54}); strip.show(); auto during=strip.shown;
    feed({11,22,33,44,55,66});
    CHECK(during==before);
    CHECK(strip.shown[0]==RGBW32(11,22,33,0) && strip.shown[1]==RGBW32(44,55,66,0));
  }});
  cases.push_back({"ongoing main-segment serial frame cannot expose a partial update", [] {
    useMainSegmentOnly=true; realtimeMode=REALTIME_MODE_ADALIGHT; strip.main.freeze=true;
    std::fill(strip.main.colors.begin(),strip.main.colors.end(),RGBW32(100,100,0,0));
    auto before=strip.main.colors;
    feed({'A','d','a',0,1,0x54,11,22,33}); strip.show(); auto during=strip.shown;
    feed({44,55,66});
    CHECK(during==before);
    CHECK(strip.shown[0]==RGBW32(11,22,33,0) && strip.shown[1]==RGBW32(44,55,66,0));
  }});
  cases.push_back({"long serial frames remain intact while realtime maintenance runs", [] {
    fakeTime=100; feed({'A','d','a',0,2,0x57,11});
    for (byte b : {22,33,44,55,66,77,88,99}) {
      fakeTime+=700; feed({b}); checkRealtimeMaintenance();
    }
    CHECK(strip.shown[0]==RGBW32(11,22,33,0) && strip.shown[2]==RGBW32(77,88,99,0));
  }});
  cases.push_back({"invalid TPM2 footer cannot publish its candidate RGB data", [] {
    realtimeMode=REALTIME_MODE_ADALIGHT; strip.colors[0]=RGBW32(100,100,0,0);
    feed({0xC9,0xDA,0,3,11,22,33,0x00});
    CHECK(strip.shows==0 && strip.colors[0]==RGBW32(100,100,0,0));
  }});
  cases.push_back({"serial candidate storage is bounded and reused across frames", [] {
    for (unsigned frame=0;frame<100;frame++) feed({'A','d','a',0,0,0x55,11,22,33});
    CHECK(serialAllocations==1 && serialFrees==0 && lastSerialAllocationSize==4);
    CHECK(strip.shows==100);
  }});
  cases.push_back({"maximum Adalight count allocates only the visible canvas", [] {
    std::vector<byte> bytes={'A','d','a',255,255,0x55};
    for (unsigned i=0;i<65536;i++) bytes.insert(bytes.end(),{11,22,33});
    feed(bytes); CHECK(lastSerialAllocationSize==8*sizeof(uint32_t) && serialAllocations==1);
    CHECK(strip.shows==1 && strip.shown[7]==RGBW32(11,22,33,0));
  }});
  cases.push_back({"positive serial offset stages only its clipped visible span", [] {
    arlsOffset=7; feed({'A','d','a',0,1,0x54,11,22,33,44,55,66});
    CHECK(lastSerialAllocationSize==4 && strip.shown[7]==RGBW32(11,22,33,0));
    CHECK(strip.shown[0]==0);
  }});
  cases.push_back({"negative serial offset drops leading pixels before staging", [] {
    arlsOffset=-1; feed({'A','d','a',0,1,0x54,11,22,33,44,55,66});
    CHECK(lastSerialAllocationSize==4 && strip.shown[0]==RGBW32(44,55,66,0));
    CHECK(strip.shown[1]==0);
  }});
  cases.push_back({"fully clipped serial frames need no candidate allocation", [] {
    arlsOffset=-10; feed({'A','d','a',0,0,0x55,11,22,33});
    CHECK(serialAllocations==0 && strip.shows==1 && strip.shown[0]==0);
  }});
  cases.push_back({"serial allocation failure writes frames directly and recovers", [] {
    strip.colors[0]=RGBW32(100,100,0,0); failSerialAllocation=true;
    feed({'A','d','a',0,0,0x55,11,22,33});
    CHECK(errorFlag==ERR_NORAM_PX && strip.shows==1 && realtimeMode==REALTIME_MODE_ADALIGHT);
    CHECK(strip.shown[0]==RGBW32(11,22,33,0));
    failSerialAllocation=false; feed({'A','d','a',0,0,0x55,44,55,66});
    CHECK(strip.shows==2 && strip.shown[0]==RGBW32(44,55,66,0));
  }});
  cases.push_back({"direct serial fallback clips offsets and honors realtime override", [] {
    failSerialAllocation=true; arlsOffset=-1;
    feed({'A','d','a',0,1,0x54,11,22,33,44,55,66});
    CHECK(strip.shows==1 && strip.shown[0]==RGBW32(44,55,66,0) && strip.shown[1]==0);
    realtimeOverride=REALTIME_OVERRIDE_ALWAYS; arlsOffset=0;
    feed({'A','d','a',0,0,0x55,77,88,99});
    CHECK(strip.shows==1 && strip.colors[0]==RGBW32(44,55,66,0));
    realtimeOverride=REALTIME_OVERRIDE_NONE;
  }});
  cases.push_back({"direct serial fallback discards nothing on a bad TPM2 footer", [] {
    failSerialAllocation=true;
    feed({0xC9,0xDA,0,3,11,22,33,0x00});
    CHECK(strip.shows==0 && strip.colors[0]==RGBW32(11,22,33,0)); // written but not shown
    feed({0xC9,0xDA,0,3,44,55,66,0x36});
    CHECK(strip.shows==1 && strip.shown[0]==RGBW32(44,55,66,0));
  }});
  cases.push_back({"idle serial input releases candidate storage without repainting", [] {
    feed({'A','d','a',0,0,0x55,11,22,33}); auto before=strip.shown;
    fakeTime+=1001; handleSerial();
    CHECK(serialFrees==1 && strip.shows==1 && strip.shown==before);
    feed({'A','d','a',0,0,0x55,44,55,66});
    CHECK(serialAllocations==2 && strip.shows==2);
  }});
  cases.push_back({"interrupted serial candidates abort without changing live pixels", [] {
    useMainSegmentOnly=true; realtimeMode=REALTIME_MODE_ADALIGHT; strip.main.colors[0]=RGBW32(100,100,0,0);
    feed({'A','d','a',0,1,0x54,11,22,33}); fakeTime+=1001; handleSerial();
    CHECK(serialFrees==1 && strip.main.colors[0]==RGBW32(100,100,0,0));
    feed({'A','d','a',0,0,0x55,44,55,66});
    CHECK(strip.shows==1 && strip.shown[0]==RGBW32(44,55,66,0));
  }});
  cases.push_back({"USB serial disconnection aborts and frees a partial candidate", [] {
    strip.colors[0]=RGBW32(100,100,0,0); feed({'A','d','a',0,1,0x54,11,22,33});
    Serial.connected=false; handleSerial();
    CHECK(serialFrees==1 && strip.colors[0]==RGBW32(100,100,0,0));
    Serial.connected=true; feed({'A','d','a',0,0,0x55,44,55,66});
    CHECK(strip.shows==1 && strip.shown[0]==RGBW32(44,55,66,0));
  }});
  cases.push_back({"serial disabled during a frame releases candidate storage", [] {
    feed({'A','d','a',0,1,0x54,11,22,33}); serialCanRX=false; handleSerial();
    CHECK(serialFrees==1 && strip.shows==0);
    serialCanRX=true; feed({'A','d','a',0,0,0x55,44,55,66});
    CHECK(strip.shows==1 && strip.shown[0]==RGBW32(44,55,66,0));
  }});
  cases.push_back({"signed offset changes reject an in-progress serial candidate", [] {
    feed({'A','d','a',0,1,0x54,11,22,33}); arlsOffset=1; feed({44,55,66});
    CHECK(strip.shows==0 && realtimeMode==REALTIME_MODE_INACTIVE && strip.colors[1]==0);
    feed({'A','d','a',0,0,0x55,44,55,66}); CHECK(strip.shown[1]==RGBW32(44,55,66,0));
  }});
  cases.push_back({"canvas changes reject an in-progress serial candidate", [] {
    feed({'A','d','a',0,1,0x54,11,22,33}); strip.resizePixels(4); feed({44,55,66});
    CHECK(strip.shows==0 && realtimeMode==REALTIME_MODE_INACTIVE);
    feed({'A','d','a',0,0,0x55,44,55,66}); CHECK(strip.shown[0]==RGBW32(44,55,66,0));
  }});
  cases.push_back({"changing the realtime target rejects an in-progress serial candidate", [] {
    feed({'A','d','a',0,1,0x54,11,22,33}); useMainSegmentOnly=true; feed({44,55,66});
    CHECK(strip.shows==0 && strip.main.colors[0]==0);
  }});
  cases.push_back({"changing the main segment rejects an in-progress serial candidate", [] {
    useMainSegmentOnly=true; feed({'A','d','a',0,1,0x54,11,22,33});
    strip.mainSegmentId=1; strip.segmentCount=2; feed({44,55,66});
    CHECK(strip.shows==0 && strip.main.colors[0]==0);
  }});
  cases.push_back({"an absent main segment cannot crash serial candidate setup", [] {
    useMainSegmentOnly=true; strip.segmentCount=0;
    feed({'A','d','a',0,0,0x55,11,22,33});
    CHECK(errorFlag==ERR_NORAM_PX && serialAllocations==0 && strip.shows==0);
  }});
  cases.push_back({"overridden pixels preserve full RGBW values across initial takeover", [] {
    strip.colors[0]=RGBW32(0,0,0,128); realtimeOverride=REALTIME_OVERRIDE_ALWAYS;
    feed({'A','d','a',0,1,0x54,11,22,33});
    realtimeOverride=REALTIME_OVERRIDE_NONE; feed({44,55,66});
    CHECK(strip.shown[0]==RGBW32(0,0,0,128) && strip.shown[1]==RGBW32(44,55,66,0));
  }});
  cases.push_back({"override enabled before commit preserves the displayed scene", [] {
    strip.colors[0]=RGBW32(100,100,0,0); feed({'A','d','a',0,1,0x54,11,22,33});
    realtimeOverride=REALTIME_OVERRIDE_ALWAYS; feed({44,55,66});
    CHECK(strip.shows==0 && strip.colors[0]==RGBW32(100,100,0,0));
  }});
  cases.push_back({"TPM2 candidate waits for a separately received valid footer", [] {
    strip.colors[0]=RGBW32(100,100,0,0); feed({0xC9,0xDA,0,3,11,22,33});
    CHECK(strip.shows==0 && strip.colors[0]==RGBW32(100,100,0,0));
    feed({0x36}); CHECK(strip.shows==1 && strip.shown[0]==RGBW32(11,22,33,0));
  }});
  cases.push_back({"TPM2 bad footer can be the next valid Adalight prefix", [] {
    feed({0xC9,0xDA,0,3,11,22,33,'A','d','a',0,0,0x55,44,55,66});
    CHECK(strip.shows==1 && strip.shown[0]==RGBW32(44,55,66,0));
  }});
  cases.push_back({"an existing serial stream remains active across a long candidate", [] {
    feed({'A','d','a',0,0,0x55,1,2,3});
    feed({'A','d','a',0,2,0x57,11});
    for (byte b : {22,33,44,55,66,77,88,99}) {
      fakeTime+=700; feed({b}); checkRealtimeMaintenance();
      CHECK(realtimeMode==REALTIME_MODE_ADALIGHT);
    }
    CHECK(strip.shown[0]==RGBW32(11,22,33,0) && strip.shown[2]==RGBW32(77,88,99,0));
  }});
  cases.push_back({"serial staging rejects unavailable main-segment pixel storage", [] {
    useMainSegmentOnly=true; strip.main.storageAvailable=false;
    feed({'A','d','a',0,0,0x55,11,22,33});
    CHECK(serialAllocations==0 && strip.shows==0 && realtimeMode==REALTIME_MODE_INACTIVE);
  }});
  cases.push_back({"main-segment storage loss cannot publish an in-progress candidate", [] {
    useMainSegmentOnly=true; feed({'A','d','a',0,1,0x54,11,22,33});
    strip.main.storageAvailable=false; feed({44,55,66});
    CHECK(strip.shows==0 && realtimeMode==REALTIME_MODE_INACTIVE && strip.main.colors[0]==0);
  }});
  cases.push_back({"serial staging grows when needed and reuses the larger allocation", [] {
    feed({'A','d','a',0,0,0x55,11,22,33});
    feed({'A','d','a',0,2,0x57,11,22,33,44,55,66,77,88,99});
    CHECK(serialAllocations==2 && serialFrees==1 && lastSerialAllocationSize==12);
    feed({'A','d','a',0,0,0x55,44,55,66});
    CHECK(serialAllocations==2 && serialFrees==1 && strip.shows==3);
  }});
  cases.push_back({"failed serial staging growth falls back to direct writes", [] {
    feed({'A','d','a',0,0,0x55,11,22,33});
    failSerialAllocation=true;
    feed({'A','d','a',0,2,0x57,44,55,66,77,88,99,100,110,120});
    CHECK(errorFlag==ERR_NORAM_PX && strip.shows==2 && strip.shown[2]==RGBW32(100,110,120,0));
    failSerialAllocation=false; feed({'A','d','a',0,0,0x55,44,55,66});
    CHECK(strip.shows==3 && strip.shown[0]==RGBW32(44,55,66,0));
  }});
  cases.push_back({"serial frames remain atomic at every byte split boundary", [] {
    for (bool mainOnly : {false,true}) {
      for (const auto& frame : {std::vector<byte>{'A','d','a',0,1,0x54,11,22,33,44,55,66},
                              std::vector<byte>{0xC9,0xDA,0,6,11,22,33,44,55,66,0x36}}) {
        for (unsigned split=1;split<frame.size();split++) {
          reset(); useMainSegmentOnly=mainOnly;
          auto& target=mainOnly ? strip.main.colors : strip.colors;
          std::fill(target.begin(),target.end(),RGBW32(100,100,0,0)); auto before=target;
          feed(std::vector<byte>(frame.begin(),frame.begin()+split));
          CHECK(strip.shows==0 && target==before);
          feed(std::vector<byte>(frame.begin()+split,frame.end()));
          CHECK(strip.shows==1 && strip.shown[0]==RGBW32(11,22,33,0) && strip.shown[1]==RGBW32(44,55,66,0));
        }
      }
    }
  }});
  cases.push_back({"TPM2 output encodes its actual RGB payload and white addition", [] {
    strip.colors[0]=RGBW32(11,22,33,44); sendBytes();
    CHECK(Serial.output.size()==4+8*3+2);
    CHECK(Serial.output[0]==0xC9 && Serial.output[1]==0xDA && Serial.output[2]==0 && Serial.output[3]==24);
    CHECK(Serial.output[4]==55 && Serial.output[5]==66 && Serial.output[6]==77);
    CHECK(Serial.output[28]==0x36 && Serial.output[29]=='\n');
  }});
  cases.push_back({"TPM2 output at its maximum RGB length remains well framed", [] {
    strip.resizePixels(UINT16_MAX/3); sendBytes();
    unsigned payload=(unsigned(Serial.output[2])<<8)|Serial.output[3];
    CHECK(payload==UINT16_MAX && Serial.output.size()==payload+6);
    CHECK(Serial.output[4+payload]==0x36);
  }});
  cases.push_back({"TPM2 output beyond its 16-bit limit never advertises a shorter body", [] {
    strip.resizePixels(50000); sendBytes();
    unsigned payload=(unsigned(Serial.output[2])<<8)|Serial.output[3];
    CHECK(Serial.output.size()==payload+6 && payload%3==0);
    CHECK(Serial.output[4+payload]==0x36);
  }});
  cases.push_back({"TPM2 ping stays silent when the TX pin is unavailable", [] {
    serialCanTX=false; feed({0xC9,0xAA});
    CHECK(Serial.output.empty()); serialCanTX=true;
  }});
  cases.push_back({"version query stays silent when the TX pin is unavailable", [] {
    serialCanTX=false; feed({'v'});
    CHECK(Serial.output.empty() && Serial.printCalls==0); serialCanTX=true;
  }});
  cases.push_back({"serial JSON allocation errors stay silent without the TX pin", [] {
    serialCanTX=false; jsonLockAvailable=false; feed({'{'});
    CHECK(Serial.output.empty() && Serial.printfCalls==0);
  }});
  cases.push_back({"serial version and JSON error replies remain enabled with TX", [] {
    feed({'v'}); CHECK(Serial.printCalls==2 && Serial.output==std::vector<byte>{' '});
    jsonLockAvailable=false; feed({'{'}); CHECK(Serial.printfCalls==1);
  }});
  cases.push_back({"TPM2 output stays silent without the TX pin", [] {
    serialCanTX=false; sendBytes(); CHECK(Serial.output.empty());
  }});
  cases.push_back({"TPM2 readback saturates white addition instead of wrapping RGB", [] {
    strip.colors[0]=RGBW32(240,100,0,128); sendBytes();
    CHECK(Serial.output[4]==255 && Serial.output[5]==228 && Serial.output[6]==128);
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

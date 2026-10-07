'use strict';

// Compile the actual pixel/ABL function bodies against a recording LED driver.
// This exercises the firmware arithmetic and dispatch without an ESP board.
const assert = require('node:assert/strict');
const { it } = require('node:test');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

function extractFunction(source, signature) {
  const start = source.indexOf(signature);
  assert.notEqual(start, -1, `Missing production function: ${signature}`);
  const brace = source.indexOf('{', start);
  let depth = 0;
  for (let end = brace; end < source.length; end++) {
    if (source[end] === '{') depth++;
    if (source[end] === '}' && --depth === 0) return source.slice(start, end + 1);
  }
  throw new Error(`Unclosed production function: ${signature}`);
}

it('digital pixel readback and automatic brightness limiter regressions', t => {
  const compiler = process.env.CXX || 'g++';
  const probe = spawnSync(compiler, ['--version'], { encoding: 'utf8' });
  if (probe.error?.code === 'ENOENT') return t.skip('Host C++ compiler is unavailable');
  assert.equal(probe.status, 0, probe.stderr);

  const repo = process.env.WLED_ABL_TEST_SOURCE || path.resolve(__dirname, '..');
  const read = name => fs.readFileSync(path.join(repo, 'wled00', name), 'utf8');
  const bus = read('bus_manager.cpp');
  const header = read('bus_manager.h');
  const wrapper = read('bus_wrapper.h');
  const colors = read('colors.cpp');
  const constants = read('const.h');
  const fx = read('FX_fcn.cpp');
  const fxHeader = read('FX.h');

  const defines = [constants, wrapper, fxHeader].flatMap(source => source.split('\n').filter(line =>
    /^#define (?:TYPE_\w+|RGBW_MODE_\w+|AW_GLOBAL_DISABLED|COL_ORDER_\w+|I_\w+|SEG_CAPABILITY_\w+|REALTIME_MODE_\w+|REALTIME_OVERRIDE_\w+)\s/.test(line)
  )).join('\n');
  const aliases = [...wrapper.matchAll(/^#define (B_(?:32|HS|SS)_\w+)\s+NeoPixelBus/gm)].map(([, name]) => {
    let color = 'RgbColor';
    if (name.includes('_SM16825_5')) color = 'Rgbww80Color';
    else if (name.endsWith('_5')) color = 'RgbwwColor';
    else if (name.includes('_UCS_4')) color = 'Rgbw64Color';
    else if (name.includes('_UCS_3')) color = 'Rgb48Color';
    else if (name.endsWith('_4')) color = 'RgbwColor';
    return `using ${name} = FakeNeoBus<${color}>;`;
  }).join('\n');

  const digitalHeader = header.slice(header.indexOf('class BusDigital :'), header.indexOf('class BusPwm :'));
  const member = digitalHeader.match(/(?:static )?uint(?:16|32)_t _milliAmpsTotal(?:\s*=\s*0)?;/);
  assert.ok(member, 'Missing per-bus current storage');
  const currentType = header.match(/virtual (uint(?:16|32)_t) getUsedCurrent/)[1];
  const globalType = header.match(/extern (uint(?:16|32)_t) _gMilliAmpsUsed/)[1];
  const ma = [...header.matchAll(/#define MA_FOR_ESP\s+(\d+)/g)].pop(); // ESP32 fixture
  assert.ok(ma, 'Missing ESP current constant');

  let fixture = fs.readFileSync(path.join(__dirname, '../test/abl/fixture.h'), 'utf8');
  const capabilities = ['static constexpr bool hasRGB(', 'static constexpr bool hasWhite(', 'static constexpr bool hasCCT(', 'static constexpr bool  isDigital(', 'static constexpr bool  is2Pin(']
    .map(signature => extractFunction(header, signature)).join('\n');
  fixture = fixture.replace('/* COLOR_STATE */', [
    'static inline int16_t  getCCT()', 'static inline void     setCCT(',
    'static inline uint16_t getWhiteBalance()', 'static inline void     setWhiteBalance('
  ].map(signature => extractFunction(header, signature)).join('\n'));
  fixture = fixture.replace('/* CONSTANTS */', `${defines}\n#define MA_FOR_ESP ${ma[1]}`)
    .replace('/* DRIVER_ALIASES */', aliases)
    .replace('/* CAPABILITIES */', capabilities)
    .replace('GLOBAL_CURRENT_TYPE', globalType)
    .replaceAll('CURRENT_TYPE', currentType)
    .replace('/* CURRENT_MEMBER */', member[0]);

  let poly = `class PolyBus { public:
    inline static bool _useParallelI2S = false;
    inline static uint8_t _rmtChannelsAssigned=0, _rmtChannel=0, _i2sChannelsAssigned=0, _parallelBusItype=0, _2PchannelsAssigned=0;
  `;
  poly += extractFunction(wrapper, 'static void setPixelColor(') + '\n';
  if (wrapper.includes('static RgbwColor getRgbwwColor(')) {
    poly += extractFunction(wrapper, 'static RgbwColor getRgbwwColor(const RgbwwColor&') + '\n';
    poly += extractFunction(wrapper, 'static RgbwColor getRgbwwColor(const Rgbww80Color&') + '\n';
  }
  poly += extractFunction(wrapper, 'static uint32_t getPixelColor(') + '\n';
  poly += extractFunction(wrapper, 'static void resetChannelTracking(') + '\n';
  poly += extractFunction(wrapper, 'static uint8_t getI(') + '\n};\n';
  fixture = fixture.replace('/* POLYBUS */', poly);

  let source = fixture + '\n';
  source += extractFunction(header, 'virtual void     setPixelColorCCT(')
    .replace('virtual void     setPixelColorCCT(', 'void Bus::setPixelColorCCT(') + '\n';
  source += extractFunction(colors, 'uint16_t approximateKelvinFromRGB(') + '\n';
  source += extractFunction(colors, 'void colorKtoRGB(') + '\n';
  source += extractFunction(colors, 'uint32_t colorBalanceFromKelvin(') + '\n';
  source += extractFunction(colors, 'uint32_t IRAM_ATTR color_fade(') + '\n';
  for (const signature of ['void Bus::calculateCCT(', 'uint32_t Bus::autoWhiteCalc(',
    'void BusDigital::estimateCurrent(', 'void BusDigital::applyBriLimit(',
    'void BusManager::setSegmentCCT(', 'void IRAM_ATTR BusManager::setPixelColor(',
    'void IRAM_ATTR BusDigital::setPixelColor(', 'void IRAM_ATTR BusDigital::setPixelColorCCT(', 'void IRAM_ATTR BusDigital::setPixelColorInternal(',
    'void BusPwm::setPixelColor(', 'void BusPwm::setPixelColorCCT(', 'void BusPwm::setPixelColorInternal(',
    'void BusNetwork::setPixelColor(', 'void BusManager::initializeABL(', 'void BusManager::applyABL(']) {
    source += extractFunction(bus, signature) + '\n';
  }
  source += extractFunction(fx, 'bool WS2812FX::hasRGBWBus(') + '\n';
  source += extractFunction(fx, 'bool WS2812FX::hasCCTBus(') + '\n';
  source += extractFunction(fx, 'bool WS2812FX::checkSegmentAlignment(') + '\n';
  source += extractFunction(fx, 'void Segment::refreshLightCapabilities(') + '\n';
  const initializationStart = fx.indexOf('  _length = 0;', fx.indexOf('void WS2812FX::finalizeInit('));
  const initializationEnd = fx.indexOf('  BusManager::initializeABL();', initializationStart);
  assert.ok(initializationStart > 0 && initializationEnd > initializationStart);
  source += 'void WS2812FX::initializeOutputs() {\n' + fx.slice(initializationStart, initializationEnd) + '\n}\n';
  if (fx.includes('WS2812FX::blendPixelCCT(')) {
    source += extractFunction(fx, 'void WLED_O2_ATTR WS2812FX::blendPixelCCT(') + '\n';
  } else {
    source += 'void WS2812FX::blendPixelCCT(size_t pixel, uint32_t, uint8_t, uint8_t, uint8_t cct) const { if (_pixelCCT) _pixelCCT[pixel]=cct; }\n';
  }
  if (fx.includes('bool WS2812FX::updateCCTBuffer(')) {
    source += extractFunction(fx, 'bool WS2812FX::updateCCTBuffer(') + '\n';
    source += 'void WS2812FX::finishFrame() {}\n';
  } else {
    const showStart = fx.indexOf('void WS2812FX::show(');
    const allocationStart = fx.indexOf('  if ((hasCCTBus()', showStart);
    const allocationEnd = fx.indexOf('  if (realtimeMode', allocationStart);
    assert.ok(allocationStart > showStart && allocationEnd > allocationStart);
    source += 'bool WS2812FX::updateCCTBuffer() { size_t totalLen=getLengthTotal();\n';
    source += fx.slice(allocationStart, allocationEnd) + '\nreturn true; }\n';
    source += 'void WS2812FX::finishFrame() { p_free(_pixelCCT); _pixelCCT=nullptr; }\n';
  }
  if (member[0].startsWith('static ')) {
    source += `${currentType} BusDigital::_milliAmpsTotal = 0;\n`;
  }
  const paintStart = fx.indexOf('  int oldCCT =', fx.indexOf('void WS2812FX::show('));
  const paintEnd = fx.indexOf('  BusManager::show();', paintStart);
  assert.ok(paintStart >= 0 && paintEnd > paintStart);
  source += 'void WS2812FX::paintFrame() { size_t totalLen=getLengthTotal();\n' + fx.slice(paintStart, paintEnd) + '}\n';
  source += fs.readFileSync(path.join(__dirname, '../test/abl/cases.cpp'), 'utf8');

  const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-abl-'));
  try {
    const cpp = path.join(temp, 'abl.cpp');
    const binary = path.join(temp, 'abl-test');
    fs.writeFileSync(cpp, source);
    const sanitize = process.env.WLED_ABL_SANITIZE === '1' ? ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] : [];
    const build = spawnSync(compiler, ['-std=c++17', '-O0', '-Wall', '-Wextra', '-Wno-misleading-indentation', ...sanitize, cpp, '-o', binary], { encoding: 'utf8' });
    assert.equal(build.status, 0, `${build.stdout}\n${build.stderr}`);
    const run = spawnSync(binary, [], { encoding: 'utf8' });
    t.diagnostic(run.stdout.trim().split('\n').at(-1));
    assert.equal(run.status, 0, `${run.stdout}\n${run.stderr}`);
  } finally {
    fs.rmSync(temp, { recursive: true, force: true });
  }
});

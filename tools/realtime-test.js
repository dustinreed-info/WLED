'use strict';
const assert = require('node:assert/strict');
const { it } = require('node:test');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { spawnSync } = require('node:child_process');

function extract(source, signature) {
  const start = source.indexOf(signature);
  assert.ok(start >= 0, `Missing production function ${signature}`);
  const brace = source.indexOf('{', start);
  let depth = 0;
  let quote = '', comment = '';
  for (let end = brace; end < source.length; end++) {
    const char = source[end], next = source[end + 1];
    if (comment === 'line') { if (char === '\n') comment = ''; continue; }
    if (comment === 'block') { if (char === '*' && next === '/') { comment = ''; end++; } continue; }
    if (quote) {
      if (char === '\\') end++;
      else if (char === quote) quote = '';
      continue;
    }
    if (char === '/' && next === '/') { comment = 'line'; end++; continue; }
    if (char === '/' && next === '*') { comment = 'block'; end++; continue; }
    if (char === '"' || char === "'") { quote = char; continue; }
    if (source[end] === '{') depth++;
    if (source[end] === '}' && --depth === 0) return source.slice(start, end + 1);
  }
  throw new Error(`Unclosed production function ${signature}`);
}

it('realtime DDP and serial Ambilight packet regressions', t => {
  const compiler = process.env.CXX || 'g++';
  const probe = spawnSync(compiler, ['--version'], { encoding: 'utf8' });
  if (probe.error?.code === 'ENOENT') return t.skip('Host C++ compiler is unavailable');
  assert.equal(probe.status, 0, probe.stderr);
  const root = process.env.WLED_REALTIME_TEST_SOURCE || path.resolve(__dirname, '..');
  const read = name => fs.readFileSync(path.join(root, 'wled00', name), 'utf8');
  const udp = read('udp.cpp'), ddp = read('e131.cpp'), serial = read('wled_serial.cpp');
  const fx = read('FX_fcn.cpp'), fxHeader = read('FX.h');
  const stripHeader = fxHeader.slice(fxHeader.indexOf('class WS2812FX {'));
  assert.ok(stripHeader.startsWith('class WS2812FX {'));
  const constants = read('const.h'), e131 = read('src/dependencies/e131/ESPAsyncE131.h'), functions = read('fcn_declare.h');
  const defines = [constants, e131, functions].flatMap(source => source.split('\n').filter(line =>
    /^#define (?:DDP_\w+|REALTIME_MODE_\w+|REALTIME_OVERRIDE_\w+|CALL_MODE_\w+|BFRALLOC_\w+|ERR_NORAM_PX)\s/.test(line)
  )).join('\n');
  let source = fs.readFileSync(path.join(__dirname, '../test/realtime/fixture.h'), 'utf8');
  source = source.replace('/* CONSTANTS */', defines);
  for (const [signature, qualified] of [
    ['inline void setPixelColor(unsigned n, uint32_t c) const', 'void WS2812FX::setPixelColor(unsigned n, uint32_t c) const'],
    ['inline void fill(uint32_t c) const', 'void WS2812FX::fill(uint32_t c) const'],
    ['inline uint16_t getMappedPixelIndex(', 'uint16_t WS2812FX::getMappedPixelIndex('],
    ['inline uint32_t getPixelColor(unsigned n) const', 'uint32_t WS2812FX::getPixelColor(unsigned n) const'],
    ['inline uint32_t getPixelColorNoMap(', 'uint32_t WS2812FX::getPixelColorNoMap(']
  ]) source += extract(stripHeader, signature).replace(signature, qualified) + '\n';
  source += extract(fx, 'void WS2812FX::setRealtimePixelColor(') + '\n';
  source += extract(fx, 'uint32_t WS2812FX::getRealtimePixelColor(') + '\n';
  source += extract(udp, 'void realtimeLock(') + '\n';
  source += extract(udp, 'void exitRealtime(') + '\n';
  const maintenanceStart = udp.indexOf('  if (e131NewData', udp.indexOf('void handleNotifications('));
  const maintenanceEnd = udp.indexOf('  //receive UDP notifications', maintenanceStart);
  assert.ok(maintenanceStart >= 0 && maintenanceEnd > maintenanceStart);
  source += 'static void checkRealtimeMaintenance() {\n' + udp.slice(maintenanceStart, maintenanceEnd) + '}\n';
  if (udp.includes('static bool mapRealtimePixel(')) source += extract(udp, 'static bool mapRealtimePixel(') + '\n';
  source += extract(udp, 'void setRealtimePixel(') + '\n';
  if (udp.includes('uint32_t getRealtimePixel(')) source += extract(udp, 'uint32_t getRealtimePixel(') + '\n';
  source += extract(ddp, 'static void handleDDPPacket(e131_packet_t* p, size_t packetLen) {') + '\n';
  source += extract(serial, 'enum class AdaState') + ';\n';
  source += serial.split('\n').filter(line => /^static constexpr .* TPM2_/.test(line)).join('\n') + '\n';
  if (serial.includes('static AdaState getSerialHeaderState(')) source += extract(serial, 'static AdaState getSerialHeaderState(') + '\n';
  if (serial.includes('class SerialFrameBuffer {')) source += extract(serial, 'class SerialFrameBuffer {') + ';\nstatic SerialFrameBuffer serialFrame;\n';
  source += 'static bool continuousSendLED=false; static uint32_t lastUpdate=0;\n';
  source += extract(serial, 'static void sendBytes(){') + '\n';
  source += extract(serial, 'void handleSerial(') + '\n';
  source += fs.readFileSync(path.join(__dirname, '../test/realtime/cases.cpp'), 'utf8');
  const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-realtime-'));
  try {
    const cpp = path.join(temp, 'realtime.cpp'), binary = path.join(temp, 'test');
    fs.writeFileSync(cpp, source);
    const sanitize = process.env.WLED_REALTIME_SANITIZE === '1' ? ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] : [];
    const build = spawnSync(compiler, ['-std=c++17', '-O0', '-Wall', '-Wextra', ...sanitize, cpp, '-o', binary], { encoding: 'utf8' });
    assert.equal(build.status, 0, `${build.stdout}\n${build.stderr}`);
    const run = spawnSync(binary, [], { encoding: 'utf8' });
    t.diagnostic(run.stdout.trim().split('\n').at(-1));
    assert.equal(run.status, 0, `${run.stdout}\n${run.stderr}`);
  } finally {
    fs.rmSync(temp, { recursive: true, force: true });
  }
});

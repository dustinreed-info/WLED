'use strict';
const assert = require('node:assert/strict');
const { it } = require('node:test');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { spawnSync } = require('node:child_process');

function extract(source, signature) {
  const start = source.indexOf(signature);
  assert.ok(start >= 0, 'Missing production function ' + signature);
  const brace = source.indexOf('{', start);
  let depth = 0;
  for (let end = brace; end < source.length; end++) {
    if (source[end] === '{') depth++;
    if (source[end] === '}' && --depth === 0) return source.slice(start, end + 1);
  }
  throw new Error('Unclosed production function ' + signature);
}

function section(source, first, last) {
  const start = source.indexOf(first);
  const end = source.indexOf(last, start);
  assert.ok(start >= 0 && end > start, 'Missing production config block ' + first);
  return source.slice(start, end);
}

it('partial configuration updates preserve omitted settings', t => {
  const compiler = process.env.CXX || 'g++';
  const probe = spawnSync(compiler, ['--version'], { encoding: 'utf8' });
  if (probe.error?.code === 'ENOENT') return t.skip('Host C++ compiler is unavailable');
  assert.equal(probe.status, 0, probe.stderr);
  const root = process.env.WLED_CONFIG_TEST_SOURCE || path.resolve(__dirname, '..');
  const read = name => fs.readFileSync(path.join(root, 'wled00', name), 'utf8');
  const cfg = read('cfg.cpp'), header = read('bus_manager.h'), fx = read('FX_fcn.cpp');
  const fxHeader = read('FX.h'), wled = read('wled.h');
  const defines = [read('const.h'), fxHeader].flatMap(source => source.split('\n').filter(line =>
    /^#define (?:RGBW_MODE_\w+|AW_GLOBAL_DISABLED|WLED_FPS|MIN_FRAME_DELAY)\s/.test(line)
  )).join('\n');
  let source = fs.readFileSync(path.join(__dirname, '../test/config/fixture.h'), 'utf8');
  source = source.replace('CONFIG_JSON_HEADER', JSON.stringify(path.join(root, 'wled00/src/dependencies/json/ArduinoJson-v6.h')))
    .replace('/* CONSTANTS */', defines);
  const minDelay = [...fxHeader.matchAll(/^\s*#define MIN_FRAME_DELAY\s+(\d+)/gm)].pop();
  assert.ok(minDelay, 'Missing minimum frame delay');
  source = source.replace('/* FRAME_DELAY */', '#define MIN_FRAME_DELAY ' + minDelay[1]);
  source = source.replace('/* BUS_STATE */', [
    'static inline void     setGlobalAWMode(', 'static inline uint8_t  getGlobalAWMode()',
    'static inline int8_t   getCCTBlend()', 'static inline void     setCCTBlend('
  ].map(signature => extract(header, signature)).join('\n'));
  source += extract(cfg, 'static inline void getStringFromJson(') + '\n';
  source += extract(fx, 'void WS2812FX::setTargetFps(') + '\n';
  source += 'void readLedConfig(JsonObject doc) { JsonObject hw=doc["hw"];\n' +
    section(cfg, '  JsonObject hw_led = hw["led"];', '  #ifndef WLED_DISABLE_2D') + '\n}\n';
  source += 'void readLighting(JsonObject doc) {\n' +
    section(cfg, '  JsonObject light = doc[F("light")];', '  JsonObject light_tr =') + '\n}\n';
  source += 'void readRemotes(JsonObject doc) { JsonObject nw=doc["nw"];\n' +
    section(cfg, '  CJSON(enableESPNow, nw[F("espnow")]);', '#endif') + '\n}\n';
  source += fs.readFileSync(path.join(__dirname, '../test/config/cases.cpp'), 'utf8');
  for (const [name, marker] of [['gammaCorrectVal', 'DEFAULT_GAMMA'], ['gammaCorrectCol', 'DEFAULT_COLOR_GAMMA'], ['gammaCorrectBri', 'DEFAULT_BRIGHTNESS_GAMMA']]) {
    const match = wled.match(new RegExp('WLED_GLOBAL (?:float|bool) ' + name + '\\s+_INIT\\(([^)]+)\\)'));
    assert.ok(match, 'Missing compiled default ' + name);
    source = source.replaceAll(marker, match[1]);
  }
  const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'wled-config-'));
  try {
    const cpp = path.join(temp, 'config.cpp'), binary = path.join(temp, 'config-test');
    fs.writeFileSync(cpp, source);
    const sanitize = process.env.WLED_CONFIG_SANITIZE === '1' ? ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] : [];
    const build = spawnSync(compiler, ['-std=c++17', '-O0', '-Wall', '-Wextra', ...sanitize, cpp, '-o', binary], { encoding: 'utf8' });
    assert.equal(build.status, 0, build.stdout + '\n' + build.stderr);
    const run = spawnSync(binary, [], { encoding: 'utf8' });
    t.diagnostic(run.stdout.trim().split('\n').at(-1));
    assert.equal(run.status, 0, run.stdout + '\n' + run.stderr);
  } finally {
    fs.rmSync(temp, { recursive: true, force: true });
  }
});

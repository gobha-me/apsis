// Run the production module unchanged. Only browser I/O and the clock are adapters.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';

const viewerURL = new URL('../tools/viewer/viewer.mjs', import.meta.url);
const html = await readFile(new URL('../tools/viewer/index.html', import.meta.url), 'utf8');
let instance = 0;

class Element {
  constructor(tag = 'div') {
    this.tagName = tag;
    this.children = [];
    this.listeners = new Map();
    this.text = '';
    this.hidden = false;
    this.disabled = false;
    this.value = '';
    this.className = '';
    this.files = [];
  }

  set textContent(value) {
    this.text = String(value);
    this.children = [];
  }

  get textContent() {
    return this.text + this.children.map((child) => child.textContent).join('');
  }

  append(...children) {
    for (const child of children) {
      if (child.tagName === 'fragment') this.children.push(...child.children);
      else this.children.push(child);
    }
  }

  replaceChildren(...children) {
    this.text = '';
    this.children = [];
    this.append(...children);
  }

  addEventListener(type, listener) {
    assert.equal(this.listeners.has(type), false, `duplicate ${type} listener`);
    this.listeners.set(type, listener);
  }

  dispatch(type) {
    assert.ok(this.listeners.has(type), `missing ${type} listener`);
    this.listeners.get(type)({ target: this });
  }

  click() {
    if (!this.disabled) this.dispatch('click');
  }
}

// Allow loadRun's awaited fetch/text/file promises to finish without real timers.
const settle = async () => {
  await new Promise(setImmediate);
};

async function openViewer(text, { status = 200, fetchError = null, contentLength = null } = {}) {
  const elements = new Map();
  const controls = [];
  for (const match of html.matchAll(/<([a-z][a-z0-9]*)\b([^>]*)>/g)) {
    const [, tag, attributes] = match;
    const id = attributes.match(/\bid="([^"]+)"/);
    if (!id) continue;
    const element = new Element(tag);
    element.hidden = /\bhidden\b/.test(attributes);
    element.disabled = /\bdisabled\b/.test(attributes);
    elements.set(id[1], element);
    if (/\bdata-run-control\b/.test(attributes)) controls.push(element);
  }
  elements.get('speed').value = '10';
  const frameCallbacks = new Map();
  const requests = [];
  let nextFrame = 0;
  globalThis.document = {
    getElementById(id) {
      assert.ok(elements.has(id), `unknown production DOM id ${id}`);
      return elements.get(id);
    },
    querySelectorAll(selector) {
      assert.equal(selector, '[data-run-control]');
      return controls;
    },
    createDocumentFragment: () => new Element('fragment'),
    createElement: (tag) => new Element(tag),
  };
  globalThis.fetch = async (url, options) => {
    requests.push({ url, options });
    if (fetchError) throw fetchError;
    return {
      ok: status >= 200 && status < 300, status,
      headers: { get: (name) => name === 'content-length' ? contentLength : null },
      text: async () => text,
    };
  };
  globalThis.requestAnimationFrame = (callback) => {
    frameCallbacks.set(++nextFrame, callback);
    return nextFrame;
  };
  globalThis.cancelAnimationFrame = (id) => frameCallbacks.delete(id);
  await import(`${viewerURL.href}?testInstance=${++instance}`);
  await settle();
  const get = (id) => elements.get(id);
  return {
    get,
    controls,
    requests,
    callbacks: frameCallbacks,
    state() {
      const pairs = get('state').children;
      return Object.fromEntries(pairs.filter((_, i) => i % 2 === 0)
        .map((term, i) => [term.textContent, pairs[i * 2 + 1].textContent]));
    },
    timeline() {
      return get('timeline').children.map((item) => item.children.map((child) => child.textContent));
    },
    tick(timestamp) {
      const callbacks = [...frameCallbacks.values()];
      frameCallbacks.clear();
      for (const callback of callbacks) callback(timestamp);
    },
    seek(position) {
      get('position').value = String(position);
      get('position').dispatch('input');
    },
    async file(source, name = 'saved-run.json', size = Buffer.byteLength(source)) {
      get('run-file').files = [{ name, size, text: async () => source }];
      get('run-file').dispatch('change');
      await settle();
    },
  };
}

function sampleRun() {
  const pilot = {
    alive: false, at: -1, system: -1, busyUntil: 0, tripActive: false,
    tripDestination: -1, end: 0, endDay: -1, wallet: '0', fuel: '0',
    strandings: '0', debt: '0',
  };
  return withEvidence({
    schema: 'apsis-viewer-v1',
    metadata: { adapterRevision: 'test-fixture', historyGap: 'Daily observations only; no rescue trajectory.' },
    run: { seed: 42, majors: 3, startDay: 10, horizon: 3, station: 0, settings: {}, inputs: [] },
    frames: [
      { day: 10, war: { active: false, hostilePairs: 0, pairs: 3 }, pilot: { ...pilot } },
      { day: 11, war: { active: true, hostilePairs: 2, pairs: 3 }, pilot: { ...pilot, alive: true, at: 0, system: 1, wallet: '9007199254740993' } },
      { day: 12, war: { active: true, hostilePairs: 2, pairs: 3 }, pilot: { ...pilot, alive: true, at: 2, system: 3, busyUntil: 15, strandings: '2' } },
      { day: 13, war: { active: false, hostilePairs: 1, pairs: 3 }, pilot: { ...pilot, alive: true, at: 2, system: 3, busyUntil: 15, strandings: '2' } },
    ],
    timeline: [
      { day: 11, type: 'general-war', source: 'derived', active: true, hostilePairs: 2, pairs: 3 },
      { day: 12, type: 'stranding', source: 'derived', delta: '2', total: '2' },
      { day: 12, type: 'pilot-event', source: 'Pilot.ev', kind: 5, arg: '9007199254740995' },
      { day: 13, type: 'general-war', source: 'derived', active: false, hostilePairs: 1, pairs: 3 },
    ],
  });
}

// Test-owned evidence for synthetic playback data; the real loader verifies it.
// Field insertion order here is the exporter frame() order used by sampleRun().
function hashBytes(bytes) {
  let first = 0x811c9dc5;
  let second = 0x1b873593;
  for (const byte of bytes) {
    first = Math.imul(first ^ byte, 0x01000193) >>> 0;
    second = Math.imul(second ^ byte, 0x5bd1e995) >>> 0;
  }
  return [first, second].map((lane) => lane.toString(16).padStart(8, '0')).join('');
}

function withEvidence(run) {
  const checkpoint = Buffer.from('apsis synthetic checkpoint fixture v1', 'utf8');
  run.finalCheckpoint = checkpoint.toString('hex');
  run.finalStateHash = hashBytes(checkpoint);
  run.framesHash = hashBytes(Buffer.from(
    `apsis-viewer-frames-v1\n${run.finalStateHash}\n${JSON.stringify(run.frames)}`, 'utf8',
  ));
  return run;
}

async function recordedRun() {
  const source = await readFile(new URL('fixtures/viewer/recorded-run.json', import.meta.url), 'utf8');
  const run = JSON.parse(source);
  assert.equal(run.finalStateHash, '2e41d7a6ad443e92');
  assert.match(run.finalCheckpoint, /^(?:[0-9a-f]{2})+$/);
  assert.equal(run.framesHash, '1b7f7ef382e6bf47');
  // A fixture/import/validation failure must fail before the tampering step.
  const page = await openViewer(source);
  assert.equal(page.get('run').hidden, false, page.get('status').textContent);
  assert.equal(page.get('status').className, '', page.get('status').textContent);
  assert.equal(page.get('hash').textContent, run.finalStateHash);
  assert.equal(page.get('step').disabled, false);
  return run;
}

function assertRejected(page, message) {
  assert.equal(page.get('run').hidden, true);
  assert.equal(page.get('status').className, 'error');
  assert.match(page.get('status').textContent, message);
  assert.match(page.get('status').textContent, /Generate run.json/);
  assert.ok(page.controls.every((control) => control.disabled));
  assert.equal(page.get('hash').textContent, '');
  assert.deepEqual(page.state(), {});
  assert.deepEqual(page.timeline(), []);
  assert.equal(page.callbacks.size, 0);
}

if (process.argv.includes('--hash-mismatch')) {
  await test('reject a tampered final checkpoint hash', async () => {
    const run = await recordedRun();
    run.finalStateHash = '0000000000000000';
    const page = await openViewer(JSON.stringify(run));
    assertRejected(page, /hash|checkpoint/i);
  });
} else if (process.argv.includes('--frame-mismatch')) {
  await test('reject an edited recorded wallet with unchanged checkpoint and hashes', async () => {
    const run = await recordedRun();
    run.frames[5].pilot.wallet = '999999999999';
    const page = await openViewer(JSON.stringify(run));
    assertRejected(page, /frames.*hash|framesHash/i);
  });
} else {
  await test('reloading and reparsing a recorded export preserves full timeline and checkpoint hash', async () => {
    const source = await readFile(new URL('fixtures/viewer/recorded-run.json', import.meta.url), 'utf8');
    const run = JSON.parse(source);
    const page = await openViewer(source);
    assert.equal(page.get('status').className, '', page.get('status').textContent);
    assert.equal(page.get('hash').textContent, '2e41d7a6ad443e92');
    assert.equal(run.run.seed, 333860);
    assert.equal(run.run.startDay, 9000);
    assert.equal(run.run.horizon, 720);
    page.seek(run.run.horizon);
    const timeline = page.timeline();
    const state = page.state();
    const record = page.get('record').textContent;
    assert.equal(timeline.length, run.timeline.length);
    page.get('reload').click();
    await settle();
    assert.equal(page.get('position').value, '0');
    assert.equal(page.get('play').textContent, 'Play');
    assert.equal(page.get('hash').textContent, run.finalStateHash);
    page.seek(run.run.horizon);
    assert.deepEqual(page.timeline(), timeline);
    assert.deepEqual(page.state(), state);
    assert.equal(page.get('record').textContent, record);
    assert.deepEqual(page.requests, [
      { url: 'run.json', options: { cache: 'no-store' } },
      { url: 'run.json', options: { cache: 'no-store' } },
    ]);
    await page.file(JSON.stringify(JSON.parse(source)));
    page.seek(run.run.horizon);
    assert.deepEqual(page.timeline(), timeline);
    assert.deepEqual(page.state(), state);
    assert.equal(page.get('hash').textContent, run.finalStateHash);
    page.get('reload').click();
    await settle();
    assert.equal(page.get('position').value, '0');
    assert.equal(page.requests.length, 2, 'selected-file reload must reuse that file');
  });

  await test('derived observations and actual rescue events retain their separate sources and prices', async () => {
    const page = await openViewer(JSON.stringify(sampleRun()));
    assert.equal(page.get('timeline-count').textContent, '0 / 4 entries');
    assert.equal(page.get('empty-timeline').hidden, false);
    assert.match(page.get('empty-timeline').textContent, /No entries through this day/);
    page.get('step').click();
    assert.equal(page.state().Wallet, '9007199254740993');
    assert.deepEqual(page.timeline(), [[
      'Day 11', 'Derived observation', 'War-gate statistic over threshold (derived, daily): 2 hostile / 3 pairs.',
    ]]);
    page.get('step').click();
    assert.deepEqual(page.timeline().slice(1), [
      ['Day 12', 'Derived observation', 'Rescues and lost-in-space deaths counter changed by 2; cumulative total 2.'],
      ['Day 12', 'Actual Pilot.ev event', 'Rescued · actual rescue price 9007199254740995.'],
    ]);
    assert.equal(page.state().Location, 'station 2, system 3');
    assert.equal(page.state().Trip, 'inactive');
    assert.equal(page.state()['Busy until'], 'day 15');
    page.get('step').click();
    assert.deepEqual(page.timeline().at(-1), [
      'Day 13', 'Derived observation', 'War-gate statistic below threshold (derived, daily): 1 hostile / 3 pairs.',
    ]);
    assert.equal(page.get('empty-timeline').hidden, true);
    const observations = page.timeline();
    const finalState = page.state();
    await page.file(JSON.stringify(sampleRun()));
    page.seek(3);
    assert.deepEqual(page.timeline(), observations);
    assert.deepEqual(page.state(), finalState);
    page.get('reload').click();
    await settle();
    assert.equal(page.get('position').value, '0');
    page.seek(3);
    assert.deepEqual(page.timeline(), observations);
    assert.deepEqual(page.state(), finalState);
    assert.equal(page.get('hash').textContent, sampleRun().finalStateHash);
  });

  await test('empty observations stay empty, including an active starting war baseline', async () => {
    const run = sampleRun();
    run.timeline = [];
    for (const frame of run.frames) {
      frame.war = { active: true, hostilePairs: 2, pairs: 3 };
      frame.pilot.strandings = '0';
      frame.pilot.at = 0;
      frame.pilot.system = 1;
    }
    withEvidence(run);
    const page = await openViewer(JSON.stringify(run));
    for (let day = 0; day <= run.run.horizon; day++) {
      page.seek(day);
      assert.deepEqual(page.timeline(), []);
      assert.equal(page.get('timeline-count').textContent, '0 / 0 entries');
      assert.equal(page.get('empty-timeline').hidden, false);
      assert.equal(page.get('empty-timeline').textContent,
        'No daily war-gate threshold changes, rescues and lost-in-space deaths counter deltas, or pilot events were recorded in this run.');
      assert.equal(page.state()['War-gate statistic over threshold (derived, daily)'], 'over threshold (2 / 3 hostile pairs)');
      assert.equal(page.state()['Start war-gate statistic baseline (derived, daily)'], 'over threshold (2 / 3 hostile pairs)');
      assert.equal(page.state().Location, 'station 0, system 1');
      assert.equal(page.state().Trip, 'inactive');
    }
  });

  await test('play, pause, step and scrub advance recorded frames and clamp at the end', async () => {
    const run = sampleRun();
    const page = await openViewer(JSON.stringify(run));
    assert.equal(page.get('position').value, '0');
    assert.equal(page.callbacks.size, 0);
    page.get('speed').value = '2';
    page.get('play').click();
    assert.equal(page.get('play').textContent, 'Pause');
    assert.equal(page.callbacks.size, 1);
    page.tick(1000);
    page.tick(1249);
    assert.equal(page.get('position').value, '0');
    page.tick(1500);
    assert.equal(page.get('position').value, '1');
    assert.equal(page.get('day').textContent, 'Day 11 · 1/3');
    page.get('play').click();
    assert.equal(page.callbacks.size, 0);
    assert.equal(page.get('play').textContent, 'Play');
    page.tick(20000);
    assert.equal(page.get('position').value, '1');
    page.get('play').click();
    page.tick(20000);
    page.tick(20250);
    assert.equal(page.get('position').value, '1', 'resume resets elapsed time');
    page.get('step').click();
    assert.equal(page.get('position').value, '2');
    assert.equal(page.get('play').textContent, 'Play');
    assert.equal(page.callbacks.size, 0);
    page.get('play').click();
    page.tick(30000);
    page.tick(90000);
    assert.equal(page.get('position').value, '3');
    assert.equal(page.get('step').disabled, true);
    assert.equal(page.get('play').textContent, 'Play');
    assert.equal(page.callbacks.size, 0);
    page.get('step').click();
    assert.equal(page.get('position').value, '3');
    page.get('play').click();
    assert.equal(page.get('position').value, '0', 'play restarts at the baseline after the final frame');
    page.seek(2);
    assert.equal(page.get('position').value, '2');
    assert.equal(page.get('play').textContent, 'Play');
    assert.equal(page.get('step').disabled, false);
    assert.equal(page.callbacks.size, 0);
    assert.equal(page.get('hash').textContent, run.finalStateHash, 'final hash does not depend on playback day');
  });

  await test('missing export and network errors fail visibly with disabled playback', async () => {
    assertRejected(await openViewer('', { status: 404 }), /run.json returned HTTP 404/);
    assertRejected(await openViewer('', { fetchError: new Error('Network disconnected') }), /Network disconnected/);
  });

  await test('malformed JSON, missing observations and invalid frames fail through the real loader', async () => {
    for (const source of ['', '{', 'null', '[]']) {
      const page = await openViewer(source);
      assertRejected(page, /JSON|apsis-viewer-v1/);
    }
    const cases = [
      [(run) => { delete run.timeline; }, /apsis-viewer-v1/],
      [(run) => { run.frames = []; }, /apsis-viewer-v1/],
      [(run) => { delete run.finalStateHash; }, /apsis-viewer-v1/],
      [(run) => { run.finalStateHash = 42; }, /apsis-viewer-v1/],
      [(run) => { run.run.horizon = 2; }, /baseline frame/],
      [(run) => { run.frames[1].day++; }, /daily state frame/],
      [(run) => { delete run.frames[1].pilot; }, /daily state frame/],
      [(run) => { run.timeline[0].type = 'war-declared'; }, /observation timeline/],
      [(run) => { run.timeline[1].source = 'Pilot.ev'; }, /observation timeline/],
      [(run) => { run.timeline[2].source = 'derived'; }, /observation timeline/],
      [(run) => { run.timeline.reverse(); }, /observation timeline/],
      [(run) => { run.timeline[3].day = 14; }, /observation timeline/],
    ];
    for (const [mutate, message] of cases) {
      const run = sampleRun();
      mutate(run);
      assertRejected(await openViewer(JSON.stringify(run)), message);
    }
  });

  await test('malformed final hashes are rejected before any state or hash is rendered', async () => {
    const original = await recordedRun();
    const cases = [
      ['number', 42], ['null', null], ['missing', undefined], ['empty', ''],
      ['wrong length', '1234'], ['uppercase', original.finalStateHash.toUpperCase()],
      ['non-hex', 'gggggggggggggggg'], ['well-formed wrong value', '0000000000000000'],
    ];
    for (const [name, value] of cases) {
      const run = structuredClone(original);
      if (value === undefined) delete run.finalStateHash;
      else run.finalStateHash = value;
      const page = await openViewer(JSON.stringify(run));
      assertRejected(page, /hash|checkpoint|apsis-viewer-v1/i);
      assert.equal(page.get('run').hidden, true, name);
    }
  });

  await test('constructor is rejected as a pilot end state despite matching frame evidence', async () => {
    const run = sampleRun();
    run.frames[1].pilot.end = 'constructor';
    withEvidence(run);
    assertRejected(await openViewer(JSON.stringify(run)), /daily state frame/);
  });

  await test('fetched exports obey the chosen-file 32 MiB cap for headers and actual UTF-8 bytes', async () => {
    const limit = 32 * 1024 * 1024;
    const valid = JSON.stringify(sampleRun());
    // A short valid body must still reject a declared oversized response.
    assertRejected(await openViewer(valid, { contentLength: String(limit + 1) }), /smaller than 32 MiB/);
    // Valid JSON padded with whitespace tests actual text length without a header.
    assertRejected(await openViewer(valid + ' '.repeat(limit + 1 - valid.length)), /smaller than 32 MiB/);
    // Characters fit under the cap, but actual UTF-8 bytes exceed it; a lying
    // Content-Length must not bypass the independently checked response body.
    const run = sampleRun();
    run.metadata.historyGap = 'é'.repeat(Math.floor(limit / 2) + 1);
    const multibyte = JSON.stringify(run);
    assert.ok(multibyte.length < limit);
    assert.ok(Buffer.byteLength(multibyte, 'utf8') > limit);
    assertRejected(await openViewer(multibyte, { contentLength: '1' }), /smaller than 32 MiB/);
  });

  await test('file validation and recovery use the real file loader', async () => {
    const page = await openViewer(JSON.stringify(sampleRun()));
    await page.file('{');
    assertRejected(page, /JSON/);
    await page.file(JSON.stringify(sampleRun()), 'oversized.json', 32 * 1024 * 1024 + 1);
    assertRejected(page, /smaller than 32 MiB/);
    await page.file(JSON.stringify(sampleRun()));
    assert.equal(page.get('run').hidden, false);
    assert.equal(page.get('status').className, '', page.get('status').textContent);
    assert.equal(page.get('position').value, '0');
    assert.equal(page.get('step').disabled, false);
    assert.match(page.get('status').textContent, /Loaded saved-run.json/);
  });
}

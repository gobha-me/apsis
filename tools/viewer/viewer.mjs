const byId = (id) => document.getElementById(id);
const controls = document.querySelectorAll('[data-run-control]');
const maxExportBytes = 32 * 1024 * 1024;
const endStates = ['active', 'lost in space', 'death by hazard', 'bankrupt'];
const eventNames = new Map([
  [1, 'Debt warning'],
  [2, 'Bankruptcy'],
  [3, 'Death lost in space'],
  [4, 'Death by hazard'],
  [5, 'Rescued'],
]);

let artifact = null;
let artifactText = '';
let selectedFile = null;
let position = 0;
let playing = false;
let previousTime = null;
let elapsed = 0;
let animation = null;
let loadNumber = 0;

function pause() {
  playing = false;
  previousTime = null;
  elapsed = 0;
  if (animation !== null) cancelAnimationFrame(animation);
  animation = null;
  byId('play').textContent = 'Play';
}

const isHash = (value) => typeof value === 'string' && /^[0-9a-f]{16}$/.test(value);

function hashBytes(bytes) {
  // Match both apsis::hashBytes lanes, including uint32 overflow.
  let firstLane = 0x811c9dc5;
  let secondLane = 0x1b873593;
  for (const byte of bytes) {
    firstLane = Math.imul(firstLane ^ byte, 0x01000193) >>> 0;
    secondLane = Math.imul(secondLane ^ byte, 0x5bd1e995) >>> 0;
  }
  return [firstLane, secondLane].map((lane) => lane.toString(16).padStart(8, '0')).join('');
}

function verifyFinalCheckpoint(value) {
  const checkpoint = value.finalCheckpoint;
  if (!isHash(value.finalStateHash) ||
      typeof checkpoint !== 'string' || checkpoint.length === 0 ||
      checkpoint.length % 2 !== 0 || /[^0-9a-f]/.test(checkpoint)) {
    throw new Error('The export requires a valid final checkpoint and 16-digit lowercase hex hash.');
  }
  const bytes = new Uint8Array(checkpoint.length / 2);
  for (let offset = 0; offset < bytes.length; offset++) {
    bytes[offset] = Number.parseInt(checkpoint.slice(offset * 2, offset * 2 + 2), 16);
  }
  if (hashBytes(bytes) !== value.finalStateHash) {
    throw new Error('The final checkpoint hash does not match finalStateHash.');
  }
}

function isInt64String(value) {
  return typeof value === 'string' && value.length <= 20 &&
    /^(0|-?[1-9][0-9]*)$/.test(value) &&
    BigInt(value) >= -9223372036854775808n && BigInt(value) <= 9223372036854775807n;
}

const isRecord = (value) => value !== null && typeof value === 'object' && !Array.isArray(value);
const hasFields = (value, fields) => isRecord(value) &&
  Object.keys(value).length === fields.length && fields.every((field) => Object.hasOwn(value, field));
const isCount = (value) => Number.isSafeInteger(value) && value >= 0;
const isSetting = (value) => isInt64String(value) || (Array.isArray(value) && value.every(isSetting));

function validateRun(run) {
  if (!hasFields(run, ['seed', 'majors', 'startDay', 'horizon', 'station', 'settings', 'inputs']) ||
      !isCount(run.seed) || run.seed > 0xffffffff ||
      !Number.isInteger(run.majors) || run.majors < 3 || run.majors > 5 ||
      !isCount(run.startDay) || run.startDay > 36000 ||
      !Number.isInteger(run.horizon) || run.horizon < 1 || run.horizon > 3600 ||
      !isCount(run.station) ||
      !hasFields(run.settings, ['rules', 'core', 'world']) ||
      !isRecord(run.settings.rules) || !isRecord(run.settings.core) || !isRecord(run.settings.world) ||
      !Object.values(run.settings.rules).every((field) => typeof field === 'boolean') ||
      !Object.values(run.settings.core).every(isInt64String) ||
      !Object.values(run.settings.world).every(isSetting) ||
      !Array.isArray(run.inputs) || run.inputs.length > 4096 ||
      !run.inputs.every((entry) => hasFields(entry, ['day', 'kind', 'fields']) &&
        Number.isSafeInteger(entry.day) && entry.day >= run.startDay &&
        entry.day < run.startDay + run.horizon &&
        Number.isInteger(entry.kind) && entry.kind >= 0 && entry.kind <= 10 &&
        Array.isArray(entry.fields) && entry.fields.length === 6 && entry.fields.every(isInt64String))) {
    throw new Error('The export has an invalid run record.');
  }
}

function verifyTimeline(value) {
  // Rebuild the exporter's run/observation field order; setting names are sorted.
  const { seed, majors, startDay, horizon, station, settings, inputs } = value.run;
  const sortedFields = (fields) => Object.fromEntries(Object.keys(fields).sort().map((key) => [key, fields[key]]));
  const run = {
    seed, majors, startDay, horizon, station,
    settings: {
      rules: sortedFields(settings.rules), core: sortedFields(settings.core), world: sortedFields(settings.world),
    },
    inputs: inputs.map(({ day, kind, fields }) => ({ day, kind, fields })),
  };
  const timeline = value.timeline.map(({ day, type, source, active, hostilePairs, pairs, delta, total, kind, arg }) => {
    if (type === 'general-war') return { day, type, source, active, hostilePairs, pairs };
    if (type === 'stranding') return { day, type, source, delta, total };
    return { day, type, source, kind, arg };
  });
  const evidence = `apsis-viewer-timeline-v1\n${value.finalStateHash}\n${JSON.stringify(run)}\n${JSON.stringify(timeline)}`;
  if (!isHash(value.timelineHash) || hashBytes(new TextEncoder().encode(evidence)) !== value.timelineHash) {
    throw new Error('The run record and observation timeline hash does not match timelineHash and the final checkpoint hash.');
  }
}

function verifyFrames(value) {
  // Rebuild the exporter's frame() JSON order; input whitespace/key order do not matter.
  // Include every frame field rendered by the page, with its validated JSON type.
  const frames = value.frames.map(({ day, war, pilot }) => ({
    day,
    war: { active: war.active, hostilePairs: war.hostilePairs, pairs: war.pairs },
    pilot: {
      alive: pilot.alive, at: pilot.at, system: pilot.system, busyUntil: pilot.busyUntil,
      tripActive: pilot.tripActive, tripDestination: pilot.tripDestination,
      end: pilot.end, endDay: pilot.endDay, wallet: pilot.wallet, fuel: pilot.fuel,
      strandings: pilot.strandings, debt: pilot.debt,
    },
  }));
  const evidence = `apsis-viewer-frames-v1\n${value.finalStateHash}\n${JSON.stringify(frames)}`;
  if (!isHash(value.framesHash) || hashBytes(new TextEncoder().encode(evidence)) !== value.framesHash) {
    throw new Error('The daily frames hash does not match framesHash and the final checkpoint hash.');
  }
}

function validate(value) {
  if (value?.schema !== 'apsis-viewer-v1' || !value.run || !value.metadata ||
      !Array.isArray(value.frames) || value.frames.length === 0 ||
      !Array.isArray(value.timeline) || !Array.isArray(value.run.inputs) ||
      typeof value.finalStateHash !== 'string') {
    throw new Error('Choose an apsis-viewer-v1 JSON export generated by the viewer adapter.');
  }
  if (!Number.isSafeInteger(value.run.startDay) ||
      !Number.isSafeInteger(value.run.horizon) || value.run.horizon < 1 ||
      value.frames.length !== value.run.horizon + 1) {
    throw new Error('The export must contain a baseline frame and one frame per bounded run day.');
  }
  validateRun(value.run);
  for (const [index, frame] of value.frames.entries()) {
    const pilot = frame?.pilot;
    const war = frame?.war;
    if (frame?.day !== value.run.startDay + index || !Number.isSafeInteger(frame.day) ||
        !pilot || !war || typeof war.active !== 'boolean' ||
        ![war.hostilePairs, war.pairs].every((field) => Number.isSafeInteger(field) && field >= 0) ||
        typeof pilot.alive !== 'boolean' || typeof pilot.tripActive !== 'boolean' ||
        ![pilot.at, pilot.system, pilot.busyUntil, pilot.tripDestination, pilot.endDay]
          .every(Number.isSafeInteger) ||
        !Number.isInteger(pilot.end) || !Object.hasOwn(endStates, pilot.end) ||
        ![pilot.wallet, pilot.fuel, pilot.strandings, pilot.debt].every(isInt64String)) {
      throw new Error('The export has an invalid or missing daily state frame.');
    }
  }
  let priorDay = value.run.startDay;
  for (const entry of value.timeline) {
    if (!isRecord(entry) || !Number.isSafeInteger(entry.day) || entry.day < priorDay ||
        entry.day > value.frames.at(-1).day ||
        !['general-war', 'stranding', 'pilot-event'].includes(entry.type) ||
        entry.source !== (entry.type === 'pilot-event' ? 'Pilot.ev' : 'derived') ||
        (entry.type === 'general-war' &&
          (!hasFields(entry, ['day', 'type', 'source', 'active', 'hostilePairs', 'pairs']) ||
            typeof entry.active !== 'boolean' || ![entry.hostilePairs, entry.pairs].every(isCount))) ||
        (entry.type === 'stranding' &&
          (!hasFields(entry, ['day', 'type', 'source', 'delta', 'total']) ||
            ![entry.delta, entry.total].every(isInt64String))) ||
        (entry.type === 'pilot-event' &&
          (!hasFields(entry, ['day', 'type', 'source', 'kind', 'arg']) ||
            !Number.isSafeInteger(entry.kind) || !isInt64String(entry.arg)))) {
      throw new Error('The export has an invalid observation timeline.');
    }
    priorDay = entry.day;
  }
  verifyFinalCheckpoint(value);
  verifyFrames(value);
  verifyTimeline(value);
  return value;
}

function setState(rows) {
  const fragment = document.createDocumentFragment();
  for (const [label, value] of rows) {
    const term = document.createElement('dt');
    const description = document.createElement('dd');
    term.textContent = label;
    description.textContent = String(value);
    fragment.append(term, description);
  }
  byId('state').replaceChildren(fragment);
}

function timelineText(entry) {
  if (entry.type === 'general-war') {
    return `War-gate statistic ${entry.active ? 'over' : 'below'} threshold (derived, daily): ` +
      `${entry.hostilePairs} hostile / ${entry.pairs} pairs.`;
  }
  if (entry.type === 'stranding') {
    return `Rescues and lost-in-space deaths counter changed by ${entry.delta}; cumulative total ${entry.total}.`;
  }
  if (entry.kind === 5) return `Rescued · actual rescue price ${entry.arg}.`;
  return `${eventNames.get(entry.kind) ?? `Pilot event kind ${entry.kind}`} · argument ${entry.arg}.`;
}

function renderTimeline(day) {
  const visible = artifact.timeline.filter((entry) => entry.day <= day);
  const fragment = document.createDocumentFragment();
  for (const entry of visible) {
    const item = document.createElement('li');
    const time = document.createElement('div');
    const source = document.createElement('div');
    const description = document.createElement('div');
    time.textContent = `Day ${entry.day}`;
    time.className = 'entry-day';
    source.className = 'source';
    source.textContent = entry.source === 'derived' ? 'Derived observation' : 'Actual Pilot.ev event';
    description.textContent = timelineText(entry);
    item.append(time, source, description);
    fragment.append(item);
  }
  byId('timeline').replaceChildren(fragment);
  byId('timeline-count').textContent = `${visible.length} / ${artifact.timeline.length} entries`;
  byId('empty-timeline').hidden = visible.length > 0;
  byId('empty-timeline').textContent = artifact.timeline.length === 0
    ? 'No daily war-gate threshold changes, rescues and lost-in-space deaths counter deltas, or pilot events were recorded in this run.'
    : 'No entries through this day. Later observations appear as you advance.';
}

function renderInspection() {
  const entity = byId('entity').value === 'war' ? 'war' : 'pilot';
  const frame = artifact.frames[position];
  const previous = position > 0 ? artifact.frames[position - 1] : null;
  // Only inspect the exact fields covered by framesHash. Unknown JSON properties
  // are never displayed as though they were validated exporter evidence.
  const fields = entity === 'pilot'
    ? ['alive', 'at', 'system', 'busyUntil', 'tripActive', 'tripDestination',
      'end', 'endDay', 'wallet', 'fuel', 'strandings', 'debt']
    : ['active', 'hostilePairs', 'pairs'];
  const fragment = document.createDocumentFragment();
  for (const field of fields) {
    const current = frame[entity][field];
    const before = previous?.[entity][field];
    const row = document.createElement('tr');
    let change = previous ? 'unchanged' : 'baseline';
    if (previous && current !== before) {
      row.className = 'changed';
      if (typeof current === 'boolean') change = 'changed';
      else {
        const difference = BigInt(current) - BigInt(before);
        change = `${difference > 0n ? '+' : ''}${difference}`;
      }
    }
    for (const text of [`${entity}.${field}`, String(current), previous ? String(before) : '—', change]) {
      const cell = document.createElement('td');
      cell.textContent = text;
      row.append(cell);
    }
    fragment.append(row);
  }
  byId('inspection').replaceChildren(fragment);
  byId('inspector-description').textContent = `Day ${frame.day}` +
    (previous ? ` compared with day ${previous.day}.` : ' is the baseline before A_START.');
}

function render() {
  const frame = artifact.frames[position];
  const pilot = frame.pilot;
  const baseline = artifact.frames[0].war;
  const warLabel = (war) => `${war.active ? 'over' : 'below'} threshold (${war.hostilePairs} / ${war.pairs} hostile pairs)`;
  byId('position').value = String(position);
  byId('day').textContent = `Day ${frame.day} · ${position}/${artifact.run.horizon}`;
  byId('step').disabled = position === artifact.frames.length - 1;
  setState([
    ['Pilot', pilot.alive ? 'alive' : position === 0 ? 'not started' : 'not alive'],
    ['Career end', Object.hasOwn(endStates, pilot.end) ? endStates[pilot.end] : `end state ${pilot.end}`],
    ['End day', pilot.endDay < 0 ? 'none' : `day ${pilot.endDay}`],
    ['Location', pilot.at >= 0 ? `station ${pilot.at}, system ${pilot.system}` : `undocked, system ${pilot.system}`],
    ['Trip', pilot.tripActive ? `active, destination ${pilot.tripDestination}` : 'inactive'],
    ['Busy until', `day ${pilot.busyUntil}`],
    ['Wallet', pilot.wallet],
    ['Fuel', pilot.fuel],
    ['Debt', pilot.debt],
    ['Rescues and lost-in-space deaths (derived counter)', pilot.strandings],
    ['War-gate statistic (derived, daily)', warLabel(frame.war)],
    ['Start war-gate statistic baseline (derived, daily)', warLabel(baseline)],
  ]);
  renderTimeline(frame.day);
  renderInspection();
}

function advance(timestamp) {
  if (!playing) return;
  if (previousTime !== null) elapsed += (timestamp - previousTime) / 1000;
  previousTime = timestamp;
  const speed = Number(byId('speed').value);
  const days = Math.floor(elapsed * speed);
  if (days > 0) {
    elapsed -= days / speed;
    position = Math.min(position + days, artifact.frames.length - 1);
    render();
  }
  if (position === artifact.frames.length - 1) {
    pause();
    return;
  }
  animation = requestAnimationFrame(advance);
}

async function loadRun(file) {
  const request = ++loadNumber;
  pause();
  controls.forEach((control) => { control.disabled = true; });
  byId('run').hidden = true;
  byId('hash').textContent = '';
  byId('state').replaceChildren();
  byId('timeline').replaceChildren();
  byId('inspection').replaceChildren();
  byId('inspector-description').textContent = '';
  byId('status').className = '';
  byId('status').textContent = `Loading ${file ? file.name : 'run.json'}…`;
  try {
    if (file && file.size > maxExportBytes) throw new Error('Choose an export smaller than 32 MiB.');
    let text;
    if (file) {
      text = await file.text();
    } else {
      const response = await fetch('run.json', { cache: 'no-store' });
      if (!response.ok) throw new Error(`run.json returned HTTP ${response.status}.`);
      if (Number(response.headers?.get('content-length')) > maxExportBytes) {
        throw new Error('Choose an export smaller than 32 MiB.');
      }
      text = await response.text();
    }
    if (request !== loadNumber) return;
    if (text.length > maxExportBytes || new TextEncoder().encode(text).byteLength > maxExportBytes) {
      throw new Error('Choose an export smaller than 32 MiB.');
    }
    artifact = validate(JSON.parse(text));
    artifactText = text;
    selectedFile = file;
    position = 0;
    byId('entity').value = 'pilot';
    const run = artifact.run;
    byId('run-title').textContent = `Seed ${run.seed}`;
    byId('run-description').textContent = `${run.majors} majors · start day ${run.startDay} · ` +
      `${run.horizon} days · opening station ${run.station}`;
    byId('hash').textContent = artifact.finalStateHash;
    byId('history-gap').textContent = artifact.metadata.historyGap;
    byId('adapter').textContent = `Adapter revision: ${artifact.metadata.adapterRevision}`;
    byId('record').textContent = JSON.stringify(run, null, 2);
    byId('position').max = String(artifact.frames.length - 1);
    byId('first-day').textContent = `Day ${artifact.frames[0].day} · before A_START`;
    byId('last-day').textContent = `Day ${artifact.frames.at(-1).day} · final export state`;
    controls.forEach((control) => { control.disabled = false; });
    byId('run').hidden = false;
    byId('status').textContent = `Loaded ${file ? file.name : 'run.json'}. Playback is paused at the baseline.`;
    render();
  } catch (error) {
    if (request !== loadNumber) return;
    artifact = null;
    byId('status').className = 'error';
    byId('status').textContent = `${error.message} Generate run.json using the commands in tools/viewer/README.md, ` +
      'serve this directory over HTTP, or open an exported JSON file above.';
  }
}

byId('play').addEventListener('click', () => {
  if (playing) {
    pause();
    return;
  }
  if (position === artifact.frames.length - 1) position = 0;
  render();
  playing = true;
  byId('play').textContent = 'Pause';
  animation = requestAnimationFrame(advance);
});

byId('step').addEventListener('click', () => {
  pause();
  position = Math.min(position + 1, artifact.frames.length - 1);
  render();
});

byId('position').addEventListener('input', () => {
  pause();
  position = Number(byId('position').value);
  render();
});

byId('entity').addEventListener('change', renderInspection);

byId('reload').addEventListener('click', () => { void loadRun(selectedFile); });
byId('run-file').addEventListener('change', () => {
  const file = byId('run-file').files[0];
  if (file) void loadRun(file);
});

byId('download').addEventListener('click', () => {
  const url = URL.createObjectURL(new Blob([artifactText], { type: 'application/json' }));
  const link = document.createElement('a');
  link.href = url;
  link.download = `apsis-seed-${artifact.run.seed}-day-${artifact.run.startDay}.json`;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
});

void loadRun(null);

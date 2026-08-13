const fs = require('fs');
const vm = require('vm');
const assert = require('assert');

const source = fs.readFileSync('web/trade-incidence.js', 'utf8');
const marker = '// All slider and negotiation parameter edits are staged.';
const start = source.indexOf(marker);
assert(start >= 0, 'global staging controller must be present');
const controllerSource = source.slice(start);

const listeners = {click:[]};
let legacyScheduleCalls = 0;
let dashboardRunClicks = 0;
let publishCalls = 0;
let refreshCalls = 0;

const autoStatus = {textContent:''};
const linkedNote = {textContent:''};
const partyView = {
  querySelectorAll() { return []; },
  querySelector() { return null; }
};
const partyRunStatus = {textContent:''};
const partyRun = {
  disabled:false,
  dataset:{},
  setAttribute() {},
  addEventListener() {}
};
const dashboardRun = {
  disabled:false,
  click() {
    dashboardRunClicks++;
    const target = {closest: selector => selector === '#run' ? dashboardRun : null};
    listeners.click.forEach(listener => listener({type:'click', target}));
  }
};
const preset = {dataset:{rate:'25'}, onclick:null};

global.window = {};
global.schedule = () => { legacyScheduleCalls++; };
global.tariff = {value:'50'};
global.updateTariff = () => {};
global.updatePosition = () => {};
global.syncPartyView = () => {};
global.refreshPartySectorMetrics = () => { refreshCalls++; };
global.publishNegotiation = () => { publishCalls++; };
global.document = {
  querySelector(selector) {
    if (selector === '#partyView') return partyView;
    if (selector === '#run') return dashboardRun;
    if (selector === '#partyRun') return partyRun;
    if (selector === '#partyRunStatus') return partyRunStatus;
    if (selector === '.auto span') return autoStatus;
    if (selector === '.preferences .linked-note') return linkedNote;
    return null;
  },
  querySelectorAll(selector) {
    if (selector === '.presets button') return [preset];
    return [];
  },
  addEventListener(type, handler) {
    if (listeners[type]) listeners[type].push(handler);
  }
};
global.MutationObserver = undefined;

vm.runInThisContext(controllerSource);

schedule();
assert.strictEqual(legacyScheduleCalls, 0,
  'dashboard slider commits must never launch the legacy optimizer timer');
assert.strictEqual(window.EvaluationRunController.state().staged, true,
  'dashboard slider commit must mark inputs staged');
assert.strictEqual(autoStatus.textContent.includes('No global search is running'), true,
  'staged state must tell the user no global search is running');

schedule();
assert.strictEqual(legacyScheduleCalls, 0,
  'repeated slider commits must remain search-free');

assert.strictEqual(typeof preset.onclick, 'function',
  'tariff presets must be rebound to staged behavior');
preset.onclick();
assert.strictEqual(tariff.value, '25', 'preset must retain the selected tariff value');
assert.strictEqual(publishCalls, 1, 'preset must still publish the selected negotiation input');
assert.strictEqual(refreshCalls, 1, 'preset must refresh displayed sector metrics');
assert.strictEqual(legacyScheduleCalls, 0,
  'preset selection must not launch a global search');
assert.strictEqual(window.EvaluationRunController.state().staged, true,
  'preset selection must remain staged until explicit run');

assert(controllerSource.includes(
  "partyRun.addEventListener('click', () => dashboardRun.click())"),
  'delegation Run new run button must be wired to the Dashboard run button');
window.EvaluationRunController.run();
assert.strictEqual(dashboardRunClicks, 1,
  'explicit run controller must invoke Dashboard Run again now');
assert.strictEqual(window.EvaluationRunController.state().staged, false,
  'explicit run must clear the staged marker');
assert.strictEqual(partyRunStatus.textContent,
  'Uses Dashboard optimizer · Run again now');

console.log('global slider staging test passed');

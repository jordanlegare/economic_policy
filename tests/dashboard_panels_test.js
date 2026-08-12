const fs = require('fs');
const vm = require('vm');
const assert = require('assert');

const html = fs.readFileSync('web/index.html', 'utf8');
const required = [
  'trade-scenario',
  'negotiating-positions',
  'win-win-preferences',
  'data-guardrails',
  'decision-overview',
  'fiscal-ledger',
  'strategies',
  'projection',
  'sectors'
];
required.forEach(id => assert(
  html.includes(`data-dashboard-panel="${id}"`),
  `dashboard section ${id} must be a collapsible panel`
));
assert(html.includes('id="collapseDashboardPanels"'));
assert(html.includes('id="expandDashboardPanels"'));
assert(html.includes('id="dashboardPanelStyles"'));
assert(html.includes('id="initialOpeningScenarioController"'));
assert(html.includes('id="usTariff" type="range" min="0" max="60" step="1" value="50"'),
  'trade shock control must render at 50% before scripts run');
assert(html.includes('id="sectorCoverage" type="range" min="0" max="100" value="100"'),
  'delegation sector control must render at 100% before scripts run');

const match = html.match(/<script id="dashboardPanelController">([\s\S]*?)<\/script>/);
assert(match, 'dashboard panel controller must be bundled in the page shell');

function eventTarget(extra = {}) {
  const listeners = new Map();
  return Object.assign({
    addEventListener(type, handler) { listeners.set(type, handler); },
    fire(type) { const handler = listeners.get(type); if (handler) handler(); }
  }, extra);
}

const panels = required.map(id => eventTarget({
  dataset: {dashboardPanel:id},
  open: true
}));
const collapse = eventTarget();
const expand = eventTarget();
const root = {
  querySelectorAll(selector) {
    assert.strictEqual(selector, 'details.dashboard-panel[data-dashboard-panel]');
    return panels;
  }
};

global.window = {};
global.document = {
  getElementById(id) {
    if (id === 'dashboardView') return root;
    if (id === 'collapseDashboardPanels') return collapse;
    if (id === 'expandDashboardPanels') return expand;
    return null;
  }
};
const storage = new Map();
storage.set('economic-policy-dashboard-panels-v1', JSON.stringify({
  'trade-scenario': false,
  'data-guardrails': false
}));
global.localStorage = {
  getItem(key) { return storage.has(key) ? storage.get(key) : null; },
  setItem(key, value) { storage.set(key, value); }
};

vm.runInThisContext(match[1]);
assert(window.DashboardPanels, 'panel controller must expose inspection helpers');
assert.strictEqual(panels[0].open, true,
  'Trade shock must always start expanded even if a previous session collapsed it');
assert.strictEqual(panels[3].open, false,
  'other individual panel preferences may still be restored');

collapse.fire('click');
assert(panels.every(panel => panel.open === false), 'collapse all must stack every panel');
let saved = JSON.parse(storage.get('economic-policy-dashboard-panels-v1'));
required.forEach(id => assert.strictEqual(saved[id], false));

expand.fire('click');
assert(panels.every(panel => panel.open === true), 'expand all must reopen every panel');

panels[3].open = false;
panels[3].fire('toggle');
saved = JSON.parse(storage.get('economic-policy-dashboard-panels-v1'));
assert.strictEqual(saved['data-guardrails'], false, 'individual panel state must persist');
assert.strictEqual(window.DashboardPanels.panels().length, required.length);

const openingMatch = html.match(/<script id="initialOpeningScenarioController">([\s\S]*?)<\/script>/);
assert(openingMatch, 'initial opening scenario controller must be bundled after app.js');

const elements = new Map();
const node = (id, value = '') => {
  const x = {id, value:String(value), textContent:'', hidden:true};
  elements.set('#' + id, x);
  return x;
};
node('retaliatoryTariff', 9);
node('retaliatoryTariffValue');
node('partyView').hidden = true;
global.$ = selector => elements.get(selector) || null;
global.tariff = {value:'7'};
global.positions = {
  canada:Array(20).fill(22),
  us:Array(20).fill(33)
};
let tariffUpdates = 0;
let positionUpdates = 0;
let partySyncs = 0;
global.updateTariff = () => tariffUpdates++;
global.updatePosition = () => positionUpdates++;
global.syncPartyView = () => partySyncs++;
global.renderPartySectors = () => { throw new Error('hidden party view must not render during initial preparation'); };

const baseFetch = async input => {
  assert.strictEqual(String(input), '/api/calibration');
  return {
    ok:true,
    status:200,
    json:async()=>({
      effectiveState:{
        usTariff:5,
        retaliatoryTariff:1.5,
        usSectorCoverage:Array(20).fill(0),
        canadaSectorCoverage:Array(20).fill(0)
      }
    })
  };
};
window.fetch = baseFetch;
let submittedCalibration;
global.evaluate = async function controlledEvaluationStub() {
  const response = await window.fetch('/api/calibration', {cache:'no-store'});
  submittedCalibration = await response.json();
  return submittedCalibration;
};

vm.runInThisContext(openingMatch[1]);
assert(window.InitialOpeningScenario, 'opening controller must expose its declared posture');
assert.strictEqual(tariff.value, '50', 'visible U.S. trade shock must be 50% before automatic evaluation starts');
assert.strictEqual(elements.get('#retaliatoryTariff').value, '5');
assert(positions.us.every(value => value === 100), 'U.S. delegation table must start at 100% coverage');
assert(positions.canada.every(value => value === 100), 'Canada delegation table must start at 100% coverage');
assert(tariffUpdates > 0 && positionUpdates > 0 && partySyncs > 0,
  'opening preparation must synchronize all visible readouts');

(async()=>{
  await evaluate();
  const state = submittedCalibration.effectiveState;
  assert.strictEqual(state.usTariff, 50,
    'first automatic solve must receive the visible 50% trade-shock opening, not calibrated tariff state');
  assert.strictEqual(state.retaliatoryTariff, 5);
  assert(state.usSectorCoverage.every(value => value === 100),
    'first automatic solve must receive 100% U.S. sector coverage across all 20 sectors');
  assert(state.canadaSectorCoverage.every(value => value === 100),
    'first automatic solve must receive 100% Canadian sector coverage across all 20 sectors');
  const restored = await window.fetch('/api/calibration');
  const restoredCalibration = await restored.json();
  assert.strictEqual(restoredCalibration.effectiveState.usTariff, 5,
    'startup calibration override must be one-shot and restore the real calibration fetch afterward');
  assert(restoredCalibration.effectiveState.usSectorCoverage.every(value => value === 0));
  console.log('dashboard panels test passed');
})().catch(error => { console.error(error); process.exit(1); });

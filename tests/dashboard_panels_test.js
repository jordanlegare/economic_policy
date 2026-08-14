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
assert(html.includes('id="usTariff" type="range" min="0" max="200" step="1" value="50"'),
  'trade shock control must render at 50% with a 200% U.S. maximum before scripts run');
assert(html.includes('<button data-rate="200">200%</button>'),
  'trade shock presets must expose the 200% endpoint');
assert(html.includes('id="retaliatoryTariff" type="range" min="0" max="60" value="5"'),
  'Canada retaliatory tariff control must retain its 60% ceiling');
assert(html.includes('id="tariffShockCeilingController"'),
  'delegation-room headline tariff control must apply country-specific U.S./Canada ceilings');
assert(html.includes("negotiator === 'us' ? '200' : '60'"),
  'U.S. delegation-room headline tariff must use the 200% ceiling while Canada remains at 60%');
assert(html.includes('id="sectorCoverage" type="range" min="0" max="100" value="100"'),
  'normalized delegation sector seed must remain full coverage before app.js converts it to an actual tariff display');

const incidenceTag = '<script src="/trade-incidence.js"></script>';
assert.strictEqual(html.split(incidenceTag).length - 1, 1,
  'trade incidence diagnostics must be loaded exactly once');
const incidenceSource = fs.readFileSync('web/trade-incidence.js', 'utf8');
new vm.Script(incidenceSource, {filename:'web/trade-incidence.js'});
[
  'usAppliedTariff', 'canadaAppliedTariff', 'usBuyerPassThrough',
  'canadaBuyerPassThrough', 'canadaExporterAbsorption',
  'usExporterAbsorption', 'usImporterAbsorption',
  'canadaImporterAbsorption', 'canadaUpstreamCost', 'usUpstreamCost'
].forEach(field => assert(incidenceSource.includes(field),
  `trade incidence UI must render ${field}`));
assert(incidenceSource.includes('Canada IO · StatCan empirical'));
assert(incidenceSource.includes('U.S. IO · BEA artifact pending'));
const cmakeSource = fs.readFileSync('CMakeLists.txt', 'utf8');
assert(cmakeSource.includes('web/trade-incidence.js'),
  'Windows standalone must embed the tariff-incidence diagnostics asset');

const appSource = fs.readFileSync('web/app.js', 'utf8');
const helperMatch = appSource.match(/const clampNumber=.*?const tariffToCoverage=.*?;/s);
assert(helperMatch, 'delegation tariff conversion helpers must be present');
const tariffMath = {};
vm.runInNewContext(`${helperMatch[0]}\nthis.coverageToTariff=coverageToTariff;this.tariffToCoverage=tariffToCoverage;`, tariffMath);
assert.strictEqual(tariffMath.coverageToTariff(50, 100), 50,
  '100% internal coverage at a 50% headline must display as a 50% sector tariff');
assert.strictEqual(tariffMath.coverageToTariff(50, 50), 25,
  '50% internal coverage at a 50% headline must display as a 25% sector tariff');
assert.strictEqual(tariffMath.coverageToTariff(5, 25), 1.25,
  'Canada sector display must use the Canadian headline tariff, not the raw coverage percentage');
assert.strictEqual(tariffMath.tariffToCoverage(50, 25), 50,
  'a user-entered 25% sector tariff under a 50% headline must submit 50% normalized coverage');
assert.strictEqual(tariffMath.tariffToCoverage(5, 1.25), 25,
  'actual Canadian sector tariffs must convert back to the same normalized model state');
assert.strictEqual(tariffMath.tariffToCoverage(0, 0), 0,
  'zero headline tariffs must not divide by zero');
assert(appSource.includes("child.textContent='Applied tariff for this sector '"),
  'joint delegation control must be labeled as an applied tariff rather than raw coverage');
assert(appSource.includes('Sector-by-sector applied tariffs and deal metrics'),
  'delegation table heading must describe the actual tariff rates shown');
assert(appSource.includes('positions[negotiator][i]=tariffToCoverage(headlineFor(negotiator),+input.value)'),
  'delegation table edits must convert actual tariff percentages back to model coverage');
assert(appSource.includes("positions[negotiator][i]=tariffToCoverage(headlineFor(negotiator),+$('#sectorCoverage').value)"),
  'joint sidebar sector edits must convert actual tariff percentages back to model coverage');
assert(appSource.includes('aria-label="${name} applied tariff percentage"'),
  'delegation sector sliders must expose actual-tariff semantics to assistive technology');
assert(appSource.includes('U.S. average applied sector tariff'),
  'published deal summary must report an applied tariff rather than equilibrium coverage');

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

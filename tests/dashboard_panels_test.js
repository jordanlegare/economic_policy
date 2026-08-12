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
global.localStorage = {
  getItem(key) { return storage.has(key) ? storage.get(key) : null; },
  setItem(key, value) { storage.set(key, value); }
};

vm.runInThisContext(fs.readFileSync('web/dashboard-panels.js', 'utf8'));
assert(window.DashboardPanels, 'panel controller must expose inspection helpers');

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

console.log('dashboard panels test passed');

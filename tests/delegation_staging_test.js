const fs = require('fs');
const vm = require('vm');
const assert = require('assert');

const source = fs.readFileSync('web/trade-incidence.js', 'utf8');
const marker = '// Delegation trade-table edits are staged.';
const start = source.indexOf(marker);
assert(start >= 0, 'delegation staging controller must be present');
const controllerSource = source.slice(start);

const listeners = {change:[], click:[]};
let scheduleCalls = 0;
let dashboardRunClicks = 0;

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

global.window = {};
global.schedule = () => { scheduleCalls++; };
global.document = {
  querySelector(selector) {
    if (selector === '#partyView') return partyView;
    if (selector === '#run') return dashboardRun;
    if (selector === '#partyRun') return partyRun;
    if (selector === '#partyRunStatus') return partyRunStatus;
    return null;
  },
  addEventListener(type, handler) {
    if (listeners[type]) listeners[type].push(handler);
  }
};
global.MutationObserver = undefined;

vm.runInThisContext(controllerSource);

(async () => {
  const delegationRange = {
    closest(selector) { return selector === '#partyView' ? partyView : null; },
    matches(selector) { return selector === 'input[type="range"]'; }
  };

  listeners.change.forEach(listener => listener({type:'change', target:delegationRange}));
  schedule();
  assert.strictEqual(scheduleCalls, 0,
    'delegation slider commit must not schedule an optimizer run');
  assert.strictEqual(window.DelegationRunController.state().staged, true,
    'delegation slider commit must be retained as staged state');
  assert.strictEqual(partyRunStatus.textContent, 'Positions saved · optimizer not run yet');

  await Promise.resolve();
  schedule();
  assert.strictEqual(scheduleCalls, 1,
    'non-delegation scheduling must retain the existing dashboard behavior');

  assert(controllerSource.includes(
    "partyRun.addEventListener('click', () => dashboardRun.click())"),
    'delegation Run new run button must be wired to the Dashboard run button');
  window.DelegationRunController.run();
  assert.strictEqual(dashboardRunClicks, 1,
    'delegation Run new run must invoke the Dashboard Run again now button');
  assert.strictEqual(window.DelegationRunController.state().staged, false,
    'explicit run must clear the staged marker');

  console.log('delegation staging test passed');
})().catch(error => { console.error(error); process.exit(1); });

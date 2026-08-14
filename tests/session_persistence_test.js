const fs = require('fs');
const vm = require('vm');
const assert = require('assert');

const source = fs.readFileSync('web/session.js', 'utf8');

function storage(initial = {}) {
  const values = new Map(Object.entries(initial));
  return {
    getItem(key) { return values.has(key) ? values.get(key) : null; },
    setItem(key, value) { values.set(key, String(value)); },
    removeItem(key) { values.delete(key); },
    dump() { return Object.fromEntries(values); }
  };
}

const localStorage = storage();
const sessionStorage = storage();
const requests = [];
const controls = {};
for (const id of [
  'usTariff', 'retaliatoryTariff', 'canadaPriority', 'usPriority',
  'riskAversion', 'cooperationCeiling', 'canadaPriorityValue',
  'usPriorityValue', 'priorityTotal', 'riskAversionValue',
  'cooperationCeilingValue', 'retaliatoryTariffValue', 'partyView'
]) controls[id] = {value:'', textContent:'', hidden:true};

const document = {
  readyState:'complete',
  getElementById(id) { return controls[id] || null; },
  addEventListener() {}
};

class Headers {
  constructor(initial) { this.values = new Map(Object.entries(initial || {})); }
  set(key, value) { this.values.set(key, value); }
}
class Request {}

const context = {
  console,
  Date,
  Math,
  JSON,
  URL,
  Headers,
  Request,
  Uint8Array,
  localStorage,
  sessionStorage,
  document,
  location:{href:'http://localhost:8080/', origin:'http://localhost:8080'},
  crypto:{randomUUID:()=>'persistent-session-id'},
  fetch:(input, init={}) => {
    requests.push({input, init});
    return Promise.resolve({ok:true, json:async()=>({})});
  },
  prompt:()=>'',
  CAD_API_AUTH_REQUIRED:false,
  InitialOpeningScenario:{
    opening:{
      usTariff:50,
      retaliatoryTariff:5,
      usSectorCoverage:Array(20).fill(100),
      canadaSectorCoverage:Array(20).fill(100)
    }
  },
  positions:{us:Array(20).fill(100), canada:Array(20).fill(100)},
  updateTariff() {},
  updatePosition() {},
  syncPartyView() {},
  refreshPartySectorMetrics() {},
  evaluate:async()=>{}
};
context.globalThis = context;
context.window = context;
vm.createContext(context);
vm.runInContext(source, context);

assert.strictEqual(localStorage.getItem('cad-policy-studio.session-id'), 'persistent-session-id',
  'session identity must survive browser restarts');

const primary = {
  usTariff:37,
  retaliatoryTariff:11,
  canadaPriority:62,
  usPriority:38,
  riskAversion:71,
  cooperationCeiling:44
};
for (let i = 0; i < 20; i++) {
  primary['usSector' + i] = 10 + i;
  primary['canadaSector' + i] = 70 - i;
}

context.fetch('/api/evaluate', {
  method:'POST',
  headers:{'Content-Type':'application/json'},
  body:JSON.stringify(primary)
});
const savedAfterPrimary = JSON.parse(localStorage.getItem('cad-policy-studio.last-run-settings.v1'));
assert.strictEqual(savedAfterPrimary.usTariff, 37);
assert.strictEqual(savedAfterPrimary.retaliatoryTariff, 11);
assert.deepStrictEqual(Array.from(savedAfterPrimary.usSectorCoverage),
  Array.from({length:20}, (_, i) => 10 + i));
assert.deepStrictEqual(Array.from(savedAfterPrimary.canadaSectorCoverage),
  Array.from({length:20}, (_, i) => 70 - i));
const savedAt = savedAfterPrimary.savedAt;

// The controller deliberately schedules this derived comparison later. It must
// never replace the primary operator checkpoint regardless of timing.
context.fetch('/api/evaluate', {
  method:'POST',
  headers:{'Content-Type':'application/json'},
  body:JSON.stringify({...primary, usTariff:0, retaliatoryTariff:0, comparisonOnly:true})
});
const savedAfterComparison = JSON.parse(localStorage.getItem('cad-policy-studio.last-run-settings.v1'));
assert.strictEqual(savedAfterComparison.usTariff, 37);
assert.strictEqual(savedAfterComparison.retaliatoryTariff, 11);
assert.strictEqual(savedAfterComparison.savedAt, savedAt);

// Re-reading a checkpoint is observational and must not fabricate a newer save time.
const reread = context.CADLastRunSettings.read();
assert.strictEqual(reread.savedAt, savedAt);

// Simulate controls being reset during startup, then recover the exact run.
Object.values(controls).forEach(node => { node.value = '0'; node.textContent = ''; });
context.positions.us.fill(100);
context.positions.canada.fill(100);
context.InitialOpeningScenario.opening.usTariff = 50;
context.InitialOpeningScenario.opening.retaliatoryTariff = 5;
assert.strictEqual(context.CADLastRunSettings.restore(), true);
assert.strictEqual(controls.usTariff.value, '37');
assert.strictEqual(controls.retaliatoryTariff.value, '11');
assert.strictEqual(controls.canadaPriority.value, '62');
assert.strictEqual(controls.usPriority.value, '38');
assert.strictEqual(controls.riskAversion.value, '71');
assert.strictEqual(controls.cooperationCeiling.value, '44');
assert.deepStrictEqual(Array.from(context.positions.us), Array.from({length:20}, (_, i) => 10 + i));
assert.deepStrictEqual(Array.from(context.positions.canada), Array.from({length:20}, (_, i) => 70 - i));
assert.strictEqual(context.InitialOpeningScenario.opening.usTariff, 37);
assert.strictEqual(context.InitialOpeningScenario.opening.retaliatoryTariff, 11);

console.log('session persistence and interruption recovery test passed');

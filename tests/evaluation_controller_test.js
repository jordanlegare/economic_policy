const fs = require('fs');
const vm = require('vm');
const assert = require('assert');

const elements = new Map();
function element(id, value = '') {
  const node = {id, value:String(value), textContent:'', hidden:true, disabled:false, firstChild:{textContent:''}};
  elements.set('#' + id, node);
  return node;
}

element('run');
element('strategyLoading');
element('signal');
element('negotiationSync');
element('canadaPriority', 50);
element('usPriority', 50);
element('riskAversion', 50);
element('cooperationCeiling', 50);
element('retaliatoryTariff', 5);

global.window = {};
global.$ = selector => elements.get(selector) || null;
global.settings = {policyRate:2.75, gdpGrowth:1.6};
global.result = undefined;
global.noTariff = undefined;
global.selected = undefined;
global.openingCalibrated = false;
global.evaluationSequence = 0;
global.adjustingRanges = new Set();
global.tariff = {value:'50'};
global.positions = {canada:Array(20).fill(100), us:Array(20).fill(100)};
global.sameCoverage = (a,b) => Array.isArray(a) && Array.isArray(b) && a.length === b.length && a.every((v,i)=>+v===+b[i]);
global.schedule = () => { throw new Error('schedule should not be called during a settled evaluation'); };
let publishCount = 0;
global.publishNegotiation = actor => { assert.strictEqual(actor, 'automatic'); publishCount++; };
let renderCount = 0;
global.render = () => { renderCount++; };

global.applyRecommendation = () => {
  const rec = global.result.recommendation;
  const changed = !global.sameCoverage(rec.usSectorCoverage, global.positions.us)
    || !global.sameCoverage(rec.canadaSectorCoverage, global.positions.canada);
  global.positions.us.splice(0,20,...rec.usSectorCoverage);
  global.positions.canada.splice(0,20,...rec.canadaSectorCoverage);
  return changed;
};

const fullPayload = () => ({
  scenarios:[{id:'compact',name:'Compact'}],
  recommendation:{
    usSectorCoverage:Array(20).fill(50),
    canadaSectorCoverage:Array(20).fill(75)
  }
});
const comparisonPayload = () => ({scenarios:[{id:'compact',name:'Compact baseline'}]});
const requests = [];
global.fetch = async (url, options = {}) => {
  assert.strictEqual(url, '/api/evaluate');
  const payload = JSON.parse(options.body);
  requests.push(payload);
  return {
    ok:true,
    status:200,
    json:async()=>payload.comparisonOnly ? comparisonPayload() : fullPayload()
  };
};

// The production app declares evaluate before this controller is appended.
global.evaluate = async function originalEvaluate() { throw new Error('original evaluator should have been replaced'); };

vm.runInThisContext(fs.readFileSync('web/evaluation-controller.js','utf8'));

(async()=>{
  await evaluate();
  assert.strictEqual(requests.length, 2, 'first run should make one full request and one comparison request');
  assert.strictEqual(requests.filter(x=>x.comparisonOnly).length, 1);
  assert.strictEqual(requests.filter(x=>!x.comparisonOnly).length, 1);
  assert.strictEqual(publishCount, 1, 'verified recommendation should publish once without recursive evaluation');
  assert.strictEqual(renderCount, 1);
  assert.strictEqual(elements.get('#strategyLoading').hidden, true, 'loading overlay must be released');
  assert.strictEqual(elements.get('#run').disabled, false, 'run button must be restored');
  assert(global.positions.us.every(x=>x===50));
  assert(global.positions.canada.every(x=>x===75));

  requests.length = 0;
  await evaluate();
  assert.strictEqual(requests.length, 1, 'second run should reuse cached no-tariff comparison');
  assert.strictEqual(requests[0].comparisonOnly, false);
  assert.strictEqual(publishCount, 1, 'unchanged auto recommendation must not republish');
  assert.strictEqual(renderCount, 2);

  // A real user sector edit becomes the next negotiation anchor. Merely showing
  // the auto recommendation did not do so on the preceding run.
  global.positions.us[0] = 42;
  requests.length = 0;
  await evaluate();
  const full = requests.find(x=>!x.comparisonOnly);
  assert(full, 'expected full evaluation request');
  assert.strictEqual(full.usSector0, 42, 'explicit user sector edit must advance the evaluation anchor');
  assert.strictEqual(elements.get('#strategyLoading').hidden, true);

  console.log('evaluation controller test passed');
})().catch(error => { console.error(error); process.exit(1); });

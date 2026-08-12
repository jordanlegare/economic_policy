const fs = require('fs');
const vm = require('vm');
const assert = require('assert');

const elements = new Map();
function element(id, value = '') {
  const node = {
    id,
    value:String(value),
    textContent:'',
    hidden:true,
    disabled:false,
    firstChild:{textContent:''},
    querySelector(){ return null; }
  };
  elements.set('#' + id, node);
  return node;
}

element('run');
element('strategyLoading');
element('signal');
element('negotiationSync');
element('impactGrowth');
element('canadaPriority', 50);
element('usPriority', 50);
element('riskAversion', 50);
element('cooperationCeiling', 50);
element('retaliatoryTariff', 5);
element('retaliatoryTariffValue');
element('partyView').hidden = true;
const autoStatus = {textContent:''};

global.document = {
  querySelector(selector) {
    return selector === '.auto span' ? autoStatus : null;
  }
};
global.window = {__EVALUATION_COMPARISON_DELAY_MS:0};
global.$ = selector => elements.get(selector) || null;
global.settings = {
  policyRate:2.75,
  gdpGrowth:1.6,
  inflation:2.4,
  usInflation:2.7,
  unemployment:6.4,
  usGrowth:2.0,
  borderFriction:2.0,
  diversification:0
};
global.result = undefined;
global.noTariff = undefined;
global.selected = undefined;
global.openingCalibrated = false;
global.evaluationSequence = 0;
global.adjustingRanges = new Set();
global.tariff = {value:'50'};
global.positions = {canada:Array(20).fill(100), us:Array(20).fill(100)};
global.sameCoverage = (a,b) => Array.isArray(a) && Array.isArray(b) && a.length === b.length && a.every((v,i)=>+v===+b[i]);
global.signed = (x, suffix = '%') => (x >= 0 ? '+' : '') + Number(x).toFixed(1) + suffix;
global.schedule = () => { throw new Error('schedule should not be called during a settled evaluation'); };
global.updateTariff = () => {};
global.updatePosition = () => {};
global.syncPartyView = () => {};
global.renderPartySectors = () => {};
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

let recommendationVerified = true;
const fullPayload = () => ({
  scenarios:[{id:'compact',name:'Compact',growth:1.7}],
  recommendation:{
    strategyId:'compact',
    usSectorCoverage:Array(20).fill(50),
    canadaSectorCoverage:Array(20).fill(75),
    policyCandidatesVerified:301,
    globalSearchComplete:true,
    sectorCandidatesExamined:4200,
    sectorParetoFrontierSize:18,
    sectorFinalistsResimulated:18,
    verifiedCanadaScore:83,
    verifiedUsScore:86,
    verifiedWinWin:recommendationVerified,
    growthConstraintMet:true
  }
});
const comparisonPayload = () => ({scenarios:[{id:'compact',name:'Compact baseline',growth:2.0}]});
const calibratedUsCoverage = Array(20).fill(0);
const calibratedCanadaCoverage = Array(20).fill(0);
[0,1,4].forEach(i => {
  calibratedUsCoverage[i] = 100;
  calibratedCanadaCoverage[i] = 100;
});
const calibrationPayload = () => ({
  effectiveState:{
    usTariff:5,
    retaliatoryTariff:1.5,
    usSectorCoverage:calibratedUsCoverage,
    canadaSectorCoverage:calibratedCanadaCoverage
  }
});

const requests = [];
let calibrationFetches = 0;
let releaseComparison;
let comparisonGate = new Promise(resolve => { releaseComparison = resolve; });
global.fetch = async (url, options = {}) => {
  if (url === '/api/calibration') {
    calibrationFetches++;
    return {ok:true,status:200,json:async()=>calibrationPayload()};
  }
  assert.strictEqual(url, '/api/evaluate');
  const payload = JSON.parse(options.body);
  requests.push(payload);
  if (payload.comparisonOnly) await comparisonGate;
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
  assert.strictEqual(calibrationFetches, 1,
    'certified tariff/sector state must seed the initial displayed evaluation exactly once');
  assert.strictEqual(requests.length, 1,
    'initial loading path must await only the real policy evaluation');
  assert.strictEqual(requests[0].comparisonOnly, false);
  assert.strictEqual(requests[0].usTariff, 5,
    'initial solve must use the certified U.S. tariff shown to the user');
  assert.strictEqual(requests[0].retaliatoryTariff, 1.5,
    'initial solve must use the certified Canadian tariff shown to the user');
  calibratedUsCoverage.forEach((value, i) => assert.strictEqual(requests[0]['usSector'+i], value));
  calibratedCanadaCoverage.forEach((value, i) => assert.strictEqual(requests[0]['canadaSector'+i], value));
  assert.strictEqual(publishCount, 1,
    'verified recommendation should publish both delegations once without recursive evaluation');
  assert.strictEqual(renderCount, 1);
  assert.strictEqual(elements.get('#strategyLoading').hidden, true,
    'loading overlay must be released before the no-tariff comparator finishes');
  assert.strictEqual(elements.get('#run').disabled, false,
    'run button must be restored before the comparator finishes');
  assert.strictEqual(elements.get('#impactGrowth').textContent, 'Calculating…');
  assert(global.positions.us.every(x=>x===50));
  assert(global.positions.canada.every(x=>x===75));
  assert(autoStatus.textContent.includes('Auto-apply verified win-win agreement ON'));
  assert(autoStatus.textContent.includes('301 policy candidates verified'));
  assert(autoStatus.textContent.includes('complete declared startup grid'));
  assert.strictEqual(window.EvaluationController.state().verifiedWinWin, true);
  assert.strictEqual(window.EvaluationController.state().globalSearchComplete, true);
  assert.strictEqual(window.EvaluationController.state().initialCalibrationApplied, true);
  assert(window.EvaluationController.state().autoAppliedCoverage.us.every(x=>x===50));
  assert.deepStrictEqual(window.EvaluationController.state().searchAnchor.us, calibratedUsCoverage,
    'auto-applied winning agreement must not become a new concession anchor');

  // The delegation table must expose a live bilateral response surface instead
  // of repeating only the metric vector at the verified recommendation. Use the
  // status-quo strategy here because its relief envelope leaves sector coverage
  // free to move across the declared search grid.
  const savedStrategy = global.result.recommendation.strategyId;
  const savedScenarioId = global.result.scenarios[0].id;
  global.result.recommendation.strategyId = 'statusquo';
  global.result.scenarios[0].id = 'statusquo';
  const verifiedMetric = window.EvaluationController.sectorMetrics(0, 50, 75);
  const exploreUs = window.EvaluationController.sectorMetrics(0, 75, 75);
  const exploreCanada = window.EvaluationController.sectorMetrics(0, 50, 50);
  assert(verifiedMetric && exploreUs && exploreCanada,
    'sector response model must produce metrics for the delegation table');
  assert.strictEqual(verifiedMetric.verified, true,
    'the auto-applied recommendation must remain visibly identifiable');
  assert.strictEqual(exploreUs.verified, false,
    'moving a delegation slider must switch the row into exploration mode');
  assert.strictEqual(exploreUs.insideSearchEnvelope, true,
    'live metrics should cover the same feasible relief envelope searched by the engine');
  assert.notStrictEqual(exploreUs.canada.output, verifiedMetric.canada.output,
    'U.S. coverage must immediately change the Canadian sector outcome');
  assert.notStrictEqual(exploreUs.canada.score, verifiedMetric.canada.score,
    'U.S. coverage must immediately change the Canadian balanced-deal score');
  assert.notStrictEqual(exploreCanada.us.prices, verifiedMetric.us.prices,
    'Canadian retaliation coverage must immediately change the U.S. price outcome');
  global.result.recommendation.strategyId = savedStrategy;
  global.result.scenarios[0].id = savedScenarioId;

  const appSource = fs.readFileSync('web/app.js','utf8');
  assert(appSource.includes('window.EvaluationController?.sectorMetrics?.('),
    'party table must read live response metrics from the evaluation controller');
  assert(appSource.includes("updatePartySectorMetric(i,input.parentElement.querySelector('.sector-deal-metric'))"),
    'party slider input must repaint its metric before re-optimization completes');

  // Let the deferred comparison start. It is deliberately unresolved here;
  // the full-screen loading state must already be gone.
  await new Promise(resolve => setTimeout(resolve, 0));
  assert.strictEqual(requests.length, 2);
  assert.strictEqual(requests.filter(x=>x.comparisonOnly).length, 1);
  assert.strictEqual(elements.get('#strategyLoading').hidden, true);

  releaseComparison();
  await window.EvaluationController.waitForComparison();
  assert.strictEqual(elements.get('#impactGrowth').textContent, '-0.3 pp');

  requests.length = 0;
  comparisonGate = Promise.resolve();
  await evaluate();
  assert.strictEqual(calibrationFetches, 1,
    'calibration seeding must not overwrite later scenarios');
  assert.strictEqual(requests.length, 1,
    'subsequent runs should reuse the cached no-tariff comparison');
  assert.strictEqual(requests[0].comparisonOnly, false);
  assert.strictEqual(requests[0].usTariff, 5);
  assert.strictEqual(requests[0].retaliatoryTariff, 1.5);
  assert.strictEqual(requests[0].usSector0, calibratedUsCoverage[0],
    'unchanged auto display must continue solving from the certified negotiation anchor');
  assert.strictEqual(publishCount, 1, 'unchanged auto recommendation must not republish');
  assert.strictEqual(renderCount, 2);

  // A real user sector edit becomes the next negotiation anchor. The verified
  // response is then auto-applied back onto both delegations' visible sliders.
  global.positions.us[0] = 42;
  requests.length = 0;
  await evaluate();
  const full = requests.find(x=>!x.comparisonOnly);
  assert(full, 'expected full evaluation request');
  assert.strictEqual(full.usSector0, 42,
    'explicit user sector edit must advance the evaluation anchor');
  assert.strictEqual(calibrationFetches, 1,
    'what-if controls must not be re-seeded from calibration after startup');
  assert.strictEqual(global.positions.us[0], 50,
    'verified result must auto-apply the winning U.S. sector coverage');
  assert(global.positions.canada.every(x=>x===75),
    'verified result must keep the Canadian side synchronized too');
  assert.strictEqual(publishCount, 2,
    'changed verified agreement must publish exactly once for both delegations');
  assert.strictEqual(elements.get('#strategyLoading').hidden, true);

  // An unverified package must never overwrite either delegation's current
  // posture, even though the engine still explored and returned a recommendation.
  recommendationVerified = false;
  global.positions.us[0] = 61;
  global.positions.canada[0] = 73;
  requests.length = 0;
  await evaluate();
  const unverifiedRequest = requests.find(x=>!x.comparisonOnly);
  assert.strictEqual(unverifiedRequest.usSector0, 61);
  assert.strictEqual(unverifiedRequest.canadaSector0, 73);
  assert.strictEqual(global.positions.us[0], 61);
  assert.strictEqual(global.positions.canada[0], 73);
  assert.strictEqual(publishCount, 2,
    'unverified recommendation must not be published or auto-applied');
  assert(autoStatus.textContent.includes('paused'));
  assert.strictEqual(window.EvaluationController.state().verifiedWinWin, false);
  assert.strictEqual(calibrationFetches, 1);

  console.log('evaluation controller test passed');
})().catch(error => { console.error(error); process.exit(1); });
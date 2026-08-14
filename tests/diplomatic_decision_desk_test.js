const fs = require('fs');
const vm = require('vm');
const assert = require('assert');

global.window = {};
global.document = {
  readyState: 'loading',
  addEventListener(){},
  querySelector(){ return null; },
  querySelectorAll(){ return []; }
};
global.localStorage = {getItem(){return null;}, setItem(){}};
global.fetch = async () => ({ok:true, json:async()=>({})});
global.MutationObserver = function(){ this.observe=()=>{}; };

const source = fs.readFileSync('web/diplomat.js', 'utf8');
vm.runInThisContext(source);
const desk = window.DiplomaticDecisionDesk;
assert(desk, 'decision desk helpers must be exposed');

const generated = (id, diversify, score=10) => ({
  id,
  name: `Generated policy mix ${id.replace('custom-','')}`,
  move: -25,
  fiscal: .6,
  score,
  description: `Generated policy mix ${id.replace('custom-','')} of 288: ease 25 bp, 0.6% fiscal impulse, 0% negotiated rate relief, productive share 90%, diversification ${diversify}%.`
});

const scenarios = [
  generated('custom-96',66,10),
  generated('custom-94',66,9.9),
  generated('custom-92',51,9.8),
  ...Array.from({length:8},(_,i)=>({id:`expert-${i}`,name:`Expert ${i}`,score:9-i}))
];
const unique = desk.uniqueScenarios(scenarios,7);
assert.strictEqual(unique.length,7);
assert.strictEqual(unique[0].id,'custom-96','highest-ranked generated duplicate must survive');
assert(!unique.some(x=>x.id==='custom-94'),'same visible generated controls must deduplicate despite different mix IDs');
assert(unique.some(x=>x.id==='custom-92'),'materially different diversification must remain distinct');
assert.strictEqual(new Set(unique.map(desk.scenarioIdentityKey)).size,unique.length);

const primaryPackage = {id:'p1',strategy:'Strategy A',issues:[{id:'tariff',canadaMove:10,usMove:20}]};
const duplicateBridge = {id:'p2',strategy:'Strategy A',issues:[{id:'tariff',canadaMove:10,usMove:20}]};
const model = {
  best: primaryPackage,
  bridge: duplicateBridge,
  whereWeAre:[
    'Round 4, phase: conditional exchange.',
    'Dashboard evaluated state: U.S. tariff 35.0%.',
    'Model trust: VERIFIED.',
    'Country-specific BATNAs remain independent.',
    'Country-specific BATNAs remain independent.'
  ],
  decisionRequired:[
    'Authorize package A.',
    '  Authorize package A.  ',
    'Preserve the BATNA.',
    'Require reciprocity.',
    'Review legal authority.'
  ],
  whatChanged:['Tariff setting changed.','Tariff setting changed.','Round advanced.'],
  whatTheyWant:['Procurement access.','Procurement access.','Tariff relief.'],
  whatWeWant:['Reciprocal tariff relief.','Reciprocal tariff relief.'],
  redLines:{mandate:['Procurement requires approval.','Procurement requires approval.','Tariff relief <= 60.']},
  uncertainties:['Elasticity is uncertain.','Elasticity is uncertain.','Pass-through is uncertain.','Political acceptance is not modeled.'],
  recommendedLanguage:[
    {label:'Opening',text:'Reciprocity first.'},
    {label:'Duplicate',text:'Reciprocity first.'},
    {label:'Close',text:'Sequence implementation.'}
  ],
  evidenceSources:[
    {agency:'Statistics Canada',dataset:'Trade',vintage:'2025',status:'loaded',domain:'statcan.gc.ca'},
    {agency:'Statistics Canada',dataset:'Trade',vintage:'2025',status:'loaded',domain:'statcan.gc.ca'},
    {agency:'USITC',dataset:'HTS',vintage:'2026',status:'loaded',domain:'usitc.gov'}
  ]
};
const concessions = [
  {name:'Autos',usRelief:20,caRelief:10,total:30,posture:'Mutual move'},
  {name:'Autos',usRelief:20,caRelief:10,total:30,posture:'Mutual move'},
  {name:'Energy',usRelief:0,caRelief:0,total:0,posture:'Hold'},
  {name:'Agriculture',usRelief:12,caRelief:0,total:12,posture:'U.S. move'},
  {name:'Services',usRelief:0,caRelief:0,total:0,posture:'Hold'}
];
const view = desk.buildDeskView(model, scenarios, concessions);
assert.deepStrictEqual(view.decisions,['Authorize package A.','Preserve the BATNA.','Require reciprocity.']);
assert.deepStrictEqual(view.whereWeAre,['Round 4, phase: conditional exchange.','Country-specific BATNAs remain independent.']);
assert.deepStrictEqual(view.changes,['Tariff setting changed.','Round advanced.']);
assert.deepStrictEqual(view.theyWant,['Procurement access.','Tariff relief.']);
assert.strictEqual(view.language.length,2,'recommended language must deduplicate on operative text');
assert.strictEqual(view.evidence.length,2,'evidence rows must deduplicate');
assert.strictEqual(view.bridge,null,'a bridge materially identical to the primary package must not consume a desk slot');
assert.strictEqual(desk.bargainingPackageKey(primaryPackage),desk.bargainingPackageKey(duplicateBridge));
assert.strictEqual(view.concessions.length,2,'material sector moves should suppress repetitive holds when material rows exist');
assert.strictEqual(view.concessions[0].name,'Autos');
assert.strictEqual(view.concessions[1].name,'Agriculture');
assert(source.includes('window.PrincipalBriefing.synchronize'),'desk must use the principal synchronization contract');
assert(source.includes('window.PrincipalBriefing.buildModel'),'desk must derive its narrative from the principal brief model');
assert(source.includes('One synchronized situation · one negotiating narrative'));
assert(source.includes('What the U.S. wants'));
assert(source.includes('What Canada wants'));
assert(source.includes('Key uncertainties'));
assert(source.includes('best unique policy scenarios'));

console.log('diplomatic decision desk test passed');

const fs=require('fs');
const vm=require('vm');
const assert=require('assert');

global.window={};
global.document={readyState:'loading',addEventListener(){},querySelector(){return null;},querySelectorAll(){return[]}};
global.localStorage={getItem(){return null;},setItem(){}};
global.navigator={};
global.fetch=async()=>({ok:true,json:async()=>({})});
vm.runInThisContext(fs.readFileSync('web/principal-briefing.js','utf8'));

const issue=(id,label,canadaMove,usMove)=>({id,label,canadaMove,usMove});
const p1={id:'pareto-1',strategyId:'joint-growth',strategyName:'Joint growth compact',canadaUtility:66,usUtility:64,canadaSurplus:8,usSurplus:7,stabilityScore:91,issues:[
  issue('us-tariff-relief','U.S. tariff relief',0,75),issue('canada-tariff-relief','Canadian retaliatory-tariff relief',50,0),issue('border-facilitation','Border and standards facilitation',50,50),issue('procurement','Reciprocal procurement access',25,25),issue('supply-chain','North American supply-chain commitment',25,25)
]};
const p2={id:'pareto-2',strategyId:'bridge',strategyName:'Reciprocal bridge',canadaUtility:63,usUtility:65,canadaSurplus:6,usSurplus:8,stabilityScore:95,issues:[
  issue('us-tariff-relief','U.S. tariff relief',0,50),issue('canada-tariff-relief','Canadian retaliatory-tariff relief',25,0),issue('border-facilitation','Border and standards facilitation',75,75),issue('procurement','Reciprocal procurement access',25,25),issue('supply-chain','North American supply-chain commitment',50,50)
]};
const result={
  negotiation:{recommendedPackage:p1,frontier:[p1,p2],batna:{canada:51.2,us:50.4,canadaStrategy:'Canada outside option',usStrategy:'U.S. outside option'},reservation:{canada:52.1,us:51.3}},
  robustness:{recommendedPackageId:'pareto-1',requiredJointClearProbability:.80,secondStageMonteCarloDraws:5000,seed:20260811,uncertaintyGrade:'mixed empirical and model uncertainty',empiricallyCalibrated:false,parameterDistributions:[
    {name:'trade_elasticity',mean:.65,standardDeviation:.12,evidenceClass:'assumption',source:'calibration fallback'},
    {name:'border_friction',mean:2,standardDeviation:.3,evidenceClass:'official-derived',source:'border series'}
  ],packages:[
    {packageId:'pareto-1',jointClearProbability:.86,canadaClearProbability:.93,usClearProbability:.91,canadaCvar10Surplus:2.4,usCvar10Surplus:1.7,maxRegret:.72,rankWinProbability:.61,canadaCi95:[1.1,14.2],usCi95:[.4,13.8]},
    {packageId:'pareto-2',jointClearProbability:.91,canadaClearProbability:.92,usClearProbability:.96,canadaCvar10Surplus:1.8,usCvar10Surplus:2.1,maxRegret:.94,rankWinProbability:.36,canadaCi95:[.5,12.8],usCi95:[1.0,15.0]}
  ]},
  calibration:{snapshotId:'ca-us-2026-08-11',grade:'partial-official',completeness:68,certifiedForEmpiricalUse:false,checks:{officialTrade:true,tariffLines:false,inputOutput:false,originUtilization:false,elasticitiesEstimated:false,passThroughEstimated:false},sources:[
    {id:'statcan_trade',agency:'Statistics Canada',dataset:'Canadian international merchandise trade',vintage:'2025 annual',status:'loaded',url:'https://www150.statcan.gc.ca/example'},
    {id:'usitc_hts',agency:'USITC',dataset:'Harmonized Tariff Schedule',vintage:'2026 Revision 12',status:'registered',url:'https://hts.usitc.gov/'}
  ]},
  tradeDiplomacy:{bridgePackageId:'pareto-2',operationalReadiness:82,readinessLabel:'strong with implementation conditions'}
};
const room={round:4,phase:'conditional exchange',revision:19,offers:[
  {revision:14,round:3,side:'canada',packageId:'pareto-2',note:'Bridge offered conditionally'},
  {revision:18,round:4,side:'us',packageId:'pareto-2',note:'U.S. asks for Canadian tariff movement and procurement access'},
  {revision:19,round:4,side:'canada',packageId:'pareto-1',note:'Canadian counter tied to U.S. tariff relief'}
],mandate:[
  {issueId:'procurement',maxCanadaMove:30,minUsMove:20,authority:'senior_approval_required',hardRedLine:false,note:'Senior approval before final commitment'},
  {issueId:'canada-tariff-relief',maxCanadaMove:60,minUsMove:0,authority:'delegation_discretion',hardRedLine:true,note:'Do not exceed authorized retaliatory relief'}
],debriefs:[{revision:17,round:3,summary:'U.S. showed flexibility on border facilitation.',counterpartSignals:'Procurement remains sensitive.',unresolved:'Tariff sequencing.',nextActions:'Test a reciprocal tariff-for-border sequence.'}],concessionBalance:{canadaGiven:4,usGiven:3,usToCanadaRatio:.75},counteroffers:[{category:'Bridge',packageId:'pareto-2'}]};
const model=window.PrincipalBriefing.buildModel({result,room,settings:{usTariff:35,retaliatoryTariff:10},redlines:{minCanada:45,minUs:45,maxRecession:40,minGrowth:0,maxInflation:3.5},notes:'Principal wants a narrow opening package.',ledger:[{status:'Bridge option',packageId:'pareto-2',rationale:'Held before round 4'}],now:'2026-08-11T12:00:00Z'});

assert.strictEqual(model.best.id,'pareto-1');
assert.strictEqual(model.bridge.id,'pareto-2');
assert(model.whatTheyWant.join(' ').includes('latest recorded U.S. offer'));
assert(model.whatTheyWant.join(' ').includes('Canadian retaliatory-tariff relief'));
assert(model.redLines.mandate.join(' ').includes('Procurement'));
assert(model.whatChanged.length>0);
assert(model.decisionRequired.join(' ').includes('authority')||model.decisionRequired.join(' ').includes('reciprocity'));
assert.strictEqual(model.batna.canada,51.2);
assert(model.uncertainties.some(x=>x.includes('Missing/uncertified layers')));
assert(model.evidenceSources.some(x=>x.agency==='Statistics Canada'));

const bytes=window.PrincipalBriefing.buildPdf(model);
const pdf=Buffer.from(bytes).toString('latin1');
assert(pdf.startsWith('%PDF-1.4'));
assert(pdf.endsWith('%%EOF\n'));
assert(bytes.length>12000);
assert(pdf.includes('/BaseFont /Helvetica-Bold'));
assert(pdf.includes('/BaseFont /Helvetica-Oblique'));
assert(pdf.includes('0.055 0.125 0.205 rg'));
for(const heading of ['WHERE WE ARE','WHAT THEY WANT','WHAT WE WANT','WHAT CHANGED','RED LINES','BEST PACKAGE','BRIDGE PACKAGE','BATNA','KEY UNCERTAINTIES','DECISION REQUIRED TODAY','RECOMMENDED LANGUAGE','EVIDENCE SOURCES'])assert(pdf.includes(heading),`missing ${heading}`);
assert((pdf.match(/\/Type \/Page /g)||[]).length>=3);
assert(pdf.includes('WORKING BRIEF - ANALYTICAL SUPPORT'));
assert(pdf.includes('Snapshot ca-us-2026-08-11'));

const noOffer=window.PrincipalBriefing.buildModel({result,room:{round:1,phase:'preparation',mandate:[],offers:[],debriefs:[],concessionBalance:{}},settings:{},redlines:{minCanada:45,minUs:45,maxRecession:40,minGrowth:0,maxInflation:3.5},now:'2026-08-11T12:00:00Z'});
assert(noOffer.whatTheyWant.join(' ').includes('No U.S. package has been recorded'));
assert(noOffer.whatTheyWant.join(' ').includes('Do not describe model-implied U.S. utility as a stated U.S. ask'));
console.log('principal briefing test passed');

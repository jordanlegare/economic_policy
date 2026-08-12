const fs=require('fs');
const vm=require('vm');
const assert=require('assert');

global.window={};
global.document={readyState:'loading',addEventListener(){},querySelector(){return null;},querySelectorAll(){return[]}};
global.localStorage={getItem(){return null;},setItem(){}};
global.navigator={};
global.fetch=async()=>({ok:true,json:async()=>({})});
const principalSource=fs.readFileSync('web/principal-briefing.js','utf8');
vm.runInThisContext(principalSource);

const issue=(id,label,canadaMove,usMove)=>({id,label,canadaMove,usMove});
const p1={id:'pareto-1',strategyId:'joint-growth',strategyName:'Joint growth compact',verifiedWinWin:true,canadaUtility:66,usUtility:64,canadaSurplus:8,usSurplus:7,stabilityScore:91,issues:[
  issue('us-tariff-relief','U.S. tariff relief',0,75),issue('canada-tariff-relief','Canadian retaliatory-tariff relief',50,0),issue('border-facilitation','Border and standards facilitation',50,50),issue('procurement','Reciprocal procurement access',25,25),issue('supply-chain','North American supply-chain commitment',25,25)
]};
const p2={id:'pareto-2',strategyId:'bridge',strategyName:'Reciprocal bridge',verifiedWinWin:true,canadaUtility:63,usUtility:65,canadaSurplus:6,usSurplus:8,stabilityScore:95,issues:[
  issue('us-tariff-relief','U.S. tariff relief',0,50),issue('canada-tariff-relief','Canadian retaliatory-tariff relief',25,0),issue('border-facilitation','Border and standards facilitation',75,75),issue('procurement','Reciprocal procurement access',25,25),issue('supply-chain','North American supply-chain commitment',50,50)
]};
const result={
  scenarios:[
    {id:'joint-growth',name:'Joint growth compact',growth:1.8,usGrowth:2.1,inflation:2.4,recessionRisk:18},
    {id:'bridge',name:'Reciprocal bridge',growth:1.6,usGrowth:2.0,inflation:2.3,recessionRisk:16},
    {id:'canada-batna',name:'Canada outside option',growth:.9,usGrowth:1.7,inflation:2.6,unemployment:6.8,recessionRisk:28,exportChange:-3.2,realIncomeGrowth:.4,costOfLiving:2.8,debtGdp:43.1,usExportChange:-1.1,usTariffRevenueUsd:42.0},
    {id:'us-batna',name:'U.S. outside option',growth:1.0,usGrowth:2.2,inflation:2.5,unemployment:6.7,recessionRisk:24,exportChange:-2.7,realIncomeGrowth:.5,costOfLiving:2.7,debtGdp:42.8,usExportChange:1.3,usTariffRevenueUsd:58.4}
  ],
  recommendation:{verifiedWinWin:true,growthConstraintMet:true,globalSearchComplete:true,policyCandidatesVerified:301,sectorFinalistsResimulated:18},
  negotiation:{recommendedPackage:p1,frontier:[p1,p2],frontierComplete:true,trust:{dataIntegrityPass:true},batna:{canada:51.2,us:50.4,canadaStrategy:'Canada outside option',usStrategy:'U.S. outside option'},reservation:{canada:52.1,us:51.3}},
  robustness:{recommendedPackageId:'pareto-1',candidateSetComplete:true,requiredJointClearProbability:.80,secondStageMonteCarloDraws:5000,seed:20260811,uncertaintyGrade:'mixed empirical and model uncertainty',empiricallyCalibrated:false,parameterDistributions:[
    {name:'trade_elasticity',mean:.65,standardDeviation:.12,evidenceClass:'assumption',source:'calibration fallback'},
    {name:'border_friction',mean:2,standardDeviation:.3,evidenceClass:'official-derived',source:'border series'}
  ],packages:[
    {packageId:'pareto-1',clearsProbabilityGate:true,jointClearProbability:.86,canadaClearProbability:.93,usClearProbability:.91,canadaCvar10Surplus:2.4,usCvar10Surplus:1.7,maxRegret:.72,rankWinProbability:.61,canadaCi95:[1.1,14.2],usCi95:[.4,13.8]},
    {packageId:'pareto-2',clearsProbabilityGate:true,jointClearProbability:.91,canadaClearProbability:.92,usClearProbability:.96,canadaCvar10Surplus:1.8,usCvar10Surplus:2.1,maxRegret:.94,rankWinProbability:.36,canadaCi95:[.5,12.8],usCi95:[1.0,15.0]}
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
],debriefs:[{revision:17,round:3,summary:'U.S. showed flexibility on border facilitation.',counterpartSignals:'Procurement remains sensitive.',unresolved:'Tariff sequencing.',nextActions:'Test a reciprocal tariff-for-border sequence.'}],concessionBalance:{canadaGiven:4,usGiven:3,usToCanadaRatio:.75},counteroffers:[
  {revision:10,category:'Bridge',packageId:'pareto-1'},
  {revision:20,category:'Bridge',packageId:'pareto-2'}
]};
const displayedUs=Array(20).fill(50),displayedCanada=Array(20).fill(75),searchUs=Array(20).fill(100),searchCanada=Array(20).fill(100);
const model=window.PrincipalBriefing.buildModel({result,room,settings:{usTariff:35,retaliatoryTariff:10,canadaPriority:55,usPriority:45,riskAversion:62,cooperationCeiling:50,usSectorCoverage:displayedUs,canadaSectorCoverage:displayedCanada},dashboard:{displayedCoverage:{us:displayedUs,canada:displayedCanada},searchAnchor:{us:searchUs,canada:searchCanada}},selectedScenario:result.scenarios[1],redlines:{minCanada:45,minUs:45,maxRecession:40,minGrowth:0,maxInflation:3.5},notes:'Principal wants a narrow opening package.',ledger:[
  {timestamp:'2026-08-11T10:00:00Z',status:'Bridge option',packageId:'pareto-2',rationale:'Held before round 4'},
  {timestamp:'2026-08-11T11:00:00Z',status:'Proposed',packageId:'pareto-1',rationale:'Latest recorded decision'}
],now:'2026-08-11T12:00:00Z'});

assert.strictEqual(model.best.id,'pareto-1');
assert.strictEqual(model.best.metricsAvailable,true);
assert.strictEqual(model.robustPromoted,true,'complete candidate set plus probability gate must promote robust package');
assert.strictEqual(model.primaryKind,'robust');
assert.strictEqual(model.modelTrustStatus,'ROBUST BEST WIN-WIN ON DECLARED STARTUP GRID');
assert.strictEqual(model.bridge.id,'pareto-2','latest bridge counteroffer must win over older bridge records');
assert(model.whatTheyWant.join(' ').includes('latest recorded U.S. offer'));
assert(model.whatTheyWant.join(' ').includes('Canadian retaliatory-tariff relief'));
assert(model.redLines.mandate.join(' ').toLowerCase().includes('procurement'));
assert(model.whatChanged.length>0);
assert(model.decisionRequired.join(' ').includes('authority')||model.decisionRequired.join(' ').includes('reciprocity'));
assert.strictEqual(model.batna.canada,51.2);
assert.strictEqual(model.batna.us,50.4);
assert.strictEqual(model.batna.canadaScenario.id,'canada-batna','Canadian BATNA economics must come from the BATNA strategy, not the selected card');
assert.strictEqual(model.batna.usScenario.id,'us-batna','U.S. BATNA economics must come from the independently selected U.S. outside option');
assert.strictEqual(model.batna.independentOutsideOptions,true);
assert(Math.abs(model.batna.primary.canadaOverBatna-14.8)<1e-9);
assert(Math.abs(model.batna.primary.usOverBatna-13.6)<1e-9);
assert(Math.abs(model.batna.primary.canadaOverReservation-13.9)<1e-9);
assert(Math.abs(model.batna.primary.usOverReservation-12.7)<1e-9);
assert(Math.abs(model.batna.bridge.canadaOverBatna-11.8)<1e-9);
assert(Math.abs(model.batna.bridge.usOverBatna-14.6)<1e-9);
assert(model.decisionRequired.some(x=>x.includes('improves modeled utility over walking away')));
assert(model.whereWeAre.some(x=>x.includes('outside options are selected independently')));
assert(model.whereWeAre.some(x=>x.includes('do not change when the dashboard inspection card changes')));
assert(model.uncertainties.some(x=>x.includes('Missing/uncertified layers')));
assert(model.evidenceSources.some(x=>x.agency==='Statistics Canada'));
assert(model.whereWeAre.some(x=>x.includes('Dashboard evaluated state: U.S. tariff 35.0%')));
assert(model.whereWeAre.some(x=>x.includes('Canada/U.S. priority 55/45')));
assert(model.whereWeAre.some(x=>x.includes('displayed sector coverage averages U.S. 50.0% and Canada 75.0%')));
assert(model.whereWeAre.some(x=>x.includes('Negotiation/search posture is intentionally separate')),'brief must distinguish displayed verified agreement from next search anchor');
assert(model.whereWeAre.some(x=>x.includes('next search anchor averages U.S. 100.0% and Canada 100.0%')));
assert(model.whereWeAre.some(x=>x.includes('Agreement verification: verified win-win')));
assert(model.whereWeAre.some(x=>x.includes('global policy/sector search complete')));
assert(model.whereWeAre.some(x=>x.includes('Dashboard leading policy scenario: Joint growth compact')));
assert(model.whereWeAre.some(x=>x.includes('Dashboard inspection card: Reciprocal bridge')));
assert(model.whereWeAre.some(x=>x.includes('does not replace the primary bargaining anchor or either side\'s BATNA')));
assert(model.whereWeAre.some(x=>x.includes('Latest recorded decision status: Proposed on pareto-1')),'decision ledger must use newest timestamp, not array position');
assert.strictEqual(model.dashboardState.usTariff,35);
assert.strictEqual(model.dashboardState.verifiedWinWin,true);
assert.deepStrictEqual(model.dashboardState.searchAnchor.us,searchUs);
assert.strictEqual(model.leadingScenario.id,'joint-growth');
assert.strictEqual(model.inspectedScenario.id,'bridge');

const differentInspection=window.PrincipalBriefing.buildModel({result,room,settings:{usTariff:35,retaliatoryTariff:10},selectedScenario:result.scenarios[0],now:'2026-08-11T12:00:00Z'});
assert.strictEqual(differentInspection.inspectedScenario.id,'joint-growth');
assert.strictEqual(differentInspection.batna.canadaScenario.id,'canada-batna');
assert.strictEqual(differentInspection.batna.usScenario.id,'us-batna');
assert.strictEqual(differentInspection.batna.primary.canadaOverBatna,model.batna.primary.canadaOverBatna,'BATNA/deal premium must be invariant to inspected scenario');

const preview=window.PrincipalBriefing.previewHtml(model);
assert(preview.includes('Deal vs. walk-away (BATNA)'));
assert(preview.includes('Canadian modeled outside option'));
assert(preview.includes('U.S. modeled outside option'));
assert(preview.includes('Engine scenario provenance — not a negotiating package:'));
assert(preview.includes('Canada outside option'));
assert(preview.includes('U.S. outside option'));
assert(preview.includes('14.80'));
assert(preview.includes('Canada vs BATNA'));
assert(preview.includes('Country-specific BATNA · independent of inspected card'));

const bytes=window.PrincipalBriefing.buildPdf(model);
fs.mkdirSync('artifacts',{recursive:true});
fs.writeFileSync('artifacts/principal-briefing-smoke.pdf',Buffer.from(bytes));
const pdf=Buffer.from(bytes).toString('latin1');
assert(pdf.startsWith('%PDF-1.4'));
assert(pdf.endsWith('%%EOF\n'));
assert(bytes.length>12000);
assert(pdf.includes('/BaseFont /Helvetica-Bold'));
assert(pdf.includes('/BaseFont /Helvetica-Oblique'));
assert(pdf.includes('0.055 0.125 0.205 rg'));
for(const heading of ['WHERE WE ARE','WHAT THEY WANT','WHAT WE WANT','WHAT CHANGED','RED LINES','PRIMARY PACKAGE','BRIDGE PACKAGE','BATNA','KEY UNCERTAINTIES','DECISION REQUIRED TODAY','RECOMMENDED LANGUAGE','EVIDENCE SOURCES'])assert(pdf.includes(heading),`missing ${heading}`);
assert(pdf.includes('DEAL VS WALK-AWAY'));
assert(pdf.includes('AGREEMENT PREMIUM OVER WALK-AWAY'));
assert(pdf.includes('Canada walk-away benchmark'));
assert(pdf.includes('U.S. walk-away benchmark'));
assert(pdf.includes('not a negotiating package'));
assert(pdf.includes('Canada outside option'));
assert(pdf.includes('U.S. outside option'));
assert((pdf.match(/\/Type \/Page /g)||[]).length>=3);
assert(pdf.includes('WORKING BRIEF - ANALYTICAL SUPPORT'));
assert(pdf.includes('Snapshot ca-us-2026-08-11'));

const noOffer=window.PrincipalBriefing.buildModel({result,room:{round:1,phase:'preparation',mandate:[],offers:[],debriefs:[],concessionBalance:{}},settings:{},redlines:{minCanada:45,minUs:45,maxRecession:40,minGrowth:0,maxInflation:3.5},now:'2026-08-11T12:00:00Z'});
assert(noOffer.whatTheyWant.join(' ').includes('No U.S. package has been recorded'));
assert(noOffer.whatTheyWant.join(' ').includes('Do not describe model-implied U.S. utility as a stated U.S. ask'));

const missingBatnaScenarioResult=JSON.parse(JSON.stringify(result));
missingBatnaScenarioResult.negotiation.batna.canadaStrategy='Historical Canadian outside option';
const missingBatnaScenario=window.PrincipalBriefing.buildModel({result:missingBatnaScenarioResult,room,settings:{},now:'2026-08-11T12:00:00Z'});
assert.strictEqual(missingBatnaScenario.batna.canadaScenario,null);
assert(/matching scenario detail is unavailable/i.test(window.PrincipalBriefing.previewHtml(missingBatnaScenario)));

const missingRobustResult=JSON.parse(JSON.stringify(result));
missingRobustResult.robustness.packages=[];
const missingRobust=window.PrincipalBriefing.buildModel({result:missingRobustResult,room,settings:{usTariff:35,retaliatoryTariff:10},now:'2026-08-11T12:00:00Z'});
assert.strictEqual(missingRobust.robustPromoted,false);
assert.strictEqual(missingRobust.primaryKind,'point-estimate');
assert.strictEqual(missingRobust.best.id,'pareto-1');
assert.strictEqual(missingRobust.best.metricsAvailable,false);
assert.strictEqual(missingRobust.best.jointClear,null);
assert(missingRobust.whereWeAre.some(x=>x.includes('no second-stage robustness record')));
assert(missingRobust.decisionRequired.some(x=>x.includes('robustness metrics are unavailable')));
assert(window.PrincipalBriefing.previewHtml(missingRobust).includes('<b>n/a</b>'),'missing robustness data must render as unavailable, not zero');

const withheldResult=JSON.parse(JSON.stringify(result));
withheldResult.robustness.recommendedPackageId='pareto-2';
withheldResult.robustness.candidateSetComplete=false;
const withheld=window.PrincipalBriefing.buildModel({result:withheldResult,room,settings:{usTariff:35,retaliatoryTariff:10},now:'2026-08-11T12:00:00Z'});
assert.strictEqual(withheld.robustPromoted,false);
assert.strictEqual(withheld.best.id,'pareto-1','dashboard point-estimate package must remain primary when robust promotion gate is withheld');
assert(withheld.whereWeAre.some(x=>x.includes('Second-stage robustness ranks pareto-2')));
assert(withheld.whereWeAre.some(x=>x.includes('dashboard promotion is withheld')));
assert(withheld.decisionRequired.some(x=>x.includes('Withhold a robust-best claim')));

const staleRobustResult=JSON.parse(JSON.stringify(result));
staleRobustResult.robustness.recommendedPackageId='missing-package';
const staleRobust=window.PrincipalBriefing.buildModel({result:staleRobustResult,room,settings:{usTariff:35,retaliatoryTariff:10},now:'2026-08-11T12:00:00Z'});
assert.strictEqual(staleRobust.robustPromoted,false);
assert.strictEqual(staleRobust.best.id,'pareto-1');
assert(staleRobust.whereWeAre.some(x=>x.includes('not present in the current negotiation frontier')));
assert(staleRobust.whereWeAre.some(x=>x.includes('does not transfer robustness claims')));

const staleRoom=JSON.parse(JSON.stringify(room));
staleRoom.offers.push({revision:99,round:5,side:'us',packageId:'historical-us',note:'Historical U.S. package'});
staleRoom.offers.push({revision:100,round:5,side:'canada',packageId:'historical-ca',note:'Historical Canadian package'});
staleRoom.counteroffers.push({revision:101,category:'Bridge',packageId:'historical-bridge'});
const staleRoomModel=window.PrincipalBriefing.buildModel({result,room:staleRoom,settings:{usTariff:35,retaliatoryTariff:10},now:'2026-08-11T12:00:00Z'});
assert(staleRoomModel.whatTheyWant.some(x=>x.includes('latest recorded U.S. offer is historical-us')));
assert(staleRoomModel.whatTheyWant.some(x=>x.includes('not present in the current model frontier')));
assert(!staleRoomModel.whatTheyWant.some(x=>x.includes('No U.S. package has been recorded')));
assert(staleRoomModel.whatWeWant.some(x=>x.includes("Canada's latest recorded offer is historical-ca")));
assert(staleRoomModel.whatWeWant.some(x=>x.includes('not present in the current model frontier')));
assert.strictEqual(staleRoomModel.bridge.id,'pareto-2','stale recorded bridge must fall back to current engine bridge');
assert(staleRoomModel.whatChanged.some(x=>x.includes('latest recorded bridge counteroffer historical-bridge is not in the current frontier')));

assert(principalSource.includes('function scenarioByStrategy('),'principal brief must resolve BATNA economics from the negotiation-engine strategy, not the inspected card');
assert(principalSource.includes('function gainSummary('),'principal brief must calculate deal premiums over BATNA and reservation values');
assert(principalSource.includes("title=isCa?'Canadian modeled outside option':'U.S. modeled outside option'"),'walk-away cards must not use legacy scenario names as their titles');
assert(principalSource.includes('Engine scenario provenance — not a negotiating package:'),'legacy scenario labels must be explicitly demoted to provenance');
assert(principalSource.includes('function dashboardFingerprint()'),'principal brief must fingerprint visible dashboard inputs');
assert(principalSource.includes('async function ensureDashboardCurrent()'),'principal brief must synchronize stale controls before generation');
assert(principalSource.includes('waitForAdjustmentCommit()'),'principal brief must wait for an active range gesture to commit');
assert(principalSource.includes('waitForRenderedState()'),'principal brief must wait for the evaluated state to actually render');
assert(principalSource.includes('robust.candidateSetComplete===true&&robustMetrics?.clearsProbabilityGate===true'),'principal primary package must use the same robust promotion gate as dashboard negotiation UI');
assert(principalSource.includes('async function refreshOpenBriefIfNeeded()'),'an open brief must be able to refresh if evaluated or room state changes');
assert(principalSource.includes('startLiveBriefSync()'),'live brief synchronization must start when the dialog opens');
assert(principalSource.includes("event.stopImmediatePropagation();void openPrincipalDialog()"),'principal open action must bypass the stale legacy briefing renderer');
assert(principalSource.includes("event.stopImmediatePropagation();void savePrincipalPdf()"),'principal PDF action must use the synchronized model directly');
assert(principalSource.includes("room=await roomState()"),'principal model must fetch current room state at generation time');

// Regression for the Firefox content-process leak: assigning textContent inside
// a MutationObserver callback can itself enqueue another child-list mutation,
// even when the assigned text is unchanged. The production observer must
// disconnect around its own relabel pass and avoid no-op writes.
{
  let activeObserver=null,callbacks=0;
  class FakeNode{
    constructor(text=''){this._text=text;this.id='';}
    get textContent(){return this._text;}
    set textContent(value){this._text=String(value);if(activeObserver?.active){callbacks++;if(callbacks>6)throw new Error('principal briefing MutationObserver feedback loop');activeObserver.callback([{type:'childList',target:this}]);}}
    addEventListener(){}
    querySelectorAll(selector){return selector==='button'?[saveButton]:[];}
    closest(){return null;}
  }
  class FakeMutationObserver{
    constructor(callback){this.callback=callback;this.active=false;activeObserver=this;}
    observe(){this.active=true;}
    disconnect(){this.active=false;}
  }
  const printButton=new FakeNode('Print briefing PDF');
  const openButton=new FakeNode('Open briefing');
  const saveButton=new FakeNode('Print');
  const dialog=new FakeNode('');
  const fakeDocument={
    readyState:'complete',
    addEventListener(){},
    querySelector(selector){
      if(selector==='#printBriefing')return printButton;
      if(selector==='#openBriefing')return openButton;
      if(selector==='#diplomaticBriefing')return dialog;
      if(selector==='#saveBriefingPdfDialog')return saveButton.id==='saveBriefingPdfDialog'?saveButton:null;
      if(selector==='#copyBriefing')return null;
      if(selector==='#cards')return null;
      return null;
    },
    querySelectorAll(){return[];},
    body:{appendChild(){}}
  };
  const context={
    window:{},document:fakeDocument,localStorage:{getItem(){return null;},setItem(){}},navigator:{},
    fetch:async()=>({ok:true,json:async()=>({})}),MutationObserver:FakeMutationObserver,
    setTimeout,clearTimeout,setInterval,clearInterval,URL,Blob,Uint8Array,Map,Date,Math,Number,String,Array,Object,JSON,console
  };
  vm.runInNewContext(principalSource,context);
  assert.strictEqual(saveButton.textContent,'Save principal PDF');
  assert.strictEqual(callbacks,0,'initial relabel runs before observing');
  saveButton.textContent='Print';
  assert.strictEqual(saveButton.textContent,'Save principal PDF');
  assert.strictEqual(callbacks,1,'external dialog mutation must terminate after one callback');
  saveButton.textContent='Save principal PDF';
  assert.strictEqual(callbacks,2,'same-text mutation must not recursively re-enter observer');
}

console.log('principal briefing test passed');

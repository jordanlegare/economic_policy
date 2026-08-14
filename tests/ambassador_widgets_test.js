'use strict';

const fs = require('fs');
const assert = require('assert');
const widgets = require('../web/diplomat.js');

assert(widgets, 'ambassador widget module must be requireable from Node.js');
for (const name of ['resolutionChecks','resolutionStatus','sparklinePath','sectorImpactSummary','buildAmbassadorWidgets'])
  assert.strictEqual(typeof widgets[name], 'function', `missing Node widget API: ${name}`);

const sectors = Array.from({length:20}, (_,index) => ({
  code:String(index+1).padStart(2,'0'),
  name:`NAICS sector ${index+1}`,
  canada:{output:index<7?-(1+index/10):.4+index/30,jobs:index<6?-.5:.3,prices:index%4===0?.7:.1},
  us:{output:index<8?-(.8+index/12):.5,jobs:index<5?-.4:.35,prices:index%5===0?.6:.08}
}));

const scenario = {
  id:'selected-deal', name:'Selected ambassador package',
  canadaScore:72, usScore:68, bocScore:74, federalScore:70,
  sustainedBilateralGrowth:true, bilateralGrowthFloor:1.1,
  growth:1.6, usGrowth:1.4, growthPath:[1.0,1.1,1.2,1.25,1.3,1.35,1.4,1.45,1.5,1.53,1.57,1.6],
  recessionRisk:22, inflation:2.3, inflationPath:[2.8,2.7,2.6,2.5,2.45,2.4,2.35,2.32,2.3,2.3,2.3,2.3],
  costOfLiving:1.4, realIncomeGrowth:.8, rates:[2.5,2.4,2.3],
  exports:2.6, usExportChange:1.9, exportPath:[-1,-.4,.1,.5,.9,1.2,1.5,1.8,2.0,2.2,2.4,2.6],
  tradeBalanceGapUsd:12.5, usTariffRevenueUsd:19.4, canadaTariffRevenueCad:5.7,
  debt:42.1, fiscal:.3, sectors
};
const baseline = {
  ...scenario,
  growth:1.0, usGrowth:1.0, inflation:2.0, exports:0, usExportChange:0,
  costOfLiving:1.0, realIncomeGrowth:.4
};
const context = {
  usTariff:40, canadaTariff:8,
  usCoverage:Array(20).fill(30), canadaCoverage:Array(20).fill(50),
  usEffective:12, canadaEffective:4
};

const checks = widgets.resolutionChecks(scenario, context);
assert.strictEqual(checks.length,5);
assert(checks.every(check => check.pass), 'fixture should clear every transparent resolution condition');
assert.deepStrictEqual(widgets.resolutionStatus(checks), {passed:5,label:'Strong resolution window',tone:'positive'});

const built = widgets.buildAmbassadorWidgets(scenario, baseline, context);
assert.deepStrictEqual(built.map(widget => widget.id), [
  'resolution','balance','growth','border','trade','households','fiscal','sectors'
]);
assert.strictEqual(built.length,8,'ambassador console should ship with eight preconfigured widgets');
assert(built[0].featured,'resolution window should lead the quick-look surface');
assert.strictEqual(built[0].checks.length,5);
assert(built.find(widget=>widget.id==='growth').metrics.some(metric=>metric.value.includes('vs no-tariff')));
assert(built.find(widget=>widget.id==='border').headline.includes('US 12.0%'));
assert(built.find(widget=>widget.id==='trade').series.length===12);
assert(built.find(widget=>widget.id==='households').headline.includes('Inflation 2.3%'));
assert(built.find(widget=>widget.id==='fiscal').headline.includes('US$19.4B'));
const sectorWidget=built.find(widget=>widget.id==='sectors');
assert.strictEqual(sectorWidget.metrics.length,4);
assert.strictEqual(sectorWidget.leaders.length,4);
assert(sectorWidget.headline.includes('/20'));

const path=widgets.sparklinePath([1,2,1.5,3]);
assert(path.startsWith('M '));
assert(path.includes(' L '));
assert(!path.includes('NaN'));

const source=fs.readFileSync('web/diplomat.js','utf8');
const css=fs.readFileSync('web/diplomat.css','utf8');
for(const token of [
  'Ambassador quick look','Deal-resolution widgets','Current /api/evaluate payload',
  'transparent UI heuristic','political acceptance probability','ambassadorWidgetGrid',
  'window' // browser exposure remains supported alongside CommonJS
]) assert(source.includes(token),`missing ambassador widget source contract: ${token}`);
for(const token of [
  '.ambassador-quick-look','.ambassador-widget-grid','.ambassador-widget.featured',
  '.ambassador-resolution-checks','.ambassador-spark','.ambassador-sector-signals'
]) assert(css.includes(token),`missing ambassador widget visual contract: ${token}`);
assert(!css.includes('.diplomat-command{'),'retired Diplomatic decision desk styling must stay removed');
assert(!source.includes("section.id = 'diplomatCommand'"),'retired Diplomatic decision desk DOM must stay removed');

console.log('ambassador quick-look Node widget tests passed');

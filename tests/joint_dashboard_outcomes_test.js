'use strict';

const fs = require('fs');
const vm = require('vm');
const assert = require('assert');

const source = fs.readFileSync('web/negotiation-model.js', 'utf8');
const css = fs.readFileSync('web/negotiation-model.css', 'utf8');

global.window = {};
global.document = {readyState:'loading', addEventListener(){}};
global.Event = function Event(type, options={}) { this.type=type; this.bubbles=!!options.bubbles; };
vm.runInThisContext(source, {filename:'web/negotiation-model.js'});

assert(window.JointDashboardOutcomes, 'joint dashboard outcome helpers must be exposed');
const {buildImpactGroups, sectorImpactSummary} = window.JointDashboardOutcomes;

const sectors = Array.from({length:20}, (_, index) => ({
  code:String(index + 1).padStart(2, '0'),
  name:`Sector ${index + 1}`,
  canada:{
    output:index % 2 ? 1 + index / 10 : -(1 + index / 10),
    jobs:index % 3 ? .5 + index / 20 : -(.5 + index / 20),
    prices:index % 4 ? .2 + index / 50 : -(.2 + index / 50)
  },
  us:{output:.25,jobs:.1,prices:.05}
}));
const scenario = {
  id:'candidate', name:'Candidate deal', growth:1.4, usGrowth:1.1,
  unemployment:5.9, bilateralGrowthFloor:1.1, recessionRisk:18,
  inflation:2.2, costOfLiving:1.6, realIncomeGrowth:.8,
  rates:[2.5,2.4,2.3], exports:2.4, usExportChange:1.8,
  tradeBalanceGapUsd:14, usTariffRevenueUsd:22, canadaTariffRevenueCad:8,
  debt:42, fiscal:.4, canadaScore:71, usScore:68, bocScore:75,
  federalScore:69, sustainedBilateralGrowth:true, sectors
};
const baseline = {...scenario, growth:1.0, usGrowth:.9, inflation:2.0,
  costOfLiving:1.2, realIncomeGrowth:.5, exports:0, usExportChange:0};
const context = {
  sectorCountry:'canada', usEffective:18, canadaEffective:3,
  usTariff:50, canadaTariff:5,
  usCoverage:Array(20).fill(36), canadaCoverage:Array(20).fill(60)
};
const groups = buildImpactGroups(scenario, baseline, context);
assert.deepStrictEqual(groups.map(group => group.id),
  ['growth','households','trade','fiscal','quality','sectors']);
assert.strictEqual(groups.length, 6);
assert(groups.every(group => group.items.length >= 3), 'each impact family needs underlying members');

const labels = groups.flatMap(group => group.items.map(item => item.label));
for (const label of [
  'Canada GDP growth','U.S. GDP growth','Canada unemployment','Bilateral growth floor','Recession risk',
  'Inflation','Cost of living','Real income growth','Terminal policy rate',
  'Canada exports','U.S. exports','U.S. coverage-adjusted tariff','Canada coverage-adjusted tariff','Bilateral balance gap',
  'U.S. tariff revenue','Canada tariff revenue','Canada debt','Fiscal impulse',
  'Canada deal score','U.S. deal score','Weakest-side score','Canada mandate score','Canada public score','Growth protection',
  'Output impact','Employment impact','Consumer-price impact'
]) assert(labels.includes(label), `missing attributable outcome member: ${label}`);

const output = sectorImpactSummary(scenario, 'canada', 'output');
assert.strictEqual(output.count, 20);
assert.strictEqual(output.negative, 10);
assert.strictEqual(output.positive, 10);
assert.strictEqual(output.leaders.length, 3);
assert(Math.abs(output.leaders[0].value) >= Math.abs(output.leaders[1].value));

const gdp = groups[0].items.find(item => item.label === 'Canada GDP growth');
assert(gdp.note.includes('vs matched no-tariff'), 'GDP outcome must expose its matched no-tariff delta');
const sectorGroup = groups.find(group => group.id === 'sectors');
assert(sectorGroup.label.includes('20 NAICS sectors'));
assert(sectorGroup.items.every(item => item.leaders.length === 3),
  'each sector impact family member must expose its strongest underlying sector members');

for (const token of [
  '#dashboardView{grid-template-columns:388px', '.deal-impact-ledger', '.impact-family',
  '.impact-member', '.impact-sector-members', '.deal-attribution-banner', '.deal-attribution-chip'
]) assert(css.includes(token), `missing outcome-first dashboard style contract: ${token}`);

for (const token of [
  'Attributable impact ledger','Possible deal, traced through the whole economy.',
  'Matched no-tariff run','Complete economy · 20 NAICS sectors','data-sector-metric',
  'window.JointDashboardOutcomes = {buildImpactGroups, sectorImpactSummary, evaluatedDealContext}'
]) assert(source.includes(token), `missing outcome-first dashboard behavior contract: ${token}`);

assert(!source.includes('Computational negotiation support'),
  'retired computational negotiation UI must not return with the redesign');

console.log('joint dashboard attributable outcomes test passed');

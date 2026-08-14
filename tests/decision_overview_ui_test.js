const fs = require('fs');
const assert = require('assert');

const source = fs.readFileSync('web/negotiation-model.js', 'utf8');

assert(source.includes("<small>Deal on the table</small><b>Bilateral deal overview</b>"),
  'decision overview must remain centered on the bilateral deal');
assert(source.includes('What the deal actually does at the border'));
assert(source.includes('U.S. coverage-adjusted tariff'));
assert(source.includes('Canada coverage-adjusted tariff'));
assert(source.includes('U.S. sector relief'));
assert(source.includes('Canada sector relief'));
assert(source.includes('What each side gets from this deal'));
for (const label of ['Canada GDP','U.S. GDP','Canada exports','U.S. exports','Canada inflation','Recession risk'])
  assert(source.includes(`>${label}<`), `missing deal outcome ${label}`);
assert(source.includes('Is the bargain balanced and durable?'));
assert(source.includes('Bilateral value'));
assert(source.includes('Growth protection'));
assert(source.includes('Where relief is concentrated'));

assert(source.includes('decision-overview-compat'),
  'legacy render targets must remain hidden for backward-compatible app rendering');
for (const id of ['confidence','signal','rationale','regime','neutral','gap','impactGrowth','impactCost','impactExports','impactRisk','negotiationSync','liveDealImpact'])
  assert(source.includes(`id=\"${id}\"`), `legacy render target ${id} must be retained`);

assert(!source.includes("section.id = 'computationalNegotiation'"),
  'computational negotiation dashboard section must not be injected');
assert(!source.includes('id=\"computationalNegotiation\"'),
  'computational negotiation DOM surface must be absent');
assert(!source.includes('dealOpenDiplomatic'),
  'decision overview must not link to the retired diplomatic desk');
assert(!source.includes('moveDiplomaticDeskOutsideOverview'),
  'retired diplomatic desk relocation plumbing must be removed');

for (const id of ['openBriefing','printBriefing','diplomaticBriefing','briefingSheet'])
  assert(source.includes(id), `Principal Brief access must retain ${id}`);
assert(source.includes('ensurePrincipalBriefUi()'),
  'the deal overview must provide the minimal Principal Brief dialog without the diplomatic desk');

assert(source.includes('window.EvaluationRunController?.state?.().staged === true'),
  'overview must warn when controls have changed since the last evaluated deal');
assert(source.includes('Figures below remain tied to the last evaluated deal.'),
  'staged controls must not be mixed with stale evaluated outcomes');
assert(source.includes('Coverage-adjusted tariffs multiply the submitted headline rate'),
  'overview must explain its tariff-form diagnostic');
assert(source.includes('window.DealOverview = {dealForm, reliefLeaders}'),
  'deal-form helpers must remain inspectable for regression work');

console.log('decision overview UI test passed');

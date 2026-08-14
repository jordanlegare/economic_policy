const fs = require('fs');
const assert = require('assert');

const source = fs.readFileSync('web/negotiation-model.js', 'utf8');

assert(source.includes("<small>Deal on the table</small><b>Bilateral deal overview</b>"),
  'decision overview must be relabeled around the bilateral deal');
assert(source.includes('What the deal actually does at the border'),
  'overview must lead with actual trade terms');
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

assert(source.includes('function moveDiplomaticDeskOutsideOverview()'),
  'diplomatic desk must no longer be nested inside the compact overview');
assert(source.includes("panel.insertAdjacentElement('afterend', desk)"),
  'diplomatic desk must be moved immediately below the overview panel');
assert(source.includes('window.EvaluationRunController?.state?.().staged === true'),
  'overview must warn when controls have changed since the last evaluated deal');
assert(source.includes('Figures below remain tied to the last evaluated deal.'),
  'staged controls must not be mixed with stale evaluated outcomes');
assert(source.includes('coverage-adjusted tariffs multiply the submitted headline rate') ||
       source.includes('Coverage-adjusted tariffs multiply the submitted headline rate'),
  'overview must explain its tariff-form diagnostic');
assert(source.includes('window.DealOverview = {dealForm, reliefLeaders}'),
  'deal-form helpers must remain inspectable for regression work');

console.log('decision overview UI test passed');

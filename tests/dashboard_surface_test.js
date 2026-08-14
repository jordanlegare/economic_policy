const fs = require('fs');
const assert = require('assert');

const diplomat = fs.readFileSync('web/diplomat.js', 'utf8');
const negotiation = fs.readFileSync('web/negotiation-model.js', 'utf8');
const calibration = fs.readFileSync('web/calibration.js', 'utf8');
const robustRoom = fs.readFileSync('web/robust-room.js', 'utf8');
const operations = fs.readFileSync('web/trade-diplomacy.js', 'utf8');
const html = fs.readFileSync('web/index.html', 'utf8');

assert(!diplomat.includes("section.id = 'diplomatCommand'"),
  'Diplomatic decision desk must not be injected');
assert(!negotiation.includes("section.id = 'computationalNegotiation'"),
  'Computational negotiation support must not be injected');
assert(!negotiation.includes('id=\"computationalNegotiation\"'),
  'Computational negotiation DOM surface must be absent');
assert(!calibration.includes("section.id = 'calibrationTrust'"),
  'Data provenance & calibration must not be injected');
assert(!calibration.includes("section.id = 'modelEvidence'"),
  'Model evidence V2 must not be injected');
assert(!robustRoom.includes('document.createElement'),
  'Uncertainty & diplomat room module must be presentation-inert');
assert(!operations.includes('document.createElement'),
  'Trade diplomacy operations module must be presentation-inert');
for (const phrase of ['Counteroffers','Playbooks','Post-round debrief'])
  assert(!operations.includes(`<h3>${phrase}`) && !operations.includes(`>${phrase}<`),
    `${phrase} dashboard surface must be absent`);

assert(negotiation.includes('id=\"openBriefing\"') && negotiation.includes('id=\"printBriefing\"'),
  'Principal Brief actions must remain available from the deal overview');
assert(negotiation.includes('id = \'diplomaticBriefing\''),
  'Principal Brief dialog must remain available without the retired desk');

const remaining = [...html.matchAll(/data-dashboard-panel="([^"]+)"/g)].map(match => match[1]);
assert.deepStrictEqual(remaining, [
  'trade-scenario','negotiating-positions','win-win-preferences','data-guardrails',
  'decision-overview','fiscal-ledger','strategies','projection','sectors'
], 'only the intended nine collapsible dashboard panels should remain in the page shell');
assert(html.includes('id="collapseDashboardPanels"') && html.includes('id="expandDashboardPanels"'),
  'remaining dashboard panels must retain Collapse all / Expand all controls');

console.log('dashboard surface test passed');

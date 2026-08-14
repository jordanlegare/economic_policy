const fs = require('fs');
const vm = require('vm');
const assert = require('assert');

const source = fs.readFileSync('web/negotiation-model.js', 'utf8');
assert(source.includes('Individually rational ε-frontier'));
assert(source.includes('Materially distinct bargaining packages'));
assert(source.includes('bestUniqueFrontierPackages(model.frontier, 9)'),
  'frontier dashboard must request the nine best unique packages');
assert(source.includes('best unique'),
  'frontier count must describe the displayed packages as unique');

const helperMatch = source.match(/function materialPackageKey[\s\S]*?return unique;\n  }/);
assert(helperMatch, 'frontier de-duplication helpers must be present');
const sandbox = {};
vm.runInNewContext(`${helperMatch[0]}\nthis.materialPackageKey=materialPackageKey;this.bestUniqueFrontierPackages=bestUniqueFrontierPackages;`, sandbox);

const issue = (label, canadaMove, usMove) => ({label, canadaMove, usMove});
const package_ = (id, strategyName, issues, nashGain) => ({id, strategyId:id, strategyName, issues, nashGain});
const frontier = [
  package_('mix-96', 'Cut 25 bp · +0.6% fiscal · 0% tariff relief · 90% productive · 66% diversify', [issue('Tariff relief', 0, 20), issue('Procurement', 10, 15)], 100),
  package_('mix-94', 'Cut 25 bp · +0.6% fiscal · 0% tariff relief · 90% productive · 66% diversify', [issue('Procurement', 10, 15), issue('Tariff relief', 0, 20)], 99),
  package_('mix-92', 'Cut 25 bp · +0.6% fiscal · 0% tariff relief · 90% productive · 66% diversify', [issue('Tariff relief', 0, 20), issue('Procurement', 10, 16)], 98),
  ...Array.from({length:10}, (_, i) => package_(`unique-${i}`, `Unique package ${i}`, [issue('Supply chain', i, 20-i)], 90-i))
];

const visible = sandbox.bestUniqueFrontierPackages(frontier, 9);
assert.strictEqual(visible.length, 9, 'frontier dashboard must show at most nine unique packages');
assert.strictEqual(visible[0].id, 'mix-96', 'highest-ranked duplicate must survive');
assert(!visible.some(p => p.id === 'mix-94'), 'lower-ranked materially identical package must be removed');
assert(visible.some(p => p.id === 'mix-92'), 'different linked concession moves must remain materially distinct');
assert.strictEqual(new Set(visible.map(sandbox.materialPackageKey)).size, visible.length,
  'every displayed frontier package must have a unique material identity');

console.log('negotiation frontier UI test passed');

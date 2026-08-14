const fs = require('fs');
const vm = require('vm');
const assert = require('assert');

const source = fs.readFileSync('web/session.js', 'utf8');

function storage(initial = {}) {
  const values = new Map(Object.entries(initial));
  return {
    getItem(key) { return values.has(key) ? values.get(key) : null; },
    setItem(key, value) { values.set(key, String(value)); },
    removeItem(key) { values.delete(key); },
    dump() { return Object.fromEntries(values); }
  };
}

(async()=>{
  const localStorage = storage();
  const sessionStorage = storage();
  const requests = [];
  const controls = {};
  for (const id of [
    'usTariff', 'retaliatoryTariff', 'canadaPriority', 'usPriority',
    'riskAversion', 'cooperationCeiling', 'canadaPriorityValue',
    'usPriorityValue', 'priorityTotal', 'riskAversionValue',
    'cooperationCeilingValue', 'retaliatoryTariffValue', 'partyView'
  ]) controls[id] = {value:'', textContent:'', hidden:true};

  const document = {
    readyState:'complete',
    getElementById(id) { return controls[id] || null; },
    addEventListener() {}
  };

  class Headers {
    constructor(initial) { this.values = new Map(Object.entries(initial || {})); }
    set(key, value) { this.values.set(key, value); }
  }
  class Request {}

  let transientNegotiationFailures = 0;
  const context = {
    console,
    Date,
    Math,
    JSON,
    URL,
    Headers,
    Request,
    Uint8Array,
    localStorage,
    sessionStorage,
    document,
    location:{href:'http://localhost:8080/', origin:'http://localhost:8080'},
    crypto:{randomUUID:()=>'persistent-session-id'},
    fetch:(input, init={}) => {
      requests.push({input, init});
      if (input === '/api/negotiation' && transientNegotiationFailures > 0) {
        transientNegotiationFailures--;
        return Promise.resolve({ok:false, status:503, json:async()=>({error:'busy'})});
      }
      return Promise.resolve({ok:true, status:200, json:async()=>({})});
    },
    prompt:()=>'',
    CAD_API_AUTH_REQUIRED:false,
    InitialOpeningScenario:{
      opening:{
        usTariff:50,
        retaliatoryTariff:5,
        usSectorCoverage:Array(20).fill(100),
        canadaSectorCoverage:Array(20).fill(100)
      }
    },
    positions:{us:Array(20).fill(100), canada:Array(20).fill(100)},
    updateTariff() {},
    updatePosition() {},
    syncPartyView() {},
    refreshPartySectorMetrics() {},
    evaluate:async()=>{}
  };
  context.globalThis = context;
  context.window = context;
  vm.createContext(context);
  vm.runInContext(source, context);

  assert.strictEqual(localStorage.getItem('cad-policy-studio.session-id'), 'persistent-session-id',
    'session identity must survive browser restarts');
  assert(context.CADDelegationExchange, 'delegation exchange API must be exposed');
  assert.strictEqual(context.CADDelegationExchange.schema,
    'cad-policy-studio/delegation-settings');
  assert.strictEqual(context.CADDelegationExchange.version, 1);

  const primary = {
    usTariff:37,
    retaliatoryTariff:11,
    canadaPriority:62,
    usPriority:38,
    riskAversion:71,
    cooperationCeiling:44
  };
  for (let i = 0; i < 20; i++) {
    primary['usSector' + i] = 10 + i;
    primary['canadaSector' + i] = 70 - i;
  }

  await context.fetch('/api/evaluate', {
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify(primary)
  });
  assert.strictEqual(JSON.parse(requests[0].init.body).operationId, undefined,
    'model evaluations must not acquire mutation operation ids or retry semantics');
  const savedAfterPrimary = JSON.parse(localStorage.getItem('cad-policy-studio.last-run-settings.v1'));
  assert.strictEqual(savedAfterPrimary.usTariff, 37);
  assert.strictEqual(savedAfterPrimary.retaliatoryTariff, 11);
  assert.deepStrictEqual(Array.from(savedAfterPrimary.usSectorCoverage),
    Array.from({length:20}, (_, i) => 10 + i));
  assert.deepStrictEqual(Array.from(savedAfterPrimary.canadaSectorCoverage),
    Array.from({length:20}, (_, i) => 70 - i));
  const savedAt = savedAfterPrimary.savedAt;

  // The controller deliberately schedules this derived comparison later. It must
  // never replace the primary operator checkpoint regardless of timing.
  await context.fetch('/api/evaluate', {
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({...primary, usTariff:0, retaliatoryTariff:0, comparisonOnly:true})
  });
  const savedAfterComparison = JSON.parse(localStorage.getItem('cad-policy-studio.last-run-settings.v1'));
  assert.strictEqual(savedAfterComparison.usTariff, 37);
  assert.strictEqual(savedAfterComparison.retaliatoryTariff, 11);
  assert.strictEqual(savedAfterComparison.savedAt, savedAt);

  // Re-reading a checkpoint is observational and must not fabricate a newer save time.
  const reread = context.CADLastRunSettings.read();
  assert.strictEqual(reread.savedAt, savedAt);

  // Simulate controls being reset during startup, then recover the exact run.
  Object.values(controls).forEach(node => { node.value = '0'; node.textContent = ''; });
  context.positions.us.fill(100);
  context.positions.canada.fill(100);
  context.InitialOpeningScenario.opening.usTariff = 50;
  context.InitialOpeningScenario.opening.retaliatoryTariff = 5;
  assert.strictEqual(context.CADLastRunSettings.restore(), true);
  assert.strictEqual(controls.usTariff.value, '37');
  assert.strictEqual(controls.retaliatoryTariff.value, '11');
  assert.strictEqual(controls.canadaPriority.value, '62');
  assert.strictEqual(controls.usPriority.value, '38');
  assert.strictEqual(controls.riskAversion.value, '71');
  assert.strictEqual(controls.cooperationCeiling.value, '44');
  assert.deepStrictEqual(Array.from(context.positions.us), Array.from({length:20}, (_, i) => 10 + i));
  assert.deepStrictEqual(Array.from(context.positions.canada), Array.from({length:20}, (_, i) => 70 - i));
  assert.strictEqual(context.InitialOpeningScenario.opening.usTariff, 37);
  assert.strictEqual(context.InitialOpeningScenario.opening.retaliatoryTariff, 11);

  // A portable foreign delegation package is strict and includes the complete
  // bilateral negotiation state. The 200% U.S. ceiling must round-trip exactly.
  const foreignSettings = {
    usTariff:200,
    retaliatoryTariff:60,
    canadaPriority:55,
    usPriority:45,
    riskAversion:66,
    cooperationCeiling:42,
    usSectorCoverage:Array.from({length:20}, (_, i) => 20 + i),
    canadaSectorCoverage:Array.from({length:20}, (_, i) => 80 - i)
  };
  const foreignPackage = context.CADDelegationExchange.buildPackage(foreignSettings, {
    exportedAt:'2026-08-14T22:00:00.000Z',
    exportedBy:'U.S. delegation'
  });
  assert.strictEqual(foreignPackage.schema, 'cad-policy-studio/delegation-settings');
  assert.strictEqual(foreignPackage.version, 1);
  assert.strictEqual(foreignPackage.exportedBy, 'U.S. delegation');
  assert.strictEqual(foreignPackage.settings.usTariff, 200);
  assert.strictEqual(foreignPackage.settings.usSectorCoverage.length, 20);
  assert.strictEqual(foreignPackage.settings.canadaSectorCoverage.length, 20);

  const validated = context.CADDelegationExchange.validatePackage(foreignPackage);
  assert.strictEqual(validated.settings.usTariff, 200);
  assert.deepStrictEqual(Array.from(validated.settings.usSectorCoverage),
    Array.from(foreignSettings.usSectorCoverage));
  const exchangePayload = context.CADDelegationExchange.negotiationPayload(validated.settings);
  assert.strictEqual(exchangePayload.actor, 'exchange');
  assert.strictEqual(exchangePayload.usTariff, 200);
  assert.strictEqual(exchangePayload.retaliatoryTariff, 60);
  assert.strictEqual(exchangePayload.usSector0, 20);
  assert.strictEqual(exchangePayload.usSector19, 39);
  assert.strictEqual(exchangePayload.canadaSector0, 80);
  assert.strictEqual(exchangePayload.canadaSector19, 61);
  assert.strictEqual(Object.keys(exchangePayload).filter(key => /^usSector\d+$/.test(key)).length, 20);
  assert.strictEqual(Object.keys(exchangePayload).filter(key => /^canadaSector\d+$/.test(key)).length, 20);

  assert.throws(() => context.CADDelegationExchange.validatePackage({...foreignPackage, version:2}),
    /version/);
  assert.throws(() => context.CADDelegationExchange.validatePackage({
    ...foreignPackage,
    settings:{...foreignSettings, usTariff:201}
  }), /usTariff/);
  assert.throws(() => context.CADDelegationExchange.validatePackage({
    ...foreignPackage,
    settings:{...foreignSettings, canadaPriority:60, usPriority:30}
  }), /sum to 100/);
  assert.throws(() => context.CADDelegationExchange.validatePackage({
    ...foreignPackage,
    settings:{...foreignSettings, usSectorCoverage:foreignSettings.usSectorCoverage.slice(0,19)}
  }), /exactly 20/);

  // Import uses one replay-safe bilateral mutation and then restores the exact
  // imported controls locally. No session credential is part of the package.
  requests.length = 0;
  await context.CADDelegationExchange.importPackage(foreignPackage);
  assert.strictEqual(requests.length, 1);
  const importedMutation = JSON.parse(requests[0].init.body);
  assert.strictEqual(importedMutation.actor, 'exchange');
  assert(importedMutation.operationId, 'exchange import must carry a replay-safe operation id');
  assert.strictEqual(importedMutation.usTariff, 200);
  assert.strictEqual(controls.usTariff.value, '200');
  assert.strictEqual(controls.retaliatoryTariff.value, '60');
  assert.strictEqual(controls.canadaPriority.value, '55');
  assert.strictEqual(controls.usPriority.value, '45');
  assert.strictEqual(context.CADLastRunSettings.read().usTariff, 200,
    'high-tariff settings must not be truncated by restart persistence');
  assert.strictEqual(Object.prototype.hasOwnProperty.call(foreignPackage, 'sessionId'), false);
  assert.strictEqual(Object.prototype.hasOwnProperty.call(foreignPackage, 'token'), false);

  for (const token of ['Export settings', 'Import foreign', 'id="sync" hidden', "actor: 'exchange'"])
    assert(source.includes(token), `missing delegation exchange UI/source contract: ${token}`);

  // A transient mutation failure gets exactly one replay-safe retry with the same
  // operation id/body. This covers the lost-response case without retrying models.
  requests.length = 0;
  transientNegotiationFailures = 1;
  const mutationResponse = await context.fetch('/api/negotiation', {
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({
      actor:'canada', retaliatoryTariff:9, canadaPriority:57,
      riskAversion:48, cooperationCeiling:52
    })
  });
  assert.strictEqual(mutationResponse.status, 200);
  assert.strictEqual(requests.length, 2,
    'a transient 503 should cause one and only one mutation retry');
  const firstMutation = JSON.parse(requests[0].init.body);
  const secondMutation = JSON.parse(requests[1].init.body);
  assert(firstMutation.operationId, 'mutation must carry a durable operation id');
  assert.strictEqual(secondMutation.operationId, firstMutation.operationId,
    'retry must reuse the original operation id');
  assert.deepStrictEqual(secondMutation, firstMutation,
    'retry must preserve the exact canonical mutation body');

  console.log('session persistence, delegation exchange, and replay-safe mutation tests passed');
})().catch(error => { console.error(error); process.exit(1); });

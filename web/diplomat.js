(() => {
  'use strict';

  const LIMITS_KEY = 'canada-policy-diplomatic-redlines-v1';
  const NOTES_KEY = 'canada-policy-diplomatic-notes-v1';
  const DECISION_LEDGER_KEY = 'canada-us-trade-diplomacy-decision-ledger-v1';
  const defaults = { minCanada: 45, minUs: 45, maxRecession: 40, minGrowth: 0, maxInflation: 3.5 };
  let limits = loadObject(LIMITS_KEY, defaults);
  let renderTimer = null;
  let renderEpoch = 0;

  function loadStored(key, fallback) {
    try {
      const value = JSON.parse(localStorage.getItem(key) || 'null');
      return value ?? fallback;
    } catch (_) {
      return fallback;
    }
  }

  function loadObject(key, fallback) {
    const value = loadStored(key, {});
    return value && typeof value === 'object' && !Array.isArray(value)
      ? {...fallback, ...value}
      : {...fallback};
  }

  function saveJson(key, value) {
    try { localStorage.setItem(key, JSON.stringify(value)); } catch (_) {}
  }

  const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const n = value => Number(value || 0);
  const f = (value, digits=1) => n(value).toFixed(digits);
  const signed = (value, digits=1) => `${n(value) >= 0 ? '+' : ''}${f(value, digits)}`;
  const canadaScore = scenario => n(scenario?.canadaScore);
  const normalizeText = value => String(value ?? '').replace(/\s+/g, ' ').trim().toLowerCase();
  const average = values => Array.isArray(values) && values.length
    ? values.reduce((sum, value) => sum + n(value), 0) / values.length
    : 0;

  function uniqueText(items, limit=Infinity) {
    const seen = new Set(), out = [];
    for (const item of items || []) {
      const text = String(item ?? '').replace(/\s+/g, ' ').trim();
      if (!text) continue;
      const key = normalizeText(text);
      if (seen.has(key)) continue;
      seen.add(key);
      out.push(text);
      if (out.length >= limit) break;
    }
    return out;
  }

  function uniqueBy(items, keyFn, limit=Infinity) {
    const seen = new Set(), out = [];
    for (const item of items || []) {
      const key = keyFn(item);
      if (!key || seen.has(key)) continue;
      seen.add(key);
      out.push(item);
      if (out.length >= limit) break;
    }
    return out;
  }

  function generatedControlKey(scenario) {
    const description = String(scenario?.description || '');
    const match = description.match(/([+-]?\d+(?:\.\d+)?)% negotiated rate relief, productive share ([+-]?\d+(?:\.\d+)?)%, diversification ([+-]?\d+(?:\.\d+)?)%/i);
    const controls = [
      n(scenario?.move).toFixed(6),
      n(scenario?.fiscal).toFixed(6)
    ];
    if (match) controls.push(n(match[1]).toFixed(6), n(match[2]).toFixed(6), n(match[3]).toFixed(6));
    else controls.push(String(scenario?.name || '').replace(/\s+/g, ' ').trim());
    return controls.join('|');
  }

  function scenarioIdentityKey(scenario) {
    if (window.PackageTitles?.packageIdentityKey) return window.PackageTitles.packageIdentityKey(scenario);
    const id = String(scenario?.id || '');
    return id.startsWith('custom-')
      ? `custom|${generatedControlKey(scenario)}`
      : `strategy|${id}|${String(scenario?.name || '')}`;
  }

  function uniqueScenarios(scenarios, limit=7) {
    if (window.PackageTitles?.bestUniqueScenarios)
      return window.PackageTitles.bestUniqueScenarios(scenarios || [], limit);
    return uniqueBy(scenarios || [], scenarioIdentityKey, limit);
  }

  function checks(scenario) {
    if (!scenario) return {pass:false, detail:[]};
    const detail = [
      ['Canada floor', canadaScore(scenario) >= limits.minCanada, `${f(canadaScore(scenario),0)} ≥ ${limits.minCanada}`],
      ['U.S. floor', n(scenario.usScore) >= limits.minUs, `${f(scenario.usScore,0)} ≥ ${limits.minUs}`],
      ['Recession ceiling', n(scenario.recessionRisk) <= limits.maxRecession, `${f(scenario.recessionRisk,0)}% ≤ ${limits.maxRecession}%`],
      ['Bilateral growth floor', n(scenario.bilateralGrowthFloor) >= limits.minGrowth, `${f(scenario.bilateralGrowthFloor)}% ≥ ${limits.minGrowth}%`],
      ['Inflation ceiling', n(scenario.inflation) <= limits.maxInflation, `${f(scenario.inflation)}% ≤ ${limits.maxInflation}%`]
    ];
    return {pass: detail.every(x => x[1]), detail};
  }

  function currentInputs() {
    const base = typeof settings !== 'undefined' ? {...settings} : {};
    const state = window.EvaluationController?.state?.() || {};
    const pos = typeof positions !== 'undefined' ? positions : null;
    const usNode = document.querySelector('#usTariff');
    const caNode = document.querySelector('#retaliatoryTariff');
    if (usNode) base.usTariff = n(usNode.value);
    else if (typeof tariff !== 'undefined' && tariff) base.usTariff = n(tariff.value);
    if (caNode) base.retaliatoryTariff = n(caNode.value);
    for (const id of ['canadaPriority','usPriority','riskAversion','cooperationCeiling']) {
      const node = document.querySelector(`#${id}`);
      if (node && Number.isFinite(+node.value)) base[id] = +node.value;
    }
    base.usSectorCoverage = Array.isArray(pos?.us) ? [...pos.us] : [...(state?.displayedCoverage?.us || [])];
    base.canadaSectorCoverage = Array.isArray(pos?.canada) ? [...pos.canada] : [...(state?.displayedCoverage?.canada || [])];
    return base;
  }

  async function roomState() {
    try {
      const response = await fetch('/api/room', {cache:'no-store'});
      return response.ok ? response.json() : {};
    } catch (_) {
      return {};
    }
  }

  function currentNotes() {
    try { return localStorage.getItem(NOTES_KEY) || ''; } catch (_) { return ''; }
  }

  async function synchronizedPrincipalModel() {
    if (!window.PrincipalBriefing?.buildModel) return null;
    const coherent = typeof window.PrincipalBriefing.synchronize === 'function'
      ? await window.PrincipalBriefing.synchronize()
      : {
          result: typeof result !== 'undefined' ? result : null,
          selectedScenario: typeof selected !== 'undefined' ? selected : null,
          dashboard: window.EvaluationController?.state?.() || {}
        };
    if (!coherent?.result?.scenarios?.length) return null;
    const room = await roomState();
    return window.PrincipalBriefing.buildModel({
      result: coherent.result,
      room,
      settings: currentInputs(),
      dashboard: coherent.dashboard || {},
      selectedScenario: coherent.selectedScenario,
      redlines: limits,
      notes: currentNotes(),
      ledger: loadStored(DECISION_LEDGER_KEY, [])
    });
  }

  function scenarioForStrategy(strategy, scenarios) {
    const key = String(strategy || '');
    return (scenarios || []).find(s => s?.id === key || s?.name === key) || null;
  }

  function concessionRows(principalModel=null) {
    const dashboard = principalModel?.dashboardState || {};
    const pos = typeof positions !== 'undefined' ? positions : null;
    const us = Array.isArray(dashboard.usSectorCoverage) && dashboard.usSectorCoverage.length
      ? dashboard.usSectorCoverage
      : Array.isArray(pos?.us) ? pos.us : (result?.recommendation?.usSectorCoverage || []);
    const ca = Array.isArray(dashboard.canadaSectorCoverage) && dashboard.canadaSectorCoverage.length
      ? dashboard.canadaSectorCoverage
      : Array.isArray(pos?.canada) ? pos.canada : (result?.recommendation?.canadaSectorCoverage || []);
    const names = typeof sectorNames !== 'undefined' ? sectorNames : us.map((_,i)=>`Sector ${i+1}`);
    return names.map((name, i) => {
      const usRelief = 100 - n(us[i]);
      const caRelief = 100 - n(ca[i]);
      const total = usRelief + caRelief;
      let posture = 'Hold';
      if (usRelief > 8 && caRelief > 8) posture = 'Mutual move';
      else if (usRelief > caRelief + 8) posture = 'U.S. move';
      else if (caRelief > usRelief + 8) posture = 'Canada move';
      return {name, usRelief, caRelief, total, posture};
    }).sort((a,b) => b.total - a.total);
  }

  function relevantConcessions(rows, limit=5) {
    const unique = uniqueBy(rows || [], row =>
      `${normalizeText(row?.name)}|${n(row?.usRelief).toFixed(3)}|${n(row?.caRelief).toFixed(3)}`);
    const material = unique.filter(row => n(row.total) >= 1 || row.posture !== 'Hold');
    return (material.length ? material : unique.slice(0,3)).slice(0, limit);
  }

  function bargainingPackageKey(package_) {
    if (!package_) return '';
    const strategy = String(package_.strategy || package_.strategyName || package_.strategyId || '');
    const issues = (package_.issues || []).map(issue =>
      `${String(issue?.id || issue?.label || '')}|${n(issue?.canadaMove).toFixed(6)}|${n(issue?.usMove).toFixed(6)}`
    ).sort().join('||');
    return `${strategy}|${issues}`;
  }

  function buildDeskView(principalModel, scenarios, concessions) {
    const whereWeAre = uniqueText((principalModel?.whereWeAre || []).filter(item =>
      !/^(dashboard evaluated state|model trust):/i.test(String(item || '').trim())), 4);
    const bridge = principalModel?.bridge
      && bargainingPackageKey(principalModel.bridge) !== bargainingPackageKey(principalModel?.best)
      ? principalModel.bridge
      : null;
    return {
      scenarios: uniqueScenarios(scenarios || [], 7),
      decisions: uniqueText(principalModel?.decisionRequired, 3),
      whereWeAre,
      changes: uniqueText(principalModel?.whatChanged, 4),
      theyWant: uniqueText(principalModel?.whatTheyWant, 3),
      weWant: uniqueText(principalModel?.whatWeWant, 3),
      mandate: uniqueText(principalModel?.redLines?.mandate, 4),
      uncertainties: uniqueText(principalModel?.uncertainties, 3),
      language: uniqueBy(principalModel?.recommendedLanguage || [], item => normalizeText(item?.text), 4),
      evidence: uniqueBy(principalModel?.evidenceSources || [], item =>
        [item?.agency, item?.dataset, item?.vintage, item?.status, item?.domain].map(normalizeText).join('|'), 4),
      concessions: relevantConcessions(concessions || [], 5),
      bridge
    };
  }

  window.DiplomaticDecisionDesk = {
    uniqueText,
    scenarioIdentityKey,
    uniqueScenarios,
    relevantConcessions,
    bargainingPackageKey,
    buildDeskView
  };

  function institutionalizeLabels() {
    const live = document.querySelector('.party-live-strip b');
    if (live && /LeBlanc|Greer/i.test(live.textContent)) live.innerHTML = '<i></i> Canada ↔ U.S. delegations connected';
    const title = document.querySelector('#partyTitle');
    if (title && /LeBlanc/i.test(title.textContent)) title.textContent = 'Canada delegation trade table';
    else if (title && /Greer/i.test(title.textContent)) title.textContent = 'U.S. delegation trade table';
    const delegation = document.querySelector('#delegationName');
    if (delegation && /LeBlanc/i.test(delegation.textContent)) delegation.textContent = 'Canada delegation';
    else if (delegation && /Greer/i.test(delegation.textContent)) delegation.textContent = 'U.S. delegation';
  }

  function listHtml(items, empty='No material item is recorded for the current situation.') {
    return items?.length
      ? `<ol class="talking-points">${items.map(item => `<li>${esc(item)}</li>`).join('')}</ol>`
      : `<p>${esc(empty)}</p>`;
  }

  function renderSituation(model) {
    const target = document.querySelector('#diplomatSituation');
    if (!target) return;
    if (!model) {
      target.innerHTML = '<b>Current delegation situation:</b> Waiting for the synchronized principal-decision snapshot.';
      return;
    }
    const d = model.dashboardState || {};
    target.innerHTML = `<b>Current evaluated delegation situation:</b> Round ${esc(model.round)} · ${esc(String(model.phase || 'preparation').replace(/[_-]+/g,' '))} · U.S. tariff ${f(d.usTariff,1)}% · Canadian retaliation ${f(d.retaliatoryTariff,1)}% · outcome weights ${f(d.canadaPriority,0)}/${f(d.usPriority,0)} · tail-risk caution ${f(d.riskAversion,0)} · relief ceiling ${f(d.cooperationCeiling,0)}% · average sector coverage U.S. ${f(average(d.usSectorCoverage),1)}% / Canada ${f(average(d.canadaSectorCoverage),1)}%.<br><span>${esc(model.fingerprint)} · ${esc(model.modelTrustStatus)}</span>`;
  }

  function renderStatus(model, view) {
    const primary = model?.best;
    const authority = model?.bestAuthority;
    const premiums = model?.batna?.primary;
    const scenarioCount = Math.min(5, view.scenarios.length);
    const passCount = view.scenarios.slice(0, scenarioCount).filter(s => checks(s).pass).length;

    document.querySelector('#diplomatPrimary').textContent = primary?.id || view.scenarios[0]?.name || 'Unavailable';
    document.querySelector('#diplomatPrimaryNote').textContent = primary
      ? `${model.robustPromoted ? 'Robust primary' : 'Point-estimate primary'} · ${primary.strategy || 'strategy unavailable'}`
      : 'Principal bargaining package unavailable; showing the leading unique policy scenario.';

    document.querySelector('#diplomatAuthority').textContent = authority?.status || 'Authority not recorded';
    document.querySelector('#diplomatAuthorityNote').textContent = model
      ? model.modelTrustStatus
      : 'Principal model synchronization is not available yet.';

    document.querySelector('#diplomatBatna').textContent = premiums
      ? `CA ${signed(premiums.canadaOverBatna,2)} · US ${signed(premiums.usOverBatna,2)}`
      : 'n/a';
    document.querySelector('#diplomatBatnaNote').textContent = premiums
      ? 'Modeled utility premium over each country’s independently selected BATNA.'
      : 'Agreement-vs-walk-away comparison is unavailable.';

    document.querySelector('#diplomatRedlineStatus').textContent = scenarioCount
      ? `${passCount} of ${scenarioCount} unique clear`
      : 'No scenarios';
    document.querySelector('#diplomatRedlineNote').textContent = 'Against locally saved analytical guardrails; mandate authority is shown separately.';
  }

  function packageScenario(modelPackage, scenarios) {
    return scenarioForStrategy(modelPackage?.strategy, scenarios)
      || scenarioForStrategy(modelPackage?.id, scenarios);
  }

  function principalPackageCard(kind, label, package_, authority, scenarios) {
    if (!package_) return `<article class="package-lane ${kind}"><div class="package-type">${esc(label)}</div><h3>No package available</h3><p>The current principal model does not contain this bargaining package.</p></article>`;
    const scenario = packageScenario(package_, scenarios);
    const clickable = scenario ? ` data-diplomat-scenario="${esc(scenario.id)}"` : '';
    return `<article class="package-lane ${kind}"${clickable}>
      <div class="package-type">${esc(label)}</div>
      <h3>${esc(package_.id)} · ${esc(package_.strategy)}</h3>
      <p>${package_.metricsAvailable
        ? `Both sides clear modeled reservation values in ${f(100*n(package_.jointClear),1)}% of second-stage draws; stability ${f(package_.stability,0)}/100.`
        : `Second-stage robustness metrics are unavailable; stability ${f(package_.stability,0)}/100.`}</p>
      <div class="package-scores">
        <div><span>Canada utility</span><b>${f(package_.canadaUtility,1)}</b></div>
        <div><span>U.S. utility</span><b>${f(package_.usUtility,1)}</b></div>
        <div><span>Both clear</span><b>${package_.metricsAvailable ? `${f(100*n(package_.jointClear),0)}%` : 'n/a'}</b></div>
      </div>
      <div class="package-check ${authority?.blocked ? 'fail' : 'pass'}">${esc(authority?.status || 'Authority status unavailable')}${scenario ? ' · click to inspect policy path' : ''}</div>
    </article>`;
  }

  function walkAwayCard(model) {
    const batna = model?.batna;
    if (!batna) return `<article class="package-lane fallback"><div class="package-type">Walk-away benchmark</div><h3>BATNA unavailable</h3><p>No country-specific outside option is available in the current principal model.</p></article>`;
    const premiums = batna.primary;
    return `<article class="package-lane fallback">
      <div class="package-type">Country-specific walk-away benchmarks</div>
      <h3>BATNA · preserve below-reservation fallback</h3>
      <p>Canada and the United States have independently selected modeled outside options; these are not the inspected dashboard card.</p>
      <div class="package-scores">
        <div><span>Canada BATNA</span><b>${f(batna.canada,1)}</b></div>
        <div><span>U.S. BATNA</span><b>${f(batna.us,1)}</b></div>
        <div><span>Deal premium</span><b>${premiums ? `${signed(Math.min(n(premiums.canadaOverBatna),n(premiums.usOverBatna)),1)}` : 'n/a'}</b></div>
      </div>
      <div class="package-check pass">${premiums ? `Primary vs BATNA: Canada ${signed(premiums.canadaOverBatna,2)} · U.S. ${signed(premiums.usOverBatna,2)}` : 'Primary-package BATNA comparison unavailable.'}</div>
    </article>`;
  }

  function fallbackScenarioSet(scenarios) {
    const unique = uniqueScenarios(scenarios || [], 7);
    const preferred = unique[0] || null;
    const remaining = unique.slice(1);
    const bridge = [...remaining].sort((a,b) => {
      const fairness = s => Math.min(canadaScore(s), n(s.usScore)) - .22 * Math.abs(canadaScore(s)-n(s.usScore)) - .035*n(s.recessionRisk);
      return fairness(b) - fairness(a);
    })[0] || preferred;
    const fallback = remaining.find(s => s.id !== bridge?.id && checks(s).pass)
      || unique.find(s => s.id === 'statusquo')
      || remaining.find(s => s.id !== bridge?.id)
      || preferred;
    return {preferred, bridge, fallback};
  }

  function scenarioCard(kind, label, scenario, why) {
    if (!scenario) return `<article class="package-lane ${kind}"><div class="package-type">${esc(label)}</div><h3>Unavailable</h3></article>`;
    const gate = checks(scenario);
    return `<article class="package-lane ${kind}" data-diplomat-scenario="${esc(scenario.id)}">
      <div class="package-type">${esc(label)}</div><h3>${esc(scenario.name)}</h3><p>${esc(why)}</p>
      <div class="package-scores"><div><span>Canada</span><b>${f(canadaScore(scenario),0)}</b></div><div><span>United States</span><b>${f(scenario.usScore,0)}</b></div><div><span>Shared growth floor</span><b>${f(scenario.bilateralGrowthFloor)}%</b></div></div>
      <div class="package-check ${gate.pass?'pass':'fail'}">${gate.pass?'✓ Clears current analytical red lines':'⚠ Breaches at least one analytical red line'} · click to inspect</div>
    </article>`;
  }

  function renderPackages(model, view) {
    const target = document.querySelector('#diplomatPackages');
    if (!target) return;
    if (model?.best) {
      target.innerHTML = [
        principalPackageCard('preferred', model.robustPromoted ? 'Robust primary bargaining package' : 'Point-estimate primary bargaining package', model.best, model.bestAuthority, view.scenarios),
        principalPackageCard('bridge', 'Controlled bridge package', view.bridge, view.bridge ? model.bridgeAuthority : null, view.scenarios),
        walkAwayCard(model)
      ].join('');
      return;
    }
    const fallback = fallbackScenarioSet(view.scenarios);
    target.innerHTML = [
      scenarioCard('preferred','Leading unique policy scenario',fallback.preferred,'Highest-ranked unique policy scenario while principal bargaining context is unavailable.'),
      scenarioCard('bridge','Balanced unique alternative',fallback.bridge,'Distinct alternative selected for bilateral balance.'),
      scenarioCard('fallback','Analytical fallback',fallback.fallback,'Distinct red-line-compliant fallback where available.')
    ].join('');
  }

  function renderDecisionRequired(view) {
    const target = document.querySelector('#diplomatDecisionRequired');
    if (!target) return;
    target.innerHTML = view.decisions.length
      ? `<b>Decision required now:</b><br>${view.decisions.map((item,i)=>`${i+1}. ${esc(item)}`).join('<br>')}`
      : '<b>Decision required now:</b> No principal decision item is available yet.';
  }

  function renderContext(view) {
    document.querySelector('#diplomatWhatChanged').innerHTML = listHtml(view.changes, 'No material change is recorded since the prior offer/debrief state.');
    document.querySelector('#diplomatTheyWant').innerHTML = listHtml(view.theyWant, 'No recorded U.S. ask is available; do not infer stated demands from model utility.');
    document.querySelector('#diplomatWeWant').innerHTML = listHtml(view.weWant, 'No recorded Canadian ask is available.');
    document.querySelector('#diplomatWhereWeAre').innerHTML = listHtml(view.whereWeAre, 'No additional principal situation note remains after removing information already shown above.');
  }

  function renderRedlines(model, view) {
    const primaryScenario = packageScenario(model?.best, view.scenarios) || view.scenarios[0] || null;
    const gate = checks(primaryScenario);
    const failed = gate.detail.filter(x => !x[1]);
    const target = document.querySelector('#redlineSummary');
    target.classList.toggle('alert', failed.length > 0);
    target.innerHTML = primaryScenario
      ? failed.length
        ? `<strong>${failed.length} analytical red line${failed.length===1?'':'s'} breached by ${esc(primaryScenario.name)}.</strong><br>${failed.map(x=>`${esc(x[0])}: ${esc(x[2])}`).join('<br>')}`
        : `<strong>${esc(primaryScenario.name)} clears all local analytical red lines.</strong><br>These thresholds do not change the economic model.`
      : '<strong>No policy scenario is available for the local analytical screen.</strong>';
    document.querySelector('#principalMandateSummary').innerHTML = listHtml(
      view.mandate,
      'No non-default mandate restriction is recorded in the Diplomat Room.'
    );
  }

  function renderConcessions(view) {
    const target = document.querySelector('#concessionList');
    target.innerHTML = view.concessions.map(row => {
      const cls = row.posture === 'Hold' ? 'hold' : row.posture === 'Mutual move' ? '' : 'ask';
      return `<div class="concession-row"><div><b>${esc(row.name)}</b><small>${esc(row.posture)}</small></div>
        <span class="concession-chip ${cls}">U.S. relief ${f(row.usRelief,0)}pt</span>
        <span class="concession-chip ${cls}">CA relief ${f(row.caRelief,0)}pt</span></div>`;
    }).join('') || '<p>No material sector movement is present in the evaluated delegation settings.</p>';
  }

  function renderLanguage(view) {
    const target = document.querySelector('#diplomatTalkingPoints');
    target.innerHTML = view.language.length
      ? view.language.map(item => `<li><b>${esc(item.label || 'Language')}</b>${esc(item.text || '')}</li>`).join('')
      : '<li>Open the principal brief after the current evaluation settles to load synchronized recommended language.</li>';
  }

  function renderUncertainties(model, view) {
    document.querySelector('#diplomatUncertainties').innerHTML = listHtml(view.uncertainties, 'No uncertainty note is available in the current principal model.');
    const evidence = view.evidence.map(item => `${item.agency || 'Unknown agency'} — ${item.dataset || 'Unnamed dataset'} (${item.vintage || 'unspecified'}; ${item.status || 'unspecified'})`);
    document.querySelector('#diplomatEvidence').innerHTML = `<p><b>Calibration:</b> ${model ? `${f(model.calibrationCompleteness,0)}% · ${esc(model.calibrationGrade)}${model.empiricalCertified?' · empirically certified':' · partial/not certified'}` : 'unavailable'}</p>${listHtml(evidence, 'No evidence-source record is available.')}`;
  }

  function renderMatrix(view) {
    document.querySelector('#diplomatMatrixNote').textContent = `${view.scenarios.length} best unique policy scenarios · bargaining package authority/BATNA analysis above is a separate layer.`;
    document.querySelector('#diplomatMatrixBody').innerHTML = view.scenarios.map((scenario,i) => {
      const gate = checks(scenario);
      return `<tr><td>${i+1}</td><th scope="row">${esc(scenario.name)}</th><td>${f(canadaScore(scenario),0)}</td><td>${f(scenario.usScore,0)}</td><td>${f(scenario.growth)}%</td><td>${f(scenario.usGrowth)}%</td><td>${f(scenario.inflation)}%</td><td>${f(scenario.recessionRisk,0)}%</td><td class="${gate.pass?'yes':'no'}">${gate.pass?'CLEARS':'BREACH'}</td></tr>`;
    }).join('');
  }

  async function renderDiplomat() {
    institutionalizeLabels();
    if (typeof result === 'undefined' || !result?.scenarios?.length || !document.querySelector('#diplomatCommand')) return;
    const epoch = ++renderEpoch;
    let principalModel = null;
    try {
      principalModel = await synchronizedPrincipalModel();
    } catch (error) {
      console.error('Diplomatic decision desk synchronization failed', error);
    }
    if (epoch !== renderEpoch || !document.querySelector('#diplomatCommand')) return;
    const scenarios = principalModel?.leadingScenario
      ? (typeof result !== 'undefined' ? result.scenarios : [])
      : result.scenarios;
    const concessions = concessionRows(principalModel);
    const view = buildDeskView(principalModel, scenarios, concessions);
    renderSituation(principalModel);
    renderStatus(principalModel, view);
    renderDecisionRequired(view);
    renderPackages(principalModel, view);
    renderContext(view);
    renderRedlines(principalModel, view);
    renderConcessions(view);
    renderLanguage(view);
    renderUncertainties(principalModel, view);
    renderMatrix(view);
  }

  function scheduleRenderDiplomat(delay=0) {
    clearTimeout(renderTimer);
    renderTimer = setTimeout(() => { void renderDiplomat(); }, delay);
  }

  function bind() {
    document.querySelector('#closeBriefing').addEventListener('click', () => document.querySelector('#diplomaticBriefing').close());
    document.querySelector('#diplomatCommand').addEventListener('click', event => {
      const card = event.target.closest('[data-diplomat-scenario]');
      if (!card || typeof result === 'undefined') return;
      const scenario = result.scenarios.find(s => s.id === card.dataset.diplomatScenario);
      if (scenario && typeof selected !== 'undefined') {
        selected = scenario;
        if (typeof render === 'function') render();
      }
    });
    document.querySelectorAll('[data-redline]').forEach(input => {
      input.value = limits[input.dataset.redline];
      input.addEventListener('input', () => {
        limits[input.dataset.redline] = Number(input.value);
        saveJson(LIMITS_KEY, limits);
        scheduleRenderDiplomat();
      });
    });
    const notes = document.querySelector('#diplomatNotes');
    try { notes.value = localStorage.getItem(NOTES_KEY) || ''; } catch (_) {}
    notes.addEventListener('input', () => {
      try { localStorage.setItem(NOTES_KEY, notes.value); } catch (_) {}
      document.querySelector('#notesSaved').textContent = 'Saved locally';
      scheduleRenderDiplomat(60);
    });
  }

  function inject() {
    if (document.querySelector('#diplomatCommand')) return;
    document.title = 'Canada–U.S. Diplomatic Policy Studio';
    const brandStrong = document.querySelector('.brand strong');
    const brandSpan = document.querySelector('.brand span');
    if (brandStrong) brandStrong.textContent = 'Canada–U.S. Diplomatic Policy Studio';
    if (brandSpan) {
      brandSpan.textContent = 'Negotiation intelligence · economic scenario lab';
      brandSpan.insertAdjacentHTML('afterend','<span class="diplomat-mode-badge">Diplomatic mode</span>');
    }
    const confidenceLabel = document.querySelector('.confidence span');
    if (confidenceLabel) confidenceLabel.textContent = 'DATA COVERAGE INDICATOR';
    const canadaTab = document.querySelector('[data-negotiator="canada"]');
    if (canadaTab) canadaTab.textContent = '🇨🇦 Canada delegation';
    const usTab = document.querySelector('[data-negotiator="us"]');
    if (usTab) usTab.textContent = '🇺🇸 U.S. delegation';
    institutionalizeLabels();

    const anchor = document.querySelector('.impact-strip');
    if (!anchor) return;
    const section = document.createElement('section');
    section.id = 'diplomatCommand';
    section.className = 'diplomat-command';
    section.innerHTML = `<div class="diplomat-command-head"><div><div class="eyebrow">Diplomatic decision desk</div><h2>One synchronized situation · one negotiating narrative</h2><p>The desk now distills the same coherent principal-decision model used by the Principal Brief. Delegation tariffs, outcome weights, risk preferences, sector coverage, bargaining-room records, authority, BATNAs and robustness are kept tied to the evaluated situation; repeated package and narrative items are removed before display.</p></div><div class="diplomat-actions"><button id="openBriefing" class="primary" type="button">Open principal brief</button><button id="printBriefing" type="button">Save principal brief PDF</button></div></div>
      <div id="diplomatSituation" class="diplomat-callout"><b>Current delegation situation:</b> Synchronizing the evaluated delegation settings with the principal decision model.</div>
      <div class="diplomat-status"><div><span>Primary bargaining package</span><b id="diplomatPrimary">Evaluating…</b><small id="diplomatPrimaryNote"></small></div><div><span>Model trust & authority</span><b id="diplomatAuthority">—</b><small id="diplomatAuthorityNote"></small></div><div><span>Deal vs walk-away</span><b id="diplomatBatna">—</b><small id="diplomatBatnaNote"></small></div><div><span>Analytical red lines</span><b id="diplomatRedlineStatus">—</b><small id="diplomatRedlineNote"></small></div></div>
      <div class="diplomat-body">
        <div id="diplomatDecisionRequired" class="diplomat-callout"><b>Decision required now:</b> Evaluating.</div>
        <div id="diplomatPackages" class="package-lanes"></div>
        <div class="diplomat-workgrid">
          <section class="diplomat-pane"><div class="eyebrow">Principal brief · delta</div><h3>What changed</h3><div id="diplomatWhatChanged"></div></section>
          <section class="diplomat-pane"><div class="eyebrow">Recorded counterpart position</div><h3>What the U.S. wants</h3><div id="diplomatTheyWant"></div></section>
          <section class="diplomat-pane"><div class="eyebrow">Recorded Canadian position</div><h3>What Canada wants</h3><div id="diplomatWeWant"></div></section>
        </div>
        <div class="diplomat-workgrid">
          <section class="diplomat-pane"><div class="eyebrow">Mandate discipline</div><h3>Authority & red lines</h3><p>Browser red lines are analytical guardrails only. Recorded Diplomat Room mandate restrictions are shown separately and do not alter the economic model.</p><div class="redline-grid">
            <label for="rlCanada">Minimum Canada score</label><input id="rlCanada" data-redline="minCanada" type="number" min="0" max="100" step="1">
            <label for="rlUs">Minimum U.S. score</label><input id="rlUs" data-redline="minUs" type="number" min="0" max="100" step="1">
            <label for="rlRecession">Maximum recession risk %</label><input id="rlRecession" data-redline="maxRecession" type="number" min="0" max="100" step="1">
            <label for="rlGrowth">Minimum bilateral growth %</label><input id="rlGrowth" data-redline="minGrowth" type="number" min="-3" max="5" step="0.1">
            <label for="rlInflation">Maximum inflation %</label><input id="rlInflation" data-redline="maxInflation" type="number" min="0" max="10" step="0.1"></div><div id="redlineSummary" class="redline-summary"></div><div id="principalMandateSummary"></div></section>
          <section class="diplomat-pane"><div class="eyebrow">Evaluated delegation settings</div><h3>Reciprocity map</h3><p>Only materially moving sector positions are emphasized. Relief is measured from full headline-sector coverage and reflects the currently evaluated delegation tables.</p><div id="concessionList" class="concession-list"></div></section>
          <section class="diplomat-pane"><div class="eyebrow">Principal brief · recommended language</div><h3>In-room language</h3><ol id="diplomatTalkingPoints" class="talking-points"></ol><textarea id="diplomatNotes" class="diplomat-notes" placeholder="Private working notes, sequencing, sensitivities, names to brief…"></textarea><div class="notes-foot"><span>Stored in this browser only</span><span id="notesSaved">Working notes</span></div></section>
        </div>
        <div class="diplomat-workgrid">
          <section class="diplomat-pane"><div class="eyebrow">Principal brief · situation</div><h3>Where we are</h3><div id="diplomatWhereWeAre"></div></section>
          <section class="diplomat-pane"><div class="eyebrow">Principal brief · uncertainty</div><h3>Key uncertainties</h3><div id="diplomatUncertainties"></div></section>
          <section class="diplomat-pane"><div class="eyebrow">Traceability</div><h3>Evidence posture</h3><div id="diplomatEvidence"></div></section>
        </div>
        <div class="package-matrix-wrap"><div class="diplomat-callout"><b>Unique policy comparison:</b> <span id="diplomatMatrixNote"></span></div><table class="package-matrix"><thead><tr><th>#</th><th>Unique policy scenario</th><th>Canada</th><th>U.S.</th><th>CA GDP</th><th>U.S. GDP</th><th>Inflation</th><th>Recession</th><th>Red lines</th></tr></thead><tbody id="diplomatMatrixBody"></tbody></table></div>
        <div class="diplomat-callout"><b>Protocol:</b> the desk and the Principal Brief now use the same synchronized decision model. Separate evaluated model facts, recorded bargaining-room facts, mandate authority and political judgment; do not turn analytical ranking into an instruction that the model does not have authority to give.</div>
      </div>`;
    anchor.insertAdjacentElement('afterend', section);

    const dialog = document.createElement('dialog');
    dialog.id = 'diplomaticBriefing';
    dialog.innerHTML = '<div class="briefing-toolbar"><b>Principal decision brief</b><div><button id="copyBriefing" type="button">Copy principal brief</button><button type="button" onclick="window.print()">Save principal PDF</button><button id="closeBriefing" type="button">Close</button></div></div><div id="briefingSheet" class="briefing-sheet"></div>';
    document.body.appendChild(dialog);
    bind();

    const cards = document.querySelector('#cards');
    if (cards) new MutationObserver(() => scheduleRenderDiplomat()).observe(cards, {childList:true});
    const party = document.querySelector('#partyView');
    if (party) new MutationObserver(institutionalizeLabels).observe(party, {childList:true,subtree:true,characterData:true});
    scheduleRenderDiplomat();
    setTimeout(() => scheduleRenderDiplomat(), 0);
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', inject);
  else inject();
})();
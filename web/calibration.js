(() => {
  'use strict';

  const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const pct = value => `${Number(value || 0).toFixed(0)}%`;
  const ratioPct = value => `${(100 * Number(value || 0)).toFixed(1)}%`;
  const fmt = (value, digits = 2) => Number(value ?? 0).toFixed(digits);
  let structuralCalibration = null;
  let structuralRegistryComplete = false;
  let runtimeEvidence = null;

  function inject() {
    if (document.querySelector('#calibrationTrust')) return;
    const anchor = document.querySelector('#computationalNegotiation') || document.querySelector('#diplomatCommand') || document.querySelector('.impact-strip');
    if (!anchor) return;
    const section = document.createElement('section');
    section.id = 'calibrationTrust';
    section.className = 'calibration-trust';
    section.innerHTML = `<div class="calibration-head"><div><div class="eyebrow">Data provenance & calibration</div><h2>What is observed, estimated, or still assumed?</h2><p>A win-win optimization is only as credible as the data and behavioural estimates underneath it. This panel separates observed-data calibration from direct empirical calibration of the structural production model.</p></div><div id="calibrationGrade" class="calibration-grade">LOADING</div></div><div id="calibrationSummary" class="calibration-summary"></div><div class="eyebrow calibration-layer-label">Structural production calibration</div><div id="calibrationStructuralSummary" class="calibration-summary structural-summary"><div><span>Direct empirical mappings</span><b>Loading…</b><small>production parameters only</small></div></div><div class="calibration-grid"><section><div class="eyebrow">Integrity gates</div><div id="calibrationChecks" class="calibration-checks"></div></section><section><div class="eyebrow">Source vintages</div><div id="calibrationSources" class="calibration-sources"></div></section></div><div id="calibrationMeasures" class="calibration-measures"></div><div id="calibrationWarning" class="calibration-warning"></div>`;
    anchor.insertAdjacentElement('afterend', section);
  }

  function injectEvidence() {
    if (document.querySelector('#modelEvidence')) return;
    const anchor = document.querySelector('#calibrationTrust');
    if (!anchor) return;
    const section = document.createElement('section');
    section.id = 'modelEvidence';
    section.className = 'model-evidence';
    section.innerHTML = `
      <div class="calibration-head"><div><div class="eyebrow">Model evidence V2</div><h2>Does the recommendation survive assumptions, history, and preferences?</h2><p>These are separate diagnostics. Structural uncertainty, historical backtests, and delegation-preference sensitivity are not interchangeable confidence measures.</p></div><div id="evidenceGrade" class="calibration-grade">READY</div></div>
      <div id="evidenceRegistry" class="calibration-summary"><div><span>Structural calibration completeness</span><b>Loading…</b><small>direct production mappings only</small></div></div>
      <div class="evidence-actions">
        <button id="runHistoricalEvidence" type="button">Historical diagnostics</button>
        <button id="runWelfareEvidence" type="button">Preference sensitivity</button>
        <button id="runStructuralEvidence" type="button">Structural robustness · 6 draws</button>
      </div>
      <div id="evidenceNotice" class="evidence-notice">Loading the current production-data boundary…</div>
      <div id="evidenceOutput" class="evidence-output"></div>`;
    anchor.insertAdjacentElement('afterend', section);
    bindEvidence();
    loadRegistry();
  }

  async function jsonFetch(url, options) {
    const response = await fetch(url, options);
    const data = await response.json();
    if (!response.ok) throw new Error(data?.error || `Request failed (${response.status})`);
    return data;
  }

  function busy(button, active, label) {
    if (!button) return;
    if (active) {
      button.dataset.label = button.textContent;
      button.textContent = label;
      button.disabled = true;
    } else {
      button.textContent = button.dataset.label || button.textContent;
      button.disabled = false;
    }
  }

  function structuralCards(c) {
    const coverage = Number(c?.directEmpiricalCoverage || 0);
    return `
      <div><span>Direct empirical mappings</span><b>${fmt(coverage,1)}%</b><small>${Number(c?.directEmpiricalCount || 0)}/${Number(c?.calibrationTargetCount || 0)} estimable production parameters</small></div>
      <div><span>Shock variances</span><b>${Number(c?.directEmpiricalShockCount || 0)}/${Number(c?.shockTargetCount || 0)}</b><small>${Number(c?.realizedResidualShockCount || 0)} promoted from realized residual estimation</small></div>
      <div><span>Remaining multipliers</span><b>${Number(c?.directEmpiricalMultiplierCount || 0)}/${Number(c?.multiplierTargetCount || 0)}</b><small>${fmt(c?.multiplierCoverage,1)}% directly calibrated</small></div>
      <div><span>Still provisional</span><b>${Number(c?.provisionalCount || 0)}</b><small>assumed/calibrated production parameters</small></div>`;
  }

  function updateCalibrationGrade() {
    const grade = document.querySelector('#calibrationGrade');
    if (!grade) return;
    const observedCertified = typeof result !== 'undefined' && !!result?.calibration?.certifiedForEmpiricalUse;
    if (structuralCalibration && structuralRegistryComplete) {
      const coverage = Number(structuralCalibration.directEmpiricalCoverage || 0);
      const gradeText = String(structuralCalibration.grade || 'mostly-provisional').replaceAll('-', ' ').toUpperCase();
      grade.textContent = `STRUCTURAL ${gradeText}`;
      grade.classList.toggle('certified', observedCertified && coverage >= 95);
      return;
    }
    if (typeof result !== 'undefined' && result?.calibration) {
      const c = result.calibration;
      grade.textContent = c.certifiedForEmpiricalUse ? 'OBSERVED DATA CALIBRATED' : String(c.grade || 'INCOMPLETE').replaceAll('-', ' ').toUpperCase();
      grade.classList.toggle('certified', !!c.certifiedForEmpiricalUse);
    }
  }

  async function loadRegistry() {
    try {
      const [r, runtime, state] = await Promise.all([
        jsonFetch('/api/v2/structural-registry', {cache:'no-store'}),
        jsonFetch('/api/v2/evidence-status', {cache:'no-store'}),
        jsonFetch('/api/v2/state-measurements', {cache:'no-store'})
      ]);
      const c = r.calibrationCompleteness || {};
      structuralCalibration = c;
      structuralRegistryComplete = !!r.complete;
      runtimeEvidence = runtime;
      const coverage = Number(c.directEmpiricalCoverage || 0);
      const gradeText = String(c.grade || (r.complete ? 'mostly-provisional' : 'registry-incomplete')).replaceAll('-', ' ').toUpperCase();
      const grade = document.querySelector('#evidenceGrade');
      if (grade) {
        grade.textContent = r.complete ? gradeText : 'REGISTRY INCOMPLETE';
        grade.classList.toggle('certified', !!r.complete && coverage >= 95);
      }
      const plumbingPass = runtime.decisionLossWeightsComplete && runtime.stateMeasurementContractComplete;
      const cards = structuralCards(c) + `
        <div><span>Runtime plumbing</span><b>${plumbingPass?'PASS':'CHECK'}</b><small>${Number(runtime.decisionLossWeightCount||0)}/12 loss weights · ${Number(runtime.readyStateMeasurementCount||0)} state-measurement contracts active</small></div>
        <div><span>U.S. production network</span><b>${runtime.usIoEmpirical?'BEA EMPIRICAL':'EPA USEEIO PROXY'}</b><small>${runtime.usIoEmpirical?'certified current-vintage U.S. IO artifact active':'USEEIO v2.5 Catbird-22 · 2022 model / 2017 IO basis · BEA certification pending'}</small></div>
        <div><span>Canada↔U.S. intermediate sourcing</span><b>OECD ICIO PENDING</b><small>domestic IO networks are active; bilateral sourcing awaits official ICIO bytes plus a reviewed fractional crosswalk</small></div>
        <div><span>Historical macro validation</span><b>${Number(runtime.validHistoricalFixtureCount||0)}/${Number(runtime.historicalFixtureCount||0)} VALID</b><small>descriptive macro-policy fixtures; not statistical validation</small></div>`;
      const target = document.querySelector('#evidenceRegistry');
      if (target) target.innerHTML = cards;
      const panelTarget = document.querySelector('#calibrationStructuralSummary');
      if (panelTarget) panelTarget.innerHTML = cards;
      const notice = document.querySelector('#evidenceNotice');
      if (notice) {
        notice.innerHTML = `<b>Current evidence boundary:</b> ${runtime.usIoEmpirical ? 'the certified BEA U.S. production network is active.' : 'the U.S. production network uses the EPA USEEIO v2.5 proxy until exact BEA certification.'} OECD bilateral intermediate sourcing is not active in production. Structural and preference diagnostics rerun the production optimizer; they do not convert provisional coefficients into empirical estimates.`;
      }
      void state;
      updateCalibrationGrade();
    } catch (error) {
      structuralCalibration = null;
      structuralRegistryComplete = false;
      runtimeEvidence = null;
      const target = document.querySelector('#evidenceRegistry');
      if (target) target.innerHTML = `<div><span>V2 evidence API</span><b>Unavailable</b><small>${esc(error.message)}</small></div>`;
      const panelTarget = document.querySelector('#calibrationStructuralSummary');
      if (panelTarget) panelTarget.innerHTML = `<div><span>Structural calibration API</span><b>Unavailable</b><small>${esc(error.message)}</small></div>`;
      const notice = document.querySelector('#evidenceNotice');
      if (notice) notice.textContent = 'The evidence-status API is unavailable; do not infer production-network or calibration readiness from the UI alone.';
      updateCalibrationGrade();
    }
  }

  function renderHistorical(data) {
    const s = data.summary || {};
    const episodes = data.episodes || [];
    document.querySelector('#evidenceOutput').innerHTML = `
      <div class="evidence-result-head"><div><div class="eyebrow">Historical vintage diagnostics</div><h3>${Number(s.validCount || 0)}/${Number(s.fixtureCount || 0)} valid episodes · ${fmt(s.meanStateCoverage,0)}% mean declared-state coverage</h3></div><span class="evidence-pill">NO-LOOK-AHEAD ${s.allValidNoLookahead?'PASS':'FAIL'}</span></div>
      <div class="evidence-metrics">
        <div><span>Policy direction accuracy</span><b>${ratioPct(s.policy?.directionAccuracy)}</b><small>MAE ${fmt(s.policy?.meanAbsoluteErrorBp,1)} bp</small></div>
        <div><span>Inflation direction</span><b>${ratioPct(s.inflation?.directionAccuracy)}</b><small>MAE ${fmt(s.inflation?.meanAbsoluteError,2)}</small></div>
        <div><span>Growth direction</span><b>${ratioPct(s.growth?.directionAccuracy)}</b><small>MAE ${fmt(s.growth?.meanAbsoluteError,2)}</small></div>
        <div><span>Unemployment direction</span><b>${ratioPct(s.unemployment?.directionAccuracy)}</b><small>MAE ${fmt(s.unemployment?.meanAbsoluteError,2)}</small></div>
      </div>
      <div class="evidence-cases">${episodes.map(e=>`<article><b>${esc(e.fixtureId)}</b><span>${esc(e.decisionDate)} · ${esc(e.stateGrade)}</span><small>policy ${e.policyDirectionMatch?'direction match':'direction miss'} · predicted ${fmt(e.recommendedFirstMoveBp,0)} bp vs realized ${fmt(e.realizedFirstMoveBp,0)} bp</small></article>`).join('')}</div>
      <p class="evidence-footnote">These shipped episodes permit descriptive aggregate reporting only; they do not constitute statistical validation.</p>`;
  }

  function renderWelfare(w) {
    const alternatives = (w.alternatives || []).map(a=>`<article><b>${esc(a.strategyId)}</b><span>${ratioPct(a.winRate)} of tested profiles</span><small>${Number(a.wins || 0)} wins</small></article>`).join('');
    document.querySelector('#evidenceOutput').innerHTML = `
      <div class="evidence-result-head"><div><div class="eyebrow">Normative preference sensitivity</div><h3>${esc(w.classification || 'not evaluated')} · ${Number(w.profileCount || 0)} tested preference profiles</h3></div><span class="evidence-pill">MANDATE WEIGHTS ${w.allMandateWeightsFixed?'FIXED':'CHECK'}</span></div>
      <div class="evidence-metrics">
        <div><span>Exact control retention</span><b>${ratioPct(w.exactRecommendationRetentionRate)}</b><small>same recommended controls</small></div>
        <div><span>Strategy-family retention</span><b>${ratioPct(w.strategyFamilyRetentionRate)}</b><small>same named strategy</small></div>
        <div><span>Sector-package retention</span><b>${ratioPct(w.sectorPackageRetentionRate)}</b><small>same 20-sector package</small></div>
        <div><span>Fairness range</span><b>${fmt(w.fairnessMin)}–${fmt(w.fairnessMax)}</b><small>minimum-country score</small></div>
      </div>
      <div class="evidence-switches"><b>Nearest tested switches:</b> priority ${w.prioritySwitchObserved?`${fmt(w.nearestPrioritySwitchPoints,0)} points`:'none in grid'} · risk ${w.riskSwitchObserved?`${fmt(w.nearestRiskSwitchPoints,0)} points`:'none in grid'}.</div>
      <div class="evidence-cases">${alternatives}</div>
      <p class="evidence-footnote">The 3×3 grid is a local decision-stability experiment, not a probability distribution or confidence interval.</p>`;
  }

  function renderStructural(r) {
    document.querySelector('#evidenceOutput').innerHTML = `
      <div class="evidence-result-head"><div><div class="eyebrow">Structural decision robustness</div><h3>${esc(r.classification || 'not evaluated')} · ${Number(r.parameterDraws || 0)} structural calibrations</h3></div><span class="evidence-pill">FULL DECISION SEARCH</span></div>
      <div class="evidence-metrics">
        <div><span>Exact recommendation survival</span><b>${ratioPct(r.recommendationWinRate)}</b><small>${Number(r.recommendationWins || 0)}/${Number(r.parameterDraws || 0)} draws</small></div>
        <div><span>Strategy-family survival</span><b>${ratioPct(r.strategyFamilyWinRate)}</b><small>allows control drift</small></div>
        <div><span>Control retention</span><b>${ratioPct(r.referencePolicyControlRetentionRate)}</b><small>288-control search rerun</small></div>
        <div><span>Sector-package retention</span><b>${ratioPct(r.referencePackageRetentionRate)}</b><small>20-sector package reoptimized</small></div>
      </div>
      <div class="evidence-switches"><b>Audit:</b> ${Number(r.policyControlCandidatesExamined || 0).toLocaleString()} control candidates · ${Number(r.nestedSectorOptimizations || 0).toLocaleString()} nested sector optimizations · common random numbers ${r.commonRandomNumbers?'on':'off'}.</div>
      <p class="evidence-footnote">The interactive 6-draw run is a fast sensitivity screen. Use 24 draws for the repository's reference V2 structural experiment.</p>`;
  }

  function bindEvidence() {
    document.querySelector('#runHistoricalEvidence')?.addEventListener('click', async event => {
      const button = event.currentTarget;
      busy(button, true, 'Running historical suite…');
      try { renderHistorical(await jsonFetch('/api/v2/backtests', {cache:'no-store'})); }
      catch (error) { document.querySelector('#evidenceOutput').innerHTML = `<div class="calibration-warning"><b>Historical diagnostics failed.</b> ${esc(error.message)}</div>`; }
      finally { busy(button, false); }
    });
    document.querySelector('#runWelfareEvidence')?.addEventListener('click', async event => {
      const button = event.currentTarget;
      busy(button, true, 'Running preference grid…');
      try { renderWelfare(await jsonFetch('/api/v2/welfare', {method:'POST'})); }
      catch (error) { document.querySelector('#evidenceOutput').innerHTML = `<div class="calibration-warning"><b>Preference sensitivity failed.</b> ${esc(error.message)}</div>`; }
      finally { busy(button, false); }
    });
    document.querySelector('#runStructuralEvidence')?.addEventListener('click', async event => {
      const button = event.currentTarget;
      busy(button, true, 'Running structural search…');
      try { renderStructural(await jsonFetch('/api/v2/robustness', {method:'POST'})); }
      catch (error) { document.querySelector('#evidenceOutput').innerHTML = `<div class="calibration-warning"><b>Structural robustness failed.</b> ${esc(error.message)}</div>`; }
      finally { busy(button, false); }
    });
  }

  function render() {
    inject();
    injectEvidence();
    if (typeof result === 'undefined' || !result?.calibration) return;
    const c = result.calibration;
    const certified = !!c.certifiedForEmpiricalUse;
    updateCalibrationGrade();

    document.querySelector('#calibrationSummary').innerHTML = `<div><span>Snapshot</span><b>${esc(c.snapshotId)}</b><small>as of ${esc(c.asOf || 'unknown')}</small></div><div><span>Observed-data calibration completeness</span><b>${pct(c.completeness)}</b><small>${certified ? 'observed/derived trade-state gates pass' : 'trade/state data layer; structural production calibration is shown separately'}</small></div><div><span>Generated</span><b>${esc(c.generatedAt || 'unknown')}</b><small>snapshot provenance is versioned</small></div>`;
    if (structuralCalibration) {
      const structural = document.querySelector('#calibrationStructuralSummary');
      if (structural) structural.innerHTML = structuralCards(structuralCalibration);
    }

    const checks = c.checks || {};
    const labels = {
      officialTrade: 'Official bilateral trade', tariffLines: 'Applied tariff lines', inputOutput: 'Input-output propagation',
      originUtilization: 'Rules-of-origin utilization', elasticitiesEstimated: 'Trade elasticities', passThroughEstimated: 'Price pass-through'
    };
    document.querySelector('#calibrationChecks').innerHTML = Object.entries(labels).map(([key,label]) => `<div class="calibration-check ${checks[key]?'pass':'missing'}"><span>${checks[key]?'✓':'!'}</span><b>${esc(label)}</b><small>${checks[key]?'present in evaluated snapshot':'missing / not certified'}</small></div>`).join('');

    document.querySelector('#calibrationSources').innerHTML = (c.sources || []).slice(0,12).map(source => `<div class="calibration-source"><b>${esc(source.agency)}</b><span>${esc(source.dataset)}</span><small>${esc(source.vintage || 'vintage not captured')} · ${esc(source.status || 'status unknown')}</small></div>`).join('');

    const measures = c.measures || [];
    document.querySelector('#calibrationMeasures').innerHTML = measures.length ? `<div class="eyebrow">Legal tariff timeline</div><div class="measure-list">${measures.map(m => `<article class="measure ${String(m.status).includes('future')?'future':''}"><b>${esc(m.jurisdiction)} · ${esc(m.instrument)}</b><span>${esc(m.rate)}</span><small>announced ${esc(m.announced || '—')} · effective ${esc(m.effectiveFrom || '—')}${m.effectiveTo ? ` to ${esc(m.effectiveTo)}` : ''} · ${esc(m.status)}</small><p>${esc(m.scope)}</p></article>`).join('')}</div>` : '';

    const warning = document.querySelector('#calibrationWarning');
    const structuralCoverage = Number(structuralCalibration?.directEmpiricalCoverage || 0);
    const structuralCertified = structuralRegistryComplete && structuralCoverage >= 95;
    warning.classList.toggle('certified', certified && structuralCertified);
    if (!certified) {
      warning.innerHTML = `<b>Observed-data calibration is not complete.</b> ${esc(c.warning || 'Missing observed/derived layers remain.')} Structural production calibration is reported independently and counts only direct empirical mappings.`;
    } else if (!structuralCertified) {
      warning.innerHTML = `<b>Observed-data gates pass; structural calibration remains ${esc(String(structuralCalibration?.grade || 'mostly-provisional').replaceAll('-', ' '))}.</b> ${fmt(structuralCoverage,1)}% of estimable production parameters have direct empirical mappings; ${Number(structuralCalibration?.directEmpiricalShockCount || 0)}/${Number(structuralCalibration?.shockTargetCount || 0)} shock variances and ${Number(structuralCalibration?.directEmpiricalMultiplierCount || 0)}/${Number(structuralCalibration?.multiplierTargetCount || 0)} remaining multipliers are directly calibrated.`;
    } else {
      warning.innerHTML = `<b>Observed-data and structural empirical calibration gates pass.</b> The result still contains model uncertainty and requires legal/economic judgment; calibration is not a prediction of political acceptance.`;
    }
  }

  function appendBriefing() {
    setTimeout(() => {
      if (typeof result === 'undefined' || !result?.calibration) return;
      const sheet = document.querySelector('#briefingSheet');
      if (!sheet || sheet.querySelector('#calibrationBriefSection')) return;
      const c = result.calibration, checks = c.checks || {};
      const missing = Object.entries(checks).filter(([,ok]) => !ok).map(([key]) => key).join(', ');
      const sources = (c.sources || []).slice(0,8).map(s => `<li><b>${esc(s.agency)}</b> — ${esc(s.dataset)} · ${esc(s.vintage || 'vintage not captured')} · ${esc(s.status)}</li>`).join('');
      const structural = structuralCalibration ? `<p><b>Structural production calibration:</b> ${fmt(structuralCalibration.directEmpiricalCoverage,1)}% direct empirical mappings (${Number(structuralCalibration.directEmpiricalCount || 0)}/${Number(structuralCalibration.calibrationTargetCount || 0)}); shock variances ${Number(structuralCalibration.directEmpiricalShockCount || 0)}/${Number(structuralCalibration.shockTargetCount || 0)}; remaining multipliers ${Number(structuralCalibration.directEmpiricalMultiplierCount || 0)}/${Number(structuralCalibration.multiplierTargetCount || 0)}; ${Number(structuralCalibration.provisionalCount || 0)} parameters still provisional.</p>` : `<p><b>Structural production calibration:</b> unavailable.</p>`;
      const network = runtimeEvidence ? `<p><b>Production-network evidence:</b> Canada uses the empirical StatCan 2024 matrix; the U.S. network is ${runtimeEvidence.usIoEmpirical ? 'the certified BEA artifact' : 'the EPA USEEIO v2.5 Catbird-22 proxy (2022 model / 2017 IO basis), with BEA certification pending'}. Canada↔U.S. OECD ICIO intermediate sourcing is not yet active in production.</p>` : `<p><b>Production-network evidence:</b> runtime status unavailable.</p>`;
      const section = `<section id="calibrationBriefSection"><h2>Data provenance and calibration</h2><p><b>Snapshot:</b> ${esc(c.snapshotId)} · as of ${esc(c.asOf || 'unknown')} · observed-data calibration completeness ${pct(c.completeness)} · grade <b>${esc(c.grade)}</b>.</p>${structural}${network}<p><b>Observed-data empirical-use certification:</b> ${c.certifiedForEmpiricalUse?'PASS':'NOT YET CERTIFIED'}.</p>${missing?`<p><b>Missing calibration gates:</b> ${esc(missing)}.</p>`:''}<h3>Principal source vintages</h3><ul>${sources}</ul><p><small>Observed, official-derived, empirically estimated and assumption inputs are intentionally distinguished. A verified optimizer result is not equivalent to an empirically calibrated forecast.</small></p></section>`;
      const warning = sheet.querySelector('.briefing-warning');
      if (warning) warning.insertAdjacentHTML('beforebegin', section); else sheet.insertAdjacentHTML('beforeend', section);
    }, 0);
  }

  function start() {
    inject();
    injectEvidence();
    const cards = document.querySelector('#cards');
    if (cards) new MutationObserver(render).observe(cards, {childList:true});
    document.querySelector('#openBriefing')?.addEventListener('click', appendBriefing);
    document.querySelector('#printBriefing')?.addEventListener('click', appendBriefing);
    render();
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start); else start();
})();
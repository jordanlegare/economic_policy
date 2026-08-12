(() => {
  'use strict';

  // The policy engine returns a sector schedule that has already been re-simulated
  // and verified. The displayed auto-applied agreement is deliberately separate
  // from the delegation posture used as the next search anchor, so applying a
  // recommendation cannot compound the cooperation envelope against itself.
  let negotiationAnchor = null;
  let lastAutoCoverage = null;
  let comparisonCache = null;
  let comparisonCacheKey = '';
  let comparisonTimer = null;
  let comparisonTask = Promise.resolve();
  let initialCalibrationApplied = false;
  let sectorPolicyCache = {result:null, scenarioId:'', terms:null};

  const comparisonDelayMs = Number(window.__EVALUATION_COMPARISON_DELAY_MS ?? 900);

  const copyCoverage = source => ({
    canada: [...source.canada],
    us: [...source.us]
  });

  const coverageEqual = (a, b) => !!a && !!b
    && sameCoverage(a.canada, b.canada)
    && sameCoverage(a.us, b.us);

  const clampValue = (value, lo, hi) => Math.max(lo, Math.min(hi, value));
  const finite = (value, fallback = 0) => Number.isFinite(Number(value)) ? Number(value) : fallback;

  const sectorProfiles = [
    {trade:.82, import:.42, jobs:.72, cyclical:.65},
    {trade:.88, import:.18, jobs:.32, cyclical:.75},
    {trade:.16, import:.10, jobs:.25, cyclical:.25},
    {trade:.18, import:.28, jobs:.82, cyclical:.88},
    {trade:.94, import:.76, jobs:.68, cyclical:.92},
    {trade:.68, import:.58, jobs:.64, cyclical:.74},
    {trade:.30, import:.72, jobs:.88, cyclical:.62},
    {trade:.72, import:.48, jobs:.70, cyclical:.86},
    {trade:.34, import:.30, jobs:.48, cyclical:.44},
    {trade:.22, import:.20, jobs:.34, cyclical:.55},
    {trade:.10, import:.12, jobs:.30, cyclical:.78},
    {trade:.38, import:.26, jobs:.58, cyclical:.48},
    {trade:.20, import:.18, jobs:.24, cyclical:.40},
    {trade:.28, import:.24, jobs:.86, cyclical:.72},
    {trade:.08, import:.10, jobs:.82, cyclical:.18},
    {trade:.06, import:.14, jobs:.94, cyclical:.16},
    {trade:.14, import:.16, jobs:.88, cyclical:.68},
    {trade:.18, import:.52, jobs:.96, cyclical:.82},
    {trade:.16, import:.30, jobs:.90, cyclical:.58},
    {trade:.04, import:.08, jobs:.62, cyclical:.12}
  ];

  // Mirrors the policy-engine strategy terms. Keeping the deterministic sector
  // response equation in the browser means the delegation table can explore a
  // continuous slider position immediately, while the server remains the source
  // of truth for the expensive stochastic search and verified recommendation.
  const fixedStrategyTerms = {
    statusquo:    {fiscal:0,    productive:.50, deescalation:0,    targetedRelief:0,   diversification:0},
    retaliate:    {fiscal:.35,  productive:.25, deescalation:0,    targetedRelief:.25, diversification:0},
    relief:       {fiscal:.30,  productive:.65, deescalation:0,    targetedRelief:.35, diversification:.15},
    compact:      {fiscal:.25,  productive:.90, deescalation:.85,  targetedRelief:.10, diversification:.20},
    diversify:    {fiscal:.35,  productive:.90, deescalation:0,    targetedRelief:.10, diversification:.45},
    guardrail:    {fiscal:-.10, productive:.75, deescalation:.20,  targetedRelief:0,   diversification:.10},
    supply:       {fiscal:.40,  productive:.95, deescalation:.35,  targetedRelief:.20, diversification:.25},
    stabilizer:   {fiscal:.22,  productive:.35, deescalation:0,    targetedRelief:.30, diversification:.08},
    eastwest:     {fiscal:.48,  productive:.96, deescalation:0,    targetedRelief:.08, diversification:.60},
    productivity: {fiscal:.32,  productive:1.0, deescalation:.10,  targetedRelief:.05, diversification:.30},
    defence:      {fiscal:-.22, productive:.70, deescalation:0,    targetedRelief:0,   diversification:.12},
    sectoral:     {fiscal:.28,  productive:.62, deescalation:.05,  targetedRelief:.48, diversification:.22},
    balance:      {fiscal:.45,  productive:.95, deescalation:.70,  targetedRelief:.08, diversification:.55}
  };

  function displayedCoverage() {
    return copyCoverage(positions);
  }

  function evaluationAnchor() {
    const current = displayedCoverage();
    // If the sliders no longer match the last automatically displayed package,
    // treat the current values as an intentional new negotiating posture.
    if (!negotiationAnchor || !lastAutoCoverage || !coverageEqual(current, lastAutoCoverage)) {
      negotiationAnchor = current;
    }
    return copyCoverage(negotiationAnchor);
  }

  function recommendationIsVerified(rec) {
    return rec?.verifiedWinWin === true
      && rec?.growthConstraintMet !== false
      && Array.isArray(rec.usSectorCoverage)
      && Array.isArray(rec.canadaSectorCoverage)
      && rec.usSectorCoverage.length === positions.us.length
      && rec.canadaSectorCoverage.length === positions.canada.length;
  }

  function economyContext() {
    const nodeNumber = (selector, fallback) => {
      const node = typeof $ === 'function' ? $(selector) : null;
      return finite(node?.value, fallback);
    };
    return {
      usTariff: finite(typeof tariff !== 'undefined' ? tariff?.value : undefined,
        finite(settings?.usTariff, 50)),
      retaliatoryTariff: nodeNumber('#retaliatoryTariff', finite(settings?.retaliatoryTariff, 5)),
      riskAversion: nodeNumber('#riskAversion', finite(settings?.riskAversion, 50)),
      cooperationCeiling: nodeNumber('#cooperationCeiling', finite(settings?.cooperationCeiling, 50)),
      inflation: finite(settings?.inflation, 2.4),
      usInflation: finite(settings?.usInflation, 2.7),
      unemployment: finite(settings?.unemployment, 6.4),
      usGrowth: finite(settings?.usGrowth, 2.0),
      borderFriction: finite(settings?.borderFriction, 2.0),
      tradeDiversification: finite(settings?.diversification, 0)
    };
  }

  function coverageLevels(current, cooperationCeiling, negotiatedRelief) {
    const start = clampValue(finite(current, 100), 0, 100);
    const cap = clampValue(finite(cooperationCeiling, 0) / 100, 0, 1);
    const rateRelief = clampValue(finite(negotiatedRelief, 0) / 100, 0, cap);
    const minimumCoverageRatio = (1 - rateRelief) > 1e-12
      ? clampValue((1 - cap) / (1 - rateRelief), 0, 1)
      : 1;
    const maxCoverageRelief = 1 - minimumCoverageRatio;
    const levels = [0, .25, .50, .75, 1]
      .map(fraction => start * (1 - maxCoverageRelief * fraction))
      .sort((a, b) => a - b);
    return levels.filter((value, index) => index === 0 || Math.abs(value - levels[index - 1]) >= 1e-9);
  }

  function sectorUtility(context, terms, sector, usCoverage, canadaCoverage) {
    const profile = sectorProfiles[sector];
    if (!profile || !terms) return null;
    const deescalation = clampValue(finite(terms.negotiatedRelief, 0) / 100, 0, 1);
    const diversification = clampValue(finite(terms.diversification, 0)
      + finite(context.tradeDiversification, 0), 0, .75);
    const uc = clampValue(finite(usCoverage, 0) / 100, 0, 1);
    const cc = clampValue(finite(canadaCoverage, 0) / 100, 0, 1);
    const usTariff = finite(context.usTariff, 0) * (1 - deescalation) / 100 * uc;
    const caTariff = finite(context.retaliatoryTariff, 0) * (1 - deescalation) / 100 * cc;
    const supply = finite(terms.productive, 0) * finite(terms.fiscal, 0)
      * (.16 + .12 * profile.cyclical);
    const caShock = usTariff * profile.trade * (.72 - .28 * diversification)
      + finite(context.borderFriction, 0) / 100 * profile.trade * .18;
    const usShock = caTariff * profile.import * .46 + usTariff * profile.import * .12;
    const usProtection = usTariff * profile.trade * .24 * (1 - .5 * uc);

    const canadaOutput = 100 * (-caShock + supply
      + finite(terms.targetedRelief, 0) * .10 * profile.jobs);
    const usOutput = 100 * (-usShock + usProtection + deescalation * .012 * profile.trade);
    const canadaJobs = canadaOutput * (.30 + .42 * profile.jobs);
    const usJobs = usOutput * (.28 + .38 * profile.jobs);
    const canadaPrices = 100 * (caTariff * profile.import * .30
      + usTariff * profile.import * .05 - supply * .10);
    const usPrices = 100 * (usTariff * profile.import * .24 + caTariff * profile.import * .10);

    const priceWeight = .65 + .70 * clampValue(finite(context.riskAversion, 50) / 100, 0, 1)
      + .18 * Math.max(0, (finite(context.inflation, 0) + finite(context.usInflation, 0)) / 2 - 2);
    const caJobsWeight = .45 + .08 * Math.max(0, finite(context.unemployment, 5) - 5);
    const usJobsWeight = .45 + .08 * Math.max(0, 4.5 - finite(context.usGrowth, 2));
    const leverage = 100 * caTariff * profile.trade * .16 * (1 - .65 * cc);

    const canadaRaw = profile.trade * (canadaOutput + caJobsWeight * canadaJobs
      - priceWeight * canadaPrices + leverage);
    const usRaw = profile.import * (usOutput + usJobsWeight * usJobs - priceWeight * usPrices);
    return {
      canada: {raw:canadaRaw, output:canadaOutput, jobs:canadaJobs, prices:canadaPrices},
      us: {raw:usRaw, output:usOutput, jobs:usJobs, prices:usPrices}
    };
  }

  function finalizeFixedTerms(base, context) {
    if (!base) return null;
    const cap = clampValue(finite(context.cooperationCeiling, 0) / 100, 0, 1);
    const deescalation = Math.min(finite(base.deescalation, 0), cap);
    return {
      fiscal: finite(base.fiscal, 0),
      productive: finite(base.productive, 0),
      negotiatedRelief: 100 * deescalation,
      targetedRelief: finite(base.targetedRelief, 0),
      diversification: finite(base.diversification, 0)
    };
  }

  function customCandidateTerms(scenario, context) {
    const cap = clampValue(finite(context.cooperationCeiling, 0) / 100, 0, 1);
    const fiscal = finite(scenario?.fiscal, 0);
    const candidates = [];
    for (const productive of [.35, .65, .90]) {
      for (const cooperation of [0, .33, .67, 1]) {
        const deescalation = cooperation * cap;
        for (const diversificationBoost of [0, .15]) {
          candidates.push({
            fiscal,
            productive,
            negotiatedRelief: 100 * deescalation,
            targetedRelief: clampValue(.42 * (1 - productive) + .08 * (1 - deescalation), 0, .45),
            diversification: clampValue(.08 + .48 * productive * (1 - deescalation)
              + diversificationBoost, 0, .70)
          });
        }
      }
    }
    return candidates;
  }

  function inferCustomTerms(scenario, context) {
    const candidates = customCandidateTerms(scenario, context);
    const observed = scenario?.sectors || [];
    const rec = result?.recommendation || {};
    const usCoverage = scenario?.appliedUsSectorCoverage || rec.usSectorCoverage || positions.us;
    const canadaCoverage = scenario?.appliedCanadaSectorCoverage || rec.canadaSectorCoverage || positions.canada;
    let best = null;
    let bestError = Infinity;

    for (const terms of candidates) {
      let error = 0;
      let comparisons = 0;
      for (let i = 0; i < Math.min(sectorProfiles.length, observed.length); ++i) {
        const actual = observed[i];
        if (!actual?.canada || !actual?.us) continue;
        const predicted = sectorUtility(context, terms, i, usCoverage[i], canadaCoverage[i]);
        for (const side of ['canada', 'us']) {
          for (const field of ['output', 'jobs', 'prices']) {
            const target = Number(actual?.[side]?.[field]);
            if (!Number.isFinite(target)) continue;
            const delta = predicted[side][field] - target;
            error += delta * delta;
            ++comparisons;
          }
        }
      }
      if (comparisons && error < bestError) {
        bestError = error;
        best = terms;
      }
    }

    if (best) return best;
    return candidates.find(terms => Math.abs(terms.productive - .65) < 1e-9
      && Math.abs(terms.negotiatedRelief - 67 * clampValue(context.cooperationCeiling / 100, 0, 1)) < 1)
      || candidates[0] || null;
  }

  function policyTermsForScenario(scenario, context = economyContext()) {
    if (!scenario) return null;
    if (scenario.id !== 'custom') return finalizeFixedTerms(fixedStrategyTerms[scenario.id], context);
    if (sectorPolicyCache.result === result && sectorPolicyCache.scenarioId === scenario.id
        && sectorPolicyCache.terms) return sectorPolicyCache.terms;
    const terms = inferCustomTerms(scenario, context);
    sectorPolicyCache = {result, scenarioId:scenario.id, terms};
    return terms;
  }

  function normalizedScore(value, lo, hi) {
    if (!(hi - lo > 1e-9)) return 0;
    return clampValue(100 * (value - lo) / (hi - lo), 0, 100);
  }

  function sectorMetrics(sector, usCoverage, canadaCoverage) {
    const rec = result?.recommendation;
    const scenario = result?.scenarios?.find(item => item.id === rec?.strategyId)
      || result?.scenarios?.[0];
    if (!rec || !scenario || !sectorProfiles[sector]) return null;

    const context = economyContext();
    const terms = policyTermsForScenario(scenario, context);
    if (!terms) return null;
    const anchor = negotiationAnchor || displayedCoverage();
    const usLevels = coverageLevels(anchor.us[sector], context.cooperationCeiling, terms.negotiatedRelief);
    const canadaLevels = coverageLevels(anchor.canada[sector], context.cooperationCeiling, terms.negotiatedRelief);
    let canadaMin = Infinity, canadaMax = -Infinity, usMin = Infinity, usMax = -Infinity;
    for (const uc of usLevels) for (const cc of canadaLevels) {
      const point = sectorUtility(context, terms, sector, uc, cc);
      canadaMin = Math.min(canadaMin, point.canada.raw);
      canadaMax = Math.max(canadaMax, point.canada.raw);
      usMin = Math.min(usMin, point.us.raw);
      usMax = Math.max(usMax, point.us.raw);
    }

    const live = sectorUtility(context, terms, sector, usCoverage, canadaCoverage);
    const recommendedCoverage = {
      us: finite(rec.usSectorCoverage?.[sector], usCoverage),
      canada: finite(rec.canadaSectorCoverage?.[sector], canadaCoverage)
    };
    const within = (value, levels) => value + 1e-9 >= Math.min(...levels)
      && value - 1e-9 <= Math.max(...levels);
    return {
      strategyId: scenario.id,
      canada: {...live.canada, score:normalizedScore(live.canada.raw, canadaMin, canadaMax)},
      us: {...live.us, score:normalizedScore(live.us.raw, usMin, usMax)},
      recommendedCoverage,
      verified: recommendationIsVerified(rec)
        && Math.abs(finite(usCoverage) - recommendedCoverage.us) < .01
        && Math.abs(finite(canadaCoverage) - recommendedCoverage.canada) < .01,
      insideSearchEnvelope: within(finite(usCoverage), usLevels)
        && within(finite(canadaCoverage), canadaLevels),
      searchEnvelope: {
        us:[Math.min(...usLevels), Math.max(...usLevels)],
        canada:[Math.min(...canadaLevels), Math.max(...canadaLevels)]
      }
    };
  }

  function sectorSearchSummary(rec) {
    const policies = Number(rec?.policyCandidatesVerified || 0);
    const candidates = Number(rec?.sectorCandidatesExamined || 0);
    const pareto = Number(rec?.sectorParetoFrontierSize || 0);
    const finalists = Number(rec?.sectorFinalistsResimulated || 0);
    const policyText = policies ? `${policies.toLocaleString()} policy candidates verified · ` : '';
    return `${policyText}${candidates.toLocaleString()} sector schedules explored · ${pareto.toLocaleString()} Pareto schedules · ${finalists.toLocaleString()} finalists re-simulated`;
  }

  function setAutoApplyStatus(text) {
    const target = typeof document !== 'undefined' ? document.querySelector('.auto span') : null;
    if (target) target.textContent = text;
  }

  function announceVerifiedWinWin(rec) {
    if (typeof window?.dispatchEvent !== 'function' || typeof CustomEvent !== 'function') return;
    window.dispatchEvent(new CustomEvent('economic-policy:verified-win-win-applied', {
      detail: {
        strategyId: rec.strategyId || result?.scenarios?.[0]?.id || '',
        coverage: displayedCoverage(),
        searchAnchor: copyCoverage(negotiationAnchor || positions),
        policyCandidatesVerified: Number(rec.policyCandidatesVerified || 0),
        globalSearchComplete: rec.globalSearchComplete === true,
        sectorCandidatesExamined: Number(rec.sectorCandidatesExamined || 0),
        sectorParetoFrontierSize: Number(rec.sectorParetoFrontierSize || 0),
        sectorFinalistsResimulated: Number(rec.sectorFinalistsResimulated || 0),
        verifiedCanadaScore: Number(rec.verifiedCanadaScore || 0),
        verifiedUsScore: Number(rec.verifiedUsScore || 0)
      }
    }));
  }

  function comparisonKey(preferences) {
    // Sector coverage and headline tariffs are intentionally omitted. In the
    // no-tariff world those controls have no direct tariff effect. Recompute the
    // reference when the structural baseline or mandate/risk controls change.
    return JSON.stringify({
      settings,
      canadaPriority: preferences.canadaPriority,
      usPriority: preferences.usPriority,
      riskAversion: preferences.riskAversion,
      cooperationCeiling: preferences.cooperationCeiling
    });
  }

  async function responseJson(response, label) {
    if (!response.ok) throw new Error(`${label} failed with HTTP ${response.status}`);
    const payload = await response.json();
    if (!payload?.scenarios?.length) throw new Error(`${label} returned no scenarios`);
    return payload;
  }

  async function recoverBaselineIfNeeded() {
    if (settings && Object.keys(settings).length) return;
    const response = await fetch('/api/baseline', {cache: 'no-store'});
    if (!response.ok) throw new Error(`Baseline recovery failed with HTTP ${response.status}`);
    const baseline = await response.json();
    settings = baseline.settings || {};
    const live = (baseline.provenance?.observedLive || []).some(item => item.live);
    const status = $('#dataStatus');
    const sync = $('#sync');
    const asOf = $('#asOf');
    if (status) status.textContent = live ? 'Live official feeds' : 'Documented calibrated baseline';
    if (sync) sync.textContent = live ? 'Official feeds synchronized' : 'Calibration snapshot active';
    if (asOf && baseline.asOf) asOf.textContent = 'As of ' + new Date(baseline.asOf).toLocaleString();
  }

  async function applyInitialCalibrationState() {
    if (initialCalibrationApplied) return;
    const response = await fetch('/api/calibration', {cache: 'no-store'});
    if (!response.ok) throw new Error(`Calibration state failed with HTTP ${response.status}`);
    const calibration = await response.json();
    const state = calibration?.effectiveState;
    if (!state) throw new Error('Calibration state is missing effectiveState');

    if (Number.isFinite(+state.usTariff)) tariff.value = +state.usTariff;
    const retaliation = $('#retaliatoryTariff');
    if (retaliation && Number.isFinite(+state.retaliatoryTariff)) {
      retaliation.value = +state.retaliatoryTariff;
      const readout = $('#retaliatoryTariffValue');
      if (readout) readout.textContent = retaliation.value + '%';
    }
    if (Array.isArray(state.usSectorCoverage) && state.usSectorCoverage.length === positions.us.length)
      positions.us.splice(0, positions.us.length, ...state.usSectorCoverage.map(Number));
    if (Array.isArray(state.canadaSectorCoverage) && state.canadaSectorCoverage.length === positions.canada.length)
      positions.canada.splice(0, positions.canada.length, ...state.canadaSectorCoverage.map(Number));

    if (typeof updateTariff === 'function') updateTariff();
    if (typeof updatePosition === 'function') updatePosition();
    if (typeof syncPartyView === 'function') syncPartyView();
    const partyView = $('#partyView');
    if (partyView && !partyView.hidden && typeof renderPartySectors === 'function') renderPartySectors();
    negotiationAnchor = displayedCoverage();
    lastAutoCoverage = null;
    initialCalibrationApplied = true;
  }

  function publishVerifiedRecommendation() {
    const rec = result?.recommendation;
    if (!rec) return;

    if (!recommendationIsVerified(rec)) {
      // Never move either delegation's sliders onto an unverified package. The
      // current posture remains visible and remains the search anchor.
      lastAutoCoverage = null;
      setAutoApplyStatus(`Auto-apply verified win-win agreement paused · ${sectorSearchSummary(rec)} · no package cleared both-party and growth verification.`);
      return;
    }

    const changed = applyRecommendation(false, !openingCalibrated);
    openingCalibrated = true;
    lastAutoCoverage = displayedCoverage();
    const completeness = rec.globalSearchComplete === true
      ? 'complete declared startup grid'
      : 'retained verified candidate set';
    setAutoApplyStatus(`Auto-apply verified win-win agreement ON · ${completeness} · ${sectorSearchSummary(rec)} · both delegations' sector sliders now show the verified package.`);

    // publishNegotiation('automatic') serializes both Canada and U.S. sector
    // arrays, so the joint dashboard and either delegation view converge on the
    // same verified agreement without making that agreement a new concession
    // anchor for another optimization pass.
    if (changed) publishNegotiation('automatic');
    announceVerifiedWinWin(rec);
  }

  function provisionalComparison(evaluated) {
    return {
      scenarios: (evaluated?.scenarios || []).map(scenario => ({
        id: scenario.id,
        growth: scenario.growth
      }))
    };
  }

  function updateGrowthDelta() {
    const target = $('#impactGrowth');
    const best = result?.scenarios?.[0];
    const zero = noTariff?.scenarios?.find(s => s.id === best?.id)
      || noTariff?.scenarios?.[0];
    if (!target || !best || !zero || !Number.isFinite(+zero.growth)) return;
    target.textContent = signed(best.growth - zero.growth, ' pp');
  }

  function setComparisonPending() {
    const target = $('#impactGrowth');
    if (target) target.textContent = 'Calculating…';
  }

  function makeRequest(preferences, rate, retaliation, comparisonOnly = false) {
    return fetch('/api/evaluate', {
      method: 'POST',
      headers: {'Content-Type':'application/json'},
      body: JSON.stringify({
        ...settings,
        ...preferences,
        usTariff: rate,
        retaliatoryTariff: retaliation,
        comparisonOnly
      })
    }).then(response => responseJson(response,
      comparisonOnly ? 'No-tariff comparison' : 'Policy evaluation'));
  }

  function scheduleComparison(preferences, sequence) {
    const key = comparisonKey(preferences);
    if (comparisonCache && comparisonCacheKey === key) {
      noTariff = comparisonCache;
      updateGrowthDelta();
      return;
    }

    setComparisonPending();
    if (comparisonTimer) clearTimeout(comparisonTimer);
    comparisonTimer = setTimeout(() => {
      comparisonTimer = null;
      if (sequence !== evaluationSequence) return;
      comparisonTask = makeRequest(preferences, 0, 0, true)
        .then(value => {
          if (sequence !== evaluationSequence) return;
          comparisonCache = value;
          comparisonCacheKey = key;
          noTariff = value;
          updateGrowthDelta();
        })
        .catch(error => {
          console.warn('No-tariff comparison failed', error);
          if (sequence === evaluationSequence) {
            const target = $('#impactGrowth');
            if (target) target.textContent = 'Unavailable';
          }
        });
    }, Math.max(0, comparisonDelayMs));
  }

  function startElapsedStatus(loading) {
    const detail = loading?.querySelector?.('small');
    const label = loading?.querySelector?.('span');
    const started = Date.now();
    const searchLabel = 'Searching 13 expert + 288 generated policy mixes, sector Pareto schedules and robust packages';
    if (label) label.textContent = 'RUNNING VERIFIED GLOBAL SEARCH';
    if (detail) detail.textContent = searchLabel;
    const timer = setInterval(() => {
      if (!detail) return;
      const seconds = Math.floor((Date.now() - started) / 1000);
      detail.textContent = `${searchLabel} · ${seconds}s elapsed`;
    }, 1000);
    return () => clearInterval(timer);
  }

  evaluate = async function controlledEvaluate() {
    if (adjustingRanges.size) {
      schedule();
      return;
    }

    const sequence = ++evaluationSequence;
    const btn = $('#run');
    const loading = $('#strategyLoading');
    const signal = $('#signal');
    if (comparisonTimer) {
      clearTimeout(comparisonTimer);
      comparisonTimer = null;
    }
    if (loading) loading.hidden = false;
    const stopElapsedStatus = startElapsedStatus(loading);
    if (btn) {
      btn.disabled = true;
      if (btn.firstChild) btn.firstChild.textContent = 'Searching verified policy and sector packages… ';
    }

    try {
      await recoverBaselineIfNeeded();
      // app.js's legacy negotiation room starts at illustrative tariffs. Before
      // the first real solve, replace that display state with the exact certified
      // tariff/sector baseline the user is actually asking the engine to optimize.
      await applyInitialCalibrationState();
      const anchor = evaluationAnchor();
      const preferences = {
        canadaPriority: +$('#canadaPriority').value,
        usPriority: +$('#usPriority').value,
        riskAversion: +$('#riskAversion').value,
        cooperationCeiling: +$('#cooperationCeiling').value,
        retaliatoryTariff: +$('#retaliatoryTariff').value
      };
      anchor.us.forEach((value, i) => preferences['usSector' + i] = value);
      anchor.canada.forEach((value, i) => preferences['canadaSector' + i] = value);

      // Critical startup rule: await only the real policy evaluation. The
      // no-tariff reference is useful for one headline delta but must never gate
      // the initial best-win-win result.
      const evaluated = await makeRequest(
        preferences, +tariff.value, preferences.retaliatoryTariff, false);

      if (sequence !== evaluationSequence) return;
      if (adjustingRanges.size) {
        schedule();
        return;
      }

      result = evaluated;
      sectorPolicyCache = {result:null, scenarioId:'', terms:null};
      const key = comparisonKey(preferences);
      noTariff = comparisonCache && comparisonCacheKey === key
        ? comparisonCache
        : provisionalComparison(evaluated);
      publishVerifiedRecommendation();
      selected = result.scenarios[0];
      render();
      if (!(comparisonCache && comparisonCacheKey === key)) setComparisonPending();

      // Defer the expensive no-tariff optimizer until after the real result has
      // rendered and the loading overlay is released. It updates only the growth
      // counterfactual when it finishes.
      scheduleComparison(preferences, sequence);
    } catch (error) {
      console.error('Policy evaluation failed', error);
      if (sequence === evaluationSequence) {
        if (signal) signal.textContent = 'Evaluation failed — check the server console and retry';
        const sync = $('#negotiationSync');
        if (sync) sync.textContent = 'Evaluation error · controls remain available';
      }
    } finally {
      stopElapsedStatus();
      if (sequence === evaluationSequence) {
        if (loading) loading.hidden = true;
        if (btn) {
          btn.disabled = false;
          if (btn.firstChild) btn.firstChild.textContent = 'Run again now ';
        }
      }
    }
  };

  // app.js bound the run button to the original function object before this
  // controller was appended. Rebind it to the controlled evaluator.
  const runButton = $('#run');
  if (runButton) runButton.onclick = () => evaluate();

  window.SectorResponseModel = {
    coverageLevels,
    sectorUtility,
    policyTermsForScenario,
    sectorMetrics
  };

  window.EvaluationController = {
    invalidateComparison() {
      comparisonCache = null;
      comparisonCacheKey = '';
      if (comparisonTimer) {
        clearTimeout(comparisonTimer);
        comparisonTimer = null;
      }
    },
    resetNegotiationAnchor() {
      negotiationAnchor = displayedCoverage();
      lastAutoCoverage = null;
    },
    sectorMetrics,
    state() {
      return {
        searchAnchor: copyCoverage(negotiationAnchor || positions),
        displayedCoverage: displayedCoverage(),
        autoAppliedCoverage: lastAutoCoverage ? copyCoverage(lastAutoCoverage) : null,
        verifiedWinWin: recommendationIsVerified(result?.recommendation),
        globalSearchComplete: result?.recommendation?.globalSearchComplete === true,
        initialCalibrationApplied
      };
    },
    waitForComparison() {
      return comparisonTask;
    }
  };
})();
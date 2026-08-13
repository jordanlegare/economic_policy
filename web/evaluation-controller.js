(() => {
  'use strict';

  let negotiationAnchor = null;
  let lastAutoCoverage = null;
  let lastEvaluatedControls = null;
  let comparisonCache = null;
  let comparisonCacheKey = '';
  let comparisonTimer = null;
  let comparisonTask = Promise.resolve();
  let comparisonRunning = false;
  let comparisonQueued = null;
  let comparisonGeneration = 0;
  let initialCalibrationApplied = false;

  const comparisonDelayMs = Number(window.__EVALUATION_COMPARISON_DELAY_MS ?? 900);

  const copyCoverage = source => ({
    canada: [...source.canada],
    us: [...source.us]
  });

  const coverageEqual = (a, b) => !!a && !!b
    && sameCoverage(a.canada, b.canada)
    && sameCoverage(a.us, b.us);

  const finite = (value, fallback = 0) => Number.isFinite(Number(value))
    ? Number(value)
    : fallback;

  function displayedCoverage() {
    return copyCoverage(positions);
  }

  function controlSnapshot() {
    const value = (selector, fallback) => finite(
      typeof $ === 'function' ? $(selector)?.value : undefined,
      fallback);
    return {
      usTariff: finite(
        typeof tariff !== 'undefined' ? tariff?.value : undefined,
        finite(settings?.usTariff, 50)),
      retaliatoryTariff: value(
        '#retaliatoryTariff', finite(settings?.retaliatoryTariff, 5)),
      canadaPriority: value('#canadaPriority', 50),
      usPriority: value('#usPriority', 50),
      riskAversion: value('#riskAversion', 50),
      cooperationCeiling: value('#cooperationCeiling', 50)
    };
  }

  function controlsEqual(a, b) {
    if (!a || !b) return false;
    return Object.keys(a).every(key =>
      Math.abs(finite(a[key]) - finite(b[key])) < 1e-9);
  }

  function evaluationAnchor() {
    const current = displayedCoverage();
    const controls = controlSnapshot();
    const controlsChanged = !!lastEvaluatedControls
      && !controlsEqual(controls, lastEvaluatedControls);
    if (!negotiationAnchor || !lastAutoCoverage
        || !coverageEqual(current, lastAutoCoverage) || controlsChanged) {
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

  function sectorMetrics(sector, usCoverage, canadaCoverage) {
    const rec = result?.recommendation;
    const scenario = result?.scenarios?.find(item => item.id === rec?.strategyId)
      || result?.scenarios?.[0];
    const row = scenario?.sectors?.[sector];
    if (!rec || !scenario || !row) return null;

    const recommendedCoverage = {
      us: finite(rec.usSectorCoverage?.[sector], usCoverage),
      canada: finite(rec.canadaSectorCoverage?.[sector], canadaCoverage)
    };
    const atVerifiedCoverage =
      Math.abs(finite(usCoverage) - recommendedCoverage.us) < .01
      && Math.abs(finite(canadaCoverage) - recommendedCoverage.canada) < .01;

    return {
      strategyId: scenario.id,
      canada: {...row.canada, score:finite(rec.canadaSectorValue?.[sector], 0)},
      us: {...row.us, score:finite(rec.usSectorOutput?.[sector], 0)},
      recommendedCoverage,
      verified: recommendationIsVerified(rec) && atVerifiedCoverage,
      pending: !atVerifiedCoverage,
      serverAuthoritative: true,
      insideSearchEnvelope: atVerifiedCoverage,
      searchEnvelope: null
    };
  }

  function sectorSearchSummary(rec) {
    const policies = Number(rec?.policyCandidatesVerified || 0);
    const candidates = Number(rec?.sectorCandidatesExamined || 0);
    const pareto = Number(rec?.sectorParetoFrontierSize || 0);
    const finalists = Number(rec?.sectorFinalistsResimulated || 0);
    const policyText = policies
      ? `${policies.toLocaleString()} policy candidates verified · `
      : '';
    return `${policyText}${candidates.toLocaleString()} sector schedules explored · `
      + `${pareto.toLocaleString()} Pareto schedules · `
      + `${finalists.toLocaleString()} finalists re-simulated`;
  }

  function setAutoApplyStatus(text) {
    const target = typeof document !== 'undefined'
      ? document.querySelector('.auto span')
      : null;
    if (target) target.textContent = text;
  }

  function announceVerifiedWinWin(rec) {
    if (typeof window?.dispatchEvent !== 'function'
        || typeof CustomEvent !== 'function') return;
    window.dispatchEvent(new CustomEvent(
      'economic-policy:verified-win-win-applied', {
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
          verifiedUsScore: Number(rec.verifiedUsScore || 0),
          userAnchorSelectionActive: rec.userAnchorSelectionActive === true,
          userAnchorWelfareTolerance: Number(rec.userAnchorWelfareTolerance || 0),
          selectedTradePostureDistance: Number(rec.selectedTradePostureDistance || 0)
        }
      }));
  }

  function comparisonKey(preferences) {
    return JSON.stringify({
      settings,
      canadaPriority: preferences.canadaPriority,
      usPriority: preferences.usPriority,
      riskAversion: preferences.riskAversion,
      cooperationCeiling: preferences.cooperationCeiling
    });
  }

  async function responseJson(response, label) {
    if (!response.ok)
      throw new Error(`${label} failed with HTTP ${response.status}`);
    const payload = await response.json();
    if (!payload?.scenarios?.length)
      throw new Error(`${label} returned no scenarios`);
    return payload;
  }

  async function recoverBaselineIfNeeded() {
    if (settings && Object.keys(settings).length) return;
    const response = await fetch('/api/baseline', {cache:'no-store'});
    if (!response.ok)
      throw new Error(`Baseline recovery failed with HTTP ${response.status}`);
    const baseline = await response.json();
    settings = baseline.settings || {};
    const live = Number(baseline.provenance?.liveFieldCount || 0);
    const status = $('#dataStatus');
    const sync = $('#sync');
    const asOf = $('#asOf');
    if (status)
      status.textContent = live
        ? `Partial live official feeds · ${live}/3 market fields`
        : 'Documented calibrated/default baseline';
    if (sync)
      sync.textContent = live
        ? 'Partial official-feed refresh'
        : 'Calibration snapshot active';
    if (asOf && baseline.asOf)
      asOf.textContent = 'As of ' + new Date(baseline.asOf).toLocaleString();
  }

  async function applyInitialCalibrationState() {
    if (initialCalibrationApplied) return;
    const response = await fetch('/api/calibration', {cache:'no-store'});
    if (!response.ok)
      throw new Error(`Calibration state failed with HTTP ${response.status}`);
    const state = (await response.json())?.effectiveState;
    if (!state) throw new Error('Calibration state is missing effectiveState');

    if (Number.isFinite(+state.usTariff)) tariff.value = +state.usTariff;
    const retaliation = $('#retaliatoryTariff');
    if (retaliation && Number.isFinite(+state.retaliatoryTariff)) {
      retaliation.value = +state.retaliatoryTariff;
      const readout = $('#retaliatoryTariffValue');
      if (readout) readout.textContent = retaliation.value + '%';
    }
    if (Array.isArray(state.usSectorCoverage)
        && state.usSectorCoverage.length === positions.us.length) {
      positions.us.splice(
        0, positions.us.length, ...state.usSectorCoverage.map(Number));
    }
    if (Array.isArray(state.canadaSectorCoverage)
        && state.canadaSectorCoverage.length === positions.canada.length) {
      positions.canada.splice(
        0, positions.canada.length, ...state.canadaSectorCoverage.map(Number));
    }

    if (typeof updateTariff === 'function') updateTariff();
    if (typeof updatePosition === 'function') updatePosition();
    if (typeof syncPartyView === 'function') syncPartyView();
    const partyView = $('#partyView');
    if (partyView && !partyView.hidden
        && typeof renderPartySectors === 'function') renderPartySectors();
    negotiationAnchor = displayedCoverage();
    lastAutoCoverage = null;
    lastEvaluatedControls = null;
    initialCalibrationApplied = true;
  }

  function publishVerifiedRecommendation() {
    const rec = result?.recommendation;
    if (!rec) return;

    if (!recommendationIsVerified(rec)) {
      lastAutoCoverage = null;
      setAutoApplyStatus(
        `Auto-apply verified win-win agreement paused · ${sectorSearchSummary(rec)} · `
        + 'no package cleared both-party and growth verification.');
      return;
    }

    const changed = applyRecommendation(false, !openingCalibrated);
    openingCalibrated = true;
    lastAutoCoverage = displayedCoverage();
    const completeness = rec.globalSearchComplete === true
      ? 'complete declared startup grid'
      : 'retained verified candidate set';
    const proximity = rec.userAnchorSelectionActive === true
      ? ` · user-anchor Δtrade ${finite(rec.selectedTradePostureDistance, 0).toFixed(1)}`
        + ` · within ${finite(rec.userAnchorWelfareTolerance, .5).toFixed(1)}`
        + ' score point of max welfare'
      : '';
    setAutoApplyStatus(
      `Auto-apply verified win-win agreement ON · ${completeness}${proximity} · `
      + `${sectorSearchSummary(rec)} · both delegations' sector sliders now show `
      + 'the verified package.');
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
    const zero = noTariff?.scenarios?.find(scenario => scenario.id === best?.id)
      || noTariff?.scenarios?.[0];
    if (target && best && zero && Number.isFinite(+zero.growth))
      target.textContent = signed(best.growth - zero.growth, ' pp');
  }

  function setComparisonPending() {
    const target = $('#impactGrowth');
    if (target) target.textContent = 'Calculating…';
  }

  function makeRequest(preferences, rate, retaliation, comparisonOnly = false) {
    return fetch('/api/evaluate', {
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({
        ...settings,
        ...preferences,
        usTariff:rate,
        retaliatoryTariff:retaliation,
        comparisonOnly
      })
    }).then(response => responseJson(
      response, comparisonOnly ? 'No-tariff comparison' : 'Policy evaluation'));
  }

  function applyCachedComparison(key) {
    if (!(comparisonCache && comparisonCacheKey === key)) return false;
    noTariff = comparisonCache;
    updateGrowthDelta();
    return true;
  }

  function runQueuedComparison() {
    if (comparisonRunning || !comparisonQueued) return;
    const queued = comparisonQueued;
    comparisonQueued = null;

    if (queued.sequence !== evaluationSequence
        || queued.generation !== comparisonGeneration) return;
    if (applyCachedComparison(queued.key)) return;

    comparisonRunning = true;
    comparisonTask = makeRequest(queued.preferences, 0, 0, true)
      .then(value => {
        // A same-generation comparator remains useful even if a newer primary
        // evaluation changed the UI sequence while this request was running.
        if (queued.generation !== comparisonGeneration) return;
        comparisonCache = value;
        comparisonCacheKey = queued.key;
        if (queued.sequence === evaluationSequence) {
          noTariff = value;
          updateGrowthDelta();
        }
      })
      .catch(error => {
        console.warn('No-tariff comparison failed', error);
        if (queued.sequence === evaluationSequence
            && queued.generation === comparisonGeneration) {
          const target = $('#impactGrowth');
          if (target) target.textContent = 'Unavailable';
        }
      })
      .finally(() => {
        comparisonRunning = false;
        // Only the newest queued comparison survives while one is running.
        if (comparisonQueued) runQueuedComparison();
      });
  }

  function scheduleComparison(preferences, sequence) {
    const key = comparisonKey(preferences);
    if (applyCachedComparison(key)) return;

    setComparisonPending();
    comparisonQueued = {
      preferences:{...preferences},
      sequence,
      key,
      generation:comparisonGeneration
    };
    if (comparisonTimer) clearTimeout(comparisonTimer);
    comparisonTimer = setTimeout(() => {
      comparisonTimer = null;
      runQueuedComparison();
    }, Math.max(0, comparisonDelayMs));
  }

  function startElapsedStatus(loading) {
    const detail = loading?.querySelector?.('small');
    const label = loading?.querySelector?.('span');
    const started = Date.now();
    const searchLabel = 'Searching 13 expert + 288 generated policy mixes, '
      + 'sector Pareto schedules and robust packages';
    if (label) label.textContent = 'RUNNING VERIFIED GLOBAL SEARCH';
    if (detail) detail.textContent = searchLabel;
    const timer = setInterval(() => {
      if (detail) {
        detail.textContent = `${searchLabel} · `
          + `${Math.floor((Date.now() - started) / 1000)}s elapsed`;
      }
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
    comparisonQueued = null;
    if (loading) loading.hidden = false;
    const stopElapsedStatus = startElapsedStatus(loading);
    if (btn) {
      btn.disabled = true;
      if (btn.firstChild)
        btn.firstChild.textContent = 'Searching verified policy and sector packages… ';
    }

    try {
      await recoverBaselineIfNeeded();
      await applyInitialCalibrationState();
      const anchor = evaluationAnchor();
      const preferences = {
        canadaPriority:+$('#canadaPriority').value,
        usPriority:+$('#usPriority').value,
        riskAversion:+$('#riskAversion').value,
        cooperationCeiling:+$('#cooperationCeiling').value,
        retaliatoryTariff:+$('#retaliatoryTariff').value
      };
      anchor.us.forEach((value, index) =>
        preferences['usSector' + index] = value);
      anchor.canada.forEach((value, index) =>
        preferences['canadaSector' + index] = value);

      const evaluated = await makeRequest(
        preferences, +tariff.value, preferences.retaliatoryTariff, false);
      if (sequence !== evaluationSequence) return;
      if (adjustingRanges.size) {
        schedule();
        return;
      }

      result = evaluated;
      lastEvaluatedControls = controlSnapshot();
      const key = comparisonKey(preferences);
      noTariff = comparisonCache && comparisonCacheKey === key
        ? comparisonCache
        : provisionalComparison(evaluated);
      publishVerifiedRecommendation();
      selected = result.scenarios[0];
      render();
      if (!(comparisonCache && comparisonCacheKey === key))
        setComparisonPending();
      scheduleComparison(preferences, sequence);
    } catch (error) {
      console.error('Policy evaluation failed', error);
      if (sequence === evaluationSequence) {
        if (signal)
          signal.textContent = 'Evaluation failed — check the server console and retry';
        const sync = $('#negotiationSync');
        if (sync)
          sync.textContent = 'Evaluation error · controls remain available';
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

  const runButton = $('#run');
  if (runButton) runButton.onclick = () => evaluate();

  window.SectorResponseModel = {sectorMetrics};

  window.EvaluationController = {
    invalidateComparison() {
      comparisonCache = null;
      comparisonCacheKey = '';
      comparisonGeneration++;
      comparisonQueued = null;
      if (comparisonTimer) {
        clearTimeout(comparisonTimer);
        comparisonTimer = null;
      }
    },
    resetNegotiationAnchor() {
      negotiationAnchor = displayedCoverage();
      lastAutoCoverage = null;
      lastEvaluatedControls = controlSnapshot();
    },
    sectorMetrics,
    state() {
      return {
        searchAnchor:copyCoverage(negotiationAnchor || positions),
        displayedCoverage:displayedCoverage(),
        autoAppliedCoverage:lastAutoCoverage ? copyCoverage(lastAutoCoverage) : null,
        lastEvaluatedControls:lastEvaluatedControls
          ? {...lastEvaluatedControls}
          : null,
        verifiedWinWin:recommendationIsVerified(result?.recommendation),
        globalSearchComplete:result?.recommendation?.globalSearchComplete === true,
        userAnchorSelectionActive:
          result?.recommendation?.userAnchorSelectionActive === true,
        selectedTradePostureDistance:
          finite(result?.recommendation?.selectedTradePostureDistance, 0),
        initialCalibrationApplied,
        comparisonInFlight:comparisonRunning,
        comparisonQueued:!!comparisonQueued
      };
    },
    waitForComparison() {
      const waitUntilIdle = () => Promise.resolve(comparisonTask).then(() => {
        if (!comparisonRunning && !comparisonQueued && !comparisonTimer) return;
        return new Promise(resolve => setTimeout(resolve, 0)).then(waitUntilIdle);
      });
      return waitUntilIdle();
    }
  };
})();

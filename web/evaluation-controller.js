(() => {
  'use strict';

  // The policy engine returns a sector schedule that has already been re-simulated
  // and verified. Displaying that schedule must not silently turn it into the
  // starting posture for another optimization pass. Keep a separate negotiation
  // anchor and advance it only when the user/counterparty actually changes coverage.
  let negotiationAnchor = null;
  let lastAutoCoverage = null;
  let comparisonCache = null;
  let comparisonCacheKey = '';
  let comparisonTimer = null;
  let comparisonTask = Promise.resolve();
  let initialCalibrationApplied = false;

  const comparisonDelayMs = Number(window.__EVALUATION_COMPARISON_DELAY_MS ?? 900);

  const copyCoverage = source => ({
    canada: [...source.canada],
    us: [...source.us]
  });

  const coverageEqual = (a, b) => !!a && !!b
    && sameCoverage(a.canada, b.canada)
    && sameCoverage(a.us, b.us);

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

    updateTariff();
    updatePosition();
    syncPartyView();
    if (!$('#partyView').hidden) renderPartySectors();
    negotiationAnchor = displayedCoverage();
    lastAutoCoverage = null;
    initialCalibrationApplied = true;
  }

  function publishVerifiedRecommendation() {
    if (!result?.recommendation) return;
    const changed = applyRecommendation(false, !openingCalibrated);
    openingCalibrated = true;
    lastAutoCoverage = displayedCoverage();
    // The returned scenario has already been evaluated on this exact optimized
    // coverage schedule. Publish the display state, but do not recursively run
    // the optimizer against its own concession.
    if (changed) publishNegotiation('automatic');
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
      headers: {'Content-Type': 'application/json'},
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
      // app.js's legacy negotiation room starts at illustrative 50%/5% tariffs.
      // Before the first real solve, replace that display state with the exact
      // certified tariff/sector baseline the user is actually asking to optimize.
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
    waitForComparison() {
      return comparisonTask;
    }
  };
})();
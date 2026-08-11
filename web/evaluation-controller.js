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
    // Sector coverage and headline tariffs are intentionally omitted. The
    // no-tariff comparator is reusable across tariff/coverage edits, but is
    // recomputed when structural baseline or mandate/risk controls change.
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

  evaluate = async function controlledEvaluate() {
    if (adjustingRanges.size) {
      schedule();
      return;
    }

    const sequence = ++evaluationSequence;
    const btn = $('#run');
    const loading = $('#strategyLoading');
    const signal = $('#signal');
    if (loading) loading.hidden = false;
    if (btn) {
      btn.disabled = true;
      if (btn.firstChild) btn.firstChild.textContent = 'Searching verified policy and sector packages… ';
    }

    try {
      await recoverBaselineIfNeeded();
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

      const request = (rate, retaliation, comparisonOnly = false) => fetch('/api/evaluate', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
          ...settings,
          ...preferences,
          usTariff: rate,
          retaliatoryTariff: retaliation,
          comparisonOnly
        })
      }).then(response => responseJson(response, comparisonOnly ? 'No-tariff comparison' : 'Policy evaluation'));

      const key = comparisonKey(preferences);
      const comparisonPromise = comparisonCache && comparisonCacheKey === key
        ? Promise.resolve(comparisonCache)
        : request(0, 0, true).then(value => {
            comparisonCache = value;
            comparisonCacheKey = key;
            return value;
          });

      const [evaluated, comparison] = await Promise.all([
        request(+tariff.value, preferences.retaliatoryTariff, false),
        comparisonPromise
      ]);

      if (sequence !== evaluationSequence) return;
      if (adjustingRanges.size) {
        schedule();
        return;
      }

      result = evaluated;
      noTariff = comparison;
      publishVerifiedRecommendation();
      selected = result.scenarios[0];
      render();
    } catch (error) {
      console.error('Policy evaluation failed', error);
      if (sequence === evaluationSequence) {
        if (signal) signal.textContent = 'Evaluation failed — check the server console and retry';
        const sync = $('#negotiationSync');
        if (sync) sync.textContent = 'Evaluation error · controls remain available';
      }
    } finally {
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
    },
    resetNegotiationAnchor() {
      negotiationAnchor = displayedCoverage();
      lastAutoCoverage = null;
    }
  };
})();

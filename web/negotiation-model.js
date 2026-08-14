(() => {
  'use strict';

  const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const fmt = (value, digits=1) => Number(value || 0).toFixed(digits);
  const signed = (value, suffix='') => `${Number(value || 0) >= 0 ? '+' : ''}${fmt(value)}${suffix}`;

  function materialPackageKey(package_) {
    const issues = (package_?.issues || []).map(issue =>
      `${String(issue?.label ?? '')}|${Number(issue?.canadaMove || 0).toFixed(6)}|${Number(issue?.usMove || 0).toFixed(6)}`
    ).sort().join('||');
    return `${String(package_?.strategyName || '')}||${issues}`;
  }

  function bestUniqueFrontierPackages(frontier, limit=9) {
    const seen = new Set(), unique = [];
    for (const package_ of frontier || []) {
      const key = materialPackageKey(package_);
      if (seen.has(key)) continue;
      seen.add(key);
      unique.push(package_);
      if (unique.length >= limit) break;
    }
    return unique;
  }

  window.NegotiationFrontier = {materialPackageKey, bestUniqueFrontierPackages};

  function primaryPackage(model) {
    const robust = result?.robustness;
    const metrics = (robust?.packages || []).find(p => p.packageId === robust?.recommendedPackageId);
    const promoted = robust?.candidateSetComplete === true
      && metrics?.clearsProbabilityGate === true;
    const robustPackage = promoted
      ? (model.frontier || []).find(p => p.id === robust.recommendedPackageId)
      : null;
    return {
      package_: robustPackage || model.recommendedPackage,
      promoted: !!robustPackage,
      metrics
    };
  }

  function inject() {
    if (document.querySelector('#computationalNegotiation')) return;
    const anchor = document.querySelector('#diplomatCommand') || document.querySelector('.impact-strip');
    if (!anchor) return;
    const section = document.createElement('section');
    section.id = 'computationalNegotiation';
    section.className = 'computational-negotiation';
    section.innerHTML = `<div class="negotiation-head"><div><div class="eyebrow">Computational negotiation support</div><h2>BATNA · linked concessions · ε-Pareto frontier · stability</h2><p>The macro engine generates country-specific payoffs; the bargaining layer searches linked packages, rejects deals below either side's reservation value, preserves packages within an explicit utility indifference band, and tests unilateral incentives to defect.</p></div><div class="negotiation-engine-badge">VERIFIED PACKAGE SEARCH</div></div>
      <div id="negotiationSummary" class="negotiation-summary"></div>
      <div id="negotiationTrust" class="negotiation-method"></div>
      <div class="negotiation-grid">
        <section class="negotiation-pane"><div id="negotiationPrimaryLabel" class="eyebrow">Recommended bargaining package</div><h3 id="negotiationPackageName">Evaluating…</h3><div id="negotiationPackageMetrics"></div><div id="negotiationStability" class="stability-callout"></div></section>
        <section class="negotiation-pane"><div class="eyebrow">Issue linkage</div><h3>Who moves on what?</h3><div id="negotiationIssues" class="issue-linkage"></div></section>
        <section class="negotiation-pane"><div class="eyebrow">Separate trade channels</div><h3>Canada ≠ United States exports</h3><div id="tradeChannels" class="trade-channels"></div><p class="negotiation-note">These are explicit country-specific trade channels. The U.S. outcome is not inferred from Canada's export result, and bilateral accounting balance is not rewarded in the welfare objective.</p></section>
      </div>
      <div class="pareto-head"><div><div class="eyebrow">Individually rational ε-frontier</div><h3>Materially distinct bargaining packages</h3></div><span id="paretoCount"></span></div>
      <div id="paretoPackages" class="pareto-packages"></div>
      <div class="negotiation-method"><b>Interpretation:</b> BATNA is the strongest modeled non-cooperative outside option for each delegation. The reservation value adds a small risk-sensitive acceptance margin. A package is individually rational only if both sides clear those values. The displayed ε-Pareto set uses the API's stated utility tolerance, so packages within that indifference band may coexist even when one has a numerically tiny point-estimate advantage. “Verified win-win” additionally requires the selected sector schedule to have been re-simulated through the stochastic macro engine under fixed mandate weights with independent Canadian/U.S. trade channels.</div>`;
    anchor.insertAdjacentElement('afterend', section);
  }

  function issueRow(issue) {
    const ca = Number(issue.canadaMove || 0), us = Number(issue.usMove || 0);
    return `<div class="issue-row"><b>${esc(issue.label)}</b><div><span>Canada <em style="width:${Math.min(100,ca)}%"></em><strong>${fmt(ca,0)}</strong></span><span>U.S. <em style="width:${Math.min(100,us)}%"></em><strong>${fmt(us,0)}</strong></span></div></div>`;
  }

  function packageCard(package_, index) {
    const verified = package_.verifiedWinWin ? 'VERIFIED WIN-WIN' : package_.sectorVerified ? 'SECTOR VERIFIED' : 'UNVERIFIED SECTOR SCHEDULE';
    return `<article class="pareto-card ${package_.stable?'stable':'unstable'}" data-strategy="${esc(package_.strategyId)}">
      <div class="pareto-rank">${String(index+1).padStart(2,'0')}</div><div class="eyebrow">${verified} · ${package_.stable?'STABLE':'ENFORCEMENT NEEDED'} · Nash ${fmt(package_.nashGain,1)}</div>
      <h4>${esc(package_.strategyName)}</h4><div class="pareto-utilities"><span>Canada <b>${fmt(package_.canadaUtility,1)}</b><small>+${fmt(package_.canadaSurplus,1)} over reservation</small></span><span>U.S. <b>${fmt(package_.usUtility,1)}</b><small>+${fmt(package_.usSurplus,1)} over reservation</small></span></div>
      <div class="pareto-stability">Stability ${fmt(package_.stabilityScore,0)}/100</div></article>`;
  }

  function renderTrust(model, package_, promoted, robustMetrics) {
    const trust = model.trust || {};
    const globalComplete = result?.recommendation?.globalSearchComplete === true;
    const checks = [
      ['Global policy/sector startup grid complete', globalComplete],
      ['Bargaining ε-frontier candidate set complete', trust.frontierComplete === true],
      ['Independent Canada/U.S. trade channels', trust.independentUsTradeChannel === true],
      ['Trade balance reported, not optimized', trust.tradeBalanceIsObjective === false],
      ['Delegation mandate weights fixed', trust.mandateWeightsFixed === true],
      ['20-sector schedule stochastic re-simulated', package_.sectorVerified === true],
      ['Clears both reservation values', package_.individuallyRational === true]
    ];
    if (promoted) checks.push(['Clears robust joint-reservation probability gate', robustMetrics?.clearsProbabilityGate === true]);
    const passed = checks.filter(x => x[1]).length;
    const integrity = trust.dataIntegrityPass === true && package_.verifiedWinWin === true;
    const bestClaim = promoted && globalComplete && trust.frontierComplete === true;
    document.querySelector('#negotiationTrust').innerHTML = `<b>${bestClaim?'✓ ROBUST BEST WIN-WIN ON DECLARED STARTUP GRID':integrity?'✓ VERIFIED WIN-WIN DATA PATH':'⚠ MODEL TRUST CHECK INCOMPLETE'}</b> · ${passed}/${checks.length} integrity checks pass · ${Number(trust.verificationMonteCarloDraws||0).toLocaleString()} verification draws · ${model.bargainingGridLevels||0} bargaining levels per linked issue.<br><span>${checks.map(x=>`${x[1]?'✓':'✕'} ${esc(x[0])}`).join(' · ')}</span>`;
  }

  function render() {
    inject();
    if (typeof result === 'undefined' || !result?.negotiation || !document.querySelector('#computationalNegotiation')) return;
    const model = result.negotiation;
    const primary = primaryPackage(model);
    const package_ = primary.package_;
    if (!package_) return;

    document.querySelector('#negotiationSummary').innerHTML = `<div><span>Canada BATNA</span><b>${fmt(model.batna.canada,1)}</b><small>${esc(model.batna.canadaStrategy)} · reservation ${fmt(model.reservation.canada,1)}</small></div>
      <div><span>U.S. BATNA</span><b>${fmt(model.batna.us,1)}</b><small>${esc(model.batna.usStrategy)} · reservation ${fmt(model.reservation.us,1)}</small></div>
      <div><span>Packages searched</span><b>${Number(model.candidatesExamined).toLocaleString()}</b><small>${Number(model.individuallyRationalCount).toLocaleString()} clear both reservation values</small></div>
      <div><span>ε-Pareto frontier</span><b>${model.paretoFrontierSize}</b><small>${fmt(model.paretoUtilityTolerance,1)}-point utility indifference band</small></div>`;
    renderTrust(model, package_, primary.promoted, primary.metrics);

    const label = document.querySelector('#negotiationPrimaryLabel');
    if (label) label.textContent = primary.promoted
      ? 'Robust primary bargaining package'
      : 'Point-estimate bargaining package';
    document.querySelector('#negotiationPackageName').textContent = package_.strategyName;
    const robustNote = primary.promoted && primary.metrics
      ? `<small>Robust joint-clear ${fmt(100*primary.metrics.jointClearProbability,1)}% · max regret ${fmt(primary.metrics.maxRegret,2)}</small>`
      : `<small>Robust promotion requires a complete candidate set and the joint-clear probability gate.</small>`;
    document.querySelector('#negotiationPackageMetrics').innerHTML = `<div class="package-metric-grid"><div><span>Canada utility</span><b>${fmt(package_.canadaUtility,1)}</b><small>${signed(package_.canadaSurplus)} vs reservation</small></div><div><span>U.S. utility</span><b>${fmt(package_.usUtility,1)}</b><small>${signed(package_.usSurplus)} vs reservation</small></div><div><span>Generalized Nash surplus</span><b>${fmt(package_.nashGain,1)}</b>${robustNote}</div><div><span>Stability</span><b>${fmt(package_.stabilityScore,0)}/100</b></div></div>`;

    const caGain = Number(package_.canadaDeviationGain || 0), usGain = Number(package_.usDeviationGain || 0);
    const stability = document.querySelector('#negotiationStability');
    stability.classList.toggle('warning', !package_.stable);
    stability.innerHTML = package_.stable
      ? `<b>Incentive-compatible in the current screen.</b><span>Canada deviation gain ${signed(caGain)} · U.S. deviation gain ${signed(usGain)}.</span>`
      : `<b>Agreement needs enforcement architecture.</b><span>Canada deviation gain ${signed(caGain)} · U.S. deviation gain ${signed(usGain)}. Positive values flag incentives to withdraw commitments after receiving the counterparty's concessions.</span>`;

    document.querySelector('#negotiationIssues').innerHTML = (package_.issues || []).map(issueRow).join('');
    document.querySelector('#tradeChannels').innerHTML = `<div><span>Canadian exports</span><b class="${package_.canadaExportChange<0?'negative':'positive'}">${signed(package_.canadaExportChange,'%')}</b><small>Verified base responds to the selected U.S. sector-coverage schedule; bargaining relief and linked commitments are layered on top.</small></div><div><span>U.S. exports</span><b class="${package_.usExportChange<0?'negative':'positive'}">${signed(package_.usExportChange,'%')}</b><small>Verified independently from Canadian exports and responds to the selected Canadian retaliation schedule.</small></div>`;

    const visibleFrontier = bestUniqueFrontierPackages(model.frontier, 9);
    document.querySelector('#paretoCount').textContent = `${model.paretoFrontierSize} ε-frontier packages · showing ${visibleFrontier.length} best unique${model.frontierComplete===false?' · candidate cap bound':''}`;
    document.querySelector('#paretoPackages').innerHTML = visibleFrontier.map(packageCard).join('');
    document.querySelectorAll('.pareto-card').forEach(card => card.addEventListener('click', () => {
      if (typeof selected === 'undefined' || typeof render !== 'function') return;
      const scenario = result.scenarios.find(s => s.id === card.dataset.strategy);
      if (scenario) { selected = scenario; render(); }
    }));
  }

  function appendBriefing() {
    setTimeout(() => {
      if (typeof result === 'undefined' || !result?.negotiation) return;
      const sheet = document.querySelector('#briefingSheet');
      if (!sheet || sheet.querySelector('#bargainingBriefSection')) return;
      const model = result.negotiation, primary = primaryPackage(model), package_ = primary.package_, trust = model.trust || {};
      if (!package_) return;
      const linked = (package_.issues || []).filter(issue => Number(issue.canadaMove) > 0 || Number(issue.usMove) > 0)
        .map(issue => `<li><b>${esc(issue.label)}</b>: Canada ${fmt(issue.canadaMove,0)} · U.S. ${fmt(issue.usMove,0)}</li>`).join('');
      const fullBestClaim = primary.promoted
        && result?.recommendation?.globalSearchComplete === true
        && model.frontierComplete === true;
      const trustLine = fullBestClaim
        ? `Robust primary package on the complete declared startup search grid: independent country trade channels; fixed mandate weights; trade balance excluded from welfare; sector schedules re-simulated with ${Number(trust.verificationMonteCarloDraws||0).toLocaleString()} common-random-number draws; robust package clears the joint reservation probability gate.`
        : trust.dataIntegrityPass && package_.verifiedWinWin
          ? `Verified point-estimate win-win data path. A global robust-best claim is withheld unless globalSearchComplete, frontierComplete and the robust joint-clear probability gate all pass.`
          : 'Trust warning: at least one model-integrity condition did not pass. Do not describe this package as a verified win-win.';
      const recommendationLabel = primary.promoted ? 'Robust primary ε-Pareto package' : 'Point-estimate ε-Pareto package';
      const section = `<section id="bargainingBriefSection"><h2>Bargaining analysis</h2>
        <p><b>Model trust:</b> ${esc(trustLine)}</p>
        <p><b>Outside options:</b> Canada BATNA ${fmt(model.batna.canada,1)} (${esc(model.batna.canadaStrategy)}), reservation ${fmt(model.reservation.canada,1)}. U.S. BATNA ${fmt(model.batna.us,1)} (${esc(model.batna.usStrategy)}), reservation ${fmt(model.reservation.us,1)}.</p>
        <p><b>${recommendationLabel}:</b> ${esc(package_.strategyName)} · Canada utility ${fmt(package_.canadaUtility,1)} (${signed(package_.canadaSurplus)} over reservation) · U.S. utility ${fmt(package_.usUtility,1)} (${signed(package_.usSurplus)} over reservation) · generalized Nash gain ${fmt(package_.nashGain,1)}.</p>
        <p><b>ε-Pareto definition:</b> ${fmt(model.paretoUtilityTolerance,1)}-point utility indifference band; ${model.frontierComplete===false?'the candidate cap bound, so the retained bargaining set is incomplete.':'the retained bargaining candidate set is complete.'}</p>
        <p><b>Agreement stability:</b> ${package_.stable?'passes the current unilateral-deviation screen':'requires stronger enforcement architecture'}; Canada deviation gain ${signed(package_.canadaDeviationGain)}, U.S. deviation gain ${signed(package_.usDeviationGain)}, stability ${fmt(package_.stabilityScore,0)}/100.</p>
        <p><b>Separate trade channels:</b> Canadian exports ${signed(package_.canadaExportChange,'%')} · U.S. exports ${signed(package_.usExportChange,'%')}. Bilateral trade-balance gap ${fmt(package_.tradeBalanceGapUsd,1)} USD bn is shown for context only and is not a welfare target.</p>
        <h3>Linked concessions</h3><ul>${linked || '<li>No non-zero linked concessions in the selected package.</li>'}</ul>
        <p><small>${Number(model.candidatesExamined).toLocaleString()} linked packages searched; ${Number(model.individuallyRationalCount).toLocaleString()} clear both reservation values; ${model.paretoFrontierSize} are in the ε-frontier.</small></p></section>`;
      const warning = sheet.querySelector('.briefing-warning');
      if (warning) warning.insertAdjacentHTML('beforebegin', section); else sheet.insertAdjacentHTML('beforeend', section);
    }, 0);
  }

  function start() {
    inject();
    const cards = document.querySelector('#cards');
    if (cards) new MutationObserver(render).observe(cards, {childList:true});
    document.querySelector('#openBriefing')?.addEventListener('click', appendBriefing);
    document.querySelector('#printBriefing')?.addEventListener('click', appendBriefing);
    render();
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start); else start();
})();

// Executive trade-deal overview: keep the opening panel focused on the deal,
// not on model plumbing. Legacy IDs remain hidden so older render paths continue
// to update safely while this view presents the current evaluated scenario.
(() => {
  'use strict';

  let evaluated = null;
  let initialized = false;
  const number = (value, fallback=0) => Number.isFinite(Number(value)) ? Number(value) : fallback;
  const fmtDeal = (value, digits=1) => number(value).toFixed(digits);
  const signedDeal = (value, suffix='%') => `${number(value) >= 0 ? '+' : ''}${fmtDeal(value)}${suffix}`;
  const averageDeal = values => Array.isArray(values) && values.length
    ? values.reduce((sum, value) => sum + number(value), 0) / values.length
    : 0;

  function dealForm(snapshot) {
    if (!snapshot) return 'Evaluating deal';
    const usCoverage = averageDeal(snapshot.usCoverage);
    const caCoverage = averageDeal(snapshot.canadaCoverage);
    const usRelief = 100 - usCoverage;
    const caRelief = 100 - caCoverage;
    const usEffective = number(snapshot.usTariff) * usCoverage / 100;
    const caEffective = number(snapshot.canadaTariff) * caCoverage / 100;
    if (usEffective < 1 && caEffective < 1) return 'Near-free trade';
    if (Math.abs(usEffective - caEffective) >= 10)
      return usEffective > caEffective ? 'U.S.-weighted protection' : 'Canada-weighted protection';
    if (usRelief >= 10 && caRelief >= 10) return 'Mutual sector relief';
    if (usCoverage >= 90 && caCoverage >= 90) return 'Broad tariff coverage';
    return 'Targeted sector bargain';
  }

  function reliefLeaders(coverage, limit=3) {
    const names = typeof sectorNames !== 'undefined' ? sectorNames : [];
    return (coverage || []).map((value, index) => ({
      name:names[index] || `Sector ${index + 1}`,
      relief:Math.max(0, 100 - number(value))
    })).filter(item => item.relief >= 1)
      .sort((a,b) => b.relief - a.relief)
      .slice(0, limit);
  }

  function evaluatedSnapshot() {
    let best = null, rec = null;
    try {
      best = result?.scenarios?.[0] || null;
      rec = result?.recommendation || null;
    } catch (_) {}
    if (!best) return null;
    const state = window.EvaluationController?.state?.() || {};
    const controls = state.lastEvaluatedControls || {};
    const usCoverage = typeof positions !== 'undefined' && Array.isArray(positions?.us) ? [...positions.us] : [];
    const canadaCoverage = typeof positions !== 'undefined' && Array.isArray(positions?.canada) ? [...positions.canada] : [];
    return {
      best,
      rec,
      usTariff:number(controls.usTariff, typeof tariff !== 'undefined' ? tariff?.value : 0),
      canadaTariff:number(controls.retaliatoryTariff, document.querySelector('#retaliatoryTariff')?.value),
      usCoverage,
      canadaCoverage,
      capturedAt:Date.now()
    };
  }

  function installStyles() {
    if (document.querySelector('#dealOverviewStyles')) return;
    const style = document.createElement('style');
    style.id = 'dealOverviewStyles';
    style.textContent = `
      .decision-overview-compat{display:none!important}
      .deal-overview{display:grid;gap:14px}
      .deal-hero{display:grid;grid-template-columns:minmax(0,1fr) 240px;gap:22px;align-items:start;padding:24px;background:linear-gradient(128deg,#173d35 0%,#214c42 65%,#2b5d50 100%);color:#fff;box-shadow:0 12px 30px #173c3422}
      .deal-hero .eyebrow{color:#b7d0c8}.deal-hero h1{margin:7px 0 8px;font-size:34px;line-height:1.05;letter-spacing:-.035em}.deal-hero p{margin:0;max-width:760px;color:#c8d9d3;font-size:11px;line-height:1.55}
      .deal-verdict{border-left:1px solid #ffffff33;padding-left:18px;text-align:right}.deal-verdict span{display:block;font-size:8px;letter-spacing:.13em;text-transform:uppercase;color:#b7d0c8;font-weight:700}.deal-verdict b{display:block;margin-top:7px;font-size:20px}.deal-verdict small{display:block;margin-top:6px;color:#c8d9d3;font-size:8px;line-height:1.45}
      .deal-pending{display:flex;justify-content:space-between;gap:14px;align-items:center;padding:9px 12px;background:#fff4de;border-left:3px solid #c98a20;color:#72501d;font-size:9px}.deal-pending[hidden]{display:none!important}.deal-pending button{border:1px solid #c98a20;background:#fff8e8;color:#72501d;padding:6px 9px}
      .deal-section{background:#fff;border:1px solid var(--line)}.deal-section-head{display:flex;justify-content:space-between;gap:14px;align-items:end;padding:14px 16px 0}.deal-section-head h3{margin:4px 0 0;font-size:14px}.deal-section-head p{margin:0;max-width:620px;color:#72807a;font-size:9px;line-height:1.45}
      .deal-terms{display:grid;grid-template-columns:repeat(4,1fr);padding:14px 16px 16px}.deal-term{padding:13px 14px;background:#f6f3ed;border-right:1px solid #e2ded5}.deal-term:last-child{border-right:0}.deal-term span,.deal-metric span,.deal-quality span{display:block;font-size:7px;text-transform:uppercase;letter-spacing:.08em;color:#7a8782;font-weight:700}.deal-term b{display:block;margin-top:5px;font-size:22px}.deal-term small{display:block;margin-top:4px;color:#7a8782;font-size:8px;line-height:1.4}
      .deal-outcomes{display:grid;grid-template-columns:repeat(6,1fr);padding:14px 16px 16px}.deal-metric{padding:12px;border-right:1px solid var(--line)}.deal-metric:last-child{border-right:0}.deal-metric b{display:block;margin-top:5px;font-size:18px}.deal-metric small{display:block;margin-top:4px;color:#87918d;font-size:8px}
      .deal-quality-grid{display:grid;grid-template-columns:1fr 1fr 1.25fr;gap:0;padding:14px 16px 16px}.deal-quality{padding:13px 15px;border-right:1px solid var(--line)}.deal-quality:last-child{border-right:0}.deal-quality b{display:block;margin-top:5px;font-size:16px}.deal-quality p{margin:5px 0 0;color:#697670;font-size:9px;line-height:1.45}
      .deal-actions{display:flex;gap:7px;flex-wrap:wrap;padding:0 16px 16px}.deal-actions button{border:1px solid var(--line);background:#fff;padding:8px 10px;color:var(--ink)}.deal-actions button:hover{border-color:var(--green);color:var(--green)}
      @media(max-width:980px){.deal-hero{grid-template-columns:1fr}.deal-verdict{text-align:left;border-left:0;border-top:1px solid #ffffff33;padding:12px 0 0}.deal-terms{grid-template-columns:1fr 1fr}.deal-term:nth-child(2){border-right:0}.deal-outcomes{grid-template-columns:repeat(3,1fr)}.deal-metric:nth-child(3){border-right:0}.deal-quality-grid{grid-template-columns:1fr}.deal-quality{border-right:0;border-bottom:1px solid var(--line)}}
      @media(max-width:620px){.deal-hero h1{font-size:27px}.deal-terms,.deal-outcomes{grid-template-columns:1fr 1fr}.deal-term,.deal-metric{border-right:0;border-bottom:1px solid var(--line)}}`;
    document.head.appendChild(style);
  }

  function installOverview() {
    if (initialized) return;
    const panel = document.querySelector('details[data-dashboard-panel="decision-overview"]');
    const body = panel?.querySelector('.dashboard-panel-body');
    if (!panel || !body) return;
    initialized = true;
    installStyles();
    const title = panel.querySelector('.dashboard-panel-title');
    if (title) title.innerHTML = '<small>Deal on the table</small><b>Bilateral deal overview</b>';
    const desk = document.querySelector('#diplomatCommand');
    if (desk && panel.contains(desk)) panel.insertAdjacentElement('afterend', desk);
    body.innerHTML = `
      <div id="dealOverview" class="deal-overview">
        <section class="deal-hero"><div><div class="eyebrow">Current evaluated bilateral deal</div><h1 id="dealHeadline">Evaluating the deal on the table…</h1><p id="dealSummary">Trade terms and whole-economy effects will appear after the current run completes.</p></div><div class="deal-verdict"><span>Deal screen</span><b id="dealVerdict">—</b><small id="dealVerdictNote">Model-based bilateral assessment</small></div></section>
        <div id="dealPending" class="deal-pending" hidden><span>Delegation inputs changed after the last run. Figures below remain tied to the last evaluated deal.</span><button id="dealRunNow" type="button">Run updated deal →</button></div>
        <section class="deal-section"><div class="deal-section-head"><div><div class="eyebrow">Terms</div><h3>What the deal actually does at the border</h3></div><p>Coverage-adjusted tariffs multiply the submitted headline rate by simple average sector coverage. They summarize the form of the deal; the fiscal ledger uses trade-weighted post-elasticity receipts.</p></div><div class="deal-terms">
          <div class="deal-term"><span>U.S. coverage-adjusted tariff</span><b id="dealUsEffective">—</b><small id="dealUsEffectiveNote">—</small></div>
          <div class="deal-term"><span>Canada coverage-adjusted tariff</span><b id="dealCaEffective">—</b><small id="dealCaEffectiveNote">—</small></div>
          <div class="deal-term"><span>U.S. sector relief</span><b id="dealUsRelief">—</b><small>average exemption from full headline coverage</small></div>
          <div class="deal-term"><span>Canada sector relief</span><b id="dealCaRelief">—</b><small>average exemption from full headline coverage</small></div>
        </div></section>
        <section class="deal-section"><div class="deal-section-head"><div><div class="eyebrow">Economic result</div><h3>What each side gets from this deal</h3></div><p>Leading evaluated policy package under the submitted delegation trade settings.</p></div><div class="deal-outcomes">
          <div class="deal-metric"><span>Canada GDP</span><b id="dealCanadaGdp">—</b><small>modeled growth</small></div>
          <div class="deal-metric"><span>U.S. GDP</span><b id="dealUsGdp">—</b><small>modeled growth</small></div>
          <div class="deal-metric"><span>Canada exports</span><b id="dealCanadaExports">—</b><small>terminal change</small></div>
          <div class="deal-metric"><span>U.S. exports</span><b id="dealUsExports">—</b><small>terminal change</small></div>
          <div class="deal-metric"><span>Canada inflation</span><b id="dealInflation">—</b><small>terminal rate</small></div>
          <div class="deal-metric"><span>Recession risk</span><b id="dealRecession">—</b><small>any quarter</small></div>
        </div></section>
        <section class="deal-section"><div class="deal-section-head"><div><div class="eyebrow">Deal quality</div><h3>Is the bargain balanced and durable?</h3></div><p>These are model screens, not political acceptance probabilities or legal conclusions.</p></div><div class="deal-quality-grid">
          <div class="deal-quality"><span>Bilateral value</span><b id="dealPartyScores">—</b><p id="dealPartyScoreNote">—</p></div>
          <div class="deal-quality"><span>Growth protection</span><b id="dealGrowthFloor">—</b><p id="dealGrowthFloorNote">—</p></div>
          <div class="deal-quality"><span>Where relief is concentrated</span><b id="dealReliefShape">—</b><p id="dealReliefSectors">—</p></div>
        </div><div class="deal-actions"><button id="dealOpenDiplomatic" type="button">Diplomatic decision desk</button><button id="dealOpenFiscal" type="button">Fiscal & trade ledger</button><button id="dealOpenStrategies" type="button">Compare policy packages</button></div></section>
      </div>
      <div class="decision-overview-compat" aria-hidden="true"><div class="confidence"><b id="confidence">—</b></div><div class="brief"><div><strong id="signal"></strong><p id="rationale"></p></div><div id="regime"></div><div id="neutral"></div><div id="gap"></div></div><div class="impact-strip"><div id="impactGrowth"></div><div id="impactCost"></div><div id="impactExports"></div><div id="impactRisk"></div></div><div class="live-impact"><b id="negotiationSync"></b><span id="liveDealImpact"></span></div></div>`;
    document.querySelector('#dealRunNow')?.addEventListener('click', () => document.querySelector('#run')?.click());
    document.querySelector('#dealOpenFiscal')?.addEventListener('click', () => openPanel('fiscal-ledger'));
    document.querySelector('#dealOpenStrategies')?.addEventListener('click', () => openPanel('strategies'));
    document.querySelector('#dealOpenDiplomatic')?.addEventListener('click', () => {
      const node = document.querySelector('#diplomatCommand');
      if (node) node.scrollIntoView({behavior:'smooth', block:'start'});
    });
  }

  function openPanel(id) {
    const panel = document.querySelector(`details[data-dashboard-panel="${id}"]`);
    if (!panel) return;
    panel.open = true;
    panel.scrollIntoView({behavior:'smooth', block:'start'});
  }

  function moveDiplomaticDeskOutsideOverview() {
    const panel = document.querySelector('details[data-dashboard-panel="decision-overview"]');
    const desk = document.querySelector('#diplomatCommand');
    if (panel && desk && panel.contains(desk)) panel.insertAdjacentElement('afterend', desk);
  }

  function listRelief(items) {
    return items.length
      ? items.map(item => `${item.name} ${fmtDeal(item.relief,0)}pt`).join(' · ')
      : 'No material sector exemptions';
  }

  function renderOverview() {
    installOverview();
    moveDiplomaticDeskOutsideOverview();
    if (!evaluated) evaluated = evaluatedSnapshot();
    const snapshot = evaluated;
    if (!snapshot) return;
    const best = snapshot.best, rec = snapshot.rec || {};
    const usCoverage = averageDeal(snapshot.usCoverage), caCoverage = averageDeal(snapshot.canadaCoverage);
    const usRelief = 100 - usCoverage, caRelief = 100 - caCoverage;
    const usEffective = snapshot.usTariff * usCoverage / 100;
    const caEffective = snapshot.canadaTariff * caCoverage / 100;
    const form = dealForm(snapshot);
    const verified = rec.verifiedWinWin === true;
    const growthProtected = best.sustainedBilateralGrowth === true && rec.growthConstraintMet !== false;
    const verdict = verified && growthProtected ? 'Bilateral gain holds' : verified ? 'Win-win screen clears' : growthProtected ? 'Growth floor holds' : 'Material trade-off';
    const verdictNote = verified
      ? `Model win-win · Canada ${fmtDeal(best.canadaScore,0)}/100 · U.S. ${fmtDeal(best.usScore,0)}/100`
      : 'At least one bilateral value or verification condition is unresolved.';
    document.querySelector('#dealHeadline').textContent = `${form}: ${fmtDeal(usEffective)}% U.S. vs ${fmtDeal(caEffective)}% Canadian coverage-adjusted tariff`;
    document.querySelector('#dealSummary').textContent = `Average sector coverage is U.S. ${fmtDeal(usCoverage,0)}% and Canada ${fmtDeal(caCoverage,0)}%. The leading package produces Canada GDP ${fmtDeal(best.growth)}% and U.S. GDP ${fmtDeal(best.usGrowth)}%, with ${fmtDeal(best.recessionRisk,0)}% modeled recession risk.`;
    document.querySelector('#dealVerdict').textContent = verdict;
    document.querySelector('#dealVerdictNote').textContent = verdictNote;
    document.querySelector('#dealUsEffective').textContent = `${fmtDeal(usEffective)}%`;
    document.querySelector('#dealUsEffectiveNote').textContent = `${fmtDeal(snapshot.usTariff,0)}% headline × ${fmtDeal(usCoverage,0)}% average coverage`;
    document.querySelector('#dealCaEffective').textContent = `${fmtDeal(caEffective)}%`;
    document.querySelector('#dealCaEffectiveNote').textContent = `${fmtDeal(snapshot.canadaTariff,0)}% headline × ${fmtDeal(caCoverage,0)}% average coverage`;
    document.querySelector('#dealUsRelief').textContent = `${fmtDeal(usRelief,0)}pt`;
    document.querySelector('#dealCaRelief').textContent = `${fmtDeal(caRelief,0)}pt`;
    document.querySelector('#dealCanadaGdp').textContent = `${fmtDeal(best.growth)}%`;
    document.querySelector('#dealUsGdp').textContent = `${fmtDeal(best.usGrowth)}%`;
    document.querySelector('#dealCanadaExports').textContent = signedDeal(best.exports);
    document.querySelector('#dealUsExports').textContent = signedDeal(best.usExportChange);
    document.querySelector('#dealInflation').textContent = `${fmtDeal(best.inflation)}%`;
    document.querySelector('#dealRecession').textContent = `${fmtDeal(best.recessionRisk,0)}%`;
    const scoreGap = Math.abs(number(best.canadaScore) - number(best.usScore));
    document.querySelector('#dealPartyScores').textContent = `CA ${fmtDeal(best.canadaScore,0)} · US ${fmtDeal(best.usScore,0)}`;
    document.querySelector('#dealPartyScoreNote').textContent = `${fmtDeal(scoreGap,0)}-point modeled value gap · weakest side ${fmtDeal(Math.min(number(best.canadaScore), number(best.usScore)),0)}/100`;
    document.querySelector('#dealGrowthFloor').textContent = `${fmtDeal(best.bilateralGrowthFloor)}%`;
    document.querySelector('#dealGrowthFloorNote').textContent = growthProtected
      ? `Both GDP paths clear the searched ${fmtDeal(rec.gdpGrowthFloor)}% floor.`
      : `The searched ${fmtDeal(rec.gdpGrowthFloor)}% bilateral growth floor is not fully protected.`;
    const usLeaders = reliefLeaders(snapshot.usCoverage), caLeaders = reliefLeaders(snapshot.canadaCoverage);
    document.querySelector('#dealReliefShape').textContent = form;
    document.querySelector('#dealReliefSectors').textContent = `U.S. relief: ${listRelief(usLeaders)}. Canada relief: ${listRelief(caLeaders)}.`;
    updatePending();
  }

  function captureEvaluation() {
    const next = evaluatedSnapshot();
    if (next) evaluated = next;
    renderOverview();
  }

  function updatePending() {
    const pending = document.querySelector('#dealPending');
    if (!pending) return;
    const staged = window.EvaluationRunController?.state?.().staged === true;
    pending.hidden = !staged;
  }

  function bindOverview() {
    installOverview();
    moveDiplomaticDeskOutsideOverview();
    const cards = document.querySelector('#cards');
    if (cards && typeof MutationObserver === 'function') {
      new MutationObserver(() => setTimeout(captureEvaluation, 0)).observe(cards, {childList:true});
    }
    document.addEventListener('input', () => setTimeout(updatePending, 0), true);
    document.addEventListener('change', () => setTimeout(updatePending, 0), true);
    document.addEventListener('click', event => {
      if (event.target?.closest?.('#run')) {
        const pending = document.querySelector('#dealPending');
        if (pending) pending.hidden = true;
      }
    }, true);
    if (typeof MutationObserver === 'function') {
      new MutationObserver(moveDiplomaticDeskOutsideOverview).observe(document.body, {childList:true, subtree:true});
    }
    setTimeout(captureEvaluation, 0);
  }

  window.DealOverview = {dealForm, reliefLeaders};
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', bindOverview);
  else bindOverview();
})();
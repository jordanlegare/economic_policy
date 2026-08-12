(() => {
  'use strict';

  const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const fmt = (value, digits=1) => Number(value || 0).toFixed(digits);
  const signed = (value, suffix='') => `${Number(value || 0) >= 0 ? '+' : ''}${fmt(value)}${suffix}`;

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

    const visibleFrontier = (model.frontier || []).slice(0, 12);
    document.querySelector('#paretoCount').textContent = `${model.paretoFrontierSize} ε-frontier packages · showing ${visibleFrontier.length}${model.frontierComplete===false?' · candidate cap bound':''}`;
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
(() => {
  'use strict';

  const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const fmt = (value, digits=1) => Number(value || 0).toFixed(digits);
  const signed = (value, suffix='') => `${Number(value || 0) >= 0 ? '+' : ''}${fmt(value)}${suffix}`;

  function inject() {
    if (document.querySelector('#computationalNegotiation')) return;
    const anchor = document.querySelector('#diplomatCommand') || document.querySelector('.impact-strip');
    if (!anchor) return;
    const section = document.createElement('section');
    section.id = 'computationalNegotiation';
    section.className = 'computational-negotiation';
    section.innerHTML = `<div class="negotiation-head"><div><div class="eyebrow">Computational negotiation support</div><h2>BATNA · linked concessions · Pareto frontier · stability</h2><p>The macro engine generates payoffs; this bargaining layer searches linked packages, rejects deals below either side's reservation value, removes dominated packages, and tests unilateral incentives to defect.</p></div><div class="negotiation-engine-badge">ENDOGENOUS PACKAGE SEARCH</div></div>
      <div id="negotiationSummary" class="negotiation-summary"></div>
      <div class="negotiation-grid">
        <section class="negotiation-pane"><div class="eyebrow">Recommended bargaining package</div><h3 id="negotiationPackageName">Evaluating…</h3><div id="negotiationPackageMetrics"></div><div id="negotiationStability" class="stability-callout"></div></section>
        <section class="negotiation-pane"><div class="eyebrow">Issue linkage</div><h3>Who moves on what?</h3><div id="negotiationIssues" class="issue-linkage"></div></section>
        <section class="negotiation-pane"><div class="eyebrow">Separate trade channels</div><h3>Canada ≠ United States exports</h3><div id="tradeChannels" class="trade-channels"></div><p class="negotiation-note">These are explicit country-specific bargaining-layer trade channels. The U.S. outcome is not inferred from Canada's export result.</p></section>
      </div>
      <div class="pareto-head"><div><div class="eyebrow">Individually rational frontier</div><h3>Non-dominated packages</h3></div><span id="paretoCount"></span></div>
      <div id="paretoPackages" class="pareto-packages"></div>
      <div class="negotiation-method"><b>Interpretation:</b> BATNA is the strongest modeled non-cooperative outside option for each delegation. The reservation value adds a small risk-sensitive acceptance margin. A package is individually rational only if both sides clear those values. Pareto-efficient packages cannot improve one side without worsening the other within the searched set. Stability is a one-shot deviation screen; positive deviation gains indicate a need for sequencing, safeguards, verification, or enforcement.</div>`;
    anchor.insertAdjacentElement('afterend', section);
  }

  function issueRow(issue) {
    const ca = Number(issue.canadaMove || 0), us = Number(issue.usMove || 0);
    return `<div class="issue-row"><b>${esc(issue.label)}</b><div><span>Canada <em style="width:${Math.min(100,ca)}%"></em><strong>${fmt(ca,0)}</strong></span><span>U.S. <em style="width:${Math.min(100,us)}%"></em><strong>${fmt(us,0)}</strong></span></div></div>`;
  }

  function packageCard(package_, index) {
    return `<article class="pareto-card ${package_.stable?'stable':'unstable'}" data-strategy="${esc(package_.strategyId)}">
      <div class="pareto-rank">${String(index+1).padStart(2,'0')}</div><div class="eyebrow">${package_.stable?'STABLE':'ENFORCEMENT NEEDED'} · Nash ${fmt(package_.nashGain,1)}</div>
      <h4>${esc(package_.strategyName)}</h4><div class="pareto-utilities"><span>Canada <b>${fmt(package_.canadaUtility,1)}</b><small>+${fmt(package_.canadaSurplus,1)} over reservation</small></span><span>U.S. <b>${fmt(package_.usUtility,1)}</b><small>+${fmt(package_.usSurplus,1)} over reservation</small></span></div>
      <div class="pareto-stability">Stability ${fmt(package_.stabilityScore,0)}/100</div></article>`;
  }

  function render() {
    inject();
    if (typeof result === 'undefined' || !result?.negotiation || !document.querySelector('#computationalNegotiation')) return;
    const model = result.negotiation;
    const package_ = model.recommendedPackage;
    if (!package_) return;

    document.querySelector('#negotiationSummary').innerHTML = `<div><span>Canada BATNA</span><b>${fmt(model.batna.canada,1)}</b><small>${esc(model.batna.canadaStrategy)} · reservation ${fmt(model.reservation.canada,1)}</small></div>
      <div><span>U.S. BATNA</span><b>${fmt(model.batna.us,1)}</b><small>${esc(model.batna.usStrategy)} · reservation ${fmt(model.reservation.us,1)}</small></div>
      <div><span>Packages searched</span><b>${Number(model.candidatesExamined).toLocaleString()}</b><small>${Number(model.individuallyRationalCount).toLocaleString()} clear both reservation values</small></div>
      <div><span>Pareto frontier</span><b>${model.paretoFrontierSize}</b><small>non-dominated linked packages</small></div>`;

    document.querySelector('#negotiationPackageName').textContent = package_.strategyName;
    document.querySelector('#negotiationPackageMetrics').innerHTML = `<div class="package-metric-grid"><div><span>Canada utility</span><b>${fmt(package_.canadaUtility,1)}</b><small>${signed(package_.canadaSurplus)} vs reservation</small></div><div><span>U.S. utility</span><b>${fmt(package_.usUtility,1)}</b><small>${signed(package_.usSurplus)} vs reservation</small></div><div><span>Nash surplus</span><b>${fmt(package_.nashGain,1)}</b></div><div><span>Stability</span><b>${fmt(package_.stabilityScore,0)}/100</b></div></div>`;

    const caGain = Number(package_.canadaDeviationGain || 0), usGain = Number(package_.usDeviationGain || 0);
    const stability = document.querySelector('#negotiationStability');
    stability.classList.toggle('warning', !package_.stable);
    stability.innerHTML = package_.stable
      ? `<b>Incentive-compatible in the current screen.</b><span>Canada deviation gain ${signed(caGain)} · U.S. deviation gain ${signed(usGain)}.</span>`
      : `<b>Agreement needs enforcement architecture.</b><span>Canada deviation gain ${signed(caGain)} · U.S. deviation gain ${signed(usGain)}. Positive values flag incentives to withdraw commitments after receiving the counterparty's concessions.</span>`;

    document.querySelector('#negotiationIssues').innerHTML = (package_.issues || []).map(issueRow).join('');
    document.querySelector('#tradeChannels').innerHTML = `<div><span>Canadian exports</span><b class="${package_.canadaExportChange<0?'negative':'positive'}">${signed(package_.canadaExportChange,'%')}</b><small>Responds to U.S. tariffs, U.S.-market access, border facilitation and linked commitments.</small></div><div><span>U.S. exports</span><b class="${package_.usExportChange<0?'negative':'positive'}">${signed(package_.usExportChange,'%')}</b><small>Responds independently to Canadian retaliation and access to the Canadian market.</small></div>`;

    document.querySelector('#paretoCount').textContent = `${model.paretoFrontierSize} frontier packages · showing ${Math.min(12,(model.frontier||[]).length)}`;
    document.querySelector('#paretoPackages').innerHTML = (model.frontier || []).map(packageCard).join('');
    document.querySelectorAll('.pareto-card').forEach(card => card.addEventListener('click', () => {
      if (typeof selected === 'undefined' || typeof render !== 'function') return;
      const scenario = result.scenarios.find(s => s.id === card.dataset.strategy);
      if (scenario) { selected = scenario; render(); }
    }));
  }

  function start() {
    inject();
    const cards = document.querySelector('#cards');
    if (cards) new MutationObserver(render).observe(cards, {childList:true});
    render();
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start); else start();
})();

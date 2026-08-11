(() => {
  'use strict';

  const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const pct = value => `${Number(value || 0).toFixed(0)}%`;

  function inject() {
    if (document.querySelector('#calibrationTrust')) return;
    const anchor = document.querySelector('#computationalNegotiation') || document.querySelector('#diplomatCommand') || document.querySelector('.impact-strip');
    if (!anchor) return;
    const section = document.createElement('section');
    section.id = 'calibrationTrust';
    section.className = 'calibration-trust';
    section.innerHTML = `<div class="calibration-head"><div><div class="eyebrow">Data provenance & calibration</div><h2>What is observed, estimated, or still assumed?</h2><p>A win-win optimization is only as credible as the data and behavioural estimates underneath it. This panel reports the calibration layers used in the actual evaluation.</p></div><div id="calibrationGrade" class="calibration-grade">LOADING</div></div><div id="calibrationSummary" class="calibration-summary"></div><div class="calibration-grid"><section><div class="eyebrow">Integrity gates</div><div id="calibrationChecks" class="calibration-checks"></div></section><section><div class="eyebrow">Source vintages</div><div id="calibrationSources" class="calibration-sources"></div></section></div><div id="calibrationMeasures" class="calibration-measures"></div><div id="calibrationWarning" class="calibration-warning"></div>`;
    anchor.insertAdjacentElement('afterend', section);
  }

  function render() {
    inject();
    if (typeof result === 'undefined' || !result?.calibration) return;
    const c = result.calibration;
    const certified = !!c.certifiedForEmpiricalUse;
    const grade = document.querySelector('#calibrationGrade');
    grade.textContent = certified ? 'EMPIRICALLY CALIBRATED' : String(c.grade || 'INCOMPLETE').replaceAll('-', ' ').toUpperCase();
    grade.classList.toggle('certified', certified);

    document.querySelector('#calibrationSummary').innerHTML = `<div><span>Snapshot</span><b>${esc(c.snapshotId)}</b><small>as of ${esc(c.asOf || 'unknown')}</small></div><div><span>Calibration completeness</span><b>${pct(c.completeness)}</b><small>${certified ? 'release-grade empirical layers present' : 'do not treat missing layers as observed facts'}</small></div><div><span>Generated</span><b>${esc(c.generatedAt || 'unknown')}</b><small>snapshot provenance is versioned</small></div>`;

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
    warning.classList.toggle('certified', certified);
    warning.innerHTML = certified
      ? `<b>Empirical calibration gates pass.</b> The result still contains model uncertainty and requires legal/economic judgment; calibration is not a prediction of political acceptance.`
      : `<b>Calibration is not complete.</b> ${esc(c.warning || 'Missing layers remain model assumptions.')} The platform may be used for research and scenario comparison, but its numerical outputs should not be represented as fully calibrated government-grade estimates.`;
  }

  function appendBriefing() {
    setTimeout(() => {
      if (typeof result === 'undefined' || !result?.calibration) return;
      const sheet = document.querySelector('#briefingSheet');
      if (!sheet || sheet.querySelector('#calibrationBriefSection')) return;
      const c = result.calibration, checks = c.checks || {};
      const missing = Object.entries(checks).filter(([,ok]) => !ok).map(([key]) => key).join(', ');
      const sources = (c.sources || []).slice(0,8).map(s => `<li><b>${esc(s.agency)}</b> — ${esc(s.dataset)} · ${esc(s.vintage || 'vintage not captured')} · ${esc(s.status)}</li>`).join('');
      const section = `<section id="calibrationBriefSection"><h2>Data provenance and calibration</h2><p><b>Snapshot:</b> ${esc(c.snapshotId)} · as of ${esc(c.asOf || 'unknown')} · calibration completeness ${pct(c.completeness)} · grade <b>${esc(c.grade)}</b>.</p><p><b>Empirical-use certification:</b> ${c.certifiedForEmpiricalUse?'PASS':'NOT YET CERTIFIED'}.</p>${missing?`<p><b>Missing calibration gates:</b> ${esc(missing)}.</p>`:''}<h3>Principal source vintages</h3><ul>${sources}</ul><p><small>Observed, official-derived, empirically estimated and assumption inputs are intentionally distinguished. A verified optimizer result is not equivalent to an empirically calibrated forecast.</small></p></section>`;
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

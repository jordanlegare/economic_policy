(() => {
  'use strict';

  const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const fmt = (value, digits = 2) => Number(value ?? 0).toFixed(digits);

  function currentScenario() {
    try { return selected || result?.scenarios?.[0] || null; } catch (_) { return null; }
  }

  function currentCountry() {
    try { return sectorCountry === 'us' ? 'us' : 'canada'; } catch (_) { return 'canada'; }
  }

  function inject() {
    if (document.querySelector('#tradeIncidenceDiagnostics')) return;
    const sectorPanel = document.querySelector('#sectorRows')?.closest('.sector-panel');
    if (!sectorPanel) return;
    const style = document.createElement('style');
    style.textContent = `.trade-incidence{margin-top:22px;padding-top:18px;border-top:1px solid var(--line)}.trade-incidence-head{display:flex;justify-content:space-between;gap:18px;align-items:flex-start;margin-bottom:12px}.trade-incidence-head h3{margin:3px 0 5px;font-size:16px}.trade-incidence-head p{margin:0;max-width:720px;color:#68736f;font-size:11px;line-height:1.5}.incidence-badge{border:1px solid var(--line);padding:6px 9px;font-size:8px;letter-spacing:.09em;text-transform:uppercase;white-space:nowrap}.incidence-badge.empirical{border-color:#55795f;color:#31583c}.incidence-badge.proxy{border-color:#9c7b43;color:#76571f}.incidence-table{width:100%;border-collapse:collapse;font-size:10px}.incidence-table th,.incidence-table td{padding:8px 7px;border-bottom:1px solid var(--line);text-align:right}.incidence-table th:first-child,.incidence-table td:first-child{text-align:left}.incidence-table tbody th small{display:block;color:#7a8580;font-size:8px}.incidence-method{margin-top:10px;font-size:9px;line-height:1.5;color:#6b7671}@media(max-width:800px){.trade-incidence-head{flex-direction:column}.incidence-table{min-width:720px}}`;
    document.head.appendChild(style);
    const section = document.createElement('section');
    section.id = 'tradeIncidenceDiagnostics';
    section.className = 'trade-incidence';
    section.innerHTML = `<div class="trade-incidence-head"><div><div class="eyebrow">Tariff incidence & production network</div><h3>Who pays the tariff, and where do input costs propagate?</h3><p>Applied tariffs are percentage points after negotiated relief and sector coverage. Buyer pass-through is the modeled price incidence; exporter/importer absorption is the remaining tariff burden split across margins. Upstream cost is the downstream marginal-cost pressure propagated through the country-specific production network.</p></div><span id="incidenceEvidenceBadge" class="incidence-badge"></span></div><div class="table-wrap"><table class="incidence-table"><thead><tr><th>Sector</th><th>Applied tariff</th><th>Buyer pass-through</th><th>Foreign exporter absorption</th><th>Domestic importer absorption</th><th>Upstream input cost</th></tr></thead><tbody id="tradeIncidenceRows"></tbody></table></div><p id="tradeIncidenceMethod" class="incidence-method"></p>`;
    sectorPanel.appendChild(section);
  }

  function metric(sector, country, key) {
    const trade = sector?.trade || {};
    if (country === 'us') {
      return {
        applied: trade.usAppliedTariff,
        buyer: trade.usBuyerPassThrough,
        exporter: trade.canadaExporterAbsorption,
        importer: trade.usImporterAbsorption,
        upstream: trade.usUpstreamCost
      }[key];
    }
    return {
      applied: trade.canadaAppliedTariff,
      buyer: trade.canadaBuyerPassThrough,
      exporter: trade.usExporterAbsorption,
      importer: trade.canadaImporterAbsorption,
      upstream: trade.canadaUpstreamCost
    }[key];
  }

  function render() {
    inject();
    const rows = document.querySelector('#tradeIncidenceRows');
    if (!rows) return;
    const scenario = currentScenario();
    const country = currentCountry();
    const sectors = scenario?.sectors || [];
    let query = '';
    try { query = String(document.querySelector('#sectorSearch')?.value || '').trim().toLowerCase(); } catch (_) {}
    const filtered = sectors.filter(s => !query || String(s.name || '').toLowerCase().includes(query) || String(s.code || '').includes(query));
    rows.innerHTML = filtered.map(s => `<tr><th scope="row"><small>${esc(s.code)}</small>${esc(s.name)}</th><td>${fmt(metric(s,country,'applied'))} pp</td><td>${fmt(metric(s,country,'buyer'))} pp</td><td>${fmt(metric(s,country,'exporter'))} pp</td><td>${fmt(metric(s,country,'importer'))} pp</td><td>${fmt(metric(s,country,'upstream'),3)} pp</td></tr>`).join('') || '<tr><td colspan="6">No matching sectors.</td></tr>';

    const badge = document.querySelector('#incidenceEvidenceBadge');
    if (badge) {
      const empirical = country === 'canada';
      badge.textContent = empirical ? 'Canada IO · StatCan empirical' : 'U.S. IO · BEA artifact pending · EPA USEEIO proxy active';
      badge.className = `incidence-badge ${empirical ? 'empirical' : 'proxy'}`;
    }
    const method = document.querySelector('#tradeIncidenceMethod');
    if (method) {
      let text = '';
      try { text = result?.recommendation?.tradeNetworkMethod || ''; } catch (_) {}
      method.innerHTML = `<b>${country === 'canada' ? 'Canadian network:' : 'U.S. network:'}</b> ${esc(text || 'Network methodology unavailable.')}`;
    }
  }

  function start() {
    inject();
    const sectorRows = document.querySelector('#sectorRows');
    if (sectorRows) new MutationObserver(render).observe(sectorRows, {childList:true, subtree:true});
    document.querySelector('#sectorSearch')?.addEventListener('input', render);
    document.querySelectorAll('[data-country]').forEach(button => button.addEventListener('click', () => setTimeout(render, 0)));
    document.querySelector('#cards') && new MutationObserver(render).observe(document.querySelector('#cards'), {childList:true});
    render();
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start);
  else start();
})();

// All slider and negotiation parameter edits are staged. Values remain live in
// the UI/negotiation state, but the expensive optimizer runs only on an explicit run.
(() => {
  'use strict';

  const partyView = document.querySelector('#partyView');
  const dashboardRun = document.querySelector('#run');
  if (typeof schedule !== 'function' || !partyView || !dashboardRun) return;

  let staged = false;
  let partyRun = null;
  let partyRunStatus = null;

  function setStaged(value) {
    staged = value;
    if (partyRun) partyRun.dataset.staged = staged ? 'true' : 'false';
    if (partyRunStatus) {
      partyRunStatus.textContent = staged
        ? 'Inputs saved · optimizer not run yet'
        : 'Uses Dashboard optimizer · Run again now';
    }
    const autoStatus = document.querySelector('.auto span');
    if (autoStatus) {
      autoStatus.textContent = staged
        ? 'Inputs staged. No global search is running. Click Run again now (or Run new run in a delegation tab) to evaluate the values exactly as set.'
        : 'Optimizer runs only on an explicit Run again now / Run new run action. Slider edits remain as set until that run starts.';
    }
  }

  // This is the single choke point used by every range commit in app.js.
  // Scheduling now means "dirty/staged", never "launch evaluate after 350 ms".
  schedule = function stageOptimizerInputs() {
    setStaged(true);
  };

  function syncRunButton() {
    if (!partyRun) return;
    partyRun.disabled = !!dashboardRun.disabled;
    partyRun.setAttribute('aria-busy', dashboardRun.disabled ? 'true' : 'false');
  }

  function updateCopy() {
    const linked = document.querySelector('.preferences .linked-note');
    if (linked) {
      linked.textContent = 'Linked: moving either party automatically assigns the remainder to the other. Slider changes are staged only; they do not launch the optimizer until Run again now is clicked.';
    }
    const notes = [...partyView.querySelectorAll('.linked-note')];
    const liveNote = notes.find(node => /Live model:|Staged search:/i.test(node.textContent || ''));
    if (liveNote) {
      liveNote.innerHTML = '<b>Staged search:</b> tariff, allocation, and sector slider changes are saved as set but never launch a policy search. Click <b>Run new run</b> to execute the same optimizer as Dashboard <b>Run again now</b>.';
    }
    const boardCopy = partyView.querySelector('.sector-board-head p');
    if (boardCopy) {
      boardCopy.textContent = 'Adjust sector tariffs freely. Values remain as set while you work; the expensive bilateral search starts only when Run new run is clicked.';
    }
  }

  function stagePresetButtons() {
    document.querySelectorAll('.presets button').forEach(button => {
      button.onclick = () => {
        tariff.value = button.dataset.rate;
        updateTariff();
        updatePosition();
        syncPartyView();
        refreshPartySectorMetrics();
        publishNegotiation('us');
        schedule();
      };
    });
  }

  function injectRunControl() {
    if (document.querySelector('#partyRun')) {
      partyRun = document.querySelector('#partyRun');
      partyRunStatus = document.querySelector('#partyRunStatus');
      syncRunButton();
      return;
    }

    const summary = partyView.querySelector('.party-summary');
    if (!summary) return;
    const style = document.createElement('style');
    style.textContent = '.party-run-control{margin-top:16px;padding-top:14px;border-top:1px solid var(--line);display:grid;gap:7px}.party-run-control button{width:100%;padding:11px 12px;border:1px solid var(--ink);background:var(--ink);color:#fff;font-weight:700;cursor:pointer}.party-run-control button[data-staged="true"]{box-shadow:inset 0 0 0 2px #fff}.party-run-control button:disabled{opacity:.55;cursor:wait}.party-run-control small{font-size:9px;line-height:1.4;color:#6b7671}';
    document.head.appendChild(style);

    const control = document.createElement('div');
    control.className = 'party-run-control';
    control.innerHTML = '<button id="partyRun" type="button">Run new run →</button><small id="partyRunStatus">Uses Dashboard optimizer · Run again now</small>';
    summary.appendChild(control);
    partyRun = control.querySelector('#partyRun');
    partyRunStatus = control.querySelector('#partyRunStatus');
    partyRun.addEventListener('click', () => dashboardRun.click());
    syncRunButton();
  }

  document.addEventListener('click', event => {
    if (event.target?.closest?.('#run')) setStaged(false);
  }, true);

  if (typeof MutationObserver === 'function') {
    new MutationObserver(syncRunButton).observe(dashboardRun, {
      attributes:true,
      attributeFilter:['disabled']
    });
  }

  injectRunControl();
  updateCopy();
  stagePresetButtons();
  setStaged(false);

  const controller = {
    state: () => ({staged}),
    run: () => dashboardRun.click()
  };
  window.EvaluationRunController = controller;
  window.DelegationRunController = controller;
})();

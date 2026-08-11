(() => {
  'use strict';

  const LEDGER_KEY='canada-us-trade-diplomacy-decision-ledger-v1';
  const esc=v=>String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const f=(v,d=1)=>Number(v||0).toFixed(d);
  const signed=(v,s='')=>`${Number(v||0)>=0?'+':''}${f(v)}${s}`;

  function ledger(){try{return JSON.parse(localStorage.getItem(LEDGER_KEY)||'[]')}catch(_){return[]}}
  function saveLedger(items){try{localStorage.setItem(LEDGER_KEY,JSON.stringify(items.slice(0,20)))}catch(_){}}

  function packageById(id){
    const n=result?.negotiation;
    if(!n)return null;
    if(n.recommendedPackage?.id===id)return n.recommendedPackage;
    return (n.frontier||[]).find(p=>p.id===id)||null;
  }

  function inject(){
    if(document.querySelector('#tradeDiplomacyOps'))return;
    const anchor=document.querySelector('#computationalNegotiation')||document.querySelector('#diplomatCommand')||document.querySelector('.impact-strip');
    if(!anchor)return;
    const section=document.createElement('section');section.id='tradeDiplomacyOps';section.className='trade-ops';
    section.innerHTML=`<div class="ops-head"><div><div class="eyebrow">Canada–U.S. trade diplomacy operations</div><h2>From model output to an executable negotiating process</h2><p>Treaty-aware issue management, robustness screening, round sequencing, implementation safeguards, domestic-authority gates, evidence provenance and a decision ledger.</p></div><div class="ops-badge">OPERATIONS LAYER</div></div>
      <div id="opsSummary" class="ops-summary"></div>
      <div id="opsDoctrine" class="ops-doctrine"></div>
      <div class="ops-grid"><section class="ops-pane"><div class="ops-section-head"><div><div class="eyebrow">Issue architecture</div><h3>CUSMA/USMCA vs parallel bilateral tracks</h3></div></div><div id="opsIssues"></div></section>
      <section class="ops-pane"><div class="eyebrow">Round playbook</div><h3>Sequenced negotiation plan</h3><div id="opsRounds"></div></section></div>
      <div class="ops-grid"><section class="ops-pane"><div class="eyebrow">Robust decision support</div><h3>Does the package survive harder assumptions?</h3><div id="opsRobustCases" class="robust-grid"></div><div class="ops-table-wrap"><table class="ops-table"><thead><tr><th>Package</th><th>Worst-case surplus</th><th>Case wins</th><th>All cases</th></tr></thead><tbody id="opsRobustPackages"></tbody></table></div></section>
      <section class="ops-pane"><div class="eyebrow">Implementation architecture</div><h3>Verification, cure and response</h3><div id="opsGuardrails"></div></section></div>
      <div class="ops-grid"><section class="ops-pane"><div class="eyebrow">Domestic feasibility</div><h3>Mandate and stakeholder gates</h3><div id="opsGates"></div></section>
      <section class="ops-pane"><div class="eyebrow">Evidence and provenance</div><h3>What must be refreshed before the room</h3><div id="opsEvidence"></div></section></div>
      <div class="ops-grid"><section class="ops-pane"><div class="eyebrow">Decision discipline</div><h3>Delegation decision ledger</h3><p>Record a local snapshot of the package actually discussed or chosen. This is browser-local working data—not a secure records system.</p><div class="decision-box"><select id="decisionStatus"><option>Proposed</option><option>Bridge option</option><option>Held in reserve</option><option>Rejected</option><option>Accepted for further drafting</option></select><textarea id="decisionRationale" placeholder="Rationale, condition, red line, counterpart signal, authority required…"></textarea><div class="decision-actions"><button id="recordDecision" type="button">Record snapshot</button><button id="copyOpsSnapshot" class="secondary" type="button">Copy operational snapshot</button></div></div><div id="decisionLedger"></div></section>
      <section class="ops-pane"><div class="eyebrow">Room strategy</div><h3>Bank · link · protect</h3><div id="opsRoomStrategy"></div><div class="ops-warning"><b>Security boundary:</b> browser local storage and this research server are not a classified, protected, or production diplomatic records environment. Do not place controlled or classified material here without an approved secure deployment architecture.</div></section></div>`;
    anchor.insertAdjacentElement('afterend',section);
    document.querySelector('#recordDecision').addEventListener('click',recordDecision);
    document.querySelector('#copyOpsSnapshot').addEventListener('click',copySnapshot);
    renderLedger();
  }

  function render(){
    inject();
    const p=result?.tradeDiplomacy;if(!p||!document.querySelector('#tradeDiplomacyOps'))return;
    const preferred=packageById(p.recommendedRobustPackageId)||result?.negotiation?.recommendedPackage;
    document.querySelector('#opsSummary').innerHTML=`<div><span>Operational readiness</span><b>${f(p.operationalReadiness,0)}/100</b><small>${esc(p.readinessLabel)}</small></div><div><span>Robust package</span><b>${esc(p.recommendedRobustPackageId||'—')}</b><small>${preferred?esc(preferred.strategyName):'No package'}</small></div><div><span>Worst-case surplus floor</span><b>${signed(p.recommendedWorstCaseSurplus)}</b><small>Across ${p.robustCases} harder assumption sets</small></div><div><span>Bridge package</span><b>${esc(p.bridgePackageId||'—')}</b><small>Preserved as a controlled fallback</small></div>`;
    document.querySelector('#opsDoctrine').textContent=p.operatingDoctrine||'';

    document.querySelector('#opsIssues').innerHTML=(p.issueTracks||[]).map(x=>`<div class="issue-track"><div><b>${esc(x.label)}</b><small>${esc(x.linkGroup)}</small><span class="track-badge ${x.parallelTrack?'parallel':''}">${x.parallelTrack?'Parallel bilateral':'CUSMA/USMCA-linked'}</span></div><div><small>${esc(x.forum)}</small>${esc(x.objective)}</div><div><small>Joint value</small><span class="track-score">${f(x.jointValue,0)}</span></div><div><small>Sensitivity</small><span class="track-score ${x.domesticSensitivity>=85?'sensitive':''}">${f(x.domesticSensitivity,0)}</span></div></div>`).join('');

    document.querySelector('#opsRounds').innerHTML=(p.roundPlan||[]).map(x=>`<div class="round-step"><i>${x.order}</i><div><b>${esc(x.phase)}</b><p>${esc(x.negotiatingMove)}</p><em>Exit: ${esc(x.exitCriteria)}</em></div></div>`).join('');

    const robust=(p.robustPackages||[]);const winnerId=p.recommendedRobustPackageId;
    document.querySelector('#opsRobustCases').innerHTML=(p.robustnessCases||[]).map(x=>`<div class="robust-card ${x.winnerPackageId===winnerId?'winner':''}"><small>${esc(x.label)}</small><b>${esc(x.winnerPackageId)}</b><p>${esc(x.description)}</p></div>`).join('');
    document.querySelector('#opsRobustPackages').innerHTML=robust.slice(0,10).map(x=>`<tr><th>${esc(x.packageId)}<small>${esc(x.strategyName)}</small></th><td>${signed(x.worstCaseSurplus)}</td><td>${x.caseWins}</td><td class="${x.clearsAllCases?'yes':'no'}">${x.clearsAllCases?'CLEARS':'BREACH'}</td></tr>`).join('');

    document.querySelector('#opsGuardrails').innerHTML=(p.guardrails||[]).map(x=>`<div class="guardrail"><b>${esc(x.mechanism)}</b><p><strong>Trigger:</strong> ${esc(x.trigger)}</p><p><strong>Evidence:</strong> ${esc(x.evidence)}</p><p><strong>Response:</strong> ${esc(x.response)}</p><small>${esc(x.cadence)}</small></div>`).join('');
    document.querySelector('#opsGates').innerHTML=(p.stakeholderGates||[]).map(x=>`<div class="gate"><b>${esc(x.country)} · ${esc(x.gate)}</b><p>${esc(x.rationale)}</p><small class="${x.formalAuthority?'formal':''}">${x.formalAuthority?'AUTHORITY GATE':'CONSULTATION / IMPLEMENTATION GATE'} · sensitivity ${f(x.sensitivity,0)}</small></div>`).join('');
    document.querySelector('#opsEvidence').innerHTML=(p.evidenceLedger||[]).map(x=>`<div class="evidence-row"><b>${esc(x.source)}</b><p>${esc(x.purpose)}</p><small>${esc(x.status)} · ${esc(x.refreshRule)}</small></div>`).join('');

    const treaty=(p.issueTracks||[]).filter(x=>!x.parallelTrack).sort((a,b)=>b.jointValue-a.jointValue);
    const parallel=(p.issueTracks||[]).filter(x=>x.parallelTrack).sort((a,b)=>b.domesticSensitivity-a.domesticSensitivity);
    const bank=treaty.filter(x=>x.jointValue>=85&&x.domesticSensitivity<80).slice(0,3);
    const link=treaty.filter(x=>x.jointValue>=80).slice(0,4);
    const protect=[...(p.issueTracks||[])].sort((a,b)=>b.domesticSensitivity-a.domesticSensitivity).slice(0,4);
    document.querySelector('#opsRoomStrategy').innerHTML=`<div class="guardrail"><b>Bank early</b><p>${bank.length?bank.map(x=>esc(x.label)).join(' · '):'No low-sensitivity early harvest currently dominates.'}</p></div><div class="guardrail"><b>Link conditionally</b><p>${link.map(x=>esc(x.label)).join(' · ')}</p></div><div class="guardrail"><b>Protect / mandate-check</b><p>${protect.map(x=>esc(x.label)).join(' · ')}</p></div>${parallel.length?`<div class="guardrail"><b>Keep parallel unless deliberately linked</b><p>${parallel.map(x=>esc(x.label)).join(' · ')}</p></div>`:''}`;
    enhanceBriefing();renderLedger();
  }

  function snapshot(){
    const p=result?.tradeDiplomacy,n=result?.negotiation,preferred=packageById(p?.recommendedRobustPackageId)||n?.recommendedPackage;
    return {timestamp:new Date().toISOString(),status:document.querySelector('#decisionStatus')?.value||'Proposed',rationale:document.querySelector('#decisionRationale')?.value?.trim()||'',packageId:p?.recommendedRobustPackageId||preferred?.id||'',strategy:preferred?.strategyName||'',readiness:p?.operationalReadiness||0,readinessLabel:p?.readinessLabel||'',worstCaseSurplus:p?.recommendedWorstCaseSurplus||0,canadaUtility:preferred?.canadaUtility||0,usUtility:preferred?.usUtility||0,stability:preferred?.stabilityScore||0,model:{candidates:n?.candidatesExamined||0,pareto:n?.paretoFrontierSize||0,robustCases:p?.robustCases||0}};
  }
  function recordDecision(){const s=snapshot(),items=ledger();items.unshift(s);saveLedger(items);document.querySelector('#decisionRationale').value='';renderLedger();}
  function renderLedger(){const el=document.querySelector('#decisionLedger');if(!el)return;const items=ledger();el.innerHTML=items.length?items.slice(0,8).map(x=>`<div class="decision-entry"><b>${esc(x.status)} · ${esc(x.packageId)} · ${new Date(x.timestamp).toLocaleString()}</b><span>${esc(x.strategy)} · readiness ${f(x.readiness,0)}/100 · stability ${f(x.stability,0)}/100</span>${x.rationale?`<p>${esc(x.rationale)}</p>`:''}</div>`).join(''):'<p>No decision snapshots recorded in this browser.</p>';}
  function copySnapshot(){const text=JSON.stringify(snapshot(),null,2);if(navigator.clipboard?.writeText)navigator.clipboard.writeText(text);}

  function enhanceBriefing(){
    const sheet=document.querySelector('#briefingSheet'),p=result?.tradeDiplomacy;if(!sheet||!p||sheet.querySelector('.trade-ops-annex'))return;
    const preferred=packageById(p.recommendedRobustPackageId)||result?.negotiation?.recommendedPackage;
    const annex=document.createElement('section');annex.className='trade-ops-annex';
    annex.innerHTML=`<h2>Trade diplomacy operations annex</h2><p><b>Operational readiness:</b> ${f(p.operationalReadiness,0)}/100 — ${esc(p.readinessLabel)}. <b>Robust package:</b> ${esc(p.recommendedRobustPackageId)}${preferred?` (${esc(preferred.strategyName)})`:''}. <b>Worst-case modeled surplus floor:</b> ${signed(p.recommendedWorstCaseSurplus)} across ${p.robustCases} harder assumption sets.</p><h3>Round sequence</h3><ol>${(p.roundPlan||[]).map(x=>`<li><b>${esc(x.phase)}:</b> ${esc(x.negotiatingMove)}</li>`).join('')}</ol><h3>Authority and implementation</h3><ul>${(p.stakeholderGates||[]).filter(x=>x.formalAuthority).map(x=>`<li><b>${esc(x.country)} — ${esc(x.gate)}:</b> ${esc(x.rationale)}</li>`).join('')}</ul><p><b>Doctrine:</b> ${esc(p.operatingDoctrine)}</p>`;
    sheet.appendChild(annex);
  }

  function start(){inject();const cards=document.querySelector('#cards');if(cards)new MutationObserver(render).observe(cards,{childList:true});const brief=document.querySelector('#briefingSheet');if(brief)new MutationObserver(enhanceBriefing).observe(brief,{childList:true});render();}
  if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',start);else start();
})();
let settings={},result,noTariff,selected,series='inflationPath',sectorCountry='canada',negotiator='canada',openingCalibrated=false,negotiationRevision=-1,publishTimer,evaluationSequence=0;
const $=s=>document.querySelector(s),fmt=(x,d=1)=>Number(x).toFixed(d),signed=(x,suffix='%')=>(x>=0?'+':'')+fmt(x)+suffix;
const tariff=$('#usTariff');
const adjustingRanges=new Set();
const sectorNames=['Agriculture, forestry, fishing & hunting','Mining, quarrying, oil & gas','Utilities','Construction','Manufacturing','Wholesale trade','Retail trade','Transportation & warehousing','Information & cultural industries','Finance & insurance','Real estate, rental & leasing','Professional, scientific & technical services','Management of companies & enterprises','Administrative, support & waste services','Educational services','Health care & social assistance','Arts, entertainment & recreation','Accommodation & food services','Other services','Public administration'];
const positions={canada:Array(20).fill(100),us:Array(20).fill(100)};
const compactNumber=value=>Number(value).toFixed(2).replace(/\.0+$|(?<=\.[0-9])0$/,'');
function generatedPackageTitle(s){if(!String(s?.id||'').startsWith('custom-'))return s?.name||'';const move=Number(s.move||0),rate=move<0?`Cut ${compactNumber(Math.abs(move))} bp`:move>0?`Raise ${compactNumber(move)} bp`:'Hold rates',fiscal=Number(s.fiscal||0),fiscalLabel=`${fiscal<0?'−':'+'}${compactNumber(Math.abs(fiscal))}% fiscal`,match=String(s.description||'').match(/([+-]?\d+(?:\.\d+)?)% negotiated rate relief, productive share ([+-]?\d+(?:\.\d+)?)%, diversification ([+-]?\d+(?:\.\d+)?)%/i),parts=[rate,fiscalLabel];if(match)parts.push(`${compactNumber(match[1])}% tariff relief`,`${compactNumber(match[2])}% productive`,`${compactNumber(match[3])}% diversify`);return parts.join(' · ')}
function applyMeaningfulPackageNames(payload){const names=new Map();(payload?.scenarios||[]).forEach(s=>{if(String(s.id||'').startsWith('custom-')){s.name=generatedPackageTitle(s);names.set(s.id,s.name)}});const negotiation=payload?.negotiation,rename=p=>{if(p&&names.has(p.strategyId))p.strategyName=names.get(p.strategyId)};(negotiation?.frontier||[]).forEach(rename);rename(negotiation?.recommendedPackage);return payload}
window.PackageTitles={generatedPackageTitle,apply:applyMeaningfulPackageNames};
function updateTariff(){ $('#tariffValue').textContent=tariff.value+'%';document.querySelectorAll('.presets button').forEach(b=>b.classList.toggle('active',b.dataset.rate===tariff.value)); }
async function loadBaseline(){try{const [b]=await Promise.all([fetch('/api/baseline').then(r=>r.json()),pollNegotiation(true)]);settings=b.settings;$('#dataStatus').textContent=b.status==='live'?'Live official feeds':'Documented cached baseline';$('#asOf').textContent='As of '+new Date(b.asOf).toLocaleString();$('#sync').textContent=b.status==='live'?'Official feeds synchronized':'Baseline fallback active';$('#sourceList').innerHTML=b.sources.map(s=>`<a href="${s.url}" target="_blank" rel="noreferrer"><b>${s.name}</b><span>${s.fields} ↗</span></a>`).join('');}catch(e){settings={};$('#dataStatus').textContent='Baseline unavailable';$('#sync').textContent='Connection error';}evaluate();setInterval(()=>pollNegotiation(false),1000)}
function applyNegotiation(state,run){negotiationRevision=state.revision;tariff.value=state.usTariff;$('#retaliatoryTariff').value=state.retaliatoryTariff;$('#retaliatoryTariffValue').textContent=state.retaliatoryTariff+'%';positions.canada.splice(0,20,...state.canadaSectors);positions.us.splice(0,20,...state.usSectors);setWinWin('canadaPriority',state.canadaPriority,false);['riskAversion','cooperationCeiling'].forEach(id=>{if(state[id]!==undefined){$('#'+id).value=state[id];$('#'+id+'Value').textContent=state[id]+(id==='cooperationCeiling'?'%':'')}});updateTariff();updatePosition();syncPartyView();if(!$('#partyView').hidden)renderPartySectors();$('#negotiationSync').textContent=`Live · updated by ${state.updatedBy}`;if(run)schedule()}
async function pollNegotiation(initial){try{const state=await fetch('/api/negotiation',{cache:'no-store'}).then(r=>r.json());if(initial||state.revision>negotiationRevision)applyNegotiation(state,!initial)}catch(e){$('#negotiationSync').textContent='Live negotiation reconnecting…'}}
function publishNegotiation(actor){clearTimeout(publishTimer);publishTimer=setTimeout(async()=>{const payload={actor,canadaPriority:+$('#canadaPriority').value,usPriority:+$('#usPriority').value,riskAversion:+$('#riskAversion').value,cooperationCeiling:+$('#cooperationCeiling').value,usTariff:+tariff.value,retaliatoryTariff:+$('#retaliatoryTariff').value};const parties=actor==='automatic'?['canada','us']:[actor];parties.forEach(party=>positions[party].forEach((v,i)=>payload[party+'Sector'+i]=v));try{const state=await fetch('/api/negotiation',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)}).then(r=>r.json());negotiationRevision=state.revision;$('#negotiationSync').textContent=`Live · published by ${state.updatedBy}`}catch(e){$('#negotiationSync').textContent='Publish failed · retrying on next change'}},150)}
const preferenceIds=['canadaPriority','usPriority','riskAversion','cooperationCeiling'];
function setWinWin(changed,value,run=true){const own=Math.max(0,Math.min(100,+value)),other=100-own,otherId=changed==='canadaPriority'?'usPriority':'canadaPriority';$('#'+changed).value=own;$('#'+otherId).value=other;$('#canadaPriorityValue').textContent=$('#canadaPriority').value+'%';$('#usPriorityValue').textContent=$('#usPriority').value+'%';$('#priorityTotal').textContent=(+$('#canadaPriority').value + +$('#usPriority').value)+'%';syncPartyView();if(run)schedule()}
function sameCoverage(a,b){return Array.isArray(a)&&a.length===b.length&&a.every((v,i)=>+v===+b[i])}
function applyRecommendation(run=true,calibrateControls=true){if(!result?.recommendation)return false;const rec=result.recommendation;if(calibrateControls){setWinWin('canadaPriority',rec.canadaPriority,false);['riskAversion','cooperationCeiling'].forEach(id=>{$('#'+id).value=rec[id];$('#'+id+'Value').textContent=rec[id]+(id==='cooperationCeiling'?'%':'')})}const changed=!sameCoverage(rec.usSectorCoverage,positions.us)||!sameCoverage(rec.canadaSectorCoverage,positions.canada);if(rec.usSectorCoverage)positions.us.splice(0,20,...rec.usSectorCoverage);if(rec.canadaSectorCoverage)positions.canada.splice(0,20,...rec.canadaSectorCoverage);updatePosition();syncPartyView();if(!$('#partyView').hidden)renderPartySectors();if(run){publishNegotiation('automatic');evaluate()}return changed}
async function evaluate(){if(adjustingRanges.size){schedule();return}const sequence=++evaluationSequence,btn=$('#run'),loading=$('#strategyLoading');loading.hidden=false;btn.disabled=true;btn.firstChild.textContent='Searching policies and sector win-win… ';const preferences={canadaPriority:+$('#canadaPriority').value,usPriority:+$('#usPriority').value,riskAversion:+$('#riskAversion').value,cooperationCeiling:+$('#cooperationCeiling').value,retaliatoryTariff:+$('#retaliatoryTariff').value};positions.us.forEach((v,i)=>preferences['usSector'+i]=v);positions.canada.forEach((v,i)=>preferences['canadaSector'+i]=v);try{const request=(rate,retaliation=preferences.retaliatoryTariff)=>fetch('/api/evaluate',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({...settings,...preferences,usTariff:rate,retaliatoryTariff:retaliation})}).then(r=>r.json());const evaluated=await Promise.all([request(+tariff.value),request(0,0)]);if(sequence!==evaluationSequence)return;if(adjustingRanges.size){schedule();return}[result,noTariff]=evaluated;const changed=applyRecommendation(false,!openingCalibrated);openingCalibrated=true;if(changed){publishNegotiation('automatic');return await evaluate()}selected=result.scenarios[0];render();}finally{if(sequence===evaluationSequence){loading.hidden=true;btn.disabled=false;btn.firstChild.textContent='Run again now ';}}}
function render(){applyMeaningfulPackageNames(result);const best=result.scenarios[0],zero=noTariff.scenarios.find(s=>s.id===best.id)||noTariff.scenarios[0],rec=result.recommendation;$('#confidence').textContent=fmt(result.confidence,0)+'%';$('#searchCount').textContent=`${result.candidatesExamined} generated mixes + 13 expert strategies × ${result.allocationsExamined} allocations × ${result.gdpFloorsExamined} GDP floors evaluated`;$('#signal').textContent=result.signal;$('#rationale').textContent=result.rationale;$('#regime').textContent=result.regime;$('#neutral').textContent=fmt(result.neutralRate,2)+'%';$('#gap').textContent=signed(result.policyGap,' pp');$('#impactGrowth').textContent=signed(best.growth-zero.growth,' pp');$('#impactCost').textContent=fmt(best.costOfLiving)+'%';$('#impactExports').textContent=signed(best.exports);$('#impactRisk').textContent=fmt(best.recessionRisk,0)+'%';$('#recommendationValues').textContent=`Canada ${rec.canadaPriority} · USA ${rec.usPriority} · GDP floor ${fmt(rec.gdpGrowthFloor)}% · caution ${rec.riskAversion} · relief ${rec.cooperationCeiling}%`;$('#recommendationText').textContent=rec.explanation;$('#applyRecommendation').disabled=preferenceIds.every(id=>+$('#'+id).value===rec[id]);
 $('#usRevenue').textContent=`US$${fmt(best.usTariffRevenueUsd)}B`;$('#usRevenueCad').textContent=`C$${fmt(best.usTariffRevenueCad)}B equivalent`;$('#canadaRevenue').textContent=`C$${fmt(best.canadaTariffRevenueCad)}B`;$('#canadaRevenueUsd').textContent=`US$${fmt(best.canadaTariffRevenueUsd)}B equivalent`;$('#canadaBalance').textContent=`${best.canadaTradeBalanceCad>=0?'+':'−'}C$${fmt(Math.abs(best.canadaTradeBalanceCad))}B`;$('#usBalance').textContent=`${best.usTradeBalanceUsd>=0?'+':'−'}US$${fmt(Math.abs(best.usTradeBalanceUsd))}B`;$('#balanceGap').textContent=`US$${fmt(best.tradeBalanceGapUsd)}B remaining`;const progress=Math.max(0,Math.min(100,best.tradeBalanceProgress));$('#balanceProgress').style.width=progress+'%';$('#balanceProgressLabel').textContent=`${fmt(best.tradeBalanceProgress,0)}% toward zero from the pre-policy baseline`;$('#balanceActions').textContent=best.zeroTradeDeficit?`Target reached: US$${fmt(best.usExportExpansionUsd)}B in additional U.S. exports plus C$${fmt(best.canadaExportRedirectionCad)}B in redirected Canadian exports.`:`Best current action mix: US$${fmt(best.usExportExpansionUsd)}B in additional U.S. exports plus C$${fmt(best.canadaExportRedirectionCad)}B in redirected Canadian exports.`;
 const coverage=rec.usSectorCoverage||positions.us,averageCoverage=coverage.reduce((a,b)=>a+b,0)/coverage.length,growthGuarantee=best.sustainedBilateralGrowth?`Both GDP paths meet the searched ${fmt(rec.gdpGrowthFloor)}% floor · lowest ${fmt(best.bilateralGrowthFloor)}%`:`Searched ${fmt(rec.gdpGrowthFloor)}% GDP floor not met`;$('#liveDealImpact').textContent=`Published: ${best.name} · Canada ${fmt((best.bocScore+best.federalScore)/2,0)}/100 · USA ${fmt(best.usScore,0)}/100 · ${growthGuarantee} · U.S. equilibrium coverage ${fmt(averageCoverage,0)}%.`;$('#partyOutcome').textContent=`${best.name} · Canada GDP ${fmt(best.growth)}% · U.S. GDP ${fmt(best.usGrowth)}% · ${growthGuarantee}`;
 $('#cards').innerHTML=result.scenarios.map((s,i)=>`<article class="card ${i===0?'best':''} ${s.id===selected.id?'selected':''}" data-id="${s.id}"><div class="rank">${String(i+1).padStart(2,'0')}</div><div class="eyebrow">${s.id==='custom'||String(s.id).startsWith('custom-')?'AUTONOMOUS SEARCH · ':''}${s.move>0?'+':''}${s.move} bp · score ${fmt(s.score,0)}</div><h3>${s.name}</h3><p>${s.description}</p><div class="outcomes"><div><span>GDP</span><b>${fmt(s.growth)}%</b></div><div><span>Inflation</span><b>${fmt(s.inflation)}%</b></div><div><span>Jobs</span><b>${fmt(s.unemployment)}%</b></div><div><span>Debt</span><b>${fmt(s.debt)}%</b></div></div><div class="party-scores"><span>Canada mandate <b>${fmt(s.bocScore,0)}</b></span><span>Canada public <b>${fmt(s.federalScore,0)}</b></span><span>United States <b>${fmt(s.usScore,0)}</b></span></div><div class="risk"><span>Recession risk</span><i><em style="width:${s.recessionRisk}%"></em></i><b>${fmt(s.recessionRisk,0)}%</b></div></article>`).join('');document.querySelectorAll('.card').forEach(c=>c.onclick=()=>{selected=result.scenarios.find(s=>s.id===c.dataset.id);render()});draw();renderSectors();if(!$('#partyView').hidden)renderPartySectors()}
function renderSectors(){if(!selected?.sectors)return;const metric=$('#sectorMetric').value,q=$('#sectorSearch').value.trim().toLowerCase(),rows=selected.sectors.filter(s=>s.name.toLowerCase().includes(q)||s.code.includes(q));const values=selected.sectors.map(s=>s[sectorCountry][metric]),hurt=values.filter(v=>v<0).length,helped=values.length-hurt,avg=values.reduce((a,b)=>a+b,0)/values.length;$('#sectorSummary').innerHTML=`<div><span>SECTORS NEGATIVELY AFFECTED</span><b>${hurt} of 20</b></div><div><span>POSITIVE / PROTECTED</span><b>${helped} of 20</b></div><div><span>AVERAGE EFFECT</span><b class="${avg<0?'negative':'positive'}">${signed(avg,'%')}</b></div>`;$('#metricHeading').textContent={output:'Output change',jobs:'Employment change',prices:'Price change'}[metric];$('#sectorRows').innerHTML=rows.map(s=>{const v=s[sectorCountry][metric],width=Math.min(50,Math.abs(v)*6);return `<tr><th scope="row"><small>${s.code}</small>${s.name}</th><td><span class="exposure"><i style="width:${s.exposure}%"></i></span>${fmt(s.exposure,0)}%</td><td class="value ${v<0?'negative':'positive'}">${signed(v,'%')}</td><td><div class="impact-axis"><i class="${v<0?'negative':'positive'}" style="width:${width}%;${v<0?'right':'left'}:50%"></i></div></td></tr>`}).join('')||'<tr><td colspan="4" class="empty">No matching sectors</td></tr>'}
function draw(){if(!selected)return;$('#chartTitle').textContent=selected.name+' · 12-quarter projection';const a=selected[series],lo=Math.min(...a),hi=Math.max(...a),pad=(hi-lo)*.2||1,min=lo-pad,max=hi+pad,x=i=>55+i*72,y=v=>235-(v-min)/(max-min)*190;let path=a.map((v,i)=>(i?'L':'M')+x(i)+' '+y(v)).join(' '),svg='<defs><linearGradient id="fade" x2="0" y2="1"><stop stop-color="#b52b3a" stop-opacity=".22"/><stop offset="1" stop-color="#b52b3a" stop-opacity="0"/></linearGradient></defs>';for(let i=0;i<4;i++){let yy=45+i*63,v=max-i*(max-min)/3;svg+=`<line class="grid" x1="55" y1="${yy}" x2="850" y2="${yy}"/><text class="axis-label" x="5" y="${yy+3}">${fmt(v)}</text>`}svg+=`<path class="area" d="${path} L ${x(11)} 235 L 55 235 Z"/><path class="line" d="${path}"/>`;a.forEach((v,i)=>svg+=`<circle class="dot" cx="${x(i)}" cy="${y(v)}" r="3"/><text class="axis-label" x="${x(i)-7}" y="258">Q${i+1}</text>`);$('#chart').innerHTML=svg}
document.querySelectorAll('.country-switch button').forEach(b=>b.onclick=()=>{sectorCountry=b.dataset.country;document.querySelector('.country-switch .active').classList.remove('active');b.classList.add('active');renderSectors()});$('#sectorMetric').onchange=renderSectors;$('#sectorSearch').oninput=renderSectors;
function updatePosition(){const i=+$('#positionSector').value,value=positions[negotiator][i];$('#sectorCoverage').value=value;$('#sectorCoverageValue').textContent=value+'%';$('#positionCountry').textContent=negotiator==='canada'?'Canada position':'USA position';const rate=negotiator==='canada'?$('#retaliatoryTariff').value:tariff.value;$('#positionReadout').textContent=`${rate}% × ${value}% coverage · ${sectorNames[i]}`;}
$('#positionSector').innerHTML=sectorNames.map((name,i)=>`<option value="${i}">${name}</option>`).join('');
document.querySelectorAll('.negotiator-tabs button').forEach(b=>b.onclick=()=>{negotiator=b.dataset.negotiator;document.querySelector('.negotiator-tabs .active').classList.remove('active');b.classList.add('active');$('#canadaControls').hidden=negotiator!=='canada';$('#usControls').hidden=negotiator!=='us';updatePosition()});
function bindRangeCommit(input,preview,commit){input.onpointerdown=()=>adjustingRanges.add(input);input.onpointerup=input.onpointercancel=()=>adjustingRanges.delete(input);input.oninput=preview;input.onchange=()=>{adjustingRanges.delete(input);preview();commit()}}
function partySectorMetric(i){const live=window.EvaluationController?.sectorMetrics?.(i,positions.us[i],positions.canada[i]);if(live){const own=negotiator==='us'?live.us:live.canada,other=negotiator==='us'?live.canada:live.us,ownName=negotiator==='us'?'U.S.':'Canada',otherName=negotiator==='us'?'Canada':'U.S.',verified=live.recommendedCoverage,posture=live.verified?'VERIFIED OPTIMUM':`Exploring · verified CA ${fmt(verified.canada,0)}% / U.S. ${fmt(verified.us,0)}%`,envelope=live.insideSearchEnvelope?'':' · outside current search envelope';return{text:`${ownName} ${fmt(own.score,0)}/100 · ${otherName} ${fmt(other.score,0)}/100 · output ${signed(own.output,'%')} · jobs ${signed(own.jobs,'%')} · prices ${signed(own.prices,'%')} · ${posture}${envelope}`,score:own.score}}const rec=result?.recommendation,advantage=negotiator==='us'?rec?.usSectorOutput?.[i]:rec?.canadaSectorValue?.[i];return{text:advantage===undefined?'Searching deal…':`${negotiator==='us'?'U.S. balanced-deal score':'Canada balanced-deal score'} ${fmt(advantage,0)}/100`,score:Number.isFinite(+advantage)?+advantage:50}}
function updatePartySectorMetric(i,node){if(!node)return;const metric=partySectorMetric(i);node.textContent=metric.text;node.classList.toggle('negative',metric.score<50);node.classList.toggle('positive',metric.score>=50)}
function refreshPartySectorMetrics(){const list=$('#partySectorSliders');if(!list)return;list.querySelectorAll('[data-sector-metric]').forEach(node=>updatePartySectorMetric(+node.dataset.sectorMetric,node))}
$('#positionSector').onchange=updatePosition;bindRangeCommit($('#sectorCoverage'),()=>{positions[negotiator][+$('#positionSector').value]=+$('#sectorCoverage').value;updatePosition();refreshPartySectorMetrics()},()=>{publishNegotiation(negotiator);schedule()});bindRangeCommit($('#retaliatoryTariff'),()=>{$('#retaliatoryTariffValue').textContent=$('#retaliatoryTariff').value+'%';updatePosition();refreshPartySectorMetrics()},()=>{publishNegotiation('canada');schedule()});
let timer;function schedule(){clearTimeout(timer);timer=setTimeout(evaluate,350)}bindRangeCommit(tariff,()=>{updateTariff();updatePosition();syncPartyView();refreshPartySectorMetrics()},()=>{publishNegotiation('us');schedule()});['canadaPriority','usPriority'].forEach(id=>{bindRangeCommit($('#'+id),()=>setWinWin(id,$('#'+id).value,false),()=>{publishNegotiation(id==='canadaPriority'?'canada':'us');schedule()})});['riskAversion','cooperationCeiling'].forEach(id=>{bindRangeCommit($('#'+id),()=>{$('#'+id+'Value').textContent=$('#'+id).value+(id==='cooperationCeiling'?'%':'');refreshPartySectorMetrics()},()=>{publishNegotiation(negotiator);schedule()})});document.querySelectorAll('.presets button').forEach(b=>b.onclick=()=>{tariff.value=b.dataset.rate;updateTariff();updatePosition();syncPartyView();refreshPartySectorMetrics();publishNegotiation('us');evaluate()});$('#applyRecommendation').onclick=()=>applyRecommendation();$('#run').onclick=evaluate;document.querySelectorAll('.tabs button').forEach(b=>b.onclick=()=>{document.querySelector('.tabs .active').classList.remove('active');b.classList.add('active');series=b.dataset.series;draw()});$('#sourcesButton').onclick=()=>$('#sources').showModal();$('.close').onclick=()=>$('#sources').close();
function renderPartySectors(){const list=$('#partySectorSliders');list.innerHTML=sectorNames.map((name,i)=>{const metric=partySectorMetric(i);return `<label class="party-sector-control">${name}<output>${positions[negotiator][i]}%</output><small class="sector-deal-metric ${metric.score<50?'negative':'positive'}" data-sector-metric="${i}">${metric.text}</small><input type="range" min="0" max="100" value="${positions[negotiator][i]}" data-sector="${i}" aria-label="${name} tariff coverage"></label>`}).join('');list.querySelectorAll('input').forEach(input=>bindRangeCommit(input,()=>{const i=+input.dataset.sector;positions[negotiator][i]=+input.value;input.parentElement.querySelector('output').textContent=input.value+'%';updatePartySectorMetric(i,input.parentElement.querySelector('.sector-deal-metric'));if(+$('#positionSector').value===i){$('#sectorCoverage').value=input.value;updatePosition()}},()=>{publishNegotiation(negotiator);schedule()}))}
function syncPartyView(){if(!$('#partyView'))return;const isCanada=negotiator==='canada',priority=+$(isCanada?'#canadaPriority':'#usPriority').value;$('#partyPriority').value=priority;$('#partyPriorityValue').textContent=priority+'%';$('#counterpartyShare').textContent=(100-priority)+'%';$('#partyShare').textContent=`Canada ${$('#canadaPriority').value}% · USA ${$('#usPriority').value}%`;$('#partyTariff').value=isCanada?$('#retaliatoryTariff').value:tariff.value;$('#partyTariffValue').textContent=$('#partyTariff').value+'%'}
function showView(view,updateHash=true){if(!['dashboard','canada','us'].includes(view))view='dashboard';if(updateHash)history.replaceState(null,'',view==='dashboard'?'#dashboard':'#'+view);document.querySelectorAll('.header-tabs button').forEach(b=>{const active=b.dataset.view===view;b.classList.toggle('active',active);b.setAttribute('aria-selected',active)});const dashboard=view==='dashboard';$('#dashboardView').hidden=!dashboard;$('#partyView').hidden=dashboard;if(dashboard)return;negotiator=view;const canada=view==='canada';$('#partyEyebrow').textContent=canada?'Canada negotiation room':'United States negotiation room';$('#partyTitle').textContent=canada?'Minister LeBlanc’s trade table':'Ambassador Greer’s trade table';$('#partyIntro').textContent=canada?'Explore Canada’s retaliation and sector protections with live Canada/U.S. welfare, output, jobs and price metrics; verified agreements remain the auto-applied bargaining optimum.':'Explore U.S. tariff coverage and exemptions with live U.S./Canada welfare, output, jobs and price metrics; verified agreements remain the auto-applied bargaining optimum.';$('#delegationName').textContent=canada?'Canada · LeBlanc':'USA · Greer';renderPartySectors();syncPartyView();window.scrollTo(0,0)}
document.querySelectorAll('.header-tabs button').forEach(b=>b.onclick=()=>showView(b.dataset.view));$('#returnDashboard').onclick=()=>showView('dashboard');bindRangeCommit($('#partyPriority'),()=>setWinWin(negotiator==='canada'?'canadaPriority':'usPriority',$('#partyPriority').value,false),()=>{publishNegotiation(negotiator);schedule()});bindRangeCommit($('#partyTariff'),()=>{if(negotiator==='canada'){$('#retaliatoryTariff').value=$('#partyTariff').value;$('#retaliatoryTariffValue').textContent=$('#partyTariff').value+'%'}else{tariff.value=$('#partyTariff').value;updateTariff()}syncPartyView();updatePosition();refreshPartySectorMetrics()},()=>{publishNegotiation(negotiator);schedule()});$('#resetSectors').onclick=()=>{positions[negotiator].fill(100);renderPartySectors();updatePosition();publishNegotiation(negotiator);schedule()};setWinWin('usPriority',50,false);updateTariff();updatePosition();showView(location.hash.slice(1),false);loadBaseline();

// Delegation tariff display adapter. The engine and negotiation API continue to
// use 0–100 sector coverage internally, but every negotiator-facing control is
// expressed in the actual tariff percentage applied to that sector.
;(() => {
  const clampNumber=(value,lo,hi)=>Math.max(lo,Math.min(hi,Number(value)||0));
  const coverageToTariff=(headline,coverage)=>Math.max(0,Number(headline)||0)*clampNumber(coverage,0,100)/100;
  const tariffToCoverage=(headline,applied)=>Number(headline)>0?clampNumber(100*(Number(applied)||0)/Number(headline),0,100):0;
  const headlineFor=party=>Math.max(0,Number(party==='canada'?$('#retaliatoryTariff')?.value:tariff?.value)||0);
  const appliedFor=(party,coverage)=>coverageToTariff(headlineFor(party),coverage);
  const tariffText=value=>compactNumber(Math.max(0,Number(value)||0))+'%';
  const configureTariffInput=(input,party,coverage)=>{
    const headline=headlineFor(party),applied=coverageToTariff(headline,coverage);
    if(input){input.min='0';input.max=String(headline);input.step='0.1';input.value=String(applied);input.disabled=headline<=0;}
    return {headline,applied};
  };

  window.DelegationTariffs={coverageToTariff,tariffToCoverage,headlineFor,appliedFor,tariffText};

  function setDelegationTariffCopy(){
    const sectorInput=$('#sectorCoverage'),label=sectorInput?.closest?.('label');
    if(label){
      for(const child of label.childNodes||[]){
        if(child.nodeType===3&&String(child.textContent||'').includes('Tariff coverage for this sector')){child.textContent='Applied tariff for this sector ';break;}
      }
      const spans=label.querySelectorAll?.('small span')||[];
      if(spans[0])spans[0].textContent='0% exempt';
    }
    const usCopy=$('#usControls')?.querySelector?.('p');
    if(usCopy)usCopy.textContent='Headline U.S. tariff is controlled above. Sector sliders below show the actual tariff rate applied to each sector.';
    const board=document.querySelector?.('.sector-board-head');
    const heading=board?.querySelector?.('h2'),paragraph=board?.querySelector?.('p');
    if(heading)heading.textContent='Sector-by-sector applied tariffs and deal metrics';
    if(paragraph)paragraph.textContent='Both delegations see the searched bilateral deal metrics for their country. Adjust each sector directly in tariff percentage points, from 0% exemption up to the delegation’s current headline tariff.';
    const reset=$('#resetSectors');if(reset)reset.textContent='Reset all to headline tariff';
  }

  updatePosition=function(){
    const i=+$('#positionSector').value,coverage=positions[negotiator][i],input=$('#sectorCoverage');
    const {headline,applied}=configureTariffInput(input,negotiator,coverage);
    $('#sectorCoverageValue').textContent=tariffText(applied);
    $('#positionCountry').textContent=negotiator==='canada'?'Canada position':'USA position';
    const spans=input?.closest?.('label')?.querySelectorAll?.('small span')||[];
    if(spans[1])spans[1].textContent=`${tariffText(headline)} headline rate`;
    $('#positionReadout').textContent=`${tariffText(applied)} applied tariff · ${sectorNames[i]} · ${tariffText(headline)} headline`;
  };

  const basePartySectorMetric=partySectorMetric;
  partySectorMetric=function(i){
    const metric=basePartySectorMetric(i),live=window.EvaluationController?.sectorMetrics?.(i,positions.us[i],positions.canada[i]);
    if(metric?.text&&live&&!live.verified){
      const verified=live.recommendedCoverage||{};
      const replacement=`verified tariffs CA ${tariffText(appliedFor('canada',verified.canada))} / U.S. ${tariffText(appliedFor('us',verified.us))}`;
      metric.text=metric.text.replace(/verified CA [-\d.]+% \/ U\.S\. [-\d.]+%/,replacement);
    }
    return metric;
  };

  const baseRefreshPartySectorMetrics=refreshPartySectorMetrics;
  refreshPartySectorMetrics=function(){
    const list=$('#partySectorSliders');
    list?.querySelectorAll?.('input[data-sector]')?.forEach(input=>{
      const i=+input.dataset.sector,{applied}=configureTariffInput(input,negotiator,positions[negotiator][i]);
      const output=input.parentElement?.querySelector?.('output');if(output)output.textContent=tariffText(applied);
    });
    baseRefreshPartySectorMetrics();
  };

  renderPartySectors=function(){
    const list=$('#partySectorSliders');if(!list)return;
    const headline=headlineFor(negotiator);
    list.innerHTML=sectorNames.map((name,i)=>{
      const metric=partySectorMetric(i),applied=coverageToTariff(headline,positions[negotiator][i]);
      return `<label class="party-sector-control">${name}<output>${tariffText(applied)}</output><small class="sector-deal-metric ${metric.score<50?'negative':'positive'}" data-sector-metric="${i}">${metric.text}</small><input type="range" min="0" max="${headline}" step="0.1" value="${applied}" data-sector="${i}" aria-label="${name} applied tariff percentage"${headline<=0?' disabled':''}></label>`;
    }).join('');
    list.querySelectorAll('input').forEach(input=>bindRangeCommit(input,()=>{
      const i=+input.dataset.sector;
      positions[negotiator][i]=tariffToCoverage(headlineFor(negotiator),+input.value);
      input.parentElement.querySelector('output').textContent=tariffText(input.value);
      updatePartySectorMetric(i,input.parentElement.querySelector('.sector-deal-metric'));
      if(+$('#positionSector').value===i)updatePosition();
    },()=>{publishNegotiation(negotiator);schedule()}));
  };

  // Replace the sidebar sector input's original raw-coverage binding with an
  // applied-tariff binding. Publishing still sends normalized coverage.
  bindRangeCommit($('#sectorCoverage'),()=>{
    const i=+$('#positionSector').value;
    positions[negotiator][i]=tariffToCoverage(headlineFor(negotiator),+$('#sectorCoverage').value);
    updatePosition();refreshPartySectorMetrics();
  },()=>{publishNegotiation(negotiator);schedule()});

  // Keep delegation tariff sliders synchronized when the headline rate changes.
  bindRangeCommit($('#retaliatoryTariff'),()=>{
    $('#retaliatoryTariffValue').textContent=$('#retaliatoryTariff').value+'%';
    updatePosition();syncPartyView();refreshPartySectorMetrics();
  },()=>{publishNegotiation('canada');schedule()});

  const baseShowView=showView;
  showView=function(view,updateHash=true){
    baseShowView(view,updateHash);
    if(view==='canada')$('#partyIntro').textContent='Explore Canada’s actual retaliatory tariff rates by sector with live Canada/U.S. welfare, output, jobs and price metrics; verified agreements remain the auto-applied bargaining optimum.';
    else if(view==='us')$('#partyIntro').textContent='Explore the actual U.S. tariff rate applied to each sector, including exemptions and partial relief, with live U.S./Canada welfare, output, jobs and price metrics.';
  };

  const baseRender=render;
  render=function(){
    baseRender();
    const rec=result?.recommendation,coverage=rec?.usSectorCoverage||positions.us;
    if(coverage?.length){
      const averageCoverage=coverage.reduce((a,b)=>a+Number(b||0),0)/coverage.length;
      const averageTariff=coverageToTariff(headlineFor('us'),averageCoverage),node=$('#liveDealImpact');
      if(node)node.textContent=node.textContent.replace(/U\.S\. equilibrium coverage [-\d.]+%\./,`U.S. average applied sector tariff ${tariffText(averageTariff)}.`);
    }
  };

  setDelegationTariffCopy();
  updatePosition();
  refreshPartySectorMetrics();
  if(!$('#partyView')?.hidden){renderPartySectors();showView(negotiator,false);}
})();

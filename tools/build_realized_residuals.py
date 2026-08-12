#!/usr/bin/env python3
"""Build/verify realized quarterly residual diagnostics from frozen/compact public series."""
from __future__ import annotations
import argparse,csv,hashlib,io,math,statistics,time,urllib.request
from pathlib import Path

START,END="2001Q1","2019Q4"
FRED_CA_EXPORTS="https://fred.stlouisfed.org/graph/fredgraph.csv?id=NXRSAXDCCAQ"
FRED_US_GDP="https://fred.stlouisfed.org/graph/fredgraph.csv?id=A191RL1Q225SBEA"
FRED_US_EXPORTS="https://fred.stlouisfed.org/graph/fredgraph.csv?id=A020RL1Q158SBEA"
SERIES=("canada_growth","us_growth","canada_exports","us_exports")
PARAM={"canada_growth":"growth_shock_sd","us_growth":"us_growth_shock_sd","canada_exports":"export_shock_sd","us_exports":"us_export_shock_sd"}

def get(url):
    last=None
    for attempt in range(4):
        try:
            req=urllib.request.Request(url,headers={"User-Agent":"CanadaPolicyStudio/2.0"})
            with urllib.request.urlopen(req,timeout=45) as r:return r.read()
        except Exception as exc:last=exc;time.sleep(attempt+1)
    raise RuntimeError(f"download failed: {url}: {last}")
def qkey(q):return int(q[:4]),int(q[-1])
def qshift(q,n):
    y,k=qkey(q);x=y*4+k-1+n;return f"{x//4}Q{x%4+1}"
def qdate(s):
    y=int(s[:4]);m=int(s[5:7]);return f"{y}Q{(m-1)//3+1}"
def in_sample(q):return qkey(START)<=qkey(q)<=qkey(END)
def fred(raw):
    out={}
    for row in csv.DictReader(io.StringIO(raw.decode('utf-8-sig'))):
        vals=[v for k,v in row.items() if k!='DATE' and v not in (None,'','.')]
        if not vals:continue
        try:out[qdate(row['DATE'])]=float(vals[0])
        except ValueError:pass
    return out
def growth(levels):
    return {q:400*math.log(v/levels[qshift(q,-1)]) for q,v in levels.items() if qshift(q,-1) in levels and v>0 and levels[qshift(q,-1)]>0}
def frozen_canada_growth(root):
    path=root/'data'/'calibration'/'quarterly_estimation_panel.csv';out={}
    for row in csv.DictReader(path.open(encoding='utf-8')):
        q=row['quarter']
        try:v=float(row['statcan_gdp_growth'])
        except ValueError:continue
        if math.isfinite(v):out[q]=v
    if len(out)<70:raise RuntimeError('frozen StatCan GDP diagnostic is incomplete')
    return out

def ar1(series):
    obs=[]
    for q in sorted(series,key=qkey):
        p=qshift(q,-1)
        if p in series and in_sample(q) and in_sample(p):obs.append((series[p],series[q],q))
    if len(obs)<60:raise RuntimeError(f'insufficient realized observations: {len(obs)}')
    xs=[x for x,_,_ in obs];ys=[y for _,y,_ in obs];xm=statistics.fmean(xs);ym=statistics.fmean(ys);den=sum((x-xm)**2 for x in xs);rho=sum((x-xm)*(y-ym) for x,y in zip(xs,ys))/den if den else 0.;intercept=ym-rho*xm
    res={q:y-intercept-rho*x for x,y,q in obs};return intercept,rho,res,statistics.stdev(res.values())
def pair_stats(a,b):
    am=statistics.fmean(a);bm=statistics.fmean(b);n=len(a);cov=sum((x-am)*(y-bm) for x,y in zip(a,b))/max(1,n-1);sa=statistics.stdev(a);sb=statistics.stdev(b);return cov,cov/(sa*sb) if sa and sb else 0.
def write_csv(path,fields,rows):
    with path.open('w',newline='',encoding='utf-8') as f:w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(rows)
def refresh(root):
    ca_raw=get(FRED_CA_EXPORTS);usg_raw=get(FRED_US_GDP);use_raw=get(FRED_US_EXPORTS)
    data={"canada_growth":frozen_canada_growth(root),"us_growth":fred(usg_raw),"canada_exports":growth(fred(ca_raw)),"us_exports":fred(use_raw)}
    residuals={};est=[]
    for name in SERIES:
        intercept,rho,res,sd=ar1(data[name]);residuals[name]=res
        est.append({"parameter":PARAM[name],"source_series":name,"residual_sd":f"{sd:.10g}","ar_intercept":f"{intercept:.10g}","ar1":f"{rho:.10g}","observations":len(res),"sample_start":START,"sample_end":END,"mapping_status":"reference-only","method":"AR(1) innovation SD from realized quarterly growth"})
    common=sorted(set.intersection(*(set(residuals[n]) for n in SERIES)),key=qkey);covrows=[]
    for a in SERIES:
        for b in SERIES:
            av=[residuals[a][q] for q in common];bv=[residuals[b][q] for q in common];cv,cr=pair_stats(av,bv);covrows.append({"series_a":a,"series_b":b,"covariance":f"{cv:.10g}","correlation":f"{cr:.10g}","observations":len(common)})
    d=root/'data'/'calibration';write_csv(d/'realized_residual_estimates.csv',["parameter","source_series","residual_sd","ar_intercept","ar1","observations","sample_start","sample_end","mapping_status","method"],est);write_csv(d/'realized_residual_covariance.csv',["series_a","series_b","covariance","correlation","observations"],covrows)
    qmanifest=list(csv.DictReader((d/'quarterly_estimation_manifest.csv').open(encoding='utf-8')));gdp=next(r for r in qmanifest if r['source_id']=='statcan_gdp_quarterly')
    manifest=[{"source_id":"statcan_real_gdp_quarterly","agency":"Statistics Canada","dataset":"Table 36-10-0104-01 real GDP (frozen quarterly panel)","url":gdp['url'],"sha256":gdp['sha256']},{"source_id":"imf_fred_real_exports_canada","agency":"International Monetary Fund via FRED","dataset":"NXRSAXDCCAQ real exports of goods and services for Canada","url":FRED_CA_EXPORTS,"sha256":hashlib.sha256(ca_raw).hexdigest()},{"source_id":"bea_fred_real_gdp","agency":"U.S. Bureau of Economic Analysis via FRED","dataset":"A191RL1Q225SBEA real GDP growth","url":FRED_US_GDP,"sha256":hashlib.sha256(usg_raw).hexdigest()},{"source_id":"bea_fred_real_exports","agency":"U.S. Bureau of Economic Analysis via FRED","dataset":"A020RL1Q158SBEA real export growth","url":FRED_US_EXPORTS,"sha256":hashlib.sha256(use_raw).hexdigest()}]
    write_csv(d/'realized_residual_manifest.csv',["source_id","agency","dataset","url","sha256"],manifest)
    evidence=d/'empirical_structural_evidence.csv';lines=evidence.read_text(encoding='utf-8').splitlines();drop=set(PARAM.values());out=[]
    for line in lines:
        if line.startswith('META,registry_id,'):out.append('META,registry_id,v2-empirical-evidence-realized-2026-08-12')
        elif line.startswith('EVIDENCE,') and line.split(',')[1] in drop:continue
        else:out.append(line)
    src={"growth_shock_sd":"statcan_real_gdp_quarterly","us_growth_shock_sd":"bea_fred_real_gdp","export_shock_sd":"imf_fred_real_exports_canada","us_export_shock_sd":"bea_fred_real_exports"}
    for row in est:
        p=row['parameter'];out.append(f'EVIDENCE,{p},empirical-estimate,reference-only,{row["residual_sd"]},{src[p]},{START}-{END},AR(1) realized quarterly innovation SD,"Observed residual scale is statistically anchored but not directly substituted because the simulator shock is conditional on richer structural RHS terms."')
    evidence.write_text('\n'.join(out)+'\n',encoding='utf-8')
    print('realized residuals',[(r['parameter'],r['residual_sd']) for r in est],'common',len(common))
def verify(root):
    d=root/'data'/'calibration';e=list(csv.DictReader((d/'realized_residual_estimates.csv').open(encoding='utf-8')));c=list(csv.DictReader((d/'realized_residual_covariance.csv').open(encoding='utf-8')))
    if len(e)!=4 or len(c)!=16:raise RuntimeError('residual artifact shape mismatch')
    if {r['parameter'] for r in e}!={"growth_shock_sd","us_growth_shock_sd","export_shock_sd","us_export_shock_sd"}:raise RuntimeError('residual parameter mismatch')
    for r in e:
        if r['mapping_status']!='reference-only' or int(r['observations'])<60 or float(r['residual_sd'])<=0:raise RuntimeError('invalid residual estimate')
    for r in c:
        if int(r['observations'])<60 or abs(float(r['correlation']))>1.000001:raise RuntimeError('invalid covariance')
    text=(d/'empirical_structural_evidence.csv').read_text(encoding='utf-8')
    for r in e:
        if f'EVIDENCE,{r["parameter"]},empirical-estimate,reference-only,' not in text:raise RuntimeError('evidence missing')
    print('verified realized residuals',len(e),'estimates',len(c),'covariance cells')
def main():
    p=argparse.ArgumentParser();g=p.add_mutually_exclusive_group(required=True);g.add_argument('--refresh',action='store_true');g.add_argument('--verify',action='store_true');p.add_argument('--root',default='.');a=p.parse_args();root=Path(a.root).resolve();refresh(root) if a.refresh else verify(root)
if __name__=='__main__':main()

#!/usr/bin/env python3
"""Refresh/verify realized quarterly residual diagnostics for V2 structural evidence."""
from __future__ import annotations
import argparse,csv,hashlib,io,math,statistics,time,urllib.request,zipfile
from pathlib import Path

START,END="2001Q1","2019Q4"
STAT_GDP="https://www150.statcan.gc.ca/n1/tbl/csv/36100104-eng.zip"
STAT_EXPORTS="https://www150.statcan.gc.ca/n1/tbl/csv/12100161-eng.zip"
FRED_GDP="https://fred.stlouisfed.org/graph/fredgraph.csv?id=A191RL1Q225SBEA"
FRED_EXPORTS="https://fred.stlouisfed.org/graph/fredgraph.csv?id=A020RL1Q158SBEA"
SERIES=("canada_growth","us_growth","canada_exports","us_exports")
PARAM={"canada_growth":"growth_shock_sd","us_growth":"us_growth_shock_sd","canada_exports":"export_shock_sd","us_exports":"us_export_shock_sd"}


def get(url):
    last=None
    for attempt in range(4):
        try:
            req=urllib.request.Request(url,headers={"User-Agent":"CanadaPolicyStudio/2.0"})
            with urllib.request.urlopen(req,timeout=90) as r:return r.read()
        except Exception as exc:
            last=exc; time.sleep(1.5*(attempt+1))
    raise RuntimeError(f"download failed: {url}: {last}")

def norm(x):return " ".join(str(x).strip().lower().replace("—","-").split())
def qkey(q):return int(q[:4]),int(q[-1])
def qshift(q,n):
    y,k=qkey(q);x=y*4+k-1+n;return f"{x//4}Q{x%4+1}"
def qdate(s):
    y=int(s[:4]);m=int(s[5:7]);return f"{y}Q{(m-1)//3+1}"
def in_sample(q):return qkey(START)<=qkey(q)<=qkey(END)

def bulk(url):
    raw=get(url)
    with zipfile.ZipFile(io.BytesIO(raw)) as z:
        name=max((n for n in z.namelist() if n.lower().endswith('.csv') and 'metadata' not in n.lower()),key=lambda n:z.getinfo(n).file_size)
        rows=list(csv.DictReader(io.StringIO(z.read(name).decode('utf-8-sig'))))
    return rows,raw

def col(row,*tokens):
    for k in row:
        if all(t in norm(k) for t in tokens):return k
    return None

def statcan_levels(rows,kind):
    r=rows[0]; ref=col(r,"ref_date") or col(r,"ref","date"); geo=col(r,"geo"); price=col(r,"price"); sa=col(r,"seasonal"); val=col(r,"value")
    if not all((ref,geo,price,sa,val)):raise RuntimeError(f"unexpected StatCan {kind} schema")
    trade=col(r,"trade") if kind=="exports" else None
    estimate=col(r,"estimate") if kind=="gdp" else None
    out={}
    for row in rows:
        if norm(row[geo])!="canada" or "chained (2017) dollars" not in norm(row[price]) or "seasonally adjusted at annual rates" not in norm(row[sa]):continue
        if kind=="gdp":
            if not estimate or "gross domestic product at market prices" not in norm(row[estimate]):continue
        else:
            if not trade or norm(row[trade])!="exports":continue
            joined=" | ".join(norm(v) for k,v in row.items() if k not in {ref,geo,price,sa,trade,val})
            if "total goods and services" not in joined:continue
        try:v=float(row[val])
        except (TypeError,ValueError):continue
        if v>0:out[qdate(row[ref])]=v
    if len(out)<80:raise RuntimeError(f"insufficient StatCan {kind} levels: {len(out)}")
    return out

def annualized_growth(levels):
    out={}
    for q,v in levels.items():
        p=levels.get(qshift(q,-1))
        if p and v>0 and p>0:out[q]=400*math.log(v/p)
    return out

def fred(raw):
    rows=csv.DictReader(io.StringIO(raw.decode('utf-8-sig')));out={}
    for row in rows:
        try:v=float(next(v for k,v in row.items() if k!="DATE" and v not in (None,"",".")))
        except (StopIteration,ValueError):continue
        out[qdate(row["DATE"])]=v
    return out

def ar1(series):
    obs=[]
    for q in sorted(series,key=qkey):
        p=qshift(q,-1)
        if p in series and in_sample(q) and in_sample(p):obs.append((series[p],series[q],q))
    if len(obs)<60:raise RuntimeError(f"insufficient AR observations: {len(obs)}")
    xs=[x for x,_,_ in obs];ys=[y for _,y,_ in obs];xm=statistics.fmean(xs);ym=statistics.fmean(ys)
    den=sum((x-xm)**2 for x in xs);rho=sum((x-xm)*(y-ym) for x,y in zip(xs,ys))/den if den else 0.;intercept=ym-rho*xm
    res={q:y-intercept-rho*x for x,y,q in obs};sd=statistics.stdev(res.values());return intercept,rho,res,sd

def corr(xs,ys):
    xm=statistics.fmean(xs);ym=statistics.fmean(ys);den=max(1,len(xs)-1)
    cov=sum((x-xm)*(y-ym) for x,y in zip(xs,ys))/den;sx=statistics.stdev(xs);sy=statistics.stdev(ys);return cov/(sx*sy) if sx and sy else 0.,cov

def write_csv(path,fields,rows):
    path.parent.mkdir(parents=True,exist_ok=True)
    with path.open('w',newline='',encoding='utf-8') as f:w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(rows)

def refresh(root):
    gdpr,gdpraw=bulk(STAT_GDP);expr,expraw=bulk(STAT_EXPORTS);usgraw=get(FRED_GDP);useraw=get(FRED_EXPORTS)
    data={"canada_growth":annualized_growth(statcan_levels(gdpr,"gdp")),"us_growth":fred(usgraw),"canada_exports":annualized_growth(statcan_levels(expr,"exports")),"us_exports":fred(useraw)}
    residuals={};est=[]
    for name in SERIES:
        intercept,rho,res,sd=ar1(data[name]);residuals[name]=res
        est.append({"parameter":PARAM[name],"source_series":name,"residual_sd":f"{sd:.10g}","ar_intercept":f"{intercept:.10g}","ar1":f"{rho:.10g}","observations":len(res),"sample_start":START,"sample_end":END,"mapping_status":"reference-only","method":"AR(1) innovation SD from realized quarterly growth"})
    common=sorted(set.intersection(*(set(residuals[n]) for n in SERIES)),key=qkey)
    covrows=[]
    for a in SERIES:
        for b in SERIES:
            av=[residuals[a][q] for q in common];bv=[residuals[b][q] for q in common];c,cv=corr(av,bv)
            covrows.append({"series_a":a,"series_b":b,"covariance":f"{cv:.10g}","correlation":f"{c:.10g}","observations":len(common)})
    data_dir=root/'data'/'calibration'
    write_csv(data_dir/'realized_residual_estimates.csv',["parameter","source_series","residual_sd","ar_intercept","ar1","observations","sample_start","sample_end","mapping_status","method"],est)
    write_csv(data_dir/'realized_residual_covariance.csv',["series_a","series_b","covariance","correlation","observations"],covrows)
    manifest=[
      {"source_id":"statcan_real_gdp_quarterly","agency":"Statistics Canada","dataset":"Table 36-10-0104-01 real GDP","url":STAT_GDP,"sha256":hashlib.sha256(gdpraw).hexdigest()},
      {"source_id":"statcan_real_exports_quarterly","agency":"Statistics Canada","dataset":"Table 12-10-0161-01 real exports","url":STAT_EXPORTS,"sha256":hashlib.sha256(expraw).hexdigest()},
      {"source_id":"bea_fred_real_gdp","agency":"U.S. Bureau of Economic Analysis via FRED","dataset":"A191RL1Q225SBEA real GDP growth","url":FRED_GDP,"sha256":hashlib.sha256(usgraw).hexdigest()},
      {"source_id":"bea_fred_real_exports","agency":"U.S. Bureau of Economic Analysis via FRED","dataset":"A020RL1Q158SBEA real export growth","url":FRED_EXPORTS,"sha256":hashlib.sha256(useraw).hexdigest()}]
    write_csv(data_dir/'realized_residual_manifest.csv',["source_id","agency","dataset","url","sha256"],manifest)
    evidence=data_dir/'empirical_structural_evidence.csv';lines=evidence.read_text(encoding='utf-8').splitlines();drop=set(PARAM.values());out=[]
    for line in lines:
        if line.startswith('META,registry_id,'):out.append('META,registry_id,v2-empirical-evidence-realized-2026-08-12')
        elif line.startswith('EVIDENCE,') and line.split(',')[1] in drop:continue
        else:out.append(line)
    src={"growth_shock_sd":"statcan_real_gdp_quarterly","us_growth_shock_sd":"bea_fred_real_gdp","export_shock_sd":"statcan_real_exports_quarterly","us_export_shock_sd":"bea_fred_real_exports"}
    for row in est:
        p=row['parameter'];out.append(f'EVIDENCE,{p},empirical-estimate,reference-only,{row["residual_sd"]},{src[p]},{START}-{END},AR(1) realized quarterly innovation SD,"Observed residual scale is statistically anchored but not directly substituted because the simulator shock is conditional on richer structural RHS terms."')
    evidence.write_text('\n'.join(out)+'\n',encoding='utf-8')
    print('realized residual calibration refreshed',[(r['parameter'],r['residual_sd']) for r in est],f'common={len(common)}')

def verify(root):
    d=root/'data'/'calibration';ests=list(csv.DictReader((d/'realized_residual_estimates.csv').open(encoding='utf-8')));cov=list(csv.DictReader((d/'realized_residual_covariance.csv').open(encoding='utf-8')))
    if len(ests)!=4 or len(cov)!=16:raise RuntimeError('realized residual artifact shape mismatch')
    if {r['parameter'] for r in ests}!={"growth_shock_sd","us_growth_shock_sd","export_shock_sd","us_export_shock_sd"}:raise RuntimeError('realized residual parameter mismatch')
    for r in ests:
        if r['mapping_status']!='reference-only' or int(r['observations'])<60 or not math.isfinite(float(r['residual_sd'])) or float(r['residual_sd'])<=0:raise RuntimeError('invalid realized residual estimate')
    for r in cov:
        c=float(r['correlation']);
        if int(r['observations'])<60 or not -1.0000001<=c<=1.0000001:raise RuntimeError('invalid residual covariance')
    text=(d/'empirical_structural_evidence.csv').read_text(encoding='utf-8')
    for p in {r['parameter'] for r in ests}:
        if f'EVIDENCE,{p},empirical-estimate,reference-only,' not in text:raise RuntimeError(f'missing evidence {p}')
    print('verified realized residual calibration',len(ests),'estimates',len(cov),'covariance cells')

def main():
    p=argparse.ArgumentParser();g=p.add_mutually_exclusive_group(required=True);g.add_argument('--refresh',action='store_true');g.add_argument('--verify',action='store_true');p.add_argument('--root',default='.');a=p.parse_args();root=Path(a.root).resolve();refresh(root) if a.refresh else verify(root)
if __name__=='__main__':main()

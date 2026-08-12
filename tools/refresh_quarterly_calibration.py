#!/usr/bin/env python3
"""Build a frozen public-data quarterly panel and production-compatible estimates.

Network access is used only with --refresh. --verify re-estimates committed files
without network access so ordinary CI remains deterministic.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import math
import statistics
import sys
import urllib.error
import urllib.request
import zipfile
from collections import defaultdict
from pathlib import Path

BOC = "https://www.bankofcanada.ca/valet/observations/{series}/json?start_date=1999-01-01&end_date=2019-12-31"
STATCAN_GDP = "https://www150.statcan.gc.ca/n1/en/tbl/csv/36100104-eng.zip"
STATCAN_LFS = "https://www150.statcan.gc.ca/n1/en/tbl/csv/14100287-eng.zip"
SERIES = {
    "output_gap": "INDINF_OUTGAPR_Q",       # historical MPR output gap, no revisions
    "cpi_trim": "INDINF_CPI_TRIM_Q",
    "cpi_median": "INDINF_CPI_MEDIAN_Q",
    "policy_rate": "V39079",
    "usdcad": "FXUSDCAD",
    "wti": "WTI",
}
SAMPLE_START = "2001Q1"
SAMPLE_END = "2019Q4"
NEUTRAL_RATE = 2.75


def request_bytes(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "CanadaPolicyStudio/2.0"})
    with urllib.request.urlopen(req, timeout=90) as response:
        return response.read()


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def quarter_from_date(text: str) -> str:
    text = text.strip()
    if "Q" in text.upper():
        clean = text.upper().replace("-", "")
        year = clean[:4]
        qpos = clean.find("Q")
        return f"{year}Q{clean[qpos + 1]}"
    year = int(text[:4])
    month = int(text[5:7])
    return f"{year}Q{(month - 1) // 3 + 1}"


def quarter_key(q: str):
    return int(q[:4]), int(q[-1])


def previous_quarter(q: str) -> str:
    year, quarter = quarter_key(q)
    if quarter == 1:
        return f"{year - 1}Q4"
    return f"{year}Q{quarter - 1}"


def valet_series(series: str):
    candidates = [series]
    if series.lower() != series:
        candidates.append(series.lower())
    last_error = None
    for candidate in candidates:
        url = BOC.format(series=candidate)
        try:
            raw = request_bytes(url)
        except urllib.error.HTTPError as exc:
            last_error = exc
            continue
        payload = json.loads(raw)
        points = []
        for observation in payload.get("observations", []):
            date = observation.get("d", "")
            cell = (observation.get(series) or observation.get(series.upper())
                    or observation.get(series.lower()) or observation.get(candidate))
            if not date or not isinstance(cell, dict):
                continue
            value = cell.get("v")
            try:
                points.append((date, float(value)))
            except (TypeError, ValueError):
                pass
        if points:
            return points, raw, url
    if last_error:
        raise RuntimeError(f"Bank of Canada series {series} failed at {BOC.format(series=series)}: {last_error}")
    raise RuntimeError(f"Bank of Canada series {series} returned no numeric observations")


def quarterly_aggregate(points, mode: str):
    groups = defaultdict(list)
    for date, value in points:
        groups[quarter_from_date(date)].append((date, value))
    out = {}
    for q, values in groups.items():
        values.sort()
        if mode == "last":
            out[q] = values[-1][1]
        elif mode == "mean":
            out[q] = statistics.fmean(v for _, v in values)
        else:
            raise ValueError(mode)
    return out


def read_statcan_zip(url: str):
    raw = request_bytes(url)
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        names = [n for n in archive.namelist() if n.lower().endswith(".csv") and "metadata" not in n.lower()]
        if not names:
            raise RuntimeError(f"No data CSV in {url}")
        name = max(names, key=lambda n: archive.getinfo(n).file_size)
        text = archive.read(name).decode("utf-8-sig")
    return list(csv.DictReader(io.StringIO(text))), raw


def n(value: str) -> str:
    return " ".join(str(value).strip().lower().replace("—", "-").split())


def column(row, *needles):
    for key in row:
        nk = n(key)
        if all(token in nk for token in needles):
            return key
    return None


def statcan_gdp(rows):
    if not rows:
        raise RuntimeError("Empty Statistics Canada GDP table")
    sample = rows[0]
    ref = column(sample, "ref_date") or column(sample, "ref", "date")
    geo = column(sample, "geo")
    estimate = column(sample, "estimate")
    prices = column(sample, "price")
    seasonal = column(sample, "seasonal")
    value = column(sample, "value")
    if not all([ref, geo, estimate, prices, seasonal, value]):
        raise RuntimeError(f"Unexpected GDP schema: {list(sample)}")
    levels = {}
    for row in rows:
        if n(row[geo]) != "canada":
            continue
        if "gross domestic product at market prices" not in n(row[estimate]):
            continue
        if "chained" not in n(row[prices]):
            continue
        if "seasonally adjusted at annual rates" not in n(row[seasonal]):
            continue
        try:
            levels[quarter_from_date(row[ref])] = float(row[value])
        except (ValueError, TypeError):
            pass
    if len(levels) < 40:
        raise RuntimeError(f"GDP filter found only {len(levels)} quarters")
    growth = {}
    for q, level in levels.items():
        prev = levels.get(previous_quarter(q))
        if prev and prev > 0 and level > 0:
            growth[q] = 400.0 * math.log(level / prev)
    return growth


def statcan_unemployment(rows):
    if not rows:
        raise RuntimeError("Empty Statistics Canada labour table")
    sample = rows[0]
    ref = column(sample, "ref_date") or column(sample, "ref", "date")
    geo = column(sample, "geo")
    characteristic = column(sample, "labour", "force", "characteristic")
    age = column(sample, "age")
    gender = column(sample, "gender") or column(sample, "sex")
    data_type = column(sample, "data", "type")
    stats = column(sample, "statistic")
    value = column(sample, "value")
    required = [ref, geo, characteristic, age, gender, data_type, value]
    if not all(required):
        raise RuntimeError(f"Unexpected labour schema: {list(sample)}")
    months = defaultdict(list)
    for row in rows:
        if n(row[geo]) != "canada":
            continue
        if n(row[characteristic]) != "unemployment rate":
            continue
        if "15 years and over" not in n(row[age]):
            continue
        if not ("total" in n(row[gender]) or "both" in n(row[gender])):
            continue
        if "seasonally adjusted" not in n(row[data_type]):
            continue
        if stats and n(row[stats]) not in ("estimate", "value"):
            continue
        try:
            months[quarter_from_date(row[ref])].append(float(row[value]))
        except (ValueError, TypeError):
            pass
    out = {q: statistics.fmean(v) for q, v in months.items() if v}
    if len(out) < 40:
        raise RuntimeError(f"Unemployment filter found only {len(out)} quarters")
    return out


def solve_linear(a, b):
    nrows = len(a)
    aug = [list(a[i]) + [b[i]] for i in range(nrows)]
    for col in range(nrows):
        pivot = max(range(col, nrows), key=lambda r: abs(aug[r][col]))
        if abs(aug[pivot][col]) < 1e-12:
            raise RuntimeError("Singular normal equation")
        aug[col], aug[pivot] = aug[pivot], aug[col]
        scale = aug[col][col]
        aug[col] = [x / scale for x in aug[col]]
        for row in range(nrows):
            if row == col:
                continue
            f = aug[row][col]
            aug[row] = [aug[row][j] - f * aug[col][j] for j in range(nrows + 1)]
    return [aug[i][-1] for i in range(nrows)]


def invert_matrix(a):
    nrows = len(a)
    aug = [list(a[i]) + [1.0 if i == j else 0.0 for j in range(nrows)] for i in range(nrows)]
    for col in range(nrows):
        pivot = max(range(col, nrows), key=lambda r: abs(aug[r][col]))
        if abs(aug[pivot][col]) < 1e-12:
            raise RuntimeError("Singular covariance matrix")
        aug[col], aug[pivot] = aug[pivot], aug[col]
        scale = aug[col][col]
        aug[col] = [x / scale for x in aug[col]]
        for row in range(nrows):
            if row == col:
                continue
            f = aug[row][col]
            aug[row] = [aug[row][j] - f * aug[col][j] for j in range(2 * nrows)]
    return [row[nrows:] for row in aug]


def ols(x, y):
    if len(x) != len(y) or not x:
        raise RuntimeError("Invalid OLS sample")
    k = len(x[0])
    xtx = [[sum(row[i] * row[j] for row in x) for j in range(k)] for i in range(k)]
    xty = [sum(row[i] * yy for row, yy in zip(x, y)) for i in range(k)]
    beta = solve_linear(xtx, xty)
    residuals = [yy - sum(b * v for b, v in zip(beta, row)) for row, yy in zip(x, y)]
    dof = max(1, len(y) - k)
    sigma2 = sum(r * r for r in residuals) / dof
    inv = invert_matrix(xtx)
    se = [math.sqrt(max(0.0, sigma2 * inv[i][i])) for i in range(k)]
    residual_sd = math.sqrt(sum(r * r for r in residuals) / max(1, len(residuals) - 1))
    return beta, se, residuals, residual_sd


def policy_grid(rows):
    obs = []
    byq = {r["quarter"]: r for r in rows}
    for row in rows:
        prev = byq.get(previous_quarter(row["quarter"]))
        if prev:
            obs.append((float(prev["policy_rate"]), float(row["policy_rate"]),
                        float(row["core_inflation"]), float(row["output_gap"])))
    if len(obs) < 40:
        raise RuntimeError("Insufficient policy-rule observations")
    best = None
    center = None
    for stage in range(3):
        if stage == 0:
            alphas = [0.25 + .10 * i for i in range(23)]
            betas = [0.0 + .05 * i for i in range(21)]
            steps = [.125 + .025 * i for i in range(36)]
        else:
            a0, b0, s0 = center
            da = .025 if stage == 1 else .005
            db = .0125 if stage == 1 else .0025
            ds = .0125 if stage == 1 else .0025
            alphas = [max(.01, a0 + da * i) for i in range(-4, 5)]
            betas = [max(0.0, b0 + db * i) for i in range(-4, 5)]
            steps = [max(.025, s0 + ds * i) for i in range(-4, 5)]
        for alpha in alphas:
            for beta in betas:
                targets = [NEUTRAL_RATE + alpha * (infl - 2.0) + beta * gap
                           for _, _, infl, gap in obs]
                for step in steps:
                    sse = 0.0
                    for (prev, actual, _, _), target in zip(obs, targets):
                        delta = max(-step, min(step, target - prev))
                        pred = max(0.0, min(8.0, prev + delta))
                        sse += (actual - pred) ** 2
                    if best is None or sse < best[0]:
                        best = (sse, alpha, beta, step)
        center = best[1:]
    sse, alpha, beta, step = best
    rmse = math.sqrt(sse / len(obs))
    return alpha, beta, step, rmse, len(obs)


def estimate(rows):
    rows = [r for r in rows if SAMPLE_START <= r["quarter"] <= SAMPLE_END]
    rows.sort(key=lambda r: quarter_key(r["quarter"]))
    if len(rows) < 60:
        raise RuntimeError(f"Only {len(rows)} complete quarterly observations")
    byq = {r["quarter"]: r for r in rows}
    x_gap, y_gap, q_gap = [], [], []
    for row in rows:
        prev = byq.get(previous_quarter(row["quarter"]))
        if not prev:
            continue
        x_gap.append([float(prev["output_gap"]), -(float(row["policy_rate"]) - NEUTRAL_RATE)])
        y_gap.append(float(row["output_gap"]))
        q_gap.append(row["quarter"])
    b_gap, se_gap, resid_gap, sd_gap = ols(x_gap, y_gap)
    x_pi, y_pi, q_pi = [], [], []
    for row in rows:
        prev = byq.get(previous_quarter(row["quarter"]))
        if not prev:
            continue
        x_pi.append([float(prev["core_inflation"]) - 2.0, float(row["output_gap"]),
                     float(row["usdcad"]) - 1.34, -(float(row["wti"]) - 75.0)])
        y_pi.append(float(row["core_inflation"]) - 2.0)
        q_pi.append(row["quarter"])
    b_pi, se_pi, resid_pi, sd_pi = ols(x_pi, y_pi)
    alpha, beta_rule, step, rule_rmse, n_policy = policy_grid(rows)
    gap_by_q = dict(zip(q_gap, resid_gap))
    pi_by_q = dict(zip(q_pi, resid_pi))
    common = sorted(set(gap_by_q) & set(pi_by_q), key=quarter_key)
    rg = [gap_by_q[q] for q in common]
    rp = [pi_by_q[q] for q in common]
    mg, mp = statistics.fmean(rg), statistics.fmean(rp)
    denom = max(1, len(common) - 1)
    cov = sum((a - mg) * (b - mp) for a, b in zip(rg, rp)) / denom
    sdg = math.sqrt(sum((a - mg) ** 2 for a in rg) / denom)
    sdp = math.sqrt(sum((b - mp) ** 2 for b in rp) / denom)
    corr = cov / (sdg * sdp) if sdg > 0 and sdp > 0 else 0.0

    def rec(parameter, value, se, lo, hi, nobs, method, eligible, notes):
        return {"parameter": parameter, "estimate": value, "standard_error": se,
                "lower_bound": lo, "upper_bound": hi, "observations": nobs,
                "sample_start": rows[0]["quarter"], "sample_end": rows[-1]["quarter"],
                "method": method, "direct_eligible": "true" if eligible else "false", "notes": notes}
    estimates = [
        rec("output_persistence", b_gap[0], se_gap[0], max(.05, b_gap[0]-1.96*se_gap[0]), min(.98, b_gap[0]+1.96*se_gap[0]), len(y_gap), "OLS production-form output-gap equation", .05 < b_gap[0] < .98, "No intercept; policy wedge uses official neutral-rate midpoint."),
        rec("real_rate_demand_sensitivity", b_gap[1], se_gap[1], max(.001, b_gap[1]-1.96*se_gap[1]), max(.002, b_gap[1]+1.96*se_gap[1]), len(y_gap), "OLS production-form output-gap equation", b_gap[1] > 0, "Coefficient on -(policy rate-neutral rate); omitted fiscal/trade/global terms remain in residual."),
        rec("output_shock_sd", sd_gap, 0.0, max(.001,.70*sd_gap), 1.30*sd_gap, len(y_gap), "sample SD of output-equation residual", sd_gap > 0, "Residual innovation scale; bounds +/-30% around fitted SD."),
        rec("inflation_persistence", b_pi[0], se_pi[0], max(.05,b_pi[0]-1.96*se_pi[0]), min(.98,b_pi[0]+1.96*se_pi[0]), len(y_pi), "OLS production-form centered inflation equation", .05 < b_pi[0] < .98, "Expectations weight remains derived as one minus persistence."),
        rec("phillips_curve_slope", b_pi[1], se_pi[1], max(.001,b_pi[1]-1.96*se_pi[1]), max(.002,b_pi[1]+1.96*se_pi[1]), len(y_pi), "OLS production-form centered inflation equation", b_pi[1] > 0, "Contemporaneous historical MPR output-gap coefficient."),
        rec("fx_pass_through", b_pi[2], se_pi[2], b_pi[2]-1.96*se_pi[2], b_pi[2]+1.96*se_pi[2], len(y_pi), "OLS production-form centered inflation equation", b_pi[2] > 0, "Coefficient on quarterly-average USD/CAD minus 1.34."),
        rec("oil_inflation_sensitivity", b_pi[3], se_pi[3], b_pi[3]-1.96*se_pi[3], b_pi[3]+1.96*se_pi[3], len(y_pi), "OLS production-form centered inflation equation", True, "Uses engine sign convention -coefficient*(WTI-75); signed estimate allowed."),
        rec("inflation_shock_sd", sd_pi, 0.0, max(.001,.70*sd_pi), 1.30*sd_pi, len(y_pi), "sample SD of inflation-equation residual", sd_pi > 0, "Residual innovation scale; bounds +/-30% around fitted SD."),
        rec("rate_inflation_response", alpha, 0.0, max(.01,.65*alpha), 1.35*alpha, n_policy, "grid fit of exact clipped production policy rule", alpha > 0, f"Policy-rule in-sample RMSE={rule_rmse:.4f} pp."),
        rec("rate_output_response", beta_rule, 0.0, max(0.0,.65*beta_rule), max(.01,1.35*beta_rule), n_policy, "grid fit of exact clipped production policy rule", beta_rule >= 0, f"Policy-rule in-sample RMSE={rule_rmse:.4f} pp."),
        rec("max_quarterly_rate_step", step, 0.0, max(.025,.75*step), 1.25*step, n_policy, "grid fit of exact clipped production policy rule", step > 0, f"Policy-rule in-sample RMSE={rule_rmse:.4f} pp."),
    ]
    covariance = [
        {"row":"output_gap_residual","column":"output_gap_residual","covariance":sdg*sdg,"correlation":1.0,"observations":len(common)},
        {"row":"output_gap_residual","column":"inflation_residual","covariance":cov,"correlation":corr,"observations":len(common)},
        {"row":"inflation_residual","column":"output_gap_residual","covariance":cov,"correlation":corr,"observations":len(common)},
        {"row":"inflation_residual","column":"inflation_residual","covariance":sdp*sdp,"correlation":1.0,"observations":len(common)},
    ]
    return estimates, covariance


def write_csv(path: Path, fieldnames, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            cleaned = {key: (f"{row.get(key):.10g}" if isinstance(row.get(key), float) else row.get(key, "")) for key in fieldnames}
            writer.writerow(cleaned)


def read_panel(path: Path):
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def refresh(root: Path):
    collected = {}
    manifest = []
    modes = {"output_gap":"last","cpi_trim":"last","cpi_median":"last","policy_rate":"last","usdcad":"mean","wti":"mean"}
    for field, series in SERIES.items():
        points, raw, url = valet_series(series)
        collected[field] = quarterly_aggregate(points, modes[field])
        manifest.append({"source_id":f"boc_valet_{field}","agency":"Bank of Canada","dataset":series,"url":url,"sha256":sha256(raw),"transformation":f"quarterly {modes[field]}"})
    gdp_rows, gdp_raw = read_statcan_zip(STATCAN_GDP)
    lfs_rows, lfs_raw = read_statcan_zip(STATCAN_LFS)
    collected["gdp_growth"] = statcan_gdp(gdp_rows)
    collected["unemployment"] = statcan_unemployment(lfs_rows)
    manifest += [
        {"source_id":"statcan_gdp_quarterly","agency":"Statistics Canada","dataset":"36-10-0104-01","url":STATCAN_GDP,"sha256":sha256(gdp_raw),"transformation":"400*log(q/q-1) chained-dollar GDP"},
        {"source_id":"statcan_unemployment_monthly","agency":"Statistics Canada","dataset":"14-10-0287-01","url":STATCAN_LFS,"sha256":sha256(lfs_raw),"transformation":"quarterly mean seasonally adjusted unemployment rate"},
    ]
    quarters = sorted(set.intersection(*(set(v) for v in collected.values())), key=quarter_key)
    panel = []
    for q in quarters:
        trim, median = collected["cpi_trim"][q], collected["cpi_median"][q]
        panel.append({"quarter":q,"output_gap":collected["output_gap"][q],"core_inflation":.5*(trim+median),"cpi_trim":trim,"cpi_median":median,"policy_rate":collected["policy_rate"][q],"usdcad":collected["usdcad"][q],"wti":collected["wti"][q],"gdp_growth":collected["gdp_growth"][q],"unemployment":collected["unemployment"][q]})
    sample_count = len([r for r in panel if SAMPLE_START <= r["quarter"] <= SAMPLE_END])
    if sample_count < 60:
        spans = {k:(min(v,key=quarter_key),max(v,key=quarter_key),len(v)) for k,v in collected.items() if v}
        raise RuntimeError(f"Insufficient complete sample; count={sample_count} spans={spans}")
    estimates, covariance = estimate(panel)
    data = root / "data" / "calibration"
    write_csv(data/"quarterly_estimation_panel.csv", ["quarter","output_gap","core_inflation","cpi_trim","cpi_median","policy_rate","usdcad","wti","gdp_growth","unemployment"], panel)
    write_csv(data/"quarterly_estimation_manifest.csv", ["source_id","agency","dataset","url","sha256","transformation"], manifest)
    write_csv(data/"quarterly_structural_estimates.csv", ["parameter","estimate","standard_error","lower_bound","upper_bound","observations","sample_start","sample_end","method","direct_eligible","notes"], estimates)
    write_csv(data/"quarterly_residual_covariance.csv", ["row","column","covariance","correlation","observations"], covariance)
    print(f"panel_rows={len(panel)} sample_rows={sample_count}")
    for e in estimates:
        print(f"ESTIMATE {e['parameter']}={e['estimate']:.8g} se={e['standard_error']:.8g} eligible={e['direct_eligible']}")
    print(f"RESIDUAL_CORRELATION={covariance[1]['correlation']:.8g}")


def verify(root: Path):
    data = root/"data"/"calibration"
    panel = read_panel(data/"quarterly_estimation_panel.csv")
    expected, covariance = estimate(panel)
    with (data/"quarterly_structural_estimates.csv").open(newline="",encoding="utf-8") as f:
        committed = {r["parameter"]:r for r in csv.DictReader(f)}
    for row in expected:
        old = committed.get(row["parameter"])
        if not old: raise RuntimeError(f"Missing committed estimate {row['parameter']}")
        if abs(float(old["estimate"])-row["estimate"]) > 5e-8: raise RuntimeError(f"Estimate drift {row['parameter']}: {old['estimate']} vs {row['estimate']}")
        if old["direct_eligible"] != row["direct_eligible"]: raise RuntimeError(f"Eligibility drift {row['parameter']}")
    with (data/"quarterly_residual_covariance.csv").open(newline="",encoding="utf-8") as f:
        committed_cov = list(csv.DictReader(f))
    if len(committed_cov) != 4: raise RuntimeError("Residual covariance must be 2x2")
    if abs(float(committed_cov[1]["correlation"])-covariance[1]["correlation"]) > 5e-8: raise RuntimeError("Residual correlation drift")
    print(f"verified_quarterly_panel rows={len(panel)} direct_eligible={sum(e['direct_eligible']=='true' for e in expected)}")


def main():
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--refresh", action="store_true")
    mode.add_argument("--verify", action="store_true")
    parser.add_argument("--root", default=".")
    args = parser.parse_args()
    root = Path(args.root).resolve()
    refresh(root) if args.refresh else verify(root)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"quarterly calibration error: {exc}", file=sys.stderr)
        raise

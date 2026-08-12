#!/usr/bin/env python3
from pathlib import Path
import csv
import io
import re

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "calibration"


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def write(path, content):
    path.write_text(content, encoding="utf-8")


def patch_refresh_tool():
    path = ROOT / "tools" / "refresh_sep_calibration.py"
    text = path.read_text(encoding="utf-8")
    pattern = r'def parse_valet\(raw,series\):\n.*?\ndef fetch_series\(q,field,prefix\):'
    replacement = '''def parse_valet(raw,series):
    out={}
    for o in json.loads(raw).get("observations",[]):
        d=str(o.get("d","")); cell=o.get(series) or o.get(series.lower()) or o.get(series.upper())
        if not isinstance(cell,dict):continue
        try:value=float(cell["v"])
        except (KeyError,TypeError,ValueError):continue
        if "Q" in d.upper():q=d[:6].replace("-","").upper()
        elif len(d)>=7 and d[:4].isdigit() and d[5:7].isdigit():
            y,m=int(d[:4]),int(d[5:7]);q=f"{y}Q{(m-1)//3+1}"
        else:continue
        out[q]=value
    return out

def fetch_series(q,field,prefix):'''
    text, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise RuntimeError("Unable to patch SEP date parser")
    text = replace_once(text,
        'R("output_shock_sd",sdg,0,max(.001,.7*sdg),1.3*sdg,len(yg),"residual SD of output equation",sdg>0,"Quarterly residual innovation")',
        'R("output_shock_sd",sdg,0,max(.001,.7*sdg),1.3*sdg,len(yg),"residual SD of output equation",False,"SEP current-quarter residual scale retained as reference-only; not treated as realized macro shock variance")',
        "output shock eligibility")
    text = replace_once(text,
        'R("phillips_curve_slope",bp[1],sep[1],bp[1]-1.96*sep[1],bp[1]+1.96*sep[1],len(yp),"OLS production-form centered inflation equation",bp[1]>0,"Contemporaneous SEP output gap")',
        'R("phillips_curve_slope",bp[1],sep[1],bp[1]-1.96*sep[1],bp[1]+1.96*sep[1],len(yp),"OLS production-form centered inflation equation",bp[1]-1.96*sep[1]>0,"Contemporaneous SEP output gap; direct promotion requires a positive 95% interval")',
        "Phillips eligibility")
    text = replace_once(text,
        'R("inflation_shock_sd",sdp,0,max(.001,.7*sdp),1.3*sdp,len(yp),"residual SD of inflation equation",sdp>0,"Quarterly residual innovation")',
        'R("inflation_shock_sd",sdp,0,max(.001,.7*sdp),1.3*sdp,len(yp),"residual SD of inflation equation",False,"SEP current-quarter residual scale retained as reference-only; not treated as realized macro shock variance")',
        "inflation shock eligibility")
    text = replace_once(text,
        'R("rate_inflation_response",a,0,max(.01,.65*a),1.35*a,np,"grid fit exact clipped production policy rule",a>0 and rmse<1,f"SEP implied-rate path RMSE={rmse:.4f} pp")',
        'R("rate_inflation_response",a,0,max(.01,.65*a),1.35*a,np,"grid fit exact clipped production policy rule",False,f"SEP implied-rate path RMSE={rmse:.4f} pp; staff path is not a Governing Council reaction function")',
        "policy inflation eligibility")
    text = replace_once(text,
        'R("rate_output_response",b,0,max(.001,.65*b),max(.002,1.35*b),np,"grid fit exact clipped production policy rule",b>0 and rmse<1,f"SEP implied-rate path RMSE={rmse:.4f} pp")',
        'R("rate_output_response",b,0,max(.001,.65*b),max(.002,1.35*b),np,"grid fit exact clipped production policy rule",False,f"SEP implied-rate path RMSE={rmse:.4f} pp; boundary estimate retained as reference-only")',
        "policy output eligibility")
    text = replace_once(text,
        'R("max_quarterly_rate_step",s,0,max(.025,.75*s),1.25*s,np,"grid fit exact clipped production policy rule",s>0 and rmse<1,f"SEP implied-rate path RMSE={rmse:.4f} pp")',
        'R("max_quarterly_rate_step",s,0,max(.025,.75*s),1.25*s,np,"grid fit exact clipped production policy rule",False,f"SEP implied-rate path RMSE={rmse:.4f} pp; staff path is not an observed policy-adjustment constraint")',
        "policy step eligibility")
    write(path, text)


def patch_frozen_estimates():
    path = DATA / "quarterly_structural_estimates.csv"
    rows = list(csv.DictReader(path.open(newline="", encoding="utf-8")))
    safe = {"output_persistence", "inflation_persistence"}
    for row in rows:
        row["direct_eligible"] = "true" if row["parameter"] in safe else "false"
        if row["parameter"] == "output_shock_sd":
            row["notes"] = "SEP current-quarter residual scale retained as reference-only; not treated as realized macro shock variance"
        elif row["parameter"] == "inflation_shock_sd":
            row["notes"] = "SEP current-quarter residual scale retained as reference-only; not treated as realized macro shock variance"
        elif row["parameter"] == "phillips_curve_slope":
            row["notes"] = "Contemporaneous SEP output gap; 95% interval crosses zero so direct promotion is rejected"
        elif row["parameter"] in {"rate_inflation_response", "rate_output_response", "max_quarterly_rate_step"}:
            row["notes"] += "; staff implied-rate path retained as reference-only"
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=rows[0].keys())
        w.writeheader(); w.writerows(rows)


def patch_structural_registry():
    path = DATA / "structural_parameter_registry.csv"
    lines = path.read_text(encoding="utf-8").splitlines()
    output = []
    for line in lines:
        if line.startswith("META,registry_id,"):
            output.append("META,registry_id,v2-structural-quarterly-2026-08-12")
        elif line.startswith("META,as_of,"):
            output.append("META,as_of,2026-08-12")
        elif line.startswith("PARAM,output_persistence,"):
            output.append('PARAM,output_persistence,0.8802835399,coefficient,empirical_estimate,boc_sep_quarterly_estimation,2001Q1-2019Q4,0.7743734997,0.98,normal,0.061386,true,"Direct production-form OLS estimate from 75 consecutive real-time Bank staff vintages; 95% interval bounded below unity."')
        elif line.startswith("PARAM,inflation_persistence,"):
            output.append('PARAM,inflation_persistence,0.7371310581,coefficient,empirical_estimate,boc_sep_quarterly_estimation,2001Q1-2019Q4,0.5832242288,0.8910378873,normal,0.106527,true,"Direct production-form centered-inflation OLS estimate from 75 consecutive real-time Bank staff vintages."')
        elif line.startswith("PARAM,inflation_expectations_weight,"):
            output.append('PARAM,inflation_expectations_weight,0.2628689419,coefficient,derived,boc_sep_quarterly_estimation,2001Q1-2019Q4,0.1089621127,0.4167757712,derived,0,false,"Derived as one minus the directly estimated inflation persistence; not sampled independently."')
        else:
            output.append(line)
    write(path, "\n".join(output) + "\n")


def rewrite_empirical_evidence():
    path = DATA / "empirical_structural_evidence.csv"
    content = '''# EVIDENCE,parameter,evidence_tier,mapping_status,anchor_value,source_id,sample_period,method,notes
META,registry_id,v2-empirical-evidence-quarterly-2026-08-12
META,as_of,2026-08-12
EVIDENCE,neutral_rate,official-model-estimate,direct,2.75,boc_neutral_rate_2026,2026 assessment,multi-model official range,"Directly compatible with the model's nominal neutral-rate concept; midpoint of the Bank's 2.25%-3.25% range."
EVIDENCE,output_persistence,empirical-estimate,direct,0.8802835399,boc_sep_quarterly_estimation,2001Q1-2019Q4,OLS production-form output-gap equation,"Direct estimate from 75 consecutive real-time Bank staff vintages; SE 0.0540."
EVIDENCE,real_rate_demand_sensitivity,empirical-estimate,reference-only,-0.0003497742,boc_sep_quarterly_estimation,2001Q1-2019Q4,OLS production-form output-gap equation,"Sign conflicts with the model's positive demand-sensitivity restriction; retained as falsifying evidence rather than force-fit."
EVIDENCE,output_shock_sd,empirical-estimate,reference-only,0.004688623,boc_sep_quarterly_estimation,2001Q1-2019Q4,output-equation residual SD,"Staff current-quarter gap estimates are too smooth to identify realized macro innovation variance; retained as a lower-bound diagnostic."
EVIDENCE,rate_inflation_response,empirical-estimate,reference-only,0.13,boc_sep_quarterly_estimation,2001Q1-2019Q4,grid fit of exact clipped production policy rule,"Fits the staff implied-rate path with 0.0655 pp RMSE but is not a Governing Council reaction function, so it does not overwrite the production coefficient."
EVIDENCE,rate_output_response,empirical-estimate,reference-only,0,boc_sep_quarterly_estimation,2001Q1-2019Q4,grid fit of exact clipped production policy rule,"Boundary estimate from the staff implied-rate path; retained as reference-only."
EVIDENCE,max_quarterly_rate_step,empirical-estimate,reference-only,0.065,boc_sep_quarterly_estimation,2001Q1-2019Q4,grid fit of exact clipped production policy rule,"Staff implied-rate adjustment speed is not an observed Governing Council constraint; retained as reference-only."
EVIDENCE,inflation_persistence,empirical-estimate,direct,0.7371310581,boc_sep_quarterly_estimation,2001Q1-2019Q4,OLS production-form centered inflation equation,"Direct estimate from 75 consecutive real-time Bank staff vintages; SE 0.0785; expectations weight is derived as one minus persistence."
EVIDENCE,phillips_curve_slope,empirical-estimate,reference-only,3.951477288,boc_sep_quarterly_estimation,2001Q1-2019Q4,OLS production-form centered inflation equation,"95% interval crosses zero (-1.75 to 9.65); retained as weak evidence and not promoted."
EVIDENCE,inflation_shock_sd,empirical-estimate,reference-only,0.25288263,boc_sep_quarterly_estimation,2001Q1-2019Q4,inflation-equation residual SD,"Staff current-quarter residual variance is not equated with realized structural inflation shocks."
EVIDENCE,fx_pass_through,empirical-estimate,reference-only,-0.000855721,boc_sep_quarterly_estimation,2001Q1-2019Q4,OLS production-form centered inflation equation,"Near-zero negative estimate with wide interval; omitted oil/import/supply terms make direct promotion inappropriate."
EVIDENCE,import_price_pass_through,empirical-estimate,reference-only,0.24,boc_tariff_passthrough_2026,2025-2026,difference-in-differences retail price study,"Tariffed-goods prices rose about 6% under a 25% tariff, implying roughly one-quarter retail pass-through; aggregate model coefficient has a different incidence mapping."
'''
    write(path, content)


def append_source_registry():
    path = DATA / "source_registry.csv"
    text = path.read_text(encoding="utf-8")
    if "boc_sep_quarterly_estimation," not in text:
        text += 'boc_sep_quarterly_estimation,Bank of Canada,Staff Economic Projections real-time quarterly vintages,empirical_estimation,quarterly,public_valet,https://www.bankofcanada.ca/rates/staff-economic-projections/,"Frozen 2001Q1-2019Q4 real-time panel using SWP-LYGAP, SWP-PCPIX, SWP-R1N and SWP-USCANPFX vintage series. Production baselines are promoted only when the estimand matches the engine equation and statistical diagnostics pass explicit guards."\n'
    write(path, text)


def create_quarterly_header():
    path = ROOT / "include" / "quarterly_empirical_calibration.hpp"
    content = r'''#pragma once

#include "calibration.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace cad {

struct QuarterlyStructuralEstimate {
  std::string parameter;
  double estimate = 0.0;
  double standard_error = 0.0;
  double lower_bound = 0.0;
  double upper_bound = 0.0;
  int observations = 0;
  std::string sample_start;
  std::string sample_end;
  std::string method;
  bool direct_eligible = false;
  std::string notes;
};

struct QuarterlyStructuralEstimation {
  bool loaded = false;
  std::vector<QuarterlyStructuralEstimate> estimates;
  double output_inflation_residual_correlation = 0.0;
  int residual_covariance_observations = 0;

  const QuarterlyStructuralEstimate* find(const std::string& name) const {
    for (const auto& e : estimates) if (e.parameter == name) return &e;
    return nullptr;
  }
};

inline QuarterlyStructuralEstimation load_quarterly_structural_estimation(
    const std::string& estimates_path, const std::string& covariance_path) {
  QuarterlyStructuralEstimation out;
  std::ifstream estimates(estimates_path);
  if (!estimates) return out;
  std::string line;
  bool header = true;
  while (std::getline(estimates, line)) {
    if (header) { header = false; continue; }
    if (line.empty()) continue;
    const auto f = calibration_detail::csv_fields(line);
    if (f.size() < 11) continue;
    QuarterlyStructuralEstimate e;
    e.parameter = f[0];
    e.estimate = calibration_detail::number(f[1]);
    e.standard_error = calibration_detail::number(f[2]);
    e.lower_bound = calibration_detail::number(f[3]);
    e.upper_bound = calibration_detail::number(f[4]);
    e.observations = static_cast<int>(calibration_detail::number(f[5]));
    e.sample_start = f[6]; e.sample_end = f[7]; e.method = f[8];
    e.direct_eligible = calibration_detail::yes(f[9]); e.notes = f[10];
    out.estimates.push_back(std::move(e));
  }
  std::ifstream covariance(covariance_path);
  if (!covariance) return out;
  header = true;
  while (std::getline(covariance, line)) {
    if (header) { header = false; continue; }
    if (line.empty()) continue;
    const auto f = calibration_detail::csv_fields(line);
    if (f.size() < 5) continue;
    if (f[0] == "output_gap_residual" && f[1] == "inflation_residual") {
      out.output_inflation_residual_correlation = calibration_detail::number(f[3]);
      out.residual_covariance_observations = static_cast<int>(calibration_detail::number(f[4]));
    }
  }
  out.loaded = !out.estimates.empty() && out.residual_covariance_observations > 0;
  return out;
}

inline int quarterly_direct_eligible_count(const QuarterlyStructuralEstimation& estimation) {
  int count = 0;
  for (const auto& e : estimation.estimates) if (e.direct_eligible) ++count;
  return count;
}

inline bool quarterly_estimation_valid(const QuarterlyStructuralEstimation& estimation) {
  if (!estimation.loaded || estimation.estimates.empty()
      || estimation.residual_covariance_observations < 60
      || !std::isfinite(estimation.output_inflation_residual_correlation)
      || std::abs(estimation.output_inflation_residual_correlation) > 1.0) return false;
  for (const auto& e : estimation.estimates) {
    if (e.parameter.empty() || e.observations < 60 || e.sample_start.empty()
        || e.sample_end.empty() || e.method.empty() || e.upper_bound < e.lower_bound
        || !std::isfinite(e.estimate) || !std::isfinite(e.standard_error)) return false;
  }
  return true;
}

inline std::string quarterly_estimation_to_json(const QuarterlyStructuralEstimation& e) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6)
      << "{\"loaded\":" << (e.loaded ? "true" : "false")
      << ",\"valid\":" << (quarterly_estimation_valid(e) ? "true" : "false")
      << ",\"estimateCount\":" << e.estimates.size()
      << ",\"directEligibleCount\":" << quarterly_direct_eligible_count(e)
      << ",\"residualCorrelation\":" << e.output_inflation_residual_correlation
      << ",\"residualObservations\":" << e.residual_covariance_observations << "}";
  return out.str();
}

}  // namespace cad
'''
    write(path, content)


def patch_policy_engine():
    path = ROOT / "src" / "policy_engine.cpp"
    text = path.read_text(encoding="utf-8")
    text = replace_once(text,
        'Scenario simulate(const Economy& e, std::string id, std::string name, std::string description,',
        'Scenario simulate(const Economy& e, const StructuralParameters& p, std::string id, std::string name, std::string description,',
        "simulate signature")
    replacements = [
        ('  const double trade_drag = exposed_exports * e.exports_gdp / 100.0\n      * e.trade_elasticity * (us_tariff + e.border_friction) / 100.0;', '  const double trade_drag = p.canada_trade_drag_scale * exposed_exports * e.exports_gdp / 100.0\n      * e.trade_elasticity * (us_tariff + e.border_friction) / 100.0;'),
        ('  const double us_trade_drag = e.imports_from_us_share / 100.0\n      * e.trade_elasticity * (ca_tariff + .45 * e.border_friction) / 100.0;', '  const double us_trade_drag = p.us_retaliation_drag_scale * e.imports_from_us_share / 100.0\n      * e.trade_elasticity * (ca_tariff + .45 * e.border_friction) / 100.0;'),
        ('  const double supply = coordinated * .22 + e.productivity_growth * .035;', '  const double supply = coordinated * p.productive_supply_multiplier + e.productivity_growth * .035;'),
        ('  const double fx = (e.usdcad - 1.34) * .35;', '  const double fx = (e.usdcad - 1.34) * p.fx_pass_through;'),
        ('      const double rate_target = clamp(2.5 + .75 * (inf - 2.0) + .25 * gap, .25, 7.0);', '      const double rate_target = clamp(p.neutral_rate\n          + p.rate_inflation_response * (inf - p.inflation_target)\n          + p.rate_output_response * gap, .25, 7.0);'),
        ('      else rate = clamp(rate + clamp(rate_target - rate, -.25, .25), 0.0, 8.0);', '      else rate = clamp(rate + clamp(rate_target - rate,\n          -p.max_quarterly_rate_step, p.max_quarterly_rate_step), 0.0, 8.0);'),
        ('      const double demand = fiscal * (1.0 - productive) * .36 - (rate - 2.5) * .18;', '      const double demand = fiscal * (1.0 - productive) * p.fiscal_demand_multiplier\n          - (rate - p.neutral_rate) * p.real_rate_demand_sensitivity;'),
        ('          + 2.0 * diversification + shock(rng) * .35;', '          + 2.0 * diversification + shock(rng) * p.export_shock_sd;'),
        ('          + 1.5 * deescalation + shock(rng) * .30;', '          + 1.5 * deescalation + shock(rng) * p.us_export_shock_sd;'),
        ('      gap = .72 * gap + demand - trade_drag + .08 * (e.global_growth - 2.7)\n          + shock(rng) * .16;', '      gap = p.output_persistence * gap + demand - trade_drag\n          + p.global_growth_sensitivity * (e.global_growth - 2.7)\n          + shock(rng) * p.output_shock_sd;'),
        ('      inf = .68 * inf + .32 * e.inflation_expectations + .12 * gap + fx\n          - supply + .022 * import_price - .018 * (e.oil_price - 75.0) + shock(rng) * .11;', '      inf = p.inflation_persistence * inf\n          + p.inflation_expectations_weight * e.inflation_expectations\n          + p.phillips_curve_slope * gap + fx - supply\n          + p.import_price_pass_through * import_price\n          - p.oil_inflation_sensitivity * (e.oil_price - 75.0)\n          + shock(rng) * p.inflation_shock_sd;'),
        ('          + coordinated * .24 + shock(rng) * .25, -3.0, 5.5);', '          + coordinated * .24 + shock(rng) * p.growth_shock_sd, -3.0, 5.5);'),
        ('          + shock(rng) * .18, -3.0, 5.5);', '          + shock(rng) * p.us_growth_shock_sd, -3.0, 5.5);'),
        ('      housing = clamp(.78 * housing - 1.15 * (rate - 2.5)', '      housing = clamp(.78 * housing - 1.15 * (rate - p.neutral_rate)'),
        ('          + .045 * (rate - 2.5) * debt - .18 * growth) / 4.0;', '          + .045 * (rate - p.neutral_rate) * debt - .18 * growth) / 4.0;'),
        ('  const double ca_exports = e.canada_exports_to_us_cad\n      * std::max(.05, 1.0 - e.trade_elasticity * effective_us_rate);\n  const double ca_imports = e.canada_imports_from_us_cad\n      * std::max(.05, 1.0 - e.trade_elasticity * effective_ca_rate);', '  const double ledger_elasticity = e.trade_elasticity * p.tariff_revenue_elasticity_scale;\n  const double ca_exports = e.canada_exports_to_us_cad\n      * std::max(.05, 1.0 - ledger_elasticity * effective_us_rate);\n  const double ca_imports = e.canada_imports_from_us_cad\n      * std::max(.05, 1.0 - ledger_elasticity * effective_ca_rate);'),
        ('  const double mandate_loss = 3.8 * sq(s.inflation - 2.0)', '  const double mandate_loss = 3.8 * sq(s.inflation - p.inflation_target)'),
        ('  r.neutral_rate = clamp(2.35 + .16 * (e.productivity_growth - 1.0)\n      + .10 * (e.global_growth - 2.7), 1.75, 3.5);', '  r.neutral_rate = clamp(parameters_.neutral_rate + .16 * (e.productivity_growth - 1.0)\n      + .10 * (e.global_growth - 2.7), 1.75, 3.5);'),
    ]
    for i, (old, new) in enumerate(replacements):
        text = replace_once(text, old, new, f"policy parameter replacement {i}")
    # Every production simulation call receives the engine calibration.
    for old, new, label in [
        ('r.scenarios.push_back(simulate(e, std::move(id),', 'r.scenarios.push_back(simulate(e, parameters_, std::move(id),', 'add scenario call'),
        ('auto s = simulate(e, "custom",', 'auto s = simulate(e, parameters_, "custom",', 'custom call'),
        ('auto verified = simulate(candidate_e, base.id,', 'auto verified = simulate(candidate_e, parameters_, base.id,', 'sector verify call'),
        ('auto verified = simulate(verified_e, scenario.id,', 'auto verified = simulate(verified_e, parameters_, scenario.id,', 'final verify call'),
    ]:
        text = replace_once(text, old, new, label)
    write(path, text)


def patch_calibrated_engine():
    path = ROOT / "include" / "calibration.hpp"
    text = path.read_text(encoding="utf-8")
    old = '''  explicit CalibratedPolicyEngine(std::string snapshot_path,
                                  std::uint64_t seed = 20260810)
      : base_(seed), snapshot_(load_calibration_snapshot(snapshot_path)), path_(std::move(snapshot_path)) {}'''
    new = '''  explicit CalibratedPolicyEngine(std::string snapshot_path,
                                  std::uint64_t seed = 20260810,
                                  StructuralParameters structural_parameters = {},
                                  StructuralParameterRegistry structural_registry = {})
      : base_(seed, std::move(structural_parameters), std::move(structural_registry)),
        snapshot_(load_calibration_snapshot(snapshot_path)), path_(std::move(snapshot_path)) {}'''
    text = replace_once(text, old, new, "calibrated engine constructor")
    write(path, text)


def patch_main():
    path = ROOT / "src" / "main.cpp"
    text = path.read_text(encoding="utf-8")
    text = replace_once(text,
        '  cad::CalibratedPolicyEngine engine(calibrated_path);',
        '  cad::CalibratedPolicyEngine engine(\n      calibrated_path, 20260810, structural_parameters, structural_registry);',
        "main calibrated engine")
    write(path, text)


def create_quarterly_test_and_patch_existing():
    test = ROOT / "tests" / "quarterly_empirical_calibration_test.cpp"
    content = r'''#include "empirical_calibration.hpp"
#include "quarterly_empirical_calibration.hpp"
#include "structural_calibration.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

const cad::Scenario* scenario(const cad::Result& result, const std::string& id) {
  for (const auto& s : result.scenarios) if (s.id == id) return &s;
  return nullptr;
}

int main() {
  const auto estimation = cad::load_quarterly_structural_estimation(
      "data/calibration/quarterly_structural_estimates.csv",
      "data/calibration/quarterly_residual_covariance.csv");
  assert(estimation.loaded);
  assert(cad::quarterly_estimation_valid(estimation));
  assert(estimation.estimates.size() == 10);
  assert(cad::quarterly_direct_eligible_count(estimation) == 2);
  assert(estimation.residual_covariance_observations == 75);
  assert(std::abs(estimation.output_inflation_residual_correlation
      - (-0.006249264169)) < 1e-9);

  const auto* output = estimation.find("output_persistence");
  const auto* inflation = estimation.find("inflation_persistence");
  const auto* phillips = estimation.find("phillips_curve_slope");
  assert(output && output->direct_eligible);
  assert(inflation && inflation->direct_eligible);
  assert(phillips && !phillips->direct_eligible);
  assert(phillips->lower_bound < 0.0 && phillips->upper_bound > 0.0);

  const auto registry = cad::load_structural_parameter_registry(
      "data/calibration/structural_parameter_registry.csv");
  const auto parameters = cad::apply_structural_parameter_registry(
      cad::StructuralParameters{}, registry);
  assert(std::abs(parameters.output_persistence - output->estimate) < 1e-9);
  assert(std::abs(parameters.inflation_persistence - inflation->estimate) < 1e-9);
  assert(std::abs(parameters.inflation_persistence
      + parameters.inflation_expectations_weight - 1.0) < 1e-12);

  // The ordinary production engine must consume StructuralParameters, not only
  // the outer V2 robustness experiment.
  cad::StructuralParameters low = parameters;
  low.output_persistence = 0.20;
  low.uncertainty_scale = 0.0;
  cad::StructuralParameters high = parameters;
  high.output_persistence = 0.90;
  high.uncertainty_scale = 0.0;
  cad::Economy economy;
  economy.us_tariff_canada = 0.0;
  economy.canada_retaliatory_tariff = 0.0;
  const auto low_result = cad::PolicyEngine(20260810, low).evaluate(economy);
  const auto high_result = cad::PolicyEngine(20260810, high).evaluate(economy);
  const auto* low_status = scenario(low_result, "statusquo");
  const auto* high_status = scenario(high_result, "statusquo");
  assert(low_status && high_status);
  assert(std::abs(low_status->growth - high_status->growth) > 1e-5);

  const auto json = cad::quarterly_estimation_to_json(estimation);
  assert(json.find("\"directEligibleCount\":2") != std::string::npos);
  std::cout << "quarterly empirical calibration tests passed\n";
  return 0;
}
'''
    write(test, content)

    path = ROOT / "tests" / "empirical_calibration_test.cpp"
    text = path.read_text(encoding="utf-8")
    text = text.replace('assert(audit.statistically_anchored_count == 7);', 'assert(audit.statistically_anchored_count == 12);')
    text = text.replace('assert(audit.reference_only_count == 6);', 'assert(audit.reference_only_count == 9);')
    text = text.replace('assert(audit.direct_mapping_count == 1);', 'assert(audit.direct_mapping_count == 3);')
    text = text.replace('assert(std::abs(audit.statistically_anchored_coverage - 28.0) < 1e-12);', 'assert(std::abs(audit.statistically_anchored_coverage - 48.0) < 1e-12);')
    text = text.replace('assert(std::abs(audit.direct_mapping_coverage - 4.0) < 1e-12);', 'assert(std::abs(audit.direct_mapping_coverage - 12.0) < 1e-12);')
    text = text.replace('assert(std::abs(parameters.inflation_persistence - 0.68) < 1e-12);', 'assert(std::abs(parameters.output_persistence - 0.8802835399) < 1e-10);\n  assert(std::abs(parameters.inflation_persistence - 0.7371310581) < 1e-10);\n  assert(std::abs(parameters.inflation_expectations_weight - 0.2628689419) < 1e-10);')
    text = text.replace('assert(json.find("\\\"statisticallyAnchoredCount\\\":7") != std::string::npos);', 'assert(json.find("\\\"statisticallyAnchoredCount\\\":12") != std::string::npos);')
    text = text.replace('assert(json.find("\\\"statisticallyAnchoredCoverage\\\":28.000000") != std::string::npos);', 'assert(json.find("\\\"statisticallyAnchoredCoverage\\\":48.000000") != std::string::npos);')
    text = text.replace('assert(json.find("\\\"directMappingCount\\\":1") != std::string::npos);', 'assert(json.find("\\\"directMappingCount\\\":3") != std::string::npos);')
    write(path, text)


def patch_cmake_and_ci():
    path = ROOT / "CMakeLists.txt"
    text = path.read_text(encoding="utf-8")
    text = replace_once(text,
        '    data/calibration/empirical_structural_evidence.csv\n    data/calibration/state_measurement_registry.csv',
        '    data/calibration/empirical_structural_evidence.csv\n    data/calibration/quarterly_structural_estimates.csv\n    data/calibration/quarterly_residual_covariance.csv\n    data/calibration/state_measurement_registry.csv',
        "embedded quarterly assets")
    text = replace_once(text,
        'cad_add_test(empirical_calibration_tests tests/empirical_calibration_test.cpp empirical_structural_calibration)\nset_tests_properties(empirical_structural_calibration PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})',
        'cad_add_test(empirical_calibration_tests tests/empirical_calibration_test.cpp empirical_structural_calibration)\nset_tests_properties(empirical_structural_calibration PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})\ncad_add_test(quarterly_empirical_calibration_tests tests/quarterly_empirical_calibration_test.cpp quarterly_empirical_calibration)\nset_tests_properties(quarterly_empirical_calibration PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})',
        "quarterly test target")
    write(path, text)

    path = ROOT / ".github" / "workflows" / "ci.yml"
    text = path.read_text(encoding="utf-8")
    marker = "      - name: Check calibration tooling\n"
    pos = text.find(marker)
    if pos < 0:
        raise RuntimeError("CI calibration tooling step not found")
    # Add deterministic frozen-panel verification immediately before existing tooling.
    addition = "      - name: Verify frozen quarterly empirical calibration\n        run: python3 tools/refresh_sep_calibration.py --verify\n"
    if addition not in text:
        text = text[:pos] + addition + text[pos:]
    write(path, text)


def write_docs():
    path = ROOT / "docs" / "QUARTERLY_EMPIRICAL_CALIBRATION.md"
    content = '''# Quarterly empirical structural calibration

The V2 empirical layer now contains a frozen **2001Q1–2019Q4** quarterly estimation panel built from Bank of Canada Staff Economic Projections (SEP) real-time vintages. Each SEP vintage is a snapshot of the information set available to staff at the time; the committed panel uses the current-quarter output gap, core CPI index, implied policy rate and U.S./CAD exchange rate from each vintage. Statistics Canada GDP and unemployment series are retained as revised-data diagnostics, not mixed into the real-time structural regressions.

## Production-form estimation

The refresh tool estimates equations in the same normalization used by the production simulator and commits both coefficients and residual covariance. Ten parameters receive direct quarterly evidence. Promotion is intentionally stricter than estimation: a coefficient is allowed to overwrite a production baseline only when the estimand matches, the sign/bounds are admissible, and the diagnostic is statistically defensible.

The current promotion set is deliberately small:

- `output_persistence = 0.8802835399` (SE 0.0540357; 95% bounded interval 0.77437–0.98);
- `inflation_persistence = 0.7371310581` (SE 0.0785239; interval 0.58322–0.89104);
- `inflation_expectations_weight = 1 - inflation_persistence = 0.2628689419` remains derived rather than independently estimated.

The neutral rate remains the previously promoted 2026 Bank assessment midpoint of 2.75%. Together, those give three direct structural mappings. The broader empirical-evidence registry now covers **12 of 25 structural parameters (48%)**, while only **3 of 25 (12%)** are direct production substitutions.

## Rejected promotions are evidence

The quarterly exercise does not force estimates into the model merely to raise a coverage percentage. The real-rate demand coefficient and FX coefficient have the wrong sign/near-zero estimates; the Phillips-curve interval crosses zero; residual shock scales are based on smooth staff current-quarter estimates rather than realized macro shocks; and the fitted policy-rule coefficients describe the staff implied-rate path, not the Governing Council reaction function. These remain `reference-only` and are visible in the evidence ledger.

The estimated output-gap/inflation residual correlation is approximately **-0.00625**, effectively zero in this sample. The covariance is retained and tested, but the Monte Carlo engine keeps its independent innovation ordering rather than adding complexity unsupported by this estimate.

## Reproducibility

`tools/refresh_sep_calibration.py --refresh` rebuilds the panel from public Bank/Statistics Canada sources. Ordinary CI runs `--verify` offline against committed data, so tests do not depend on network availability. Source-family hashes and transformations are recorded in `quarterly_estimation_manifest.csv`.

This is an empirical calibration exercise, not causal identification and not an official Bank of Canada model.
'''
    write(path, content)


def cleanup_temporary_files():
    for rel in [
        ".github/workflows/quarterly-source-discovery.yml",
        "tools/discover_quarterly_series.py",
        "tools/refresh_quarterly_calibration.py",
        "tools/run_sep_refresh.py",
    ]:
        path = ROOT / rel
        if path.exists(): path.unlink()


def main():
    patch_refresh_tool()
    patch_frozen_estimates()
    patch_structural_registry()
    rewrite_empirical_evidence()
    append_source_registry()
    create_quarterly_header()
    patch_policy_engine()
    patch_calibrated_engine()
    patch_main()
    create_quarterly_test_and_patch_existing()
    patch_cmake_and_ci()
    write_docs()
    cleanup_temporary_files()
    print("quarterly empirical promotion applied")

if __name__ == "__main__":
    main()

#!/usr/bin/env python3
from pathlib import Path


def patch(path, old, new):
    p=Path(path); s=p.read_text(encoding='utf-8')
    if new in s:
        print('already patched', path); return
    if s.count(old)!=1:
        raise SystemExit(f'{path}: expected one marker, got {s.count(old)}')
    p.write_text(s.replace(old,new),encoding='utf-8')
    print('patched',path)

patch('include/structural_calibration.hpp',
'''    "output_shock_sd", "inflation_shock_sd", "output_inflation_shock_correlation",
    "growth_shock_sd", "us_growth_shock_sd", "export_shock_sd", "us_export_shock_sd"
''',
'''    "output_shock_sd", "inflation_shock_sd", "output_inflation_shock_correlation",
    "growth_shock_sd", "us_growth_shock_sd", "export_shock_sd", "us_export_shock_sd",
    "shock_tail_threshold", "shock_tail_scale", "stress_regime_shock_scale"
''')
patch('include/structural_calibration.hpp',
'''  value("us_export_shock_sd", p.us_export_shock_sd);
  if (registry.loaded) {
''',
'''  value("us_export_shock_sd", p.us_export_shock_sd);
  value("shock_tail_threshold", p.shock_tail_threshold);
  value("shock_tail_scale", p.shock_tail_scale);
  value("stress_regime_shock_scale", p.stress_regime_shock_scale);
  if (registry.loaded) {
''')
patch('include/robustness.hpp',
'''    p.us_export_shock_sd = draw(baseline.us_export_shock_sd, "us_export_shock_sd", 1e-6, inf, "lognormal");
    out.push_back(std::move(p));
''',
'''    p.us_export_shock_sd = draw(baseline.us_export_shock_sd, "us_export_shock_sd", 1e-6, inf, "lognormal");
    p.shock_tail_threshold = draw(baseline.shock_tail_threshold, "shock_tail_threshold", .5, 5.0, "normal");
    p.shock_tail_scale = draw(baseline.shock_tail_scale, "shock_tail_scale", 1.0, 5.0, "lognormal");
    p.stress_regime_shock_scale = draw(baseline.stress_regime_shock_scale, "stress_regime_shock_scale", 1.0, 4.0, "lognormal");
    out.push_back(std::move(p));
''')

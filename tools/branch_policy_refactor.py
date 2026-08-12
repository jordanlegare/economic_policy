#!/usr/bin/env python3
"""One-time exact branch refactor for large source/UI files.

This script is intentionally narrow and fail-closed: every replacement must
match exactly once. It exists only because the GitHub connector replaces whole
files rather than applying patches. The temporary workflow that invokes it is
removed before merge.
"""

from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count == 0 and new in text:
        print(f"already patched: {path}")
        return
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one match, found {count}")
    p.write_text(text.replace(old, new), encoding="utf-8")
    print(f"patched: {path}")


replace_once(
    "src/policy_engine.cpp",
    """  input.us_coverage = e.us_sector_coverage;\n  input.canada_coverage = e.canada_sector_coverage;\n  return input;\n""",
    """  input.us_coverage = e.us_sector_coverage;\n  input.canada_coverage = e.canada_sector_coverage;\n  input.us_trade_elasticity = e.us_sector_trade_elasticity;\n  input.canada_trade_elasticity = e.canada_sector_trade_elasticity;\n  input.us_price_pass_through = e.us_sector_price_pass_through;\n  input.canada_price_pass_through = e.canada_sector_price_pass_through;\n  return input;\n""",
)

replace_once(
    "src/policy_engine.cpp",
    """      if (q == 0) rate = clamp(rate + move / 100.0, 0.0, 8.0);\n      else rate = clamp(rate + clamp(rate_target - rate,\n          -p.max_quarterly_rate_step, p.max_quarterly_rate_step), 0.0, 8.0);\n""",
    """      if (q == 0) {\n        rate = clamp(rate + move / 100.0, 0.0, 8.0);\n      } else {\n        double policy_step = clamp(rate_target - rate,\n            -p.max_quarterly_rate_step, p.max_quarterly_rate_step);\n        // The optimized control remains the auditable first-quarter move. In\n        // quarter 2 it can receive one same-direction, state-contingent follow-\n        // up when the incoming data still justify the original action. This\n        // adds a dynamic policy path without expanding or obscuring the declared\n        // 288-control startup search grid.\n        if (q == 1 && std::abs(move) > 1e-9) {\n          const double followup = std::min(.25, p.max_quarterly_rate_step);\n          const bool continue_easing = move < 0.0\n              && gap < -.25 && inf <= p.inflation_target + .35;\n          const bool continue_tightening = move > 0.0\n              && (inf >= p.inflation_target + .50 || gap > .50);\n          if (continue_easing) policy_step = std::min(policy_step, -followup);\n          if (continue_tightening) policy_step = std::max(policy_step, followup);\n        }\n        rate = clamp(rate + policy_step, 0.0, 8.0);\n      }\n""",
)

replace_once(
    "src/policy_engine.cpp",
    """      gap = p.output_persistence * gap + demand - trade_drag\n          + p.global_growth_sensitivity * (e.global_growth - 2.7)\n          + shock(rng) * p.output_shock_sd;\n      inf = p.inflation_persistence * inf\n          + p.inflation_expectations_weight * e.inflation_expectations\n          + p.phillips_curve_slope * gap + fx - supply\n          + p.import_price_pass_through * import_price\n          - p.oil_inflation_sensitivity * (e.oil_price - 75.0)\n          + shock(rng) * p.inflation_shock_sd;\n""",
    """      // Preserve common-random-number ordering while allowing the only\n      // currently measured innovation dependence to enter production. The\n      // correlation comes from the frozen output/inflation residual covariance;\n      // all remaining shock relationships stay independent until identified.\n      const double output_z = shock(rng);\n      const double inflation_independent_z = shock(rng);\n      const double rho = clamp(p.output_inflation_shock_correlation, -.999, .999);\n      const double inflation_z = rho * output_z\n          + std::sqrt(std::max(0.0, 1.0 - rho * rho)) * inflation_independent_z;\n      gap = p.output_persistence * gap + demand - trade_drag\n          + p.global_growth_sensitivity * (e.global_growth - 2.7)\n          + output_z * p.output_shock_sd;\n      inf = p.inflation_persistence * inf\n          + p.inflation_expectations_weight * e.inflation_expectations\n          + p.phillips_curve_slope * gap + fx - supply\n          + p.import_price_pass_through * import_price\n          - p.oil_inflation_sensitivity * (e.oil_price - 75.0)\n          + inflation_z * p.inflation_shock_sd;\n""",
)

replace_once(
    "src/policy_engine.cpp",
    """  recommendation << \"Sector welfare now includes linear upstream supplier-demand and downstream input-cost propagation through a 20-sector production network. The network coefficients are explicitly provisional bridge coefficients mapped to the Statistics Canada 2024 sector structure rather than claimed as direct table-cell estimates. Canadian and U.S. export channels are independent. Bilateral trade balance is reported for diplomatic context but is not rewarded in the welfare objective.\";\n""",
    """  recommendation << \"Sector welfare includes upstream supplier-demand and downstream input-cost propagation through country-specific 20-sector production-network objects. The Canadian matrix is the directly aggregated Statistics Canada 2024 table. The U.S. matrix is a separately identified but explicitly provisional structural proxy pending a certified BEA extraction, and must not be described as empirical U.S. IO calibration. Directional sector elasticity and pass-through inputs fall back to audited aggregate anchors unless production-compatible sector evidence is supplied. Monetary policy can make one state-contingent quarter-2 follow-up after the optimized first move, and the measured output/inflation residual correlation is propagated without claiming a fully identified multivariate shock process. Canadian and U.S. export channels are independent. Bilateral trade balance is reported for diplomatic context but is not rewarded in the welfare objective.\";\n""",
)

replace_once(
    "web/index.html",
    '<script src="/app.js"></script>\n',
    '<script src="/app.js"></script>\n<script src="/trade-incidence.js"></script>\n',
)

replace_once(
    "CMakeLists.txt",
    """    web/evaluation-controller.js\n    web/diplomat.css\n""",
    """    web/evaluation-controller.js\n    web/trade-incidence.js\n    web/diplomat.css\n""",
)

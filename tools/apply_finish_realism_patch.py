#!/usr/bin/env python3
"""One-time fail-closed large-file patch for the final realism branch."""
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    if new in text:
        print(f"already patched: {path}")
        return
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}")
    p.write_text(text.replace(old, new), encoding="utf-8")
    print(f"patched: {path}")


replace_once(
    "src/policy_engine.cpp",
    '#include "policy_engine.hpp"\n#include "trade_network.hpp"\n',
    '#include "policy_engine.hpp"\n#include "policy_dynamics.hpp"\n#include "trade_network.hpp"\n',
)

old_block = '''  // Everything below is invariant across stochastic paths and quarters. Keep
  // it out of the Monte Carlo hot loop while preserving the exact model.
  const double coordinated = productive * fiscal;
  const double us_tariff = std::max(
      0.0, e.us_tariff_canada * us_barrier_coverage * (1.0 - deescalation));
  const double ca_tariff = std::max(
      0.0, e.canada_retaliatory_tariff * ca_barrier_coverage * (1.0 - deescalation));
  const auto network = evaluate_trade_network(make_trade_network_input(e, s));
  const double exposed_exports = e.exports_to_us_share / 100.0
      * (1.0 - clamp(diversification + e.trade_diversification, 0.0, 0.75));
  const double trade_drag = p.canada_trade_drag_scale * exposed_exports * e.exports_gdp / 100.0
      * e.trade_elasticity * (us_tariff + e.border_friction) / 100.0
      + network.canada_supply_chain_drag;
  const double us_trade_drag = p.us_retaliation_drag_scale * e.imports_from_us_share / 100.0
      * e.trade_elasticity * (ca_tariff + .45 * e.border_friction) / 100.0
      + network.us_supply_chain_drag;
  const double import_price = e.imports_from_us_share / 100.0
      * e.import_content_consumption / 100.0 * ca_tariff
      + network.canada_input_cost_pressure;
  const double supply = coordinated * p.productive_supply_multiplier + e.productivity_growth * .035;
  const double relief_cost = targeted_relief + e.tariff_relief;
  const double fx = (e.usdcad - 1.34) * p.fx_pass_through;
'''
new_block = '''  // The search chooses amplitudes; deterministic implementation rules convert
  // those amplitudes into explicit 12-quarter policy paths. Trade networks are
  // precomputed once per quarter outside the Monte Carlo path loop.
  const auto implementation = build_policy_implementation_paths(
      fiscal, productive, 100.0 * deescalation, targeted_relief, diversification);
  s.fiscal_path = implementation.fiscal;
  s.productive_investment_path = implementation.productive_investment;
  s.negotiated_relief_path = implementation.negotiated_relief;
  s.targeted_relief_path = implementation.targeted_relief;
  s.diversification_path = implementation.diversification;

  std::array<TradeNetworkResult, 12> networks{};
  std::array<double, 12> deescalation_path{}, us_tariff_path{}, ca_tariff_path{};
  std::array<double, 12> trade_drag_path{}, us_trade_drag_path{}, import_price_path{};
  std::array<double, 12> supply_path{}, relief_cost_path{};
  for (int q = 0; q < 12; ++q) {
    const auto qi = static_cast<std::size_t>(q);
    deescalation_path[qi] = clamp(implementation.negotiated_relief[qi] / 100.0, 0.0,
        clamp(e.cooperation_ceiling / 100.0, 0.0, 1.0));
    us_tariff_path[qi] = std::max(0.0, e.us_tariff_canada * us_barrier_coverage
        * (1.0 - deescalation_path[qi]));
    ca_tariff_path[qi] = std::max(0.0, e.canada_retaliatory_tariff * ca_barrier_coverage
        * (1.0 - deescalation_path[qi]));

    Scenario quarter_policy = s;
    quarter_policy.negotiated_relief = implementation.negotiated_relief[qi];
    quarter_policy.diversification = implementation.diversification[qi];
    networks[qi] = evaluate_trade_network(make_trade_network_input(e, quarter_policy));

    const double exposed_exports = e.exports_to_us_share / 100.0
        * (1.0 - clamp(implementation.diversification[qi] + e.trade_diversification, 0.0, 0.75));
    trade_drag_path[qi] = p.canada_trade_drag_scale * exposed_exports * e.exports_gdp / 100.0
        * e.trade_elasticity * (us_tariff_path[qi] + e.border_friction) / 100.0
        + networks[qi].canada_supply_chain_drag;
    us_trade_drag_path[qi] = p.us_retaliation_drag_scale * e.imports_from_us_share / 100.0
        * e.trade_elasticity * (ca_tariff_path[qi] + .45 * e.border_friction) / 100.0
        + networks[qi].us_supply_chain_drag;
    import_price_path[qi] = e.imports_from_us_share / 100.0
        * e.import_content_consumption / 100.0 * ca_tariff_path[qi]
        + networks[qi].canada_input_cost_pressure;
    supply_path[qi] = implementation.productive_investment[qi]
        * p.productive_supply_multiplier + e.productivity_growth * .035;
    relief_cost_path[qi] = implementation.targeted_relief[qi] + e.tariff_relief;
  }
  const bool stress_regime = macro_stress_regime(e);
  const double fx = (e.usdcad - 1.34) * p.fx_pass_through;
'''
replace_once("src/policy_engine.cpp", old_block, new_block)

old_loop = '''      const double demand = fiscal * (1.0 - productive) * p.fiscal_demand_multiplier
          - (rate - p.neutral_rate) * p.real_rate_demand_sensitivity;

      export_change = -100.0 * trade_drag + .35 * (e.us_growth - 2.0)
          + 2.0 * diversification + shock(rng) * p.export_shock_sd;
      // Independent U.S. channel: this responds to Canadian market access,
      // Canadian demand, de-escalation and its own shock. It never references
      // Canada's export-change variable.
      us_export_change = -100.0 * us_trade_drag + .30 * (e.gdp_growth - 1.5)
          + 1.5 * deescalation + shock(rng) * p.us_export_shock_sd;

      // Preserve common-random-number ordering while allowing the only
      // currently measured innovation dependence to enter production. The
      // correlation comes from the frozen output/inflation residual covariance;
      // all remaining shock relationships stay independent until identified.
      const double output_z = shock(rng);
      const double inflation_independent_z = shock(rng);
      const double rho = clamp(p.output_inflation_shock_correlation, -.999, .999);
      const double inflation_z = rho * output_z
          + std::sqrt(std::max(0.0, 1.0 - rho * rho)) * inflation_independent_z;
      gap = p.output_persistence * gap + demand - trade_drag
          + p.global_growth_sensitivity * (e.global_growth - 2.7)
          + output_z * p.output_shock_sd;
      inf = p.inflation_persistence * inf
          + p.inflation_expectations_weight * e.inflation_expectations
          + p.phillips_curve_slope * gap + fx - supply
          + p.import_price_pass_through * import_price
          - p.oil_inflation_sensitivity * (e.oil_price - 75.0)
          + inflation_z * p.inflation_shock_sd;
      const double growth = clamp(1.75 + gap - .18 * e.credit_spread
          + coordinated * .24 + shock(rng) * p.growth_shock_sd, -3.0, 5.5);
      const double us_growth = clamp(e.us_growth + .16 * coordinated + .28 * deescalation
          - .010 * us_tariff - .014 * ca_tariff - .04 * e.border_friction
          - .40 * network.us_supply_chain_drag
          + shock(rng) * p.us_growth_shock_sd, -3.0, 5.5);
      u = clamp(u - .10 * (growth - 1.7) + shock(rng) * .035, 3.5, 11.0);
      housing = clamp(.78 * housing - 1.15 * (rate - p.neutral_rate)
          + .08 * (e.population_growth - 1.2) + shock(rng) * .5, -15.0, 30.0);
      debt += (-e.fiscal_balance_gdp + fiscal * .8 + relief_cost * .55
          + .045 * (rate - p.neutral_rate) * debt - .18 * growth) / 4.0;
      cost = .56 * inf + .22 * std::max(0.0, housing / 10.0)
          + .14 * std::max(0.0, e.wage_growth - growth) + .08 * import_price;
'''
new_loop = '''      const auto qi = static_cast<std::size_t>(q);
      const double demand = implementation.fiscal[qi] * (1.0 - productive)
          * p.fiscal_demand_multiplier
          - (rate - p.neutral_rate) * p.real_rate_demand_sensitivity;

      const double export_z = regime_tail_innovation(shock(rng), p, stress_regime);
      export_change = -100.0 * trade_drag_path[qi] + .35 * (e.us_growth - 2.0)
          + 2.0 * implementation.diversification[qi] + export_z * p.export_shock_sd;
      // Independent U.S. channel: this responds to Canadian market access,
      // Canadian demand, de-escalation and its own shock. It never references
      // Canada's export-change variable.
      const double us_export_z = regime_tail_innovation(shock(rng), p, stress_regime);
      us_export_change = -100.0 * us_trade_drag_path[qi] + .30 * (e.gdp_growth - 1.5)
          + 1.5 * deescalation_path[qi] + us_export_z * p.us_export_shock_sd;

      // Preserve common-random-number ordering while allowing the measured
      // output/inflation dependence plus explicit assumed tail/regime mechanics.
      const double raw_output_z = shock(rng);
      const double inflation_independent_z = shock(rng);
      const double rho = clamp(p.output_inflation_shock_correlation, -.999, .999);
      const double raw_inflation_z = rho * raw_output_z
          + std::sqrt(std::max(0.0, 1.0 - rho * rho)) * inflation_independent_z;
      const double output_z = regime_tail_innovation(raw_output_z, p, stress_regime);
      const double inflation_z = regime_tail_innovation(raw_inflation_z, p, stress_regime);
      gap = p.output_persistence * gap + demand - trade_drag_path[qi]
          + p.global_growth_sensitivity * (e.global_growth - 2.7)
          + output_z * p.output_shock_sd;
      inf = p.inflation_persistence * inf
          + p.inflation_expectations_weight * e.inflation_expectations
          + p.phillips_curve_slope * gap + fx - supply_path[qi]
          + p.import_price_pass_through * import_price_path[qi]
          - p.oil_inflation_sensitivity * (e.oil_price - 75.0)
          + inflation_z * p.inflation_shock_sd;
      const double growth_z = regime_tail_innovation(shock(rng), p, stress_regime);
      const double growth = clamp(1.75 + gap - .18 * e.credit_spread
          + implementation.productive_investment[qi] * .24
          + growth_z * p.growth_shock_sd, -3.0, 5.5);
      const double us_growth_z = regime_tail_innovation(shock(rng), p, stress_regime);
      const double us_growth = clamp(e.us_growth
          + .16 * implementation.productive_investment[qi] + .28 * deescalation_path[qi]
          - .010 * us_tariff_path[qi] - .014 * ca_tariff_path[qi] - .04 * e.border_friction
          - .40 * networks[qi].us_supply_chain_drag
          + us_growth_z * p.us_growth_shock_sd, -3.0, 5.5);
      const double unemployment_z = regime_tail_innovation(shock(rng), p, stress_regime);
      u = clamp(u - .10 * (growth - 1.7) + unemployment_z * .035, 3.5, 11.0);
      const double housing_z = regime_tail_innovation(shock(rng), p, stress_regime);
      housing = clamp(.78 * housing - 1.15 * (rate - p.neutral_rate)
          + .08 * (e.population_growth - 1.2) + housing_z * .5, -15.0, 30.0);
      debt += (-e.fiscal_balance_gdp + implementation.fiscal[qi] * .8
          + relief_cost_path[qi] * .55
          + .045 * (rate - p.neutral_rate) * debt - .18 * growth) / 4.0;
      cost = .56 * inf + .22 * std::max(0.0, housing / 10.0)
          + .14 * std::max(0.0, e.wage_growth - growth) + .08 * import_price_path[qi];
'''
replace_once("src/policy_engine.cpp", old_loop, new_loop)

replace_once(
    "src/policy_engine.cpp",
    '''        income_sum += growth - cost + targeted_relief * .15;\n''',
    '''        income_sum += growth - cost + implementation.targeted_relief[qi] * .15;\n''',
)

replace_once(
    "src/policy_engine.cpp",
    '''  const double effective_us_rate = us_tariff / 100.0;\n  const double effective_ca_rate = ca_tariff / 100.0;\n''',
    '''  const double effective_us_rate = us_tariff_path.back() / 100.0;\n  const double effective_ca_rate = ca_tariff_path.back() / 100.0;\n''',
)

old_weights = '''  const double mandate_loss = 3.8 * sq(s.inflation - p.inflation_target)
      + 1.2 * sq(std::max(0.0, s.unemployment - 5.8))
      + .7 * sq(std::min(0.0, s.growth)) + .018 * s.recession_risk;
  const double federal_loss = .32 * sq(std::max(0.0, s.debt_gdp - e.federal_debt_gdp))
      + .7 * sq(std::min(0.0, s.growth))
      + .8 * sq(std::max(0.0, s.unemployment - 6.0)) + .012 * sq(s.housing_gap);
'''
new_weights = '''  const auto& w = e.loss_weights;
  const double mandate_loss = w.boc_inflation * sq(s.inflation - p.inflation_target)
      + w.boc_unemployment * sq(std::max(0.0, s.unemployment - 5.8))
      + w.boc_contraction * sq(std::min(0.0, s.growth))
      + w.boc_recession * s.recession_risk;
  const double federal_loss = w.federal_debt * sq(std::max(0.0, s.debt_gdp - e.federal_debt_gdp))
      + w.federal_contraction * sq(std::min(0.0, s.growth))
      + w.federal_unemployment * sq(std::max(0.0, s.unemployment - 6.0))
      + w.federal_housing * sq(s.housing_gap);
'''
replace_once("src/policy_engine.cpp", old_weights, new_weights)

old_us = '''  const double us_inflation_pressure = std::max(
      0.0, e.us_inflation - 2.0 + e.us_tariff_canada * us_barrier_coverage * .025
      + .10 * network.us_input_cost_pressure);
  const double us_loss = .55 * sq(std::max(0.0, -s.us_export_change))
      + .8 * sq(us_inflation_pressure)
      + .55 * sq(std::max(0.0, 1.8 - s.us_growth))
      + .25 * sq(e.canada_retaliatory_tariff * ca_barrier_coverage * (1.0 - deescalation));
'''
new_us = '''  const double us_inflation_pressure = std::max(
      0.0, e.us_inflation - 2.0 + e.us_tariff_canada * us_barrier_coverage * .025
      + .10 * networks.back().us_input_cost_pressure);
  const double us_loss = w.us_exports * sq(std::max(0.0, -s.us_export_change))
      + w.us_inflation * sq(us_inflation_pressure)
      + w.us_growth * sq(std::max(0.0, 1.8 - s.us_growth))
      + w.us_retaliation * sq(e.canada_retaliatory_tariff * ca_barrier_coverage
          * (1.0 - deescalation_path.back()));
'''
replace_once("src/policy_engine.cpp", old_us, new_us)

replace_once(
    "src/policy_engine.cpp",
    '''    o << ",\\\"usExportPath\\\":";\n    array_json(o, s.us_export_path);\n    o << ",\\\"sectors\\\":[";\n''',
    '''    o << ",\\\"usExportPath\\\":";\n    array_json(o, s.us_export_path);\n    o << ",\\\"fiscalPath\\\":";\n    array_json(o, s.fiscal_path);\n    o << ",\\\"productiveInvestmentPath\\\":";\n    array_json(o, s.productive_investment_path);\n    o << ",\\\"negotiatedReliefPath\\\":";\n    array_json(o, s.negotiated_relief_path);\n    o << ",\\\"targetedReliefPath\\\":";\n    array_json(o, s.targeted_relief_path);\n    o << ",\\\"diversificationPath\\\":";\n    array_json(o, s.diversification_path);\n    o << ",\\\"sectors\\\":[";\n''',
)

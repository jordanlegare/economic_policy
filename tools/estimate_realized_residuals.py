#!/usr/bin/env python3
"""Estimate realized-data residual evidence without overstating production calibration.

This layer is deliberately separate from the SEP vintage estimator.  It consumes
realized/revised observations, computes model-relevant residual candidates when
identification is available, and emits an auditable frontier ledger.  A numeric
estimate is not direct-eligible merely because it can be computed: direct
promotion requires the realized panel to contain the production equation's
material conditioning variables and to pass sample / finite / range guards.

The committed bootstrap currently identifies a revised Canadian GDP growth
innovation scale.  It remains reference-only because the production growth
shock is conditional on output-gap, credit-spread and policy-coordination terms
that are not all available as a long realized panel yet.
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PANEL = ROOT / "data/calibration/quarterly_estimation_panel.csv"
DEFAULT_OUTPUT = ROOT / "data/calibration/realized_calibration_frontier.csv"
MIN_OBSERVATIONS = 60
MAX_ABS_ANNUALIZED_GDP_GROWTH = 15.0

SHOCK_PARAMETERS = (
    "output_shock_sd",
    "inflation_shock_sd",
    "growth_shock_sd",
    "us_growth_shock_sd",
    "export_shock_sd",
    "us_export_shock_sd",
)

MULTIPLIER_PARAMETERS = (
    "fiscal_demand_multiplier",
    "real_rate_demand_sensitivity",
    "productive_supply_multiplier",
    "global_growth_sensitivity",
    "phillips_curve_slope",
    "fx_pass_through",
    "import_price_pass_through",
    "oil_inflation_sensitivity",
    "canada_trade_drag_scale",
    "us_retaliation_drag_scale",
    "tariff_revenue_elasticity_scale",
)

FIELDNAMES = (
    "parameter",
    "family",
    "status",
    "estimate",
    "observations",
    "sample_period",
    "method",
    "direct_eligible",
    "rejected_observations",
    "notes",
)


@dataclass(frozen=True)
class FrontierRow:
    parameter: str
    family: str
    status: str
    estimate: str
    observations: int
    sample_period: str
    method: str
    direct_eligible: bool
    rejected_observations: int
    notes: str

    def as_dict(self) -> dict[str, str]:
        return {
            "parameter": self.parameter,
            "family": self.family,
            "status": self.status,
            "estimate": self.estimate,
            "observations": str(self.observations),
            "sample_period": self.sample_period,
            "method": self.method,
            "direct_eligible": "true" if self.direct_eligible else "false",
            "rejected_observations": str(self.rejected_observations),
            "notes": self.notes,
        }


def number(value: str | None) -> float | None:
    if value is None or value.strip() in {"", "..", "NA", "N/A"}:
        return None
    try:
        out = float(value)
    except ValueError:
        return None
    return out if math.isfinite(out) else None


def read_panel(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        rows = list(csv.DictReader(handle))
    if not rows or "quarter" not in rows[0]:
        raise ValueError(f"{path} is not a quarterly calibration panel")
    return rows


def sample_period(rows: list[dict[str, str]]) -> str:
    quarters = [row.get("quarter", "") for row in rows if row.get("quarter")]
    return f"{min(quarters)}-{max(quarters)}" if quarters else "none"


def realized_growth_evidence(rows: list[dict[str, str]]) -> FrontierRow:
    accepted: list[float] = []
    rejected = 0
    accepted_rows: list[dict[str, str]] = []
    for row in rows:
        value = number(row.get("statcan_gdp_growth"))
        if value is None:
            continue
        # The source table has changed chained-dollar bases over time.  A
        # mechanically spliced level series can create a one-quarter pseudo
        # growth rate at a chain break.  Annualized moves beyond this guard are
        # quarantined rather than silently treated as macro innovations.
        if abs(value) > MAX_ABS_ANNUALIZED_GDP_GROWTH:
            rejected += 1
            continue
        accepted.append(value)
        accepted_rows.append(row)

    if len(accepted) < MIN_OBSERVATIONS:
        return FrontierRow(
            "growth_shock_sd", "shock-variance", "blocked-insufficient-realized-data", "",
            len(accepted), sample_period(accepted_rows), "realized revised GDP growth residual SD",
            False, rejected,
            "Need at least 60 finite, sanity-screened quarterly observations.",
        )

    estimate = statistics.stdev(accepted)
    return FrontierRow(
        "growth_shock_sd", "shock-variance", "estimated-reference-only",
        f"{estimate:.10f}", len(accepted), sample_period(accepted_rows),
        "sample SD of demeaned Statistics Canada annualized quarterly real-GDP growth",
        False, rejected,
        "Realized GDP innovation scale is identified, but the production growth equation also conditions on output gap, credit spread and coordinated policy; retain as reference-only until those realized regressors are frozen for the same sample.",
    )


def blocked(parameter: str, family: str, reason: str) -> FrontierRow:
    return FrontierRow(
        parameter, family, "blocked-missing-realized-identification", "", 0, "none",
        "not estimated", False, 0, reason,
    )


def estimate_frontier(rows: list[dict[str, str]]) -> list[FrontierRow]:
    out: list[FrontierRow] = []
    for parameter in SHOCK_PARAMETERS:
        if parameter == "growth_shock_sd":
            out.append(realized_growth_evidence(rows))
        elif parameter == "output_shock_sd":
            out.append(blocked(parameter, "shock-variance", "Requires a frozen ex-post output-gap series aligned to the production output equation; SEP current-quarter residuals remain reference-only."))
        elif parameter == "inflation_shock_sd":
            out.append(blocked(parameter, "shock-variance", "Requires realized core-inflation observations plus the production inflation equation's material conditioning channels; SEP forecast-vintage residuals are not realized shocks."))
        elif parameter == "us_growth_shock_sd":
            out.append(blocked(parameter, "shock-variance", "Requires a frozen realized U.S. quarterly growth panel with the bilateral-policy conditioning variables."))
        elif parameter == "export_shock_sd":
            out.append(blocked(parameter, "shock-variance", "Requires a frozen realized Canadian export-growth history mapped to the production bilateral export equation."))
        else:
            out.append(blocked(parameter, "shock-variance", "Requires a frozen realized U.S.-to-Canada export-growth history mapped to the production bilateral export equation."))

    multiplier_reasons = {
        "fiscal_demand_multiplier": "Requires dated realized fiscal impulses with a compatible output-gap response panel.",
        "real_rate_demand_sensitivity": "Requires a realized output-gap panel and the production real-rate conditioning set; the existing SEP estimate is intentionally reference-only.",
        "productive_supply_multiplier": "Requires dated productive fiscal impulses and realized inflation/output responses.",
        "global_growth_sensitivity": "Requires a frozen global-growth series aligned to the realized Canadian output-gap equation.",
        "phillips_curve_slope": "Requires realized core inflation, output gap, expectations, FX, import-price, oil and supply controls in one aligned panel.",
        "fx_pass_through": "Requires realized core inflation and the full production inflation conditioning set; firm-level literature remains a mapping reference.",
        "import_price_pass_through": "Requires realized aggregate import-price incidence aligned to the model's tariff-price construction.",
        "oil_inflation_sensitivity": "Requires realized core inflation with output-gap, expectations, FX, import-price and supply controls.",
        "canada_trade_drag_scale": "Requires realized bilateral tariff/barrier episodes and Canadian export/output responses.",
        "us_retaliation_drag_scale": "Requires realized Canadian retaliation episodes and U.S.-to-Canada export/output responses.",
        "tariff_revenue_elasticity_scale": "Requires customs-revenue and tariff-base observations under dated effective tariff schedules.",
    }
    out.extend(blocked(p, "multiplier", multiplier_reasons[p]) for p in MULTIPLIER_PARAMETERS)
    return out


def write_frontier(path: Path, rows: list[FrontierRow]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDNAMES, lineterminator="\n")
        writer.writeheader()
        writer.writerows(row.as_dict() for row in rows)


def read_frontier(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def verify(panel: Path, output: Path) -> None:
    expected = [row.as_dict() for row in estimate_frontier(read_panel(panel))]
    actual = read_frontier(output)
    if actual != expected:
        raise SystemExit(
            "realized calibration frontier drifted; run "
            "python3 tools/estimate_realized_residuals.py --refresh and review the evidence mapping"
        )
    growth = next(row for row in actual if row["parameter"] == "growth_shock_sd")
    if growth["direct_eligible"] != "false" or growth["status"] != "estimated-reference-only":
        raise SystemExit("realized GDP evidence must remain reference-only until the full production conditioning set is frozen")
    if int(growth["rejected_observations"]) < 1:
        raise SystemExit("expected chained-series break quarantine is no longer represented in the frozen panel")
    print(
        "realized calibration frontier verified: "
        f"{growth['observations']} GDP observations, estimate={growth['estimate']}, "
        f"rejected={growth['rejected_observations']}"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--panel", type=Path, default=DEFAULT_PANEL)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--refresh", action="store_true")
    mode.add_argument("--verify", action="store_true")
    args = parser.parse_args()

    if args.refresh:
        rows = estimate_frontier(read_panel(args.panel))
        write_frontier(args.output, rows)
        growth = next(row for row in rows if row.parameter == "growth_shock_sd")
        print(f"wrote {args.output}: growth shock reference estimate {growth.estimate} from {growth.observations} observations")
    else:
        verify(args.panel, args.output)


if __name__ == "__main__":
    main()

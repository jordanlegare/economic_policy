#!/usr/bin/env python3
"""Estimate sector tariff elasticities and price pass-through from historical episodes.

This is intentionally a transparent reduced-form estimator, not a claim of causal
identification. Each input row describes one historical tariff episode and its
matched/control movement. The output is consumed by refresh_calibration.py.

Required columns:
  sector_index
  pre_trade
  post_trade
  tariff_change_pp
  control_trade_change_pct
  pre_price
  post_price
  control_price_change_pct
  weight

Conventions:
  * tariff_change_pp is percentage points (e.g. +25 for a 25-point tariff rise)
  * trade elasticity is reported as a positive sensitivity: a tariff increase
    that reduces trade produces a positive elasticity
  * pass-through is the net log price response divided by the tariff change
  * estimates include weighted standard errors across episodes

For publication-grade use, the episode/control construction itself should be
reviewed by a trade economist and replaced with a stronger panel/event-study or
structural estimator when suitable microdata are available.
"""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path
import sys


def positive(value: str, name: str) -> float:
    x = float(value)
    if x <= 0:
        raise ValueError(f"{name} must be > 0")
    return x


def estimate(rows: list[dict[str, str]]) -> list[dict[str, float]]:
    grouped: dict[int, list[tuple[float, float, float]]] = {}
    for line_no, row in enumerate(rows, start=2):
        try:
            sector = int(row["sector_index"])
            if not 0 <= sector < 20:
                raise ValueError("sector_index must be 0..19")
            pre_trade = positive(row["pre_trade"], "pre_trade")
            post_trade = positive(row["post_trade"], "post_trade")
            pre_price = positive(row["pre_price"], "pre_price")
            post_price = positive(row["post_price"], "post_price")
            tariff = float(row["tariff_change_pp"]) / 100.0
            if abs(tariff) < 1e-9:
                raise ValueError("tariff_change_pp cannot be zero")
            control_trade = float(row.get("control_trade_change_pct") or 0.0) / 100.0
            control_price = float(row.get("control_price_change_pct") or 0.0) / 100.0
            weight = float(row.get("weight") or 1.0)
            if weight <= 0:
                raise ValueError("weight must be > 0")
        except (KeyError, ValueError) as exc:
            raise ValueError(f"row {line_no}: {exc}") from exc

        net_trade_log_change = math.log(post_trade / pre_trade) - control_trade
        net_price_log_change = math.log(post_price / pre_price) - control_price
        elasticity = -net_trade_log_change / tariff
        pass_through = net_price_log_change / tariff
        grouped.setdefault(sector, []).append((elasticity, pass_through, weight))

    output: list[dict[str, float]] = []
    for sector in sorted(grouped):
        episodes = grouped[sector]
        weight_sum = sum(w for _, _, w in episodes)
        elasticity = sum(e * w for e, _, w in episodes) / weight_sum
        pass_through = sum(p * w for _, p, w in episodes) / weight_sum

        # Frequency-style weighted sample uncertainty. This is deliberately
        # reported, not hidden. A single episode receives an infinite/unknown
        # standard error rather than false precision.
        if len(episodes) > 1:
            e_var_num = sum(w * (e - elasticity) ** 2 for e, _, w in episodes)
            p_var_num = sum(w * (p - pass_through) ** 2 for _, p, w in episodes)
            e_sd = math.sqrt(e_var_num / weight_sum)
            p_sd = math.sqrt(p_var_num / weight_sum)
            n_eff = weight_sum ** 2 / sum(w ** 2 for _, _, w in episodes)
            e_se = e_sd / math.sqrt(max(1.0, n_eff))
            p_se = p_sd / math.sqrt(max(1.0, n_eff))
        else:
            e_se = math.inf
            p_se = math.inf

        output.append({
            "sector_index": sector,
            "trade_elasticity": elasticity,
            "trade_elasticity_se": e_se,
            "price_pass_through": pass_through,
            "price_pass_through_se": p_se,
            "episodes": len(episodes),
            "effective_weight": weight_sum,
        })
    return output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--require-all-sectors", action="store_true")
    args = parser.parse_args()

    try:
        with args.input.open(newline="", encoding="utf-8") as fh:
            rows = list(csv.DictReader(fh))
        estimates = estimate(rows)
    except Exception as exc:
        print(f"estimation failed: {exc}", file=sys.stderr)
        return 2

    if args.require_all_sectors and {int(x["sector_index"]) for x in estimates} != set(range(20)):
        print("release estimate requires all 20 model sectors", file=sys.stderr)
        return 3
    if args.require_all_sectors and any(not math.isfinite(x["trade_elasticity_se"]) or not math.isfinite(x["price_pass_through_se"]) for x in estimates):
        print("release estimate requires at least two usable episodes per sector", file=sys.stderr)
        return 4

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=[
            "sector_index", "trade_elasticity", "trade_elasticity_se",
            "price_pass_through", "price_pass_through_se", "episodes", "effective_weight"
        ])
        writer.writeheader()
        for row in estimates:
            writer.writerow({key: (f"{value:.10g}" if isinstance(value, float) else value)
                             for key, value in row.items()})

    print(f"wrote {len(estimates)} sector estimates to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

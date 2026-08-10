"""
BANK OF CANADA
INSTITUTIONAL MONETARY-POLICY & FINANCIAL-SUSTAINABILITY ENGINE
================================================================

Research-oriented stochastic policy simulator.

CORE QUESTION
-------------
What monetary-policy path minimizes inflation/output/labour-market losses
while keeping the Bank of Canada's projected financial position within a
defined institutional-risk envelope?

IMPORTANT INSTITUTIONAL DISTINCTION
-----------------------------------
The Bank of Canada is NOT modeled as a federal government and does not have
a conventional fiscal deficit.

The model therefore distinguishes:

    Government fiscal deficit
        !=
    Bank of Canada accounting accumulated deficit / equity position

The Bank can experience accounting losses without losing its ability to
conduct monetary policy.

The financial module is therefore treated as an INSTITUTIONAL RISK CONSTRAINT,
not as a requirement that the Bank maximize profit.

MODEL STRUCTURE
---------------

1. Macro state
2. Latent economic state
3. Monetary-policy path
4. Credit / housing / FX transmission
5. Bank balance-sheet approximation
6. Bank income statement
7. Monte-Carlo simulation
8. Institutional risk constraints
9. Policy-path optimization
10. Stress testing
11. Risk-profile comparison
12. Institutional report

This is NOT the Bank of Canada's official forecasting model and does not
replicate its internal accounting, DSGE, projection, or policy framework.

Dependencies
------------
numpy
pandas
matplotlib

Python 3.10+
"""

from __future__ import annotations

from dataclasses import dataclass, asdict
from itertools import product
from typing import Dict, List, Optional, Tuple

import logging
import math

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt


# ======================================================================
# 0. LOGGING
# ======================================================================

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)

logger = logging.getLogger("BoCInstitutionalEngine")


# ======================================================================
# 1. MACROECONOMIC STATE
# ======================================================================

@dataclass
class MacroState:
    """
    Observable macro-financial state.

    Rates are percentages unless otherwise indicated.
    """

    # Monetary policy
    overnight_rate: float = 2.25
    market_terminal_rate: float = 2.25

    # FX / commodities
    usdcad: float = 1.39
    oil_price: float = 72.0

    # Inflation components
    services_inflation: float = 2.8
    goods_inflation: float = 1.5
    shelter_inflation: float = 3.5
    admin_inflation: float = 1.8

    # Real economy
    gdp_growth: float = 1.2
    unemployment: float = 6.4
    wage_growth: float = 3.0

    # Financial conditions
    credit_spread: float = 1.40
    housing_index: float = 182.0
    household_leverage: float = 175.0

    # Lending rates
    mortgage_rate: float = 4.55
    business_rate: float = 5.05
    credit_growth: float = 3.2

    # Yield curve
    term_premium: float = 0.65
    expected_short_rate_10y: float = 2.99

    @classmethod
    def from_dict(cls, data: dict) -> "MacroState":

        valid_fields = {
            f.name
            for f in cls.__dataclass_fields__.values()
        }

        filtered = {
            k: v
            for k, v in data.items()
            if k in valid_fields
        }

        return cls(**filtered)


# ======================================================================
# 2. LATENT ECONOMIC STATE
# ======================================================================

@dataclass
class LatentState:

    potential_growth: float = 1.8
    neutral_rate: float = 2.5
    nairu: float = 5.8

    output_gap: float = -0.3

    inflation_trend: float = 2.0

    regime: str = "normal"


# ======================================================================
# 3. POLICY PATH
# ======================================================================

@dataclass
class PolicyPath:

    q1: int = 0
    q2: int = 0
    q3: int = 0
    q4: int = 0
    q5: int = 0
    q6: int = 0
    q7: int = 0
    q8: int = 0

    def as_list(self) -> List[int]:

        return [
            self.q1,
            self.q2,
            self.q3,
            self.q4,
            self.q5,
            self.q6,
            self.q7,
            self.q8,
        ]

    def rate_path(
        self,
        initial_rate: float,
        horizon: int = 16,
    ) -> List[float]:

        changes = self.as_list()

        rates = []
        rate = initial_rate

        for q in range(horizon):

            bp = (
                changes[q]
                if q < len(changes)
                else changes[-1]
            )

            rate = float(
                np.clip(
                    rate + bp / 100.0,
                    0.0,
                    8.0,
                )
            )

            rates.append(rate)

        return rates


# ======================================================================
# 4. BANK OF CANADA INSTITUTIONAL FINANCIAL STATE
# ======================================================================

@dataclass
class BankFinancialState:
    """
    Simplified Bank of Canada balance-sheet / income-state representation.

    Dollar values are CAD billions.

    This is a policy-analysis approximation and NOT IFRS accounting software.
    """

    # ------------------------------------------------------------------
    # Assets
    # ------------------------------------------------------------------

    investment_assets: float = 192.2
    loans_receivables: float = 27.8

    # ------------------------------------------------------------------
    # Liabilities / monetary base
    # ------------------------------------------------------------------

    government_deposits: float = 65.0
    settlement_balances: float = 59.4
    bank_notes: float = 124.3

    # ------------------------------------------------------------------
    # Asset characteristics
    # ------------------------------------------------------------------

    asset_duration: float = 5.5
    asset_yield_spread: float = 0.35

    # ------------------------------------------------------------------
    # Deposit remuneration
    # ------------------------------------------------------------------

    deposit_spread: float = 0.05

    # ------------------------------------------------------------------
    # Operating cost
    # ------------------------------------------------------------------

    annual_operating_expense: float = 0.740

    # ------------------------------------------------------------------
    # Existing accumulated accounting position
    # ------------------------------------------------------------------

    accumulated_deficit: float = -9.899

    reserves: float = 0.100
    revaluation_reserve: float = 0.656
    actuarial_reserve: float = 0.616

    # ------------------------------------------------------------------
    # Balance-sheet normalization
    # ------------------------------------------------------------------

    quarterly_asset_runoff: float = 0.015

    def copy(self) -> "BankFinancialState":

        return BankFinancialState(
            **asdict(self)
        )


# ======================================================================
# 5. FINANCIAL METRICS
# ======================================================================

@dataclass
class FinancialMetrics:

    interest_revenue: float
    interest_expense: float
    net_interest_income: float

    operating_expense: float

    net_income: float

    accumulated_deficit: float
    equity_position: float

    asset_size: float


# ======================================================================
# 6. INSTITUTIONAL RISK PROFILE
# ======================================================================

@dataclass
class InstitutionalRiskLimits:
    """
    Institutional-risk envelope.

    These are configurable research assumptions, not official BoC limits.
    """

    name: str = "standard"

    # Probability limits
    max_probability_quarterly_loss: float = 0.35
    max_probability_material_loss: float = 0.15
    max_probability_extreme_deficiency: float = 0.10

    # Four-quarter financial loss
    max_four_quarter_loss: float = 1.00

    # Terminal accumulated accounting position
    minimum_terminal_equity: float = -10.0

    # Tail inflation
    max_probability_inflation_above_4: float = 0.20

    # Recession / financial stress
    max_probability_credit_spread_above_3: float = 0.15


def make_risk_profiles() -> Dict[str, InstitutionalRiskLimits]:

    return {

        "conservative": InstitutionalRiskLimits(
            name="conservative",

            max_probability_quarterly_loss=0.20,
            max_probability_material_loss=0.08,
            max_probability_extreme_deficiency=0.05,

            max_four_quarter_loss=0.75,

            minimum_terminal_equity=-9.5,

            max_probability_inflation_above_4=0.15,
            max_probability_credit_spread_above_3=0.10,
        ),

        "standard": InstitutionalRiskLimits(
            name="standard",

            max_probability_quarterly_loss=0.35,
            max_probability_material_loss=0.15,
            max_probability_extreme_deficiency=0.10,

            max_four_quarter_loss=1.00,

            minimum_terminal_equity=-10.0,

            max_probability_inflation_above_4=0.20,
            max_probability_credit_spread_above_3=0.15,
        ),

        "mandate_first": InstitutionalRiskLimits(
            name="mandate_first",

            max_probability_quarterly_loss=0.50,
            max_probability_material_loss=0.25,
            max_probability_extreme_deficiency=0.15,

            max_four_quarter_loss=1.50,

            minimum_terminal_equity=-11.0,

            max_probability_inflation_above_4=0.30,
            max_probability_credit_spread_above_3=0.25,
        ),
    }


# ======================================================================
# 7. POLICY OBJECTIVE WEIGHTS
# ======================================================================

@dataclass
class PolicyWeights:

    inflation: float = 5.0
    unemployment: float = 1.5
    output: float = 1.5

    housing: float = 0.40
    credit: float = 0.40

    policy_smoothing: float = 0.75
    policy_level: float = 0.10

    inflation_tail: float = 1.50
    financial_tail: float = 1.00


# ======================================================================
# 8. MAIN POLICY ENGINE
# ======================================================================

class ResearchPolicyEngine:

    TARGET = 2.0

    def __init__(
        self,
        seed: int = 42,
        weights: Optional[PolicyWeights] = None,
    ):

        self.seed = seed

        self.weights = (
            weights
            or PolicyWeights()
        )

    # ==================================================================
    # INFLATION
    # ==================================================================

    def core_inflation(
        self,
        s: MacroState,
    ) -> float:

        return (
            0.45 * s.services_inflation
            + 0.25 * s.goods_inflation
            + 0.25 * s.shelter_inflation
            + 0.05 * s.admin_inflation
        )

    # ==================================================================
    # REGIME DETECTION
    # ==================================================================

    def detect_regime(
        self,
        s: MacroState,
    ) -> str:

        core = self.core_inflation(s)

        if core >= 4.0:
            return "inflation_shock"

        if s.credit_spread >= 2.25:
            return "financial_stress"

        if s.gdp_growth <= -1.0:
            return "deep_recession"

        if s.gdp_growth < 0:
            return "recession"

        if core >= 3.0:
            return "inflation_pressure"

        return "normal"

    # ==================================================================
    # LATENT STATE
    # ==================================================================

    def estimate_latent(
        self,
        s: MacroState,
    ) -> LatentState:

        regime = self.detect_regime(s)

        neutral_rates = {
            "inflation_shock": 2.70,
            "inflation_pressure": 2.60,
            "financial_stress": 2.10,
            "deep_recession": 1.80,
            "recession": 2.10,
            "normal": 2.50,
        }

        potential_growth = {
            "inflation_shock": 1.75,
            "inflation_pressure": 1.75,
            "financial_stress": 1.60,
            "deep_recession": 1.40,
            "recession": 1.60,
            "normal": 1.80,
        }

        neutral = neutral_rates[regime]
        potential = potential_growth[regime]

        output_gap = float(
            np.clip(
                s.gdp_growth - potential,
                -5.0,
                5.0,
            )
        )

        return LatentState(
            potential_growth=potential,
            neutral_rate=neutral,
            nairu=5.8,
            output_gap=output_gap,
            inflation_trend=self.core_inflation(s),
            regime=regime,
        )

    # ==================================================================
    # CREDIT CONDITIONS
    # ==================================================================

    def update_credit(
        self,
        s: MacroState,
        latent: LatentState,
    ) -> None:

        core = self.core_inflation(s)

        real_rate = (
            s.overnight_rate - core
        )

        s.mortgage_rate = (
            s.overnight_rate
            + 2.20
            + 0.25 * s.credit_spread
        )

        s.business_rate = (
            s.overnight_rate
            + 2.70
            + 0.40 * s.credit_spread
        )

        s.credit_growth = float(
            np.clip(
                3.5
                - 0.60 * real_rate
                - 0.45 * (
                    s.credit_spread - 1.40
                )
                + 0.15 * latent.output_gap,
                -4.0,
                10.0,
            )
        )

    # ==================================================================
    # BANK FINANCIAL MODEL
    # ==================================================================

    def bank_financial_step(
        self,
        bank: BankFinancialState,
        s: MacroState,
    ) -> Tuple[
        BankFinancialState,
        FinancialMetrics,
    ]:

        b = bank.copy()

        # --------------------------------------------------------------
        # Asset runoff
        # --------------------------------------------------------------

        b.investment_assets *= (
            1.0 - b.quarterly_asset_runoff
        )

        b.investment_assets = max(
            b.investment_assets,
            130.0,
        )

        # --------------------------------------------------------------
        # Effective asset yield
        # --------------------------------------------------------------

        policy_rate = s.overnight_rate

        effective_asset_yield = (
            policy_rate
            + b.asset_yield_spread
            - 0.015 * b.asset_duration
        )

        effective_asset_yield = float(
            np.clip(
                effective_asset_yield,
                0.25,
                8.0,
            )
        )

        # --------------------------------------------------------------
        # Interest revenue
        # --------------------------------------------------------------

        interest_revenue = (
            b.investment_assets
            * effective_asset_yield
            / 100.0
        )

        loan_yield = (
            policy_rate
            + 1.10
        )

        interest_revenue += (
            b.loans_receivables
            * loan_yield
            / 100.0
        )

        # --------------------------------------------------------------
        # Interest expense
        # --------------------------------------------------------------

        remuneration_rate = max(
            policy_rate
            - b.deposit_spread,
            0.0,
        )

        interest_expense = (
            (
                b.government_deposits
                + b.settlement_balances
            )
            * remuneration_rate
            / 100.0
        )

        # Additional financial stress cost
        stress_penalty = (
            max(
                s.credit_spread - 1.40,
                0.0,
            )
            * 0.10
        )

        interest_expense += stress_penalty

        # --------------------------------------------------------------
        # Net interest income
        # --------------------------------------------------------------

        net_interest_income = (
            interest_revenue
            - interest_expense
        )

        # --------------------------------------------------------------
        # Operating costs
        # --------------------------------------------------------------

        quarterly_opex = (
            b.annual_operating_expense
            / 4.0
        )

        # --------------------------------------------------------------
        # Net income
        # --------------------------------------------------------------

        net_income = (
            net_interest_income
            - quarterly_opex
        )

        # --------------------------------------------------------------
        # Accounting accumulated position
        # --------------------------------------------------------------

        b.accumulated_deficit += net_income

        # --------------------------------------------------------------
        # Simplified equity position
        # --------------------------------------------------------------

        equity_position = (
            b.accumulated_deficit
            + b.reserves
            + b.revaluation_reserve
            + b.actuarial_reserve
        )

        asset_size = (
            b.investment_assets
            + b.loans_receivables
        )

        metrics = FinancialMetrics(
            interest_revenue=float(
                interest_revenue
            ),
            interest_expense=float(
                interest_expense
            ),
            net_interest_income=float(
                net_interest_income
            ),
            operating_expense=float(
                quarterly_opex
            ),
            net_income=float(
                net_income
            ),
            accumulated_deficit=float(
                b.accumulated_deficit
            ),
            equity_position=float(
                equity_position
            ),
            asset_size=float(
                asset_size
            ),
        )

        return b, metrics

    # ==================================================================
    # MACROECONOMIC STEP
    # ==================================================================

    def step(
        self,
        s: MacroState,
        latent: LatentState,
        bank: BankFinancialState,
        rate_change_bp: int,
        rng: np.random.Generator,
    ) -> Tuple[
        MacroState,
        LatentState,
        BankFinancialState,
        FinancialMetrics,
    ]:

        ns = MacroState(
            **asdict(s)
        )

        nl = LatentState(
            **asdict(latent)
        )

        previous_rate = (
            ns.overnight_rate
        )

        # --------------------------------------------------------------
        # Monetary policy
        # --------------------------------------------------------------

        ns.overnight_rate = float(
            np.clip(
                ns.overnight_rate
                + rate_change_bp / 100.0,
                0.0,
                8.0,
            )
        )

        # --------------------------------------------------------------
        # Expectations
        # --------------------------------------------------------------

        ns.expected_short_rate_10y = (
            0.85 * s.expected_short_rate_10y
            + 0.15 * ns.overnight_rate
        )

        # --------------------------------------------------------------
        # Term premium
        # --------------------------------------------------------------

        ns.term_premium += float(
            rng.normal(
                0.0,
                0.015,
            )
        )

        if nl.regime == "financial_stress":
            ns.term_premium += 0.05

        ns.term_premium = float(
            np.clip(
                ns.term_premium,
                0.0,
                3.0,
            )
        )

        # --------------------------------------------------------------
        # Credit spreads
        # --------------------------------------------------------------

        rate_change = (
            ns.overnight_rate
            - previous_rate
        )

        ns.credit_spread = float(
            np.clip(
                s.credit_spread
                + 0.12 * rate_change
                + 0.08 * max(
                    -nl.output_gap,
                    0.0,
                )
                + rng.normal(
                    0.0,
                    0.025,
                ),
                0.50,
                5.0,
            )
        )

        self.update_credit(
            ns,
            nl,
        )

        # --------------------------------------------------------------
        # Real interest rate
        # --------------------------------------------------------------

        current_core = (
            self.core_inflation(s)
        )

        real_rate = (
            ns.overnight_rate
            - current_core
        )

        # --------------------------------------------------------------
        # Demand impulse
        # --------------------------------------------------------------

        demand_impulse = (
            0.12 * (
                ns.credit_growth - 3.0
            )
            - 0.08 * (
                real_rate
                - nl.neutral_rate
            )
        )

        # --------------------------------------------------------------
        # GDP
        # --------------------------------------------------------------

        ns.gdp_growth = float(
            np.clip(
                0.78 * s.gdp_growth
                + 0.22 * (
                    nl.potential_growth
                    + demand_impulse
                )
                + rng.normal(
                    0.0,
                    0.15,
                ),
                -4.0,
                6.0,
            )
        )

        # --------------------------------------------------------------
        # Unemployment
        # --------------------------------------------------------------

        unemployment_pressure = (
            -0.22 * (
                ns.gdp_growth
                - nl.potential_growth
            )
        )

        ns.unemployment = float(
            np.clip(
                0.90 * s.unemployment
                + 0.10 * (
                    5.8
                    + unemployment_pressure
                )
                + rng.normal(
                    0.0,
                    0.05,
                ),
                3.5,
                12.0,
            )
        )

        # --------------------------------------------------------------
        # Wage growth
        # --------------------------------------------------------------

        ns.wage_growth = float(
            np.clip(
                0.82 * s.wage_growth
                + 0.10 * current_core
                + 0.08 * (
                    nl.nairu
                    - ns.unemployment
                ),
                1.0,
                8.0,
            )
        )

        # --------------------------------------------------------------
        # FX
        # --------------------------------------------------------------

        rate_differential = (
            ns.overnight_rate
            - 2.25
        )

        ns.usdcad = float(
            np.clip(
                0.90 * s.usdcad
                + 0.10 * (
                    1.39
                    - 0.035 * rate_differential
                )
                + rng.normal(
                    0.0,
                    0.005,
                ),
                1.05,
                1.70,
            )
        )

        # --------------------------------------------------------------
        # Oil
        # --------------------------------------------------------------

        ns.oil_price = float(
            np.clip(
                0.90 * s.oil_price
                + 0.10 * 72.0
                + rng.normal(
                    0.0,
                    2.0,
                ),
                25.0,
                160.0,
            )
        )

        # --------------------------------------------------------------
        # Housing
        # --------------------------------------------------------------

        housing_demand = (
            0.25 * (
                ns.credit_growth
                - 3.0
            )
            - 0.15 * (
                ns.mortgage_rate
                - 4.5
            )
        )

        ns.housing_index = float(
            np.clip(
                s.housing_index
                * (
                    1.0
                    + housing_demand / 100.0
                )
                + rng.normal(
                    0.0,
                    0.7,
                ),
                100.0,
                320.0,
            )
        )

        # --------------------------------------------------------------
        # Household leverage
        # --------------------------------------------------------------

        ns.household_leverage = float(
            np.clip(
                0.96 * s.household_leverage
                + 0.04 * (
                    s.household_leverage
                    + 0.8 * (
                        ns.credit_growth
                        - 3.0
                    )
                ),
                100.0,
                230.0,
            )
        )

        # --------------------------------------------------------------
        # Inflation
        # --------------------------------------------------------------

        demand_gap = (
            ns.gdp_growth
            - nl.potential_growth
        )

        exchange_pressure = (
            ns.usdcad
            - 1.39
        )

        oil_pressure = (
            ns.oil_price
            - 72.0
        ) / 10.0

        # Services
        ns.services_inflation = float(
            np.clip(
                0.80 * s.services_inflation
                + 0.12 * ns.wage_growth
                + 0.15 * demand_gap
                - 0.08 * (
                    ns.overnight_rate
                    - nl.neutral_rate
                )
                + rng.normal(
                    0.0,
                    0.05,
                ),
                0.0,
                10.0,
            )
        )

        # Goods
        ns.goods_inflation = float(
            np.clip(
                0.70 * s.goods_inflation
                + 0.12 * (
                    exchange_pressure * 10.0
                )
                + 0.06 * oil_pressure
                + rng.normal(
                    0.0,
                    0.06,
                ),
                -2.0,
                10.0,
            )
        )

        # Shelter
        ns.shelter_inflation = float(
            np.clip(
                0.85 * s.shelter_inflation
                + 0.04 * (
                    ns.housing_index
                    - s.housing_index
                )
                - 0.10 * (
                    ns.overnight_rate
                    - nl.neutral_rate
                )
                + rng.normal(
                    0.0,
                    0.04,
                ),
                0.0,
                12.0,
            )
        )

        # Administered prices
        ns.admin_inflation = float(
            np.clip(
                0.95 * s.admin_inflation
                + 0.05 * self.TARGET
                + rng.normal(
                    0.0,
                    0.015,
                ),
                0.0,
                8.0,
            )
        )

        # --------------------------------------------------------------
        # Output gap
        # --------------------------------------------------------------

        nl.output_gap = float(
            np.clip(
                0.72 * latent.output_gap
                + 0.18 * (
                    ns.credit_growth
                    - 3.0
                )
                - 0.15 * (
                    ns.overnight_rate
                    - nl.neutral_rate
                )
                + rng.normal(
                    0.0,
                    0.08,
                ),
                -5.0,
                5.0,
            )
        )

        # --------------------------------------------------------------
        # Bank financial state
        # --------------------------------------------------------------

        new_bank, financial_metrics = (
            self.bank_financial_step(
                bank,
                ns,
            )
        )

        return (
            ns,
            nl,
            new_bank,
            financial_metrics,
        )

    # ==================================================================
    # FORECAST
    # ==================================================================

    def forecast(
        self,
        state: MacroState,
        path: PolicyPath,
        horizon: int = 16,
        simulations: int = 1000,
        bank_state: Optional[
            BankFinancialState
        ] = None,
        seed: Optional[int] = None,
    ) -> pd.DataFrame:

        rng = np.random.default_rng(
            self.seed
            if seed is None
            else seed
        )

        records = []

        initial_bank = (
            bank_state.copy()
            if bank_state is not None
            else BankFinancialState()
        )

        policy_changes = path.as_list()

        for sim in range(simulations):

            s = MacroState(
                **asdict(state)
            )

            latent = self.estimate_latent(
                s
            )

            bank = initial_bank.copy()

            cumulative_net_income = 0.0

            for q in range(horizon):

                if q < len(policy_changes):
                    bp = policy_changes[q]
                else:
                    bp = policy_changes[-1]

                (
                    s,
                    latent,
                    bank,
                    fm,
                ) = self.step(
                    s,
                    latent,
                    bank,
                    bp,
                    rng,
                )

                cumulative_net_income += (
                    fm.net_income
                )

                records.append({

                    "sim": sim,
                    "q": q + 1,

                    # Policy
                    "rate_change_bp": bp,
                    "overnight_rate":
                        s.overnight_rate,

                    # Inflation
                    "core":
                        self.core_inflation(s),
                    "services":
                        s.services_inflation,
                    "goods":
                        s.goods_inflation,
                    "shelter":
                        s.shelter_inflation,
                    "admin":
                        s.admin_inflation,

                    # Economy
                    "gdp":
                        s.gdp_growth,
                    "unemployment":
                        s.unemployment,
                    "output_gap":
                        latent.output_gap,
                    "wage_growth":
                        s.wage_growth,

                    # Financial conditions
                    "housing":
                        s.housing_index,
                    "household_leverage":
                        s.household_leverage,
                    "credit_spread":
                        s.credit_spread,
                    "credit_growth":
                        s.credit_growth,
                    "mortgage_rate":
                        s.mortgage_rate,
                    "business_rate":
                        s.business_rate,

                    # FX / commodity
                    "usdcad":
                        s.usdcad,
                    "oil_price":
                        s.oil_price,

                    # Yield curve
                    "term_premium":
                        s.term_premium,
                    "expected_short_rate_10y":
                        s.expected_short_rate_10y,

                    # Bank financials
                    "bank_interest_revenue":
                        fm.interest_revenue,

                    "bank_interest_expense":
                        fm.interest_expense,

                    "bank_net_interest_income":
                        fm.net_interest_income,

                    "bank_operating_expense":
                        fm.operating_expense,

                    "bank_net_income":
                        fm.net_income,

                    "bank_cumulative_net_income":
                        cumulative_net_income,

                    "bank_accumulated_deficit":
                        fm.accumulated_deficit,

                    "bank_equity_position":
                        fm.equity_position,

                    "bank_assets":
                        fm.asset_size,
                })

        return pd.DataFrame(
            records
        )

    # ==================================================================
    # MACRO LOSS
    # ==================================================================

    def macro_loss(
        self,
        df: pd.DataFrame,
        path: PolicyPath,
    ) -> float:

        w = self.weights

        # --------------------------------------------------------------
        # Terminal outcomes
        # --------------------------------------------------------------

        terminal = (
            df.groupby("sim")
            .tail(1)
        )

        # --------------------------------------------------------------
        # Inflation
        # --------------------------------------------------------------

        inflation_loss = float(
            np.mean(
                (
                    terminal["core"]
                    - self.TARGET
                ) ** 2
            )
        )

        # --------------------------------------------------------------
        # Unemployment
        # --------------------------------------------------------------

        unemployment_loss = float(
            np.mean(
                (
                    terminal["unemployment"]
                    - 5.8
                ) ** 2
            )
        )

        # --------------------------------------------------------------
        # Output gap
        # --------------------------------------------------------------

        output_loss = float(
            np.mean(
                terminal["output_gap"] ** 2
            )
        )

        # --------------------------------------------------------------
        # Housing
        # --------------------------------------------------------------

        housing_loss = float(
            np.mean(
                np.maximum(
                    0.0,
                    terminal["housing"]
                    - 190.0,
                ) ** 2
            ) / 100.0
        )

        # --------------------------------------------------------------
        # Credit
        # --------------------------------------------------------------

        credit_loss = float(
            np.mean(
                np.maximum(
                    0.0,
                    terminal["credit_spread"]
                    - 2.0,
                ) ** 2
            )
        )

        # --------------------------------------------------------------
        # Policy smoothing
        # --------------------------------------------------------------

        changes = path.as_list()

        smooth_loss = float(
            np.mean(
                np.diff(changes) ** 2
            )
        )

        # --------------------------------------------------------------
        # Policy level penalty
        # --------------------------------------------------------------

        average_rate_change = (
            np.mean(
                np.abs(changes)
            )
        )

        policy_level_loss = (
            average_rate_change ** 2
        )

        # --------------------------------------------------------------
        # Inflation tail
        # --------------------------------------------------------------

        inflation_tail = float(
            np.mean(
                terminal["core"] > 4.0
            )
        )

        # --------------------------------------------------------------
        # Financial tail
        # --------------------------------------------------------------

        financial_tail = float(
            np.mean(
                terminal["credit_spread"] > 3.0
            )
        )

        total = (

            w.inflation
            * inflation_loss

            + w.unemployment
            * unemployment_loss

            + w.output
            * output_loss

            + w.housing
            * housing_loss

            + w.credit
            * credit_loss

            + w.policy_smoothing
            * smooth_loss

            + w.policy_level
            * policy_level_loss

            + w.inflation_tail
            * inflation_tail

            + w.financial_tail
            * financial_tail
        )

        return float(total)

    # ==================================================================
    # INSTITUTIONAL RISK ANALYSIS
    # ==================================================================

    def calculate_risk_metrics(
        self,
        df: pd.DataFrame,
        limits: InstitutionalRiskLimits,
    ) -> Dict[str, float]:

        terminal = (
            df.groupby("sim")
            .tail(1)
        )

        # --------------------------------------------------------------
        # Quarterly Bank loss
        # --------------------------------------------------------------

        probability_quarterly_loss = float(
            np.mean(
                terminal["bank_net_income"] < 0
            )
        )

        # --------------------------------------------------------------
        # Material quarterly loss
        # --------------------------------------------------------------

        probability_material_loss = float(
            np.mean(
                terminal["bank_net_income"]
                < -0.250
            )
        )

        # --------------------------------------------------------------
        # Terminal equity deficiency
        # --------------------------------------------------------------

        probability_extreme_deficiency = float(
            np.mean(
                terminal["bank_equity_position"]
                < limits.minimum_terminal_equity
            )
        )

        # --------------------------------------------------------------
        # Four-quarter accumulated loss
        # --------------------------------------------------------------

        last_four = (
            df[df["q"] >= 13]
            .groupby("sim")["bank_net_income"]
            .sum()
        )

        probability_four_quarter_loss = float(
            np.mean(
                last_four
                < -limits.max_four_quarter_loss
            )
        )

        # --------------------------------------------------------------
        # Inflation tail
        # --------------------------------------------------------------

        probability_inflation_tail = float(
            np.mean(
                terminal["core"] > 4.0
            )
        )

        # --------------------------------------------------------------
        # Credit tail
        # --------------------------------------------------------------

        probability_credit_tail = float(
            np.mean(
                terminal["credit_spread"] > 3.0
            )
        )

        # --------------------------------------------------------------
        # Average financial metrics
        # --------------------------------------------------------------

        average_quarterly_income = float(
            terminal[
                "bank_net_income"
            ].mean()
        )

        average_equity_position = float(
            terminal[
                "bank_equity_position"
            ].mean()
        )

        average_accumulated_deficit = float(
            terminal[
                "bank_accumulated_deficit"
            ].mean()
        )

        return {

            "probability_quarterly_loss":
                probability_quarterly_loss,

            "probability_material_loss":
                probability_material_loss,

            "probability_extreme_deficiency":
                probability_extreme_deficiency,

            "probability_four_quarter_loss":
                probability_four_quarter_loss,

            "probability_inflation_tail":
                probability_inflation_tail,

            "probability_credit_tail":
                probability_credit_tail,

            "average_quarterly_income":
                average_quarterly_income,

            "average_equity_position":
                average_equity_position,

            "average_accumulated_deficit":
                average_accumulated_deficit,
        }

    # ==================================================================
    # CONSTRAINT CHECK
    # ==================================================================

    def satisfies_risk_limits(
        self,
        metrics: Dict[str, float],
        limits: InstitutionalRiskLimits,
    ) -> bool:

        return (

            metrics[
                "probability_quarterly_loss"
            ]
            <= limits.max_probability_quarterly_loss

            and

            metrics[
                "probability_material_loss"
            ]
            <= limits.max_probability_material_loss

            and

            metrics[
                "probability_extreme_deficiency"
            ]
            <= limits.max_probability_extreme_deficiency

            and

            metrics[
                "probability_four_quarter_loss"
            ]
            <= 0.10

            and

            metrics[
                "probability_inflation_tail"
            ]
            <= limits.max_probability_inflation_above_4

            and

            metrics[
                "probability_credit_tail"
            ]
            <= limits.max_probability_credit_spread_above_3
        )

    # ==================================================================
    # CANDIDATE POLICY GENERATION
    # ==================================================================

    def generate_candidate_paths(
        self,
        options: List[int],
        max_paths: int = 2000,
        seed: Optional[int] = None,
    ) -> List[PolicyPath]:

        rng = np.random.default_rng(
            self.seed
            if seed is None
            else seed
        )

        candidates = []

        # --------------------------------------------------------------
        # Always include neutral policy
        # --------------------------------------------------------------

        candidates.append(
            PolicyPath()
        )

        # --------------------------------------------------------------
        # Structured policy paths
        # --------------------------------------------------------------

        structured_paths = [

            # Immediate easing
            (-50, -25, 0, 0),

            # Gradual easing
            (-25, -25, -25, 0),

            # Hold
            (0, 0, 0, 0),

            # Gradual tightening
            (25, 25, 25, 0),

            # Immediate tightening
            (50, 25, 0, 0),

            # Reversal
            (25, 25, -25, -25),

            (-25, -25, 25, 25),

            # Shock response
            (50, 50, 25, 0),

            (-50, -50, -25, 0),
        ]

        for combo in structured_paths:

            candidates.append(
                PolicyPath(
                    *(combo + combo)
                )
            )

        # --------------------------------------------------------------
        # Randomized candidate generation
        # --------------------------------------------------------------

        while len(candidates) < max_paths:

            first_half = tuple(
                int(
                    rng.choice(options)
                )
                for _ in range(4)
            )

            # Usually preserve policy direction,
            # but permit reversals.

            if rng.random() < 0.65:

                second_half = (
                    first_half
                )

            else:

                second_half = tuple(
                    int(
                        rng.choice(options)
                    )
                    for _ in range(4)
                )

            candidates.append(
                PolicyPath(
                    *(first_half + second_half)
                )
            )

        # --------------------------------------------------------------
        # Remove duplicates
        # --------------------------------------------------------------

        unique = {}

        for path in candidates:

            key = tuple(
                path.as_list()
            )

            unique[key] = path

        return list(
            unique.values()
        )

    # ==================================================================
    # OPTIMIZATION
    # ==================================================================

    def optimize(
        self,
        state: MacroState,
        risk_profile: InstitutionalRiskLimits,
        candidate_paths: int = 2000,
        screening_simulations: int = 120,
        final_simulations: int = 5000,
        horizon: int = 16,
    ) -> Dict:

        logger.info(
            "Optimizing under risk profile: %s",
            risk_profile.name,
        )

        candidates = (
            self.generate_candidate_paths(
                options=[
                    -50,
                    -25,
                    0,
                    25,
                    50,
                ],
                max_paths=candidate_paths,
            )
        )

        best_path = None
        best_macro_loss = float(
            "inf"
        )

        best_df = None
        best_metrics = None

        feasible_count = 0

        # --------------------------------------------------------------
        # Stage 1: screening
        # --------------------------------------------------------------

        for idx, path in enumerate(
            candidates,
            start=1,
        ):

            df = self.forecast(
                state=state,
                path=path,
                horizon=horizon,
                simulations=screening_simulations,
                seed=self.seed + idx,
            )

            metrics = (
                self.calculate_risk_metrics(
                    df,
                    risk_profile,
                )
            )

            if not self.satisfies_risk_limits(
                metrics,
                risk_profile,
            ):
                continue

            feasible_count += 1

            macro_loss = (
                self.macro_loss(
                    df,
                    path,
                )
            )

            if macro_loss < best_macro_loss:

                best_macro_loss = (
                    macro_loss
                )

                best_path = path
                best_df = df
                best_metrics = metrics

        # --------------------------------------------------------------
        # Fallback if no path is feasible
        # --------------------------------------------------------------

        if best_path is None:

            logger.warning(
                "No candidate satisfies all institutional "
                "risk constraints."
            )

            # Choose the path minimizing a combined
            # violation score.

            best_violation = float(
                "inf"
            )

            for idx, path in enumerate(
                candidates,
                start=10000,
            ):

                df = self.forecast(
                    state,
                    path,
                    horizon=horizon,
                    simulations=screening_simulations,
                    seed=self.seed + idx,
                )

                metrics = (
                    self.calculate_risk_metrics(
                        df,
                        risk_profile,
                    )
                )

                violation = (
                    self.constraint_violation_score(
                        metrics,
                        risk_profile,
                    )
                )

                macro = (
                    self.macro_loss(
                        df,
                        path,
                    )
                )

                score = (
                    violation * 100.0
                    + macro
                )

                if score < best_violation:

                    best_violation = score
                    best_path = path
                    best_df = df
                    best_metrics = metrics

        # --------------------------------------------------------------
        # Stage 2: high-resolution simulation
        # --------------------------------------------------------------

        final_df = self.forecast(
            state=state,
            path=best_path,
            horizon=horizon,
            simulations=final_simulations,
            seed=self.seed + 999999,
        )

        final_metrics = (
            self.calculate_risk_metrics(
                final_df,
                risk_profile,
            )
        )

        final_macro_loss = (
            self.macro_loss(
                final_df,
                best_path,
            )
        )

        return {

            "path":
                best_path,

            "df":
                final_df,

            "macro_loss":
                final_macro_loss,

            "risk_metrics":
                final_metrics,

            "feasible":
                self.satisfies_risk_limits(
                    final_metrics,
                    risk_profile,
                ),

            "screened_feasible_count":
                feasible_count,

            "candidate_count":
                len(candidates),

            "risk_profile":
                risk_profile,
        }

    # ==================================================================
    # CONSTRAINT VIOLATION SCORE
    # ==================================================================

    def constraint_violation_score(
        self,
        metrics: Dict[str, float],
        limits: InstitutionalRiskLimits,
    ) -> float:

        violations = []

        violations.append(
            max(
                0.0,
                (
                    metrics[
                        "probability_quarterly_loss"
                    ]
                    - limits.max_probability_quarterly_loss
                ),
            )
        )

        violations.append(
            max(
                0.0,
                (
                    metrics[
                        "probability_material_loss"
                    ]
                    - limits.max_probability_material_loss
                ),
            )
        )

        violations.append(
            max(
                0.0,
                (
                    metrics[
                        "probability_extreme_deficiency"
                    ]
                    - limits.max_probability_extreme_deficiency
                ),
            )
        )

        violations.append(
            max(
                0.0,
                (
                    metrics[
                        "probability_four_quarter_loss"
                    ]
                    - 0.10
                ),
            )
        )

        violations.append(
            max(
                0.0,
                (
                    metrics[
                        "probability_inflation_tail"
                    ]
                    - limits.max_probability_inflation_above_4
                ),
            )
        )

        violations.append(
            max(
                0.0,
                (
                    metrics[
                        "probability_credit_tail"
                    ]
                    - limits.max_probability_credit_spread_above_3
                ),
            )
        )

        return float(
            sum(
                v ** 2
                for v in violations
            )
        )

    # ==================================================================
    # SCENARIO SET
    # ==================================================================

    def scenario_states(
        self,
    ) -> Dict[str, MacroState]:

        return {

            "Baseline":
                MacroState(),

            "Energy Shock":
                MacroState(
                    oil_price=105.0,
                    goods_inflation=3.2,
                    services_inflation=3.0,
                ),

            "Financial Stress":
                MacroState(
                    credit_spread=2.40,
                    housing_index=170.0,
                    credit_growth=0.5,
                ),

            "Recession":
                MacroState(
                    gdp_growth=-1.0,
                    unemployment=7.5,
                    credit_spread=2.0,
                    credit_growth=-1.0,
                ),

            "Inflation Persistence":
                MacroState(
                    services_inflation=4.0,
                    goods_inflation=2.5,
                    shelter_inflation=4.5,
                    wage_growth=4.0,
                ),

            "Stagflation":
                MacroState(
                    services_inflation=4.0,
                    goods_inflation=3.0,
                    shelter_inflation=4.0,
                    gdp_growth=-0.5,
                    unemployment=7.0,
                    oil_price=105.0,
                    credit_spread=2.0,
                ),
        }

    # ==================================================================
    # FULL STRESS TEST
    # ==================================================================

    def run_scenario_stress_test(
        self,
        risk_profile: str = "standard",
        candidate_paths: int = 1500,
        screening_simulations: int = 100,
        final_simulations: int = 5000,
    ) -> Dict[str, Dict]:

        profiles = (
            make_risk_profiles()
        )

        if risk_profile not in profiles:

            raise ValueError(
                f"Unknown risk profile: "
                f"{risk_profile}. "
                f"Choose from {list(profiles)}."
            )

        limits = profiles[
            risk_profile
        ]

        results = {}

        for name, state in (
            self.scenario_states()
        ).items():

            logger.info(
                "Running scenario: %s",
                name,
            )

            result = self.optimize(
                state=state,
                risk_profile=limits,
                candidate_paths=candidate_paths,
                screening_simulations=screening_simulations,
                final_simulations=final_simulations,
                horizon=16,
            )

            results[name] = result

        return results


# ======================================================================
# 9. REPORTING HELPERS
# ======================================================================

def format_path(
    path: PolicyPath,
    initial_rate: float = 2.25,
) -> str:

    rates = path.rate_path(
        initial_rate,
        horizon=16,
    )

    return " | ".join(
        f"Q{i + 1}: {rate:.2f}%"
        for i, rate in enumerate(rates)
    )


def classify_result(
    result: Dict,
) -> str:

    if result["feasible"]:
        return (
            "INSTITUTIONALLY FEASIBLE"
        )

    return (
        "RISK CONSTRAINTS VIOLATED"
    )


def print_scenario_report(
    name: str,
    result: Dict,
) -> None:

    df = result["df"]

    terminal = (
        df.groupby("sim")
        .tail(1)
    )

    metrics = (
        result["risk_metrics"]
    )

    path = result["path"]

    print()
    print("=" * 96)
    print(
        f"SCENARIO: {name}"
    )
    print("=" * 96)

    print()
    print("POLICY PATH")
    print("-" * 96)

    print(
        format_path(
            path
        )
    )

    print()
    print("OPTIMIZATION")
    print("-" * 96)

    print(
        f"Macro objective loss:          "
        f"{result['macro_loss']:.4f}"
    )

    print(
        f"Candidate paths evaluated:     "
        f"{result['candidate_count']}"
    )

    print(
        f"Feasible paths screened:       "
        f"{result['screened_feasible_count']}"
    )

    print(
        f"Final institutional status:    "
        f"{classify_result(result)}"
    )

    print()
    print("MACROECONOMIC OUTCOMES — Q16")
    print("-" * 96)

    print(
        f"Core inflation:                "
        f"{terminal['core'].mean():.2f}%"
    )

    print(
        f"GDP growth:                   "
        f"{terminal['gdp'].mean():.2f}%"
    )

    print(
        f"Unemployment:                 "
        f"{terminal['unemployment'].mean():.2f}%"
    )

    print(
        f"Output gap:                   "
        f"{terminal['output_gap'].mean():.2f}%"
    )

    print(
        f"Housing index:                "
        f"{terminal['housing'].mean():.2f}"
    )

    print(
        f"Credit spread:                "
        f"{terminal['credit_spread'].mean():.2f}%"
    )

    print()
    print("BANK FINANCIAL POSITION — Q16")
    print("-" * 96)

    print(
        f"Quarterly net income:         "
        f"${terminal['bank_net_income'].mean():.4f}B"
    )

    print(
        f"Accumulated accounting deficit:"
        f" ${terminal['bank_accumulated_deficit'].mean():.3f}B"
    )

    print(
        f"Equity position:              "
        f"${terminal['bank_equity_position'].mean():.3f}B"
    )

    print(
        f"Bank assets:                  "
        f"${terminal['bank_assets'].mean():.2f}B"
    )

    print()
    print("INSTITUTIONAL RISK")
    print("-" * 96)

    print(
        f"P(quarterly Bank loss):       "
        f"{metrics['probability_quarterly_loss']:.1%}"
    )

    print(
        f"P(material quarterly loss):   "
        f"{metrics['probability_material_loss']:.1%}"
    )

    print(
        f"P(extreme equity deficiency): "
        f"{metrics['probability_extreme_deficiency']:.1%}"
    )

    print(
        f"P(4Q material loss):          "
        f"{metrics['probability_four_quarter_loss']:.1%}"
    )

    print(
        f"P(core inflation > 4%):       "
        f"{metrics['probability_inflation_tail']:.1%}"
    )

    print(
        f"P(credit spread > 3%):        "
        f"{metrics['probability_credit_tail']:.1%}"
    )

    print("=" * 96)


# ======================================================================
# 10. EXECUTIVE SUMMARY TABLE
# ======================================================================

def create_summary_table(
    results: Dict[str, Dict],
) -> pd.DataFrame:

    rows = []

    for scenario, result in results.items():

        df = result["df"]

        terminal = (
            df.groupby("sim")
            .tail(1)
        )

        metrics = (
            result["risk_metrics"]
        )

        rates = result[
            "path"
        ].rate_path(
            initial_rate=2.25,
            horizon=16,
        )

        rows.append({

            "Scenario":
                scenario,

            "Status":
                classify_result(
                    result
                ),

            "Macro Loss":
                result["macro_loss"],

            "Q4 Rate %":
                rates[3],

            "Q8 Rate %":
                rates[7],

            "Q16 Rate %":
                rates[15],

            "Core CPI Q16 %":
                terminal["core"].mean(),

            "GDP Q16 %":
                terminal["gdp"].mean(),

            "Unemployment Q16 %":
                terminal["unemployment"].mean(),

            "Output Gap Q16 %":
                terminal["output_gap"].mean(),

            "Bank Net Income Q16 $B":
                terminal[
                    "bank_net_income"
                ].mean(),

            "Bank Accumulated Deficit Q16 $B":
                terminal[
                    "bank_accumulated_deficit"
                ].mean(),

            "Bank Equity Q16 $B":
                terminal[
                    "bank_equity_position"
                ].mean(),

            "P(Bank Loss)":
                metrics[
                    "probability_quarterly_loss"
                ],

            "P(Material Loss)":
                metrics[
                    "probability_material_loss"
                ],

            "P(Extreme Deficiency)":
                metrics[
                    "probability_extreme_deficiency"
                ],

            "P(Inflation >4%)":
                metrics[
                    "probability_inflation_tail"
                ],
        })

    return pd.DataFrame(
        rows
    )


# ======================================================================
# 11. RISK-PROFILE COMPARISON
# ======================================================================

def compare_risk_profiles(
    engine: ResearchPolicyEngine,
    scenario: str = "Baseline",
    candidate_paths: int = 1000,
    screening_simulations: int = 100,
    final_simulations: int = 2500,
) -> pd.DataFrame:

    profiles = (
        make_risk_profiles()
    )

    states = (
        engine.scenario_states()
    )

    if scenario not in states:

        raise ValueError(
            f"Unknown scenario: {scenario}"
        )

    rows = []

    for profile_name, limits in (
        profiles.items()
    ):

        result = engine.optimize(
            state=states[scenario],
            risk_profile=limits,
            candidate_paths=candidate_paths,
            screening_simulations=screening_simulations,
            final_simulations=final_simulations,
            horizon=16,
        )

        df = result["df"]

        terminal = (
            df.groupby("sim")
            .tail(1)
        )

        rates = result[
            "path"
        ].rate_path(
            initial_rate=2.25,
            horizon=16,
        )

        rows.append({

            "Risk Profile":
                profile_name,

            "Feasible":
                result["feasible"],

            "Macro Loss":
                result["macro_loss"],

            "Q1 Rate":
                rates[0],

            "Q4 Rate":
                rates[3],

            "Q8 Rate":
                rates[7],

            "Q16 Rate":
                rates[15],

            "Core CPI Q16":
                terminal["core"].mean(),

            "GDP Q16":
                terminal["gdp"].mean(),

            "Unemployment Q16":
                terminal["unemployment"].mean(),

            "Bank Net Income Q16":
                terminal[
                    "bank_net_income"
                ].mean(),

            "Bank Equity Q16":
                terminal[
                    "bank_equity_position"
                ].mean(),

            "P Bank Loss":
                result[
                    "risk_metrics"
                ][
                    "probability_quarterly_loss"
                ],

            "P Material Loss":
                result[
                    "risk_metrics"
                ][
                    "probability_material_loss"
                ],
        })

    return pd.DataFrame(
        rows
    )


# ======================================================================
# 12. PLOT POLICY PATH
# ======================================================================

def plot_policy_path(
    result: Dict,
    title: str = "Optimal Monetary-Policy Path",
) -> None:

    path = result["path"]

    rates = path.rate_path(
        initial_rate=2.25,
        horizon=16,
    )

    quarters = np.arange(
        1,
        17,
    )

    plt.figure(
        figsize=(11, 5)
    )

    plt.plot(
        quarters,
        rates,
        marker="o",
    )

    plt.axhline(
        2.25,
        linestyle="--",
        linewidth=1,
    )

    plt.xlabel(
        "Quarter"
    )

    plt.ylabel(
        "Overnight Rate (%)"
    )

    plt.title(
        title
    )

    plt.grid(
        alpha=0.25
    )

    plt.tight_layout()

    plt.show()


# ======================================================================
# 13. PLOT MACRO OUTCOMES
# ======================================================================

def plot_forecast(
    result: Dict,
    variable: str,
    title: Optional[str] = None,
    target: Optional[float] = None,
) -> None:

    df = result["df"]

    grouped = (
        df.groupby("q")[variable]
    )

    summary = grouped.agg(
        [
            "mean",
            lambda x: np.percentile(
                x,
                10,
            ),
            lambda x: np.percentile(
                x,
                90,
            ),
        ]
    )

    summary.columns = [
        "mean",
        "p10",
        "p90",
    ]

    q = summary.index.values

    plt.figure(
        figsize=(11, 5)
    )

    plt.plot(
        q,
        summary["mean"],
        linewidth=2,
        label="Mean",
    )

    plt.fill_between(
        q,
        summary["p10"],
        summary["p90"],
        alpha=0.20,
        label="10–90% range",
    )

    if target is not None:

        plt.axhline(
            target,
            linestyle="--",
            linewidth=1,
            label="Target",
        )

    plt.xlabel(
        "Quarter"
    )

    plt.ylabel(
        variable
    )

    plt.title(
        title
        or f"{variable} Forecast"
    )

    plt.legend()

    plt.grid(
        alpha=0.25
    )

    plt.tight_layout()

    plt.show()


# ======================================================================
# 14. MAIN EXECUTION
# ======================================================================

def main():

    logger.info(
        "Starting Bank of Canada institutional policy engine."
    )

    engine = (
        ResearchPolicyEngine(
            seed=42
        )
    )

    # --------------------------------------------------------------
    # Select institutional risk tolerance
    # --------------------------------------------------------------

    risk_profile = "standard"

    # Available:
    #
    #   conservative
    #   standard
    #   mandate_first
    #
    # The model DOES NOT attempt to maximize Bank profitability.
    # Financial position is treated as a risk constraint.

    logger.info(
        "Institutional risk profile: %s",
        risk_profile,
    )

    # --------------------------------------------------------------
    # Run scenarios
    # --------------------------------------------------------------

    results = (
        engine.run_scenario_stress_test(
            risk_profile=risk_profile,
            candidate_paths=1500,
            screening_simulations=100,
            final_simulations=5000,
        )
    )

    # --------------------------------------------------------------
    # Header
    # --------------------------------------------------------------

    print()
    print("=" * 96)
    print(
        " BANK OF CANADA — CONSTRAINED MONETARY POLICY ENGINE"
    )
    print("=" * 96)

    print()
    print(
        "Objective:"
    )

    print(
        "Minimize macroeconomic inflation/output/labour-market "
        "losses subject to an institutional financial-risk envelope."
    )

    print()
    print(
        "Institutional interpretation:"
    )

    print(
        "Bank accounting losses are NOT treated as equivalent "
        "to a federal-government fiscal deficit."
    )

    print()
    print(
        f"Starting overnight rate: 2.25%"
    )

    print(
        f"Optimization horizon:    16 quarters"
    )

    print(
        f"Risk profile:             {risk_profile}"
    )

    print(
        f"Final simulations:        5,000"
    )

    # --------------------------------------------------------------
    # Detailed reports
    # --------------------------------------------------------------

    for scenario, result in (
        results.items()
    ):

        print_scenario_report(
            scenario,
            result,
        )

    # --------------------------------------------------------------
    # Executive summary
    # --------------------------------------------------------------

    summary = (
        create_summary_table(
            results
        )
    )

    print()
    print("=" * 96)
    print(
        "EXECUTIVE SCENARIO SUMMARY"
    )
    print("=" * 96)

    with pd.option_context(
        "display.max_columns",
        None,
        "display.width",
        240,
        "display.float_format",
        lambda x: f"{x:.3f}",
    ):

        print(
            summary.to_string(
                index=False
            )
        )

    # --------------------------------------------------------------
    # Risk-profile comparison
    # --------------------------------------------------------------

    print()
    print("=" * 96)
    print(
        "BASELINE — RISK PROFILE COMPARISON"
    )
    print("=" * 96)

    profile_comparison = (
        compare_risk_profiles(
            engine,
            scenario="Baseline",
            candidate_paths=750,
            screening_simulations=75,
            final_simulations=2500,
        )
    )

    with pd.option_context(
        "display.max_columns",
        None,
        "display.width",
        220,
        "display.float_format",
        lambda x: f"{x:.3f}",
    ):

        print(
            profile_comparison.to_string(
                index=False
            )
        )

    # --------------------------------------------------------------
    # Optional plots
    # --------------------------------------------------------------

    baseline = (
        results["Baseline"]
    )

    plot_policy_path(
        baseline,
        title=(
            "Baseline — Constrained Optimal "
            "Monetary-Policy Path"
        ),
    )

    plot_forecast(
        baseline,
        variable="core",
        title=(
            "Baseline — Core Inflation"
        ),
        target=2.0,
    )

    plot_forecast(
        baseline,
        variable="gdp",
        title=(
            "Baseline — Real GDP Growth"
        ),
    )

    plot_forecast(
        baseline,
        variable="bank_equity_position",
        title=(
            "Baseline — Bank Accounting "
            "Equity Position"
        ),
    )

    logger.info(
        "Institutional policy analysis complete."
    )


# ======================================================================
# ENTRY POINT
# ======================================================================

if __name__ == "__main__":
    main()

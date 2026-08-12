#!/usr/bin/env python3
from pathlib import Path

p = Path('tools/build_oecd_bilateral_io.py')
s = p.read_text(encoding='utf-8')
replacements = [
(
"""    denominator = [0.0] * 20
    ca_from_us = [[0.0] * 20 for _ in range(20)]
""",
"""    denominator = {"CAN": [0.0] * 20, "USA": [0.0] * 20}
    ca_from_us = [[0.0] * 20 for _ in range(20)]
"""),
(
"""        for down_sector, down_weight in down_targets:
            denominator[down_sector] += total_input * down_weight
""",
"""        for down_sector, down_weight in down_targets:
            denominator[down_country][down_sector] += total_input * down_weight
"""),
(
"""    if any(value <= 0 for value in denominator):
        missing = [MODEL_CODES[i] for i, value in enumerate(denominator) if value <= 0]
        raise SystemExit("No intermediate-input denominator for model sectors: " + ", ".join(missing))

    for matrix in (ca_from_us, us_from_ca):
        for downstream in range(20):
            matrix[downstream] = [value / denominator[downstream] for value in matrix[downstream]]
            if sum(matrix[downstream]) >= 1.0 + 1e-9:
                raise SystemExit(f"Implausible bilateral sourcing share for {MODEL_CODES[downstream]}")
""",
"""    for country in ("CAN", "USA"):
        if any(value <= 0 for value in denominator[country]):
            missing = [MODEL_CODES[i] for i, value in enumerate(denominator[country]) if value <= 0]
            raise SystemExit(
                f"No intermediate-input denominator for {country} model sectors: " + ", ".join(missing)
            )

    for country, matrix in (("CAN", ca_from_us), ("USA", us_from_ca)):
        for downstream in range(20):
            matrix[downstream] = [
                value / denominator[country][downstream] for value in matrix[downstream]
            ]
            if sum(matrix[downstream]) >= 1.0 + 1e-9:
                raise SystemExit(
                    f"Implausible bilateral sourcing share for {country} {MODEL_CODES[downstream]}"
                )
""")]

for old, new in replacements:
    if new in s:
        continue
    if s.count(old) != 1:
        raise SystemExit(f'expected exactly one OECD denominator marker, found {s.count(old)}')
    s = s.replace(old, new)

p.write_text(s, encoding='utf-8')
print('OECD bilateral denominators are country-specific')

#!/usr/bin/env python3
from pathlib import Path
import json
import refresh_sep_calibration as cal


def parse_valet(raw, series):
    out = {}
    for obs in json.loads(raw).get("observations", []):
        date = str(obs.get("d", ""))
        cell = obs.get(series) or obs.get(series.lower()) or obs.get(series.upper())
        if not isinstance(cell, dict):
            continue
        try:
            value = float(cell["v"])
        except (KeyError, TypeError, ValueError):
            continue
        if "Q" in date.upper():
            q = date[:6].replace("-", "").upper()
        elif len(date) >= 7 and date[:4].isdigit() and date[5:7].isdigit():
            year, month = int(date[:4]), int(date[5:7])
            q = f"{year}Q{(month - 1)//3 + 1}"
        else:
            continue
        out[q] = value
    return out


cal.parse_valet = parse_valet

if __name__ == "__main__":
    cal.refresh(Path(".").resolve())

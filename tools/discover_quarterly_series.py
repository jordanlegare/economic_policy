#!/usr/bin/env python3
import json
import urllib.request


def get_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": "CanadaPolicyStudio/2.0"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def flatten_text(value):
    if isinstance(value, dict):
        return " ".join(flatten_text(v) for v in value.values())
    if isinstance(value, list):
        return " ".join(flatten_text(v) for v in value)
    return str(value)


def main():
    data = get_json("https://www.bankofcanada.ca/valet/lists/series/json")
    series = data.get("series", data)
    rows = []
    if isinstance(series, dict):
        rows = [(code, meta) for code, meta in series.items()]
    elif isinstance(series, list):
        for item in series:
            if isinstance(item, dict):
                code = item.get("name") or item.get("series") or item.get("code") or "?"
                rows.append((code, item))
    needles = [
        ("OUTPUT GAP", ["output gap"]),
        ("CPI TRIM", ["cpi", "trim"]),
        ("CPI MEDIAN", ["cpi", "median"]),
        ("CORE CPI", ["core", "cpi"]),
    ]
    print(f"series_count={len(rows)}")
    for label, tokens in needles:
        print(f"\n## {label}")
        matches = []
        for code, meta in rows:
            text = flatten_text(meta).lower()
            if all(token in text for token in tokens):
                matches.append((str(code), text[:500]))
        for code, text in matches[:80]:
            print(code, "|", text.replace("\n", " "))
        print(f"matches={len(matches)}")


if __name__ == "__main__":
    main()

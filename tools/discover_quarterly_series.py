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
    rows = list(series.items()) if isinstance(series, dict) else []
    print(f"series_count={len(rows)}")
    categories = [
        ("SWP POLICY", ["policy", "rate"]),
        ("SWP EXCHANGE", ["exchange", "rate"]),
        ("SWP OIL", ["oil"]),
        ("SWP WTI", ["west texas"]),
        ("SWP OUTPUT GAP", ["output gap"]),
        ("SWP CORE CPI", ["core consumer price"]),
    ]
    for label, tokens in categories:
        print(f"\n## {label}")
        matches = []
        for code, meta in rows:
            code_text = str(code)
            if not code_text.upper().startswith("SWP-"):
                continue
            text = flatten_text(meta).lower()
            if all(token in text for token in tokens):
                matches.append((code_text, text[:350]))
        for code, text in matches[:60]:
            print(code, "|", text.replace("\n", " "))
        print(f"matches={len(matches)}")


if __name__ == "__main__":
    main()

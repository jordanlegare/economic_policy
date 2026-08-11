#!/usr/bin/env python3
"""Generate a C++17 header containing immutable text assets.

The Windows release uses this at build time so the executable can serve the
browser UI and bootstrap calibration without relying on repository-relative
files. Runtime negotiation state is intentionally not embedded.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


def raw_literal(text: str, key: str) -> str:
    digest = hashlib.sha256((key + "\0" + text).encode("utf-8")).hexdigest()
    for width in range(8, 17):
        delimiter = "CAD" + digest[:width]
        if f"){delimiter}\"" not in text:
            return f'R"{delimiter}({text}){delimiter}"'
    raise RuntimeError(f"unable to choose raw-string delimiter for {key}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("files", nargs="+")
    args = parser.parse_args()

    root = args.root.resolve()
    assets: list[tuple[str, str]] = []
    for item in args.files:
        relative = Path(item).as_posix()
        source = (root / relative).resolve()
        if root not in source.parents and source != root:
            raise ValueError(f"asset escapes source root: {item}")
        text = source.read_text(encoding="utf-8")
        assets.append((relative, text))

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "#pragma once",
        "",
        "#include <array>",
        "#include <string_view>",
        "",
        "namespace cad::embedded {",
        "",
        "struct Asset {",
        "  std::string_view path;",
        "  std::string_view content;",
        "};",
        "",
        f"inline constexpr std::array<Asset, {len(assets)}> kAssets{{{{",
    ]
    for path, text in assets:
        lines.append(
            f'  Asset{{std::string_view{{"{path}"}}, std::string_view{{{raw_literal(text, path)}}}}},'
        )
    lines.extend(
        [
            "}};",
            "",
            "inline const std::string_view* find(std::string_view path) {",
            "  for (const auto& asset : kAssets) {",
            "    if (asset.path == path) return &asset.content;",
            "  }",
            "  return nullptr;",
            "}",
            "",
            "}  // namespace cad::embedded",
            "",
        ]
    )
    output.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

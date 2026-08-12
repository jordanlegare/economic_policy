#!/usr/bin/env python3
import csv
import subprocess
import tempfile
import zipfile
from pathlib import Path

MODEL_CODES = [
    "11", "21", "22", "23", "31-33", "42", "44-45", "48-49", "51", "52",
    "53", "54", "55", "56", "61", "62", "71", "72", "81", "91",
]

with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    crosswalk = root / "crosswalk.csv"
    with crosswalk.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["icio_industry", "model_sector", "weight"])
        for i, code in enumerate(MODEL_CODES):
            writer.writerow([f"I{i}", code, 1.0])

    # Synthetic wide ICIO matrix: every downstream industry buys 10 units from
    # its domestic counterpart and 2 units from the same partner-country sector.
    # Thus the bilateral sourcing share on the matching diagonal is 2/24 when
    # both CAN and USA rows are included as the full intermediate-input universe.
    labels = [f"CAN_I{i}" for i in range(20)] + [f"USA_I{i}" for i in range(20)]
    matrix = root / "ICIO_2022.csv"
    with matrix.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow([""] + labels)
        for row_label in labels:
            row_country, row_industry = row_label.split("_", 1)
            values = []
            for col_label in labels:
                col_country, col_industry = col_label.split("_", 1)
                value = 0.0
                if row_industry == col_industry:
                    value = 10.0 if row_country == col_country else 2.0
                values.append(value)
            writer.writerow([row_label] + values)

    archive = root / "official-oecd-icio.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.write(matrix, matrix.name)

    output = root / "bilateral.csv"
    provenance = root / "provenance.json"
    subprocess.run([
        "python3", "tools/build_oecd_bilateral_io.py",
        "--input", str(archive),
        "--crosswalk", str(crosswalk),
        "--year", "2022",
        "--output", str(output),
        "--provenance", str(provenance),
    ], check=True)

    rows = list(csv.reader(output.open(newline="", encoding="utf-8")))
    assert len(rows) == 41
    header = rows[0]
    assert header[0:2] == ["importing_country", "downstream_code"]
    can_manufacturing = next(row for row in rows[1:] if row[0] == "CAN" and row[1] == "31-33")
    manufacturing_col = 2 + MODEL_CODES.index("31-33")
    # Each detailed downstream has exactly 12 units of modeled intermediate
    # purchases in this synthetic matrix: 10 domestic + 2 bilateral.
    assert abs(float(can_manufacturing[manufacturing_col]) - 2.0 / 12.0) < 1e-12
    assert provenance.exists() and provenance.stat().st_size > 100

print("OECD bilateral IO builder test passed")

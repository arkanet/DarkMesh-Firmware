#!/usr/bin/env python3
"""Exercise the production reader and SdFat bridge; no device or network needed."""

import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TEST = ROOT / "test/heltec_pmtiles"
with tempfile.TemporaryDirectory(prefix="darkmesh-pmtiles-") as tmp:
    work = Path(tmp)
    subprocess.run(["python3", "-I", str(TEST / "make_fixtures.py"), tmp], check=True)
    for suite in ("reader", "adapter"):
        cmd = [
            os.environ.get("CXX", "g++"),
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DHELTEC_PMTILES_HOST=1",
            "-DHELTEC_V4_R2_P04=1",
            f"-I{TEST / 'fakes'}",
            f"-I{ROOT / 'src'}",
            f"-I{ROOT / 'src/graphics/pmtiles'}",
            str(ROOT / "src/graphics/pmtiles/PmTiles.cpp"),
            str(TEST / f"test_{suite}.cpp"),
            "-lz",
            "-o",
            str(work / suite),
        ]
        if suite == "adapter":
            cmd.insert(-3, str(ROOT / "src/graphics/HeltecV4R2PMTiles.cpp"))
        subprocess.run(cmd, check=True)
        subprocess.run([str(work / suite), tmp], check=True)

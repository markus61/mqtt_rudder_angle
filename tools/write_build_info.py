#!/usr/bin/env python3
"""Write ESP-IDF application metadata from `esptool image-info` as JSON."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


def json_key(label: str) -> str:
    key = label.strip().lower()
    key = key.replace("esp-idf", "esp_idf")
    key = key.replace("sha256", "sha256")
    key = re.sub(r"[^a-z0-9]+", "_", key)
    return key.strip("_")


def parse_application_information(output: str) -> dict[str, str]:
    lines = output.splitlines()
    in_section = False
    info: dict[str, str] = {}

    for line in lines:
        stripped = line.strip()

        if stripped == "Application Information":
            in_section = True
            continue

        if not in_section:
            continue

        if not stripped or set(stripped) == {"="}:
            continue

        if re.fullmatch(r"[A-Za-z0-9 ].+", stripped) and ":" not in stripped:
            break

        label, separator, value = stripped.partition(":")
        if not separator:
            continue

        info[json_key(label)] = value.strip()

    if not info:
        raise RuntimeError(
            "Application Information section not found in esptool output"
        )

    return info


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()

    result = subprocess.run(
        [sys.executable, "-m", "esptool", "image-info", str(args.image)],
        check=True,
        capture_output=True,
        text=True,
    )
    app_info = parse_application_information(result.stdout)
    app_version = app_info.get("app_version")
    if not app_version:
        raise RuntimeError("App version not found in Application Information section")

    output_path = args.build_dir / "build_info.json"
    output_path.write_text(json.dumps(app_info, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

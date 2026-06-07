from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]


def resolve_codec(explicit: str | None) -> Path:
    requested = explicit or os.environ.get("DIC_CODEC")
    if requested:
        path = Path(requested).expanduser().resolve()
        if path.is_file():
            return path
        raise FileNotFoundError(f"codec executable not found: {path}")

    candidates = (
        REPO_ROOT / "build" / "bin" / "finalproj.exe",
        REPO_ROOT / "build" / "bin" / "finalproj",
    )
    for path in candidates:
        if path.is_file():
            return path

    from_path = shutil.which("finalproj")
    if from_path:
        return Path(from_path).resolve()
    raise FileNotFoundError(
        "cannot find finalproj; build it or set DIC_CODEC/--codec"
    )


def run_codec(codec: Path, image: Path, q: str) -> dict[str, Any]:
    command = [
        str(codec),
        "bit",
        "codec",
        "--input",
        str(image.resolve()),
        "--quant",
        q,
    ]
    with tempfile.TemporaryDirectory(prefix="dic-codec-") as workdir:
        completed = subprocess.run(
            command,
            cwd=workdir,
            capture_output=True,
            text=True,
            check=False,
        )

    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise RuntimeError(
            f"codec failed for {image.name} at q={q}: {detail}"
        )

    for line in reversed(completed.stdout.splitlines()):
        line = line.strip()
        if line.startswith("{") and line.endswith("}"):
            result = json.loads(line)
            if result.get("psnr") is None and result.get("lossless"):
                result["psnr"] = float("inf")
            return result
    raise RuntimeError(
        f"codec returned no JSON result for {image.name} at q={q}"
    )

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path


def parse_float(text: str) -> float:
    value = text.strip()
    if value == "":
        return math.nan
    lowered = value.lower()
    if lowered in {"inf", "+inf", "infinity", "+infinity"}:
        return math.inf
    if lowered in {"-inf", "-infinity"}:
        return -math.inf
    if lowered in {"nan", "none", "null"}:
        return math.nan
    return float(value)


def find_column(fieldnames: list[str] | None, wanted: str) -> str:
    if not fieldnames:
        raise ValueError("CSV has no header row")
    normalized = {name.strip().lower(): name for name in fieldnames}
    key = wanted.lower()
    if key not in normalized:
        raise ValueError(
            f"CSV must contain column {wanted!r}; found {', '.join(fieldnames)}"
        )
    return normalized[key]


def load_rd_csv(path: Path) -> tuple[list[float], list[float], list[float]]:
    with path.open(newline="", encoding="utf-8") as csv_file:
        reader = csv.DictReader(csv_file)
        q_col = find_column(reader.fieldnames, "q")
        psnr_col = find_column(reader.fieldnames, "PSNR")
        ratio_col = find_column(reader.fieldnames, "compression_ratio")

        rows: list[tuple[float, float, float]] = []
        for line_number, row in enumerate(reader, start=2):
            try:
                q = parse_float(row[q_col])
                psnr = parse_float(row[psnr_col])
                ratio = parse_float(row[ratio_col])
            except (KeyError, TypeError, ValueError) as exc:
                raise ValueError(f"{path}:{line_number}: invalid numeric row") from exc
            if not math.isfinite(q) or q <= 0.0:
                raise ValueError(f"{path}:{line_number}: q must be positive and finite")
            if not math.isfinite(ratio):
                raise ValueError(
                    f"{path}:{line_number}: compression_ratio must be finite"
                )
            rows.append((q, psnr, ratio))

    if not rows:
        raise ValueError(f"{path}: no data rows")

    rows.sort(key=lambda item: item[0])
    return (
        [row[0] for row in rows],
        [row[1] for row in rows],
        [row[2] for row in rows],
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Plot PSNR and compression_ratio versus q from one or more "
            "evalrd.py CSV files."
        )
    )
    parser.add_argument("csv", nargs="+", type=Path, help="evalrd.py CSV file")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("rd_curves.png"),
        help="output image path (default: rd_curves.png)",
    )
    parser.add_argument(
        "--title",
        default="Rate-distortion evaluation",
        help="figure title",
    )
    parser.add_argument("--dpi", type=int, default=160, help="output image DPI")
    parser.add_argument(
        "--show",
        action="store_true",
        help="display the figure after saving it",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()

    try:
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise SystemExit(
            "matplotlib is required; install it with `python -m pip install matplotlib`"
        ) from exc

    datasets = []
    for csv_path in args.csv:
        path = csv_path.resolve()
        if not path.is_file():
            raise SystemExit(f"CSV file not found: {path}")
        try:
            q_values, psnr_values, ratio_values = load_rd_csv(path)
        except ValueError as exc:
            raise SystemExit(str(exc)) from exc
        datasets.append((path.stem, q_values, psnr_values, ratio_values))

    finite_psnr = [
        value
        for _, _, psnr_values, _ in datasets
        for value in psnr_values
        if math.isfinite(value)
    ]
    psnr_inf_level = (max(finite_psnr) + 2.0) if finite_psnr else 60.0

    fig, (psnr_ax, ratio_ax) = plt.subplots(2, 1, sharex=True, figsize=(9, 7))
    fig.suptitle(args.title)

    for label, q_values, psnr_values, ratio_values in datasets:
        finite_q = [
            q for q, psnr in zip(q_values, psnr_values) if math.isfinite(psnr)
        ]
        finite_y = [psnr for psnr in psnr_values if math.isfinite(psnr)]
        inf_q = [q for q, psnr in zip(q_values, psnr_values) if math.isinf(psnr)]

        if finite_q:
            psnr_ax.plot(finite_q, finite_y, marker="o", label=label)
        if inf_q:
            marker_label = f"{label} (PSNR=inf)" if not finite_q else None
            psnr_ax.scatter(
                inf_q,
                [psnr_inf_level] * len(inf_q),
                marker="^",
                s=55,
                label=marker_label,
            )
            for q in inf_q:
                psnr_ax.annotate(
                    "inf",
                    (q, psnr_inf_level),
                    xytext=(0, 5),
                    textcoords="offset points",
                    ha="center",
                    fontsize=8,
                )

        ratio_ax.plot(q_values, ratio_values, marker="o", label=label)

    psnr_ax.set_ylabel("PSNR (dB)")
    psnr_ax.grid(True, alpha=0.3)
    if finite_psnr:
        psnr_ax.set_ylim(bottom=min(finite_psnr) - 2.0, top=psnr_inf_level + 4.0)

    ratio_ax.set_xlabel("Quantization step q")
    ratio_ax.set_ylabel("compression_ratio")
    ratio_ax.grid(True, alpha=0.3)

    psnr_ax.legend()
    ratio_ax.legend()
    fig.tight_layout()

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=args.dpi)
    print(f"wrote {output}")

    if args.show:
        plt.show()
    plt.close(fig)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

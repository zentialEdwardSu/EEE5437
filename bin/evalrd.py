from __future__ import annotations

import argparse
import csv
from decimal import Decimal, InvalidOperation
from pathlib import Path

from _codec import resolve_codec, run_codec


def parse_range(spec: str) -> list[Decimal]:
    parts = spec.split(":")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError(
            f"range must be START:STEP:END, got {spec!r}"
        )
    try:
        start, step, end = (Decimal(part) for part in parts)
    except InvalidOperation as exc:
        raise argparse.ArgumentTypeError(f"invalid numeric range: {spec!r}") from exc
    if not all(value.is_finite() for value in (start, step, end)):
        raise argparse.ArgumentTypeError("range values must be finite")
    if step == 0:
        raise argparse.ArgumentTypeError("range step cannot be zero")
    if (end - start) * step < 0:
        raise argparse.ArgumentTypeError(
            f"range step points away from end: {spec!r}"
        )

    values: list[Decimal] = []
    value = start
    while (step > 0 and value <= end) or (step < 0 and value >= end):
        if value <= 0:
            raise argparse.ArgumentTypeError("all q values must be positive")
        values.append(value)
        value += step
    return values


def decimal_text(value: Decimal) -> str:
    return format(value, "f")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Evaluate rate-distortion points with finalproj bit codec."
    )
    parser.add_argument("image", type=Path, help="input PGM or PPM image")
    parser.add_argument(
        "-r",
        "--range",
        dest="ranges",
        action="append",
        required=True,
        metavar="START:STEP:END",
        help="inclusive q range; may be repeated",
    )
    parser.add_argument(
        "--codec",
        help="finalproj executable (default: DIC_CODEC or build/bin/finalproj)",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    image = args.image.resolve()
    if not image.is_file():
        raise SystemExit(f"input image not found: {image}")

    q_values: list[Decimal] = []
    seen: set[Decimal] = set()
    for spec in args.ranges:
        for value in parse_range(spec):
            if value not in seen:
                seen.add(value)
                q_values.append(value)

    codec = resolve_codec(args.codec)
    output = image.with_suffix(".csv")
    with output.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(("q", "PSNR", "compression_ratio"))
        for q in q_values:
            q_text = decimal_text(q)
            result = run_codec(codec, image, q_text)
            writer.writerow(
                (q_text, result["psnr"], result["compression_ratio"])
            )
            print(
                f"q={q_text} PSNR={result['psnr']:.9g} "
                f"compression_ratio={result['compression_ratio']:.9g}"
            )

    print(f"wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

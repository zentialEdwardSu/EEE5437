import argparse
import csv
import json
import math
import glob
from pathlib import Path

from _codec import resolve_codec, run_codec


SYMBOLS = ("IZ", "ZTR", "POS", "NEG")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Average per-image Huffman symbol probabilities for PPM files."
    )
    parser.add_argument("folder", type=Path, help="folder containing *.ppm images")
    parser.add_argument(
        "-q",
        "--quant",
        default="1",
        help="positive codec quantization step (default: 1)",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="output CSV (default: FOLDER/tuninghuff.csv)",
    )
    parser.add_argument(
        "--codec",
        help="finalproj executable (default: DIC_CODEC or build/bin/finalproj)",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    folder = args.folder.resolve()

    if not folder.is_dir():
        raise SystemExit(f"input folder not found: {folder}")

    try:
        quant = float(args.quant)
        if not math.isfinite(quant) or quant <= 0:
            raise ValueError
    except ValueError:
        raise SystemExit("--quant must be a positive number") from None

    images = sorted(
        Path(path)
        for path in glob.glob(str(folder / "*.ppm"))
        if Path(path).is_file()
    )

    if not images:
        raise SystemExit(f"no PPM images found in {folder}")

    codec = resolve_codec(args.codec)
    sums = [0.0] * len(SYMBOLS)

    for image in images:
        result = run_codec(codec, image, args.quant)
        probabilities = result.get("huffman_probabilities")

        if not isinstance(probabilities, list) or len(probabilities) != len(SYMBOLS):
            raise RuntimeError(f"invalid Huffman statistics for {image.name}")

        for index, probability in enumerate(probabilities):
            sums[index] += float(probability)

        print(f"processed {image.name}")

    averages = [total / len(images) for total in sums]

    output = (args.output or folder / "tuninghuff.csv").resolve()

    with output.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(("symbol", "probability"))
        writer.writerows(zip(SYMBOLS, averages))

    print(json.dumps(dict(zip(SYMBOLS, averages)), separators=(",", ":")))
    print(f"wrote {output}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

import argparse
from pathlib import Path
from PIL import Image


def parse_size(size: str) -> tuple[int, int]:
    try:
        width_str, height_str = size.lower().split("x")
        width = int(width_str)
        height = int(height_str)
    except ValueError:
        raise argparse.ArgumentTypeError(
            "Invalid size format. Expected format: WIDTHxHEIGHT, e.g., 800x600"
        )

    if width <= 0 or height <= 0:
        raise argparse.ArgumentTypeError("Width and height must be positive integers")

    return width, height


def convert_image(
    input_path: Path,
    output_path: Path,
    output_format: str | None = None,
    size: tuple[int, int] | None = None,
    grey: bool = False,
) -> Path:
    if not input_path.exists():
        raise FileNotFoundError(f"Input file does not exist: {input_path}")

    with Image.open(input_path) as img:
        if size is not None:
            img = img.resize(size, Image.Resampling.LANCZOS)

        if output_format is None:
            suffix = output_path.suffix.lstrip(".")
            if not suffix:
                raise ValueError(
                    "Output format cannot be inferred. "
                    "Please specify --format, e.g., --format PNG"
                )
            output_format = suffix

        output_format = output_format.upper()

        if output_format == "JPG":
            output_format = "JPEG"

        if grey:
            img = img.convert("L")

            if output_format == "PPM":
                output_path = output_path.with_suffix(".pgm")
        else:
            if output_format in {"JPEG", "PPM", "BMP"}:
                img = img.convert("RGB")
            elif output_format in {"PNG", "WEBP"}:
                if img.mode not in {"RGB", "RGBA"}:
                    img = img.convert("RGBA")

        output_path.parent.mkdir(parents=True, exist_ok=True)
        img.save(output_path, format=output_format)

    return output_path


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Read images, resize them, optionally convert to grey, and export to the specified format"
    )

    parser.add_argument(
        "input",
        type=Path,
        help="Input image path, e.g., input.png",
    )

    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        required=True,
        help="Output image path, e.g., output.jpg, output.png, output.ppm",
    )

    parser.add_argument(
        "-f",
        "--format",
        type=str,
        default=None,
        help=(
            "Output image format, e.g., PNG, JPEG, BMP, WEBP, TIFF, PPM. "
            "If omitted, inferred from output file extension."
        ),
    )

    parser.add_argument(
        "-s",
        "--size",
        type=parse_size,
        default=None,
        help="Output size, format is WIDTHxHEIGHT, e.g., 800x600",
    )

    parser.add_argument(
        "--grey",
        "--gray",
        action="store_true",
        help="Convert image to greyscale before saving",
    )

    args = parser.parse_args()

    try:
        actual_output = convert_image(
            input_path=args.input,
            output_path=args.output,
            output_format=args.format,
            size=args.size,
            grey=args.grey,
        )
    except Exception as e:
        print(f"Error: {e}")
        raise SystemExit(1)

    print(f"Exported: {actual_output}")


if __name__ == "__main__":
    main()
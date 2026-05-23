"""View ordinary images or JPEG 2000 quality-layer previews.

The default mode preserves the original one-argument viewer. The ``layers``
subcommand shells out to the repository ``finalproj`` binary so every preview is
decoded from the real J2K/JP2 packet prefix ending after that quality layer.
"""

import argparse
import math
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageTk
import tkinter as tk


WINDOW_WIDTH = 1368
WINDOW_HEIGHT = 768

SUPPORTED_FORMATS = {
    ".ppm",
    ".pgm",
    ".jp2",
    ".png",
    ".jpg",
    ".jpeg",
}

def load_image(path: Path) -> Image.Image:
    """Loads a supported still image with Pillow and owns the decoded pixels."""
    if not path.exists():
        raise FileNotFoundError(f"File does not exist: {path}")

    if not path.is_file():
        raise IsADirectoryError(f"Path is not a file: {path}")

    suffix = path.suffix.lower()
    if suffix not in SUPPORTED_FORMATS:
        raise ValueError(
            f"Unsupported image format: {suffix}. "
            f"Supported formats: {', '.join(sorted(SUPPORTED_FORMATS))}"
        )

    img = Image.open(path)
    img.load()
    return img


def fit_image_to_window(
    image: Image.Image,
    window_width: int,
    window_height: int,
) -> Image.Image:
    """Returns a resized copy that fits inside a fixed window without upscaling."""
    img_width, img_height = image.size

    scale = min(
        window_width / img_width,
        window_height / img_height,
        1.0,
    )

    new_width = max(1, int(img_width * scale))
    new_height = max(1, int(img_height * scale))

    return image.resize(
        (new_width, new_height),
        Image.Resampling.LANCZOS,
    )


class ImageViewer:
    """Fixed-size single-image viewer."""

    def __init__(self, root: tk.Tk, image: Image.Image, title: str):
        self.root = root
        self.original_image = image

        self.root.title(title)
        self.root.geometry(f"{WINDOW_WIDTH}x{WINDOW_HEIGHT}")
        self.root.resizable(False, False)

        self.canvas = tk.Canvas(
            root,
            width=WINDOW_WIDTH,
            height=WINDOW_HEIGHT,
            bg="black",
            highlightthickness=0,
        )
        self.canvas.pack()

        self.display_image = fit_image_to_window(
            self.original_image,
            WINDOW_WIDTH,
            WINDOW_HEIGHT,
        )

        self.tk_image = ImageTk.PhotoImage(self.display_image)

        x = WINDOW_WIDTH // 2
        y = WINDOW_HEIGHT // 2

        self.canvas.create_image(
            x,
            y,
            image=self.tk_image,
            anchor=tk.CENTER,
        )

        self.root.bind("<Escape>", lambda event: self.root.destroy())


class LayerPreviewViewer:
    """Scrollable grid that compares decoded JPEG 2000 quality-layer prefixes."""

    def __init__(self, root: tk.Tk, previews: list[tuple[int, Path, Image.Image]], title: str):
        self.root = root
        self.previews = previews
        self.tk_images: list[ImageTk.PhotoImage] = []

        self.root.title(title)
        self.root.geometry(f"{WINDOW_WIDTH}x{WINDOW_HEIGHT}")
        self.root.resizable(False, False)

        canvas = tk.Canvas(root, width=WINDOW_WIDTH, height=WINDOW_HEIGHT, bg="#101010", highlightthickness=0)
        scrollbar = tk.Scrollbar(root, orient=tk.VERTICAL, command=canvas.yview)
        frame = tk.Frame(canvas, bg="#101010")
        frame.bind("<Configure>", lambda event: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.create_window((0, 0), window=frame, anchor=tk.NW)
        canvas.configure(yscrollcommand=scrollbar.set)
        canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        columns = self._column_count(len(previews))
        cell_width = max(240, (WINDOW_WIDTH - 48) // columns)
        thumb_height = 260

        for index, (layer, path, image) in enumerate(previews):
            row = index // columns
            column = index % columns
            cell = tk.Frame(frame, bg="#101010", padx=10, pady=10)
            cell.grid(row=row, column=column, sticky="n")

            label = tk.Label(
                cell,
                text=f"Layer {layer}: {path.name}",
                fg="#f4f4f4",
                bg="#101010",
                font=("Helvetica", 11, "bold"),
            )
            label.pack(anchor=tk.W)

            fitted = fit_image_to_window(image, cell_width - 20, thumb_height)
            tk_image = ImageTk.PhotoImage(fitted)
            self.tk_images.append(tk_image)
            image_label = tk.Label(cell, image=tk_image, bg="#101010")
            image_label.pack(anchor=tk.CENTER, pady=(6, 0))

        self.root.bind("<Escape>", lambda event: self.root.destroy())

    @staticmethod
    def _column_count(count: int) -> int:
        """Chooses a compact grid width for comparing several layer previews."""
        if count <= 2:
            return count
        return min(4, max(2, int(math.ceil(math.sqrt(count)))))


def default_finalproj_path() -> Path:
    """Returns the repository-local finalproj executable path for this platform."""
    executable = "finalproj.exe" if sys.platform.startswith("win") else "finalproj"
    return Path(__file__).resolve().parents[1] / "build" / "bin" / executable


def parse_info_layers(output: str) -> int:
    """Extracts the COD layer count from finalproj j2k-info or jp2-info output."""
    for line in output.splitlines():
        key, _, value = line.partition(" ")
        if key == "layers":
            return int(value.strip())
    raise ValueError("finalproj info output did not contain a layers line")


def run_finalproj(finalproj: Path, arguments: Iterable[str]) -> subprocess.CompletedProcess[str]:
    """Runs finalproj with captured text output and a concrete error on failure."""
    command = [str(finalproj), *arguments]
    return subprocess.run(command, check=True, capture_output=True, text=True)


def codestream_kind(path: Path) -> str:
    """Returns the finalproj command prefix for a supported J2K or JP2 file."""
    suffix = path.suffix.lower()
    if suffix == ".j2k":
        return "j2k"
    if suffix == ".jp2":
        return "jp2"
    raise ValueError("Layer preview input must be .j2k or .jp2")


def build_layer_previews(
    codestream: Path,
    finalproj: Path,
    output_dir: Path,
    requested_layers: int | None,
) -> list[tuple[int, Path, Image.Image]]:
    """Decodes layer-prefix images and returns them as Pillow images with paths."""
    if not codestream.exists() or not codestream.is_file():
        raise FileNotFoundError(f"File does not exist: {codestream}")
    if not finalproj.exists() or not finalproj.is_file():
        raise FileNotFoundError(f"finalproj executable does not exist: {finalproj}")

    kind = codestream_kind(codestream)
    info = run_finalproj(finalproj, [f"{kind}-info", str(codestream)])
    total_layers = parse_info_layers(info.stdout)
    if total_layers <= 0:
        raise ValueError("Codestream does not signal a positive layer count")
    if requested_layers is not None and (requested_layers <= 0 or requested_layers > total_layers):
        raise ValueError(f"--layers must be in 1..{total_layers}")

    layers_to_show = requested_layers if requested_layers is not None else total_layers
    output_dir.mkdir(parents=True, exist_ok=True)
    previews: list[tuple[int, Path, Image.Image]] = []

    for layer in range(1, layers_to_show + 1):
        output_path = output_dir / f"{codestream.stem}_layer_{layer:02d}.ppm"
        run_finalproj(finalproj, [f"{kind}-decode-layer", str(codestream), str(output_path), str(layer)])
        image = load_image(output_path)
        previews.append((layer, output_path, image))

    return previews


def print_image_info(path: Path, image: Image.Image) -> None:
    """Prints concise image metadata before opening a viewer."""
    print(f"File: {path}")
    print(f"Format: {image.format}")
    print(f"Mode: {image.mode}")
    print(f"Original size: {image.width}x{image.height}")


def run_show_mode(args: argparse.Namespace) -> int:
    """Runs the original single-image viewing mode."""
    try:
        img = load_image(args.image)
    except Exception as exc:
        print(f"Error: {exc}")
        return 1

    print_image_info(args.image, img)

    root = tk.Tk()
    ImageViewer(root, img, str(args.image))
    root.mainloop()
    return 0


def run_layers_mode(args: argparse.Namespace) -> int:
    """Generates and optionally displays quality-layer preview images."""
    finalproj = args.finalproj.resolve()
    remove_temp = args.output_dir is None
    temp_dir: tempfile.TemporaryDirectory[str] | None = None

    if args.output_dir is None:
        temp_dir = tempfile.TemporaryDirectory(prefix="dic_j2k_layers_")
        output_dir = Path(temp_dir.name)
    else:
        output_dir = args.output_dir.resolve()

    try:
        previews = build_layer_previews(args.codestream.resolve(), finalproj, output_dir, args.layers)
        for layer, path, image in previews:
            print(f"Layer {layer}: {path} ({image.width}x{image.height}, {image.mode})")

        if not args.no_open:
            root = tk.Tk()
            LayerPreviewViewer(root, previews, f"Quality layers: {args.codestream}")
            root.mainloop()
    except Exception as exc:
        print(f"Error: {exc}")
        return 1
    finally:
        if remove_temp and temp_dir is not None:
            temp_dir.cleanup()

    return 0


def build_parser() -> argparse.ArgumentParser:
    """Builds the command-line parser while preserving legacy positional usage."""
    parser = argparse.ArgumentParser(
        description="Pillow image viewer and JPEG 2000 quality-layer preview tool."
    )
    subparsers = parser.add_subparsers(dest="command")

    show_parser = subparsers.add_parser("show", help="View one image file.")
    show_parser.add_argument("image", type=Path, help="Path to image file: ppm, pgm, jp2, png, jpg, jpeg")
    show_parser.set_defaults(func=run_show_mode)

    layers_parser = subparsers.add_parser("layers", help="Decode and view J2K/JP2 quality-layer prefixes.")
    layers_parser.add_argument("codestream", type=Path, help="Path to .j2k or .jp2 file")
    layers_parser.add_argument(
        "--finalproj",
        type=Path,
        default=default_finalproj_path(),
        help="Path to build/bin/finalproj executable",
    )
    layers_parser.add_argument(
        "--output-dir",
        type=Path,
        help="Directory for generated layer PPM previews. Defaults to a temporary directory.",
    )
    layers_parser.add_argument(
        "--layers",
        type=int,
        help="Limit preview generation to the first N layers.",
    )
    layers_parser.add_argument(
        "--no-open",
        action="store_true",
        help="Generate previews without opening the Tk viewer.",
    )
    layers_parser.set_defaults(func=run_layers_mode)

    return parser


def main(argv: list[str] | None = None) -> int:
    """Entrypoint for both legacy image viewing and layer preview subcommands."""
    args_list = list(sys.argv[1:] if argv is None else argv)
    if args_list and args_list[0] not in {"show", "layers", "-h", "--help"}:
        args_list.insert(0, "show")

    parser = build_parser()
    args = parser.parse_args(args_list)
    if not hasattr(args, "func"):
        parser.print_help()
        return 1
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())

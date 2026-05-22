import argparse
from pathlib import Path
from PIL import Image, ImageTk
import tkinter as tk


WINDOW_WIDTH = 1368
WINDOW_HEIGHT = 768

SUPPORTED_FORMATS = {
    ".ppm",
    ".jp2",
    ".png",
    ".jpg",
    ".jpeg",
}


def load_image(path: Path) -> Image.Image:
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


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Simple Pillow-based image viewer with fixed 1368x768 window."
    )

    parser.add_argument(
        "image",
        type=Path,
        help="Path to image file: ppm, jp2, png, jpg, jpeg",
    )

    args = parser.parse_args()

    try:
        img = load_image(args.image)
    except Exception as e:
        print(f"Error: {e}")
        raise SystemExit(1)

    print(f"File: {args.image}")
    print(f"Format: {img.format}")
    print(f"Mode: {img.mode}")
    print(f"Original size: {img.width}x{img.height}")

    root = tk.Tk()
    ImageViewer(root, img, str(args.image))
    root.mainloop()


if __name__ == "__main__":
    main()
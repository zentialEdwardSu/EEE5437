"""Polling PPM/PGM viewer — watches a file and displays it as it is written.

Intended to run alongside ``finalproj receive`` so the user sees the image
appear and improve in quality as more data arrives over the network.
When the file cannot be read the window stays black.
"""

import argparse
import os
import sys
from pathlib import Path

from PIL import Image, ImageTk
import tkinter as tk


WINDOW_WIDTH = 1368
WINDOW_HEIGHT = 768


def load_image(path: Path) -> Image.Image:
    """Load a PPM or PGM image with Pillow.  Raises on any failure."""
    if not path.exists():
        raise FileNotFoundError(f"File does not exist: {path}")
    if not path.is_file():
        raise IsADirectoryError(f"Path is not a file: {path}")

    img = Image.open(path)
    img.load()
    return img


def fit_image_to_window(
    image: Image.Image,
    window_width: int,
    window_height: int,
) -> Image.Image:
    """Return a resized copy that fits inside the window without upscaling."""
    img_width, img_height = image.size

    scale = min(
        window_width / img_width,
        window_height / img_height,
        1.0,
    )

    new_width = max(1, int(img_width * scale))
    new_height = max(1, int(img_height * scale))

    return image.resize((new_width, new_height), Image.Resampling.LANCZOS)


class PollViewer:
    """Tkinter window that polls a PPM file and displays it when readable."""

    def __init__(self, root: tk.Tk, file_path: Path, interval_ms: int):
        self.root = root
        self.file_path = file_path
        self.interval_ms = interval_ms
        self._last_pixels: bytes | None = None
        self._last_mtime: float = 0.0
        self._last_size: int = 0
        self._error_logged: bool = False

        self.root.title("Waiting for file...")
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

        self.root.bind("<Escape>", lambda event: self.root.destroy())

        # Fire first poll immediately, then schedule periodic polls.
        self.poll_cycle()
        self.root.after(self.interval_ms, self._schedule_poll)

    def _schedule_poll(self) -> None:
        """Re-schedule poll_cycle (avoids stacking calls)."""
        self.poll_cycle()
        self.root.after(self.interval_ms, self._schedule_poll)

    def poll_cycle(self) -> None:
        """Try to load the file; update display or show black."""
        try:
            st = self.file_path.stat()
            if st.st_mtime == self._last_mtime and st.st_size == self._last_size:
                return  # file unchanged, skip I/O
            self._last_mtime = st.st_mtime
            self._last_size = st.st_size

            img = load_image(self.file_path)
            self._error_logged = False
        except Exception as exc:
            if not self._error_logged:
                print(f"viwer: {exc}", file=sys.stderr)
                self._error_logged = True
            self._show_black()
            self.root.title("Waiting for file...")
            return

        pixels = img.tobytes()
        if pixels == self._last_pixels:
            return  # unchanged

        self._last_pixels = pixels

        fitted = fit_image_to_window(img, WINDOW_WIDTH, WINDOW_HEIGHT)
        self.tk_image = ImageTk.PhotoImage(fitted)

        self.canvas.delete("all")
        x = WINDOW_WIDTH // 2
        y = WINDOW_HEIGHT // 2
        self.canvas.create_image(x, y, image=self.tk_image, anchor=tk.CENTER)

        self.root.title(f"{self.file_path.name} ({img.width}×{img.height})")

    def _show_black(self) -> None:
        """Clear the canvas to a black background."""
        if self._last_pixels is not None:
            self.canvas.delete("all")
            self._last_pixels = None
            self._last_mtime = 0.0
            self._last_size = 0


def main(argv: list[str] | None = None) -> int:
    """Entry point."""
    args_list = sys.argv[1:] if argv is None else argv

    parser = argparse.ArgumentParser(
        description="Polling PPM/PGM viewer — watches a file and displays it live."
    )
    parser.add_argument("file", type=Path, help="Path to PPM or PGM file to watch")
    parser.add_argument(
        "--interval",
        type=int,
        default=200,
        metavar="MS",
        help="Polling interval in milliseconds (default: 200)",
    )
    args = parser.parse_args(args_list)

    root = tk.Tk()
    PollViewer(root, args.file.resolve(), args.interval)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

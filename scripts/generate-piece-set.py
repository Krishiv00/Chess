from pathlib import Path
import argparse
import io

# requires pillow
from PIL import Image


# for svg -> png
CELL_SIZE = 150
SUPERSAMPLE = 4

PIECES = ["P", "B", "N", "R", "Q", "K"]
COLORS = ["w", "b"]


def render_svg(path: Path, size: int) -> Image.Image:
    # requires `resvg-py`
    from resvg_py import svg_to_bytes

    png_data = svg_to_bytes(
        svg_path=str(path),
        width=size
    )

    with Image.open(io.BytesIO(png_data)) as image:
        return image.convert("RGBA")


def render_piece(path: Path, size: int) -> Image.Image:
    if path.suffix.lower() == ".svg":
        return render_svg(path, size)

    with Image.open(path) as source:
        return source.convert("RGBA")


def get_set_extension(folder: Path) -> str | None:
    for extension in (".svg", ".png"):
        if all(
            (folder / f"{color}{piece}{extension}").is_file()
            for color in COLORS for piece in PIECES
        ):
            return extension

    return None


def generate_sprite(input_dir: Path, output_path: Path) -> None:
    extension = get_set_extension(input_dir)

    if extension is None:
        raise ValueError("Invalid set (missing or inconsistent piece files).")

    if extension == ".svg":
        cell_size = CELL_SIZE * SUPERSAMPLE
    else:
        dimensions = []

        for color in COLORS:
            for piece in PIECES:
                path = input_dir / f"{color}{piece}{extension}"

                with Image.open(path) as image:
                    dimensions.append(min(image.size))

        cell_size = min(dimensions)

    sprite = Image.new(
        "RGBA",
        (6 * cell_size, 2 * cell_size),
        (0, 0, 0, 0)
    )

    for row, color in enumerate(COLORS):
        for col, piece in enumerate(PIECES):
            filename = f"{color}{piece}{extension}"
            piece_path = input_dir / filename

            image = render_piece(piece_path, cell_size)
            image.thumbnail((cell_size, cell_size), Image.Resampling.LANCZOS)

            x = col * cell_size + (cell_size - image.width) // 2
            y = row * cell_size + (cell_size - image.height) // 2

            sprite.alpha_composite(image, (x, y))

    if extension == ".svg":
        sprite = sprite.resize(
            (6 * CELL_SIZE, 2 * CELL_SIZE),
            Image.Resampling.LANCZOS
        )

    sprite.save(output_path, format="PNG", optimize=True)

    print(f"  Generated: {output_path.name} ({sprite.width}x{sprite.height})")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate chess piece sprite sheets."
    )

    parser.add_argument(
        "sets",
        nargs="+",
        help="Chess set folder names to convert."
    )

    args = parser.parse_args()

    for name in args.sets:
        folder = Path(name).resolve()

        if not folder.is_dir():
            print(f"Skipping: {name} (folder not found)")
            continue

        print(f"Processing: {folder.name}")

        output_path = folder.parent / f"{folder.name}.png"

        try:
            generate_sprite(folder, output_path)
        except Exception as e:
            print(f"  ERROR: {e}")

    print("\nFinished!")


if __name__ == "__main__":
    main()

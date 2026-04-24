# Flipper Books

An EPUB reader for the Flipper Zero (Momentum firmware).

Load `.epub` files from your phone or computer onto the SD card, then read them
on the device with bookmarks, table of contents, search, night mode,
auto-scroll, and battery-friendly power modes.

## Features

- EPUB support via a bundled Python converter (runs on phone or computer).
- `.txt` support directly on device (auto-imported to the internal format).
- Library browser that scans the SD card for supported books.
- Reader with word-wrap, real pagination, and resume-from-last-page.
- **Text size** scaling with proper layout (Tiny / Small / Medium / Large).
- **Toggleable images** (auto-dithered to 1bpp and scaled to fit the screen).
- **Page animations**: None / Slide / Fade / Curl (rendered only in Graphics mode).
- **Three power modes**:
  - **Power Saver** - backlight off, no animations, no images, CPU-light rendering.
  - **Balanced** - auto backlight, lightweight animations.
  - **Graphics** - backlight enforced on, full animations and images.
- **Bookmarks** (up to 8 per book, named, with percentage progress).
- **Table of Contents** navigation to any chapter.
- **Full-text search** (case-insensitive) with "search next" from the reader.
- **Night mode** (inverted display).
- **Auto-scroll** mode with adjustable speed (1-10 seconds per page).
- **Progress bar** toggle at the bottom of each page.
- **Reading statistics**: total time read, pages read, books opened/finished.
- **Justify / hyphenation** toggles for the text layout.
- **Vibrate on page turn** toggle.
- Per-book progress is saved automatically on every page turn and on exit.

## Controls (in reader)

| Key        | Action                                |
| ---------- | ------------------------------------- |
| `Left`     | Previous page                         |
| `Right`    | Next page                             |
| `Up`       | Add bookmark at current position      |
| `Down`     | Open table of contents                |
| `OK`       | Open in-book menu (bookmarks/search)  |
| `Back`     | Save progress and return to library   |

## Loading books

1. Connect the Flipper to your computer or use qFlipper / the mobile app.
2. Copy your book into `/ext/apps_data/books/library/`.
3. Either:
   - Drop a `.epub` file in, **after** running the converter once (the device
     keeps the converted sidecar in `/ext/apps_data/books/cache/`), or
   - Run `python3 tools/epub_to_fbook.py my-book.epub` on your computer and
     copy the resulting `.fbook` onto the Flipper's `apps_data/books/library`.
   - Or drop a plain `.txt` file (no conversion needed).

## Converter

```bash
# Requires Python 3.8+ and Pillow for image support
pip install Pillow
python3 tools/epub_to_fbook.py my-book.epub -o my-book.fbook
```

Flags:

- `--no-images` - skip image extraction (smaller files, faster conversion).
- `-o PATH` - custom output path.

### Regenerating older .fbook files

If you have `.fbook` files made before this version of the converter
they may be empty (text body length = 0) or have images packed in the
wrong byte order. Re-run the converter on the original `.epub` and
delete the stale entries from `/ext/apps_data/books/cache/` so the
new copies are used.

## Building

Uses [ufbt](https://github.com/flipperdevices/flipperzero-ufbt) with the
Momentum SDK:

```bash
pip install ufbt
ufbt update --index-url=https://up.momentum-fw.dev/firmware/directory.json --channel=release
ufbt
```

The built `.fap` lands in `dist/`.

## CI

A single GitHub Actions workflow (`.github/workflows/build-and-release.yml`)
builds the FAP on every push and publishes a GitHub Release whenever a tag
starting with `v` is pushed (or manually via the `workflow_dispatch` input).

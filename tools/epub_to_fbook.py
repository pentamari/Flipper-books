#!/usr/bin/env python3
"""Convert an EPUB to the Flipper Books .fbook format.

Usage:
    python3 tools/epub_to_fbook.py book.epub [-o out.fbook] [--no-images]

The resulting .fbook file is a compact, stream-readable binary layout
that the on-device reader understands without any ZIP/HTML parser.
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import sys
import zipfile
from dataclasses import dataclass, field
from html.parser import HTMLParser
from typing import List, Optional, Tuple
from xml.etree import ElementTree as ET

try:
    from PIL import Image  # type: ignore
except ImportError:  # pragma: no cover - Pillow is optional
    Image = None  # type: ignore

FBOOK_MAGIC = b"FBOOK\x01"
TITLE_LEN = 64
AUTHOR_LEN = 48
CHAPTER_TITLE_LEN = 32
IMAGE_MAX_WIDTH = 120
IMAGE_MAX_HEIGHT = 48
# Must match FBOOK_MAX_CHAPTERS / FBOOK_MAX_IMAGES in src/helpers/book_storage.h.
# If a book exceeds these, the on-device reader silently drops the excess, so we
# truncate here too - otherwise the file pointer desyncs and the reader can
# render garbage or nothing at all.
MAX_CHAPTERS = 128
MAX_IMAGES = 64

NS = {
    "opf": "http://www.idpf.org/2007/opf",
    "dc": "http://purl.org/dc/elements/1.1/",
    "container": "urn:oasis:names:tc:opendocument:xmlns:container",
    "xhtml": "http://www.w3.org/1999/xhtml",
    "ncx": "http://www.daisy.org/z3986/2005/ncx/",
}


@dataclass
class ChapterOut:
    offset: int
    title: str


@dataclass
class ImageOut:
    offset_in_text: int
    width: int
    height: int
    data: bytes  # 1bpp packed, MSB-first, row-major


class _TextExtractor(HTMLParser):
    """Very small HTML->text converter that keeps paragraph breaks and notes images."""

    SKIP_TAGS = {"script", "style", "head", "nav", "meta", "link"}
    BLOCK_TAGS = {
        "p", "div", "section", "article", "li", "ul", "ol",
        "h1", "h2", "h3", "h4", "h5", "h6", "br", "hr", "blockquote",
        "pre",
    }

    def __init__(self, image_hook=None):
        super().__init__(convert_charrefs=True)
        self.chunks: List[str] = []
        self._skip_depth = 0
        self._image_hook = image_hook

    def handle_starttag(self, tag, attrs):
        tag = tag.lower()
        if tag in self.SKIP_TAGS:
            self._skip_depth += 1
            return
        if tag == "img" and self._image_hook:
            src = dict(attrs).get("src")
            if src:
                marker = self._image_hook(src)
                if marker:
                    self.chunks.append(marker)
                    return
        if tag in self.BLOCK_TAGS:
            self.chunks.append("\n\n")

    def handle_endtag(self, tag):
        tag = tag.lower()
        if tag in self.SKIP_TAGS:
            if self._skip_depth > 0:
                self._skip_depth -= 1
            return
        if tag in self.BLOCK_TAGS:
            self.chunks.append("\n")

    def handle_startendtag(self, tag, attrs):
        if tag.lower() == "br":
            self.chunks.append("\n")
        else:
            self.handle_starttag(tag, attrs)

    def handle_data(self, data):
        if self._skip_depth:
            return
        self.chunks.append(data)

    def get_text(self) -> str:
        text = "".join(self.chunks)
        text = re.sub(r"[ \t]+", " ", text)
        text = re.sub(r" *\n *", "\n", text)
        text = re.sub(r"\n{3,}", "\n\n", text)
        return text.strip()


def _dither_to_1bpp(im) -> Tuple[int, int, bytes]:
    im = im.convert("L")
    w, h = im.size
    max_w, max_h = IMAGE_MAX_WIDTH, IMAGE_MAX_HEIGHT
    scale = min(max_w / w, max_h / h, 1.0)
    if scale < 1.0:
        w = max(1, int(w * scale))
        h = max(1, int(h * scale))
        im = im.resize((w, h))
    mono = im.convert("1")
    pixels = mono.tobytes()
    # PIL packs 1bpp MSB-first already; but rows are padded to full bytes per row.
    # We repack tightly: row-major, MSB-first.
    row_bytes = (w + 7) // 8
    padded_row = (w + 7) // 8  # PIL uses same - but row padding differs on some builds.
    if len(pixels) == row_bytes * h:
        return w, h, pixels
    # Fallback: repack pixel by pixel.
    out = bytearray(row_bytes * h)
    px = mono.load()
    for y in range(h):
        for x in range(w):
            if px[x, y] == 0:  # 0 = black in PIL 1bpp
                out[y * row_bytes + (x >> 3)] |= 0x80 >> (x & 7)
    return w, h, bytes(out)


def _find_opf(zf: zipfile.ZipFile) -> str:
    try:
        with zf.open("META-INF/container.xml") as f:
            root = ET.parse(f).getroot()
    except KeyError as exc:
        raise ValueError("Not a valid EPUB: missing META-INF/container.xml") from exc
    rootfile = root.find(".//container:rootfile", NS)
    if rootfile is None:
        raise ValueError("EPUB container.xml missing rootfile")
    return rootfile.attrib["full-path"]


def _load_spine(zf: zipfile.ZipFile, opf_path: str):
    with zf.open(opf_path) as f:
        root = ET.parse(f).getroot()

    title = (root.findtext(".//dc:title", default="", namespaces=NS) or "").strip()
    author = (root.findtext(".//dc:creator", default="", namespaces=NS) or "").strip()

    manifest = {}
    for item in root.findall(".//opf:manifest/opf:item", NS):
        manifest[item.attrib["id"]] = (
            item.attrib["href"],
            item.attrib.get("media-type", ""),
        )

    spine_ids = [
        itemref.attrib["idref"]
        for itemref in root.findall(".//opf:spine/opf:itemref", NS)
    ]

    base = os.path.dirname(opf_path)

    def resolve(href: str) -> str:
        return os.path.normpath(os.path.join(base, href)).replace("\\", "/")

    documents = []
    for idref in spine_ids:
        entry = manifest.get(idref)
        if not entry:
            continue
        href, media = entry
        if "html" not in media and not href.lower().endswith((".html", ".xhtml", ".htm")):
            continue
        documents.append(resolve(href))

    return title, author, manifest, base, documents


def _extract_title_from_doc(raw_html: str) -> Optional[str]:
    m = re.search(r"<h[12][^>]*>(.*?)</h[12]>", raw_html, re.IGNORECASE | re.DOTALL)
    if m:
        text = re.sub(r"<[^>]+>", "", m.group(1)).strip()
        text = re.sub(r"\s+", " ", text)
        if text:
            return text[:CHAPTER_TITLE_LEN - 1]
    return None


def convert(
    epub_path: str,
    out_path: Optional[str] = None,
    include_images: bool = True,
) -> str:
    if out_path is None:
        base = os.path.splitext(os.path.basename(epub_path))[0]
        out_path = base + ".fbook"

    with zipfile.ZipFile(epub_path) as zf:
        opf_path = _find_opf(zf)
        title, author, manifest, base_dir, documents = _load_spine(zf, opf_path)

        text_buf = bytearray()
        chapters: List[ChapterOut] = []
        images: List[ImageOut] = []
        image_cache: dict[str, int] = {}

        placeholder_re = re.compile(r"\x01IMG\x01(\d+)\x01")

        for doc_path in documents:
            try:
                raw = zf.read(doc_path).decode("utf-8", errors="replace")
            except KeyError:
                continue
            chap_title = _extract_title_from_doc(raw) or os.path.basename(doc_path)

            pending_images: List[Tuple[int, str]] = []

            def image_hook(src: str) -> Optional[str]:
                if not include_images or Image is None:
                    return None
                doc_dir = os.path.dirname(doc_path)
                full = os.path.normpath(os.path.join(doc_dir, src)).replace("\\", "/")
                if full in image_cache:
                    idx = image_cache[full]
                else:
                    if len(images) >= MAX_IMAGES:
                        return None
                    try:
                        data = zf.read(full)
                    except KeyError:
                        return None
                    try:
                        with Image.open(__import__("io").BytesIO(data)) as im:
                            w, h, packed = _dither_to_1bpp(im)
                    except Exception:
                        return None
                    idx = len(images)
                    images.append(ImageOut(offset_in_text=0, width=w, height=h, data=packed))
                    image_cache[full] = idx
                return f"\x01IMG\x01{idx}\x01"

            parser = _TextExtractor(image_hook=image_hook)
            parser.feed(raw)
            parser.close()
            text = parser.get_text()

            chapter_start = len(text_buf)
            if len(chapters) < MAX_CHAPTERS:
                chapters.append(ChapterOut(offset=chapter_start, title=chap_title))

            pos = 0
            for m in placeholder_re.finditer(text):
                text_buf.extend(text[pos:m.start()].encode("utf-8", errors="replace"))
                idx = int(m.group(1))
                images[idx].offset_in_text = len(text_buf)
                pos = m.end()
            text_buf.extend(text[pos:].encode("utf-8", errors="replace"))
            text_buf.extend(b"\n\n")

        header_size = (
            len(FBOOK_MAGIC) + 2 + TITLE_LEN + AUTHOR_LEN + 4 + 4 + 2 + 2
        )
        chapter_table_size = len(chapters) * (4 + CHAPTER_TITLE_LEN)
        image_table_size = len(images) * (4 + 2 + 2 + 4)

        text_offset = header_size + chapter_table_size + image_table_size
        text_len = len(text_buf)
        image_data_offset_start = text_offset + text_len

        # Assign image data offsets
        cursor = image_data_offset_start
        for img in images:
            img_data_offset = cursor
            cursor += len(img.data)
            img.data_offset = img_data_offset  # type: ignore[attr-defined]

        with open(out_path, "wb") as f:
            f.write(FBOOK_MAGIC)
            f.write(struct.pack("<H", 1))
            f.write(title.encode("utf-8")[:TITLE_LEN - 1].ljust(TITLE_LEN, b"\x00"))
            f.write(author.encode("utf-8")[:AUTHOR_LEN - 1].ljust(AUTHOR_LEN, b"\x00"))
            f.write(struct.pack("<I", text_offset))
            f.write(struct.pack("<I", text_len))
            f.write(struct.pack("<H", len(chapters)))
            f.write(struct.pack("<H", len(images)))

            for ch in chapters:
                f.write(struct.pack("<I", ch.offset))
                f.write(ch.title.encode("utf-8")[:CHAPTER_TITLE_LEN - 1].ljust(CHAPTER_TITLE_LEN, b"\x00"))

            for img in images:
                f.write(struct.pack("<I", img.offset_in_text))
                f.write(struct.pack("<H", img.width))
                f.write(struct.pack("<H", img.height))
                f.write(struct.pack("<I", img.data_offset))  # type: ignore[attr-defined]

            f.write(bytes(text_buf))
            for img in images:
                f.write(img.data)

    return out_path


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Convert EPUB to Flipper .fbook")
    parser.add_argument("epub", help="Path to input .epub file")
    parser.add_argument("-o", "--output", help="Output .fbook path")
    parser.add_argument("--no-images", action="store_true", help="Strip images")
    args = parser.parse_args(argv)

    out = convert(args.epub, args.output, include_images=not args.no_images)
    print(f"Wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

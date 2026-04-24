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

FBOOK_V1_MAGIC = b"FBOOK\x01"
FBOOK_V2_MAGIC = b"FBOOK\x02"
TITLE_LEN = 64
AUTHOR_LEN = 48
LANGUAGE_LEN = 8
CHAPTER_TITLE_V1 = 32
CHAPTER_TITLE_V2 = 48
IMAGE_MAX_WIDTH = 128
IMAGE_MAX_HEIGHT = 64           # v2 raised from 48
COVER_MAX_WIDTH = 64            # cover thumbnail size for the library view
COVER_MAX_HEIGHT = 64
# Must match FBOOK_MAX_CHAPTERS / FBOOK_MAX_IMAGES in src/helpers/book_storage.h.
# If a book exceeds these, the on-device reader silently drops the excess, so we
# truncate here too - otherwise the file pointer desyncs and the reader can
# render garbage or nothing at all.
MAX_CHAPTERS = 256
MAX_IMAGES = 96
V2_HEADER_SIZE = 224
V2_CHAPTER_SIZE = 4 + 4 + CHAPTER_TITLE_V2  # 56
V2_IMAGE_SIZE = 4 + 2 + 2 + 4 + 4 + 1 + 3   # 20

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
class ChapterOutV2:
    offset: int
    word_count: int
    title: str


@dataclass
class ImageOut:
    offset_in_text: int
    width: int
    height: int
    data: bytes
    fmt: int = 0  # 0 = 1bpp, 1 = 2bpp grayscale


class _TextExtractor(HTMLParser):
    """Very small HTML->text converter that keeps paragraph breaks and notes images."""

    SKIP_TAGS = {"script", "style", "head", "nav"}
    # Void elements per HTML spec - they can't have descendants. We do NOT track
    # a skip-depth for them, otherwise an XHTML-style self-closing <meta/> or
    # <link/> in <head> (which has no matching end tag) leaves the parser stuck
    # in skip mode forever and the entire body of the book is silently dropped.
    VOID_SKIP_TAGS = {"meta", "link", "base", "param", "source", "track"}
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
        if tag in self.VOID_SKIP_TAGS:
            return
        if tag in self.SKIP_TAGS:
            self._skip_depth += 1
            return
        if self._skip_depth:
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
        if tag in self.VOID_SKIP_TAGS:
            return
        if tag in self.SKIP_TAGS:
            if self._skip_depth > 0:
                self._skip_depth -= 1
            return
        if self._skip_depth:
            return
        if tag in self.BLOCK_TAGS:
            self.chunks.append("\n")

    def handle_startendtag(self, tag, attrs):
        tl = tag.lower()
        if tl == "br":
            if not self._skip_depth:
                self.chunks.append("\n")
            return
        if tl in self.VOID_SKIP_TAGS:
            return
        if tl in self.SKIP_TAGS:
            # Self-closing <head/> etc. has no descendants, no skip context.
            return
        # img, hr and friends: run start logic only.
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
    # Flipper's canvas_draw_xbm expects XBM byte order: LSB-first within each
    # byte (bit 0 = leftmost pixel). PIL packs 1bpp MSB-first, so we pack
    # pixel-by-pixel in LSB-first order. Black in PIL 1bpp is 0; in XBM a set
    # bit means "pixel drawn" (black on the e-ink-ish LCD).
    row_bytes = (w + 7) // 8
    out = bytearray(row_bytes * h)
    px = mono.load()
    for y in range(h):
        for x in range(w):
            if px[x, y] == 0:  # black
                out[y * row_bytes + (x >> 3)] |= 1 << (x & 7)
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
    language = (root.findtext(".//dc:language", default="", namespaces=NS) or "").strip()

    manifest = {}
    for item in root.findall(".//opf:manifest/opf:item", NS):
        manifest[item.attrib["id"]] = (
            item.attrib["href"],
            item.attrib.get("media-type", ""),
            item.attrib.get("properties", ""),
        )

    spine_ids = [
        itemref.attrib["idref"]
        for itemref in root.findall(".//opf:spine/opf:itemref", NS)
    ]

    # Cover detection: prefer the EPUB3 manifest "cover-image" property,
    # then EPUB2 <meta name="cover" content="..."/>, then a manifest id of
    # "cover" or any item whose href looks like a cover image.
    cover_ref = None
    for item_id, (href, media, props) in manifest.items():
        if "cover-image" in (props or ""):
            cover_ref = href
            break
    if cover_ref is None:
        for meta in root.findall(".//opf:metadata/opf:meta", NS):
            if meta.attrib.get("name", "").lower() == "cover":
                ref_id = meta.attrib.get("content")
                if ref_id and ref_id in manifest:
                    cover_ref = manifest[ref_id][0]
                    break
    if cover_ref is None:
        for cand in ("cover", "cover-image", "coverimage"):
            if cand in manifest:
                cover_ref = manifest[cand][0]
                break
    if cover_ref is None:
        for href, media, _ in manifest.values():
            if media.startswith("image/") and "cover" in href.lower():
                cover_ref = href
                break

    base = os.path.dirname(opf_path)

    def resolve(href: str) -> str:
        return os.path.normpath(os.path.join(base, href)).replace("\\", "/")

    documents = []
    for idref in spine_ids:
        entry = manifest.get(idref)
        if not entry:
            continue
        href, media, _ = entry
        if "html" not in media and not href.lower().endswith((".html", ".xhtml", ".htm")):
            continue
        documents.append(resolve(href))

    cover_path = resolve(cover_ref) if cover_ref else None
    return title, author, language, manifest, base, documents, cover_path


def _extract_title_from_doc(raw_html: str) -> Optional[str]:
    m = re.search(r"<h[12][^>]*>(.*?)</h[12]>", raw_html, re.IGNORECASE | re.DOTALL)
    if m:
        text = re.sub(r"<[^>]+>", "", m.group(1)).strip()
        text = re.sub(r"\s+", " ", text)
        if text:
            return text[:CHAPTER_TITLE_V2 - 1]
    return None


def _word_count(s: str) -> int:
    """Cheap word counter used for progress estimates and chapter metadata."""
    return len(re.findall(r"\b\w+\b", s, flags=re.UNICODE))


def _render_cover_1bpp(im, max_w=COVER_MAX_WIDTH, max_h=COVER_MAX_HEIGHT) -> Tuple[int, int, bytes]:
    """Convert a PIL image to a tightly-packed LSB-first 1bpp cover thumbnail."""
    im = im.convert("L")
    w, h = im.size
    scale = min(max_w / w, max_h / h, 1.0)
    if scale < 1.0:
        w = max(1, int(w * scale))
        h = max(1, int(h * scale))
        im = im.resize((w, h))
    # Floyd-Steinberg dither (PIL's default for "1" mode) keeps cover art
    # readable at thumbnail size.
    mono = im.convert("1")
    row_bytes = (w + 7) // 8
    out = bytearray(row_bytes * h)
    px = mono.load()
    for y in range(h):
        for x in range(w):
            if px[x, y] == 0:
                out[y * row_bytes + (x >> 3)] |= 1 << (x & 7)
    return w, h, bytes(out)


def _build_book_data(zf, opf_path, include_images):
    """Walk the spine, extract text, collect images. Returns the per-book
    in-memory representation used by both the v1 and v2 writers."""
    title, author, language, manifest, base_dir, documents, cover_path = _load_spine(zf, opf_path)

    text_buf = bytearray()
    chapters: List[ChapterOutV2] = []
    images: List[ImageOut] = []
    image_cache: dict[str, int] = {}

    placeholder_re = re.compile(r"\x01IMG\x01(\d+)\x01")

    for doc_path in documents:
        try:
            raw = zf.read(doc_path).decode("utf-8", errors="replace")
        except KeyError:
            continue
        chap_title = _extract_title_from_doc(raw) or os.path.basename(doc_path)

        def image_hook(src: str, _doc_path=doc_path) -> Optional[str]:
            if not include_images or Image is None:
                return None
            doc_dir = os.path.dirname(_doc_path)
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
                images.append(
                    ImageOut(offset_in_text=0, width=w, height=h, data=packed, fmt=0))
                image_cache[full] = idx
            return f"\x01IMG\x01{idx}\x01"

        parser = _TextExtractor(image_hook=image_hook)
        parser.feed(raw)
        parser.close()
        text = parser.get_text()

        chapter_start = len(text_buf)
        chap_words = _word_count(text)
        if len(chapters) < MAX_CHAPTERS:
            chapters.append(ChapterOutV2(offset=chapter_start,
                                         word_count=chap_words,
                                         title=chap_title))

        pos = 0
        for m in placeholder_re.finditer(text):
            text_buf.extend(text[pos:m.start()].encode("utf-8", errors="replace"))
            idx = int(m.group(1))
            images[idx].offset_in_text = len(text_buf)
            pos = m.end()
        text_buf.extend(text[pos:].encode("utf-8", errors="replace"))
        text_buf.extend(b"\n\n")

    cover_w = cover_h = 0
    cover_data = b""
    if cover_path and Image is not None:
        try:
            raw = zf.read(cover_path)
            with Image.open(__import__("io").BytesIO(raw)) as im:
                cover_w, cover_h, cover_data = _render_cover_1bpp(im)
        except Exception:
            cover_w = cover_h = 0
            cover_data = b""

    return {
        "title": title,
        "author": author,
        "language": language,
        "text_buf": text_buf,
        "chapters": chapters,
        "images": images,
        "cover_w": cover_w,
        "cover_h": cover_h,
        "cover_data": cover_data,
    }


def _write_v1(book, out_path):
    """Write a v1 (.fbook) file. Kept as a fallback for older readers."""
    chapters = book["chapters"]
    images = book["images"]
    text_buf = book["text_buf"]

    header_size = (
        len(FBOOK_V1_MAGIC) + 2 + TITLE_LEN + AUTHOR_LEN + 4 + 4 + 2 + 2
    )
    chapter_table_size = len(chapters) * (4 + CHAPTER_TITLE_V1)
    image_table_size = len(images) * (4 + 2 + 2 + 4)
    text_offset = header_size + chapter_table_size + image_table_size
    text_len = len(text_buf)
    cursor = text_offset + text_len
    for img in images:
        img.data_offset = cursor  # type: ignore[attr-defined]
        cursor += len(img.data)

    with open(out_path, "wb") as f:
        f.write(FBOOK_V1_MAGIC)
        f.write(struct.pack("<H", 1))
        f.write(book["title"].encode("utf-8")[:TITLE_LEN - 1].ljust(TITLE_LEN, b"\x00"))
        f.write(book["author"].encode("utf-8")[:AUTHOR_LEN - 1].ljust(AUTHOR_LEN, b"\x00"))
        f.write(struct.pack("<I", text_offset))
        f.write(struct.pack("<I", text_len))
        f.write(struct.pack("<H", len(chapters)))
        f.write(struct.pack("<H", len(images)))
        for ch in chapters:
            f.write(struct.pack("<I", ch.offset))
            f.write(ch.title.encode("utf-8")[:CHAPTER_TITLE_V1 - 1].ljust(CHAPTER_TITLE_V1, b"\x00"))
        for img in images:
            f.write(struct.pack("<I", img.offset_in_text))
            f.write(struct.pack("<H", img.width))
            f.write(struct.pack("<H", img.height))
            f.write(struct.pack("<I", img.data_offset))  # type: ignore[attr-defined]
        f.write(bytes(text_buf))
        for img in images:
            f.write(img.data)


def _write_v2(book, out_path):
    """Write a v2 (.fbook2) file with cover thumbnail and per-chapter word
    counts. The reader auto-detects v1 vs v2 from the magic prefix."""
    chapters = book["chapters"]
    images = book["images"]
    text_buf = book["text_buf"]
    cover_w = book["cover_w"]
    cover_h = book["cover_h"]
    cover_data = book["cover_data"]

    chapter_table_size = len(chapters) * V2_CHAPTER_SIZE
    image_table_size = len(images) * V2_IMAGE_SIZE
    text_offset = V2_HEADER_SIZE + chapter_table_size + image_table_size
    text_len = len(text_buf)

    cursor = text_offset + text_len
    cover_offset = 0
    if cover_data:
        cover_offset = cursor
        cursor += len(cover_data)
    for img in images:
        img.data_offset = cursor  # type: ignore[attr-defined]
        cursor += len(img.data)

    word_count = sum(c.word_count for c in chapters)
    flags = 0
    if cover_data:
        flags |= 0x0001  # has-cover

    with open(out_path, "wb") as f:
        f.write(FBOOK_V2_MAGIC)
        f.write(struct.pack("<H", 2))   # version
        f.write(struct.pack("<H", flags))
        f.write(book["title"].encode("utf-8")[:TITLE_LEN - 1].ljust(TITLE_LEN, b"\x00"))
        f.write(book["author"].encode("utf-8")[:AUTHOR_LEN - 1].ljust(AUTHOR_LEN, b"\x00"))
        f.write(book["language"].encode("utf-8")[:LANGUAGE_LEN - 1].ljust(LANGUAGE_LEN, b"\x00"))
        f.write(struct.pack("<I", text_offset))
        f.write(struct.pack("<I", text_len))
        f.write(struct.pack("<I", word_count))
        f.write(struct.pack("<H", len(chapters)))
        f.write(struct.pack("<H", len(images)))
        f.write(struct.pack("<I", cover_offset))
        f.write(struct.pack("<H", cover_w))
        f.write(struct.pack("<H", cover_h))
        f.write(struct.pack("<I", len(cover_data)))
        f.write(struct.pack("<B", 0))  # cover_format = 1bpp
        f.write(struct.pack("<B", 0))  # default image_format = 1bpp
        # Pad header to V2_HEADER_SIZE.
        consumed = (
            len(FBOOK_V2_MAGIC) + 2 + 2 + TITLE_LEN + AUTHOR_LEN + LANGUAGE_LEN +
            4 + 4 + 4 + 2 + 2 + 4 + 2 + 2 + 4 + 1 + 1
        )
        f.write(b"\x00" * (V2_HEADER_SIZE - consumed))

        for ch in chapters:
            f.write(struct.pack("<I", ch.offset))
            f.write(struct.pack("<I", ch.word_count))
            f.write(ch.title.encode("utf-8")[:CHAPTER_TITLE_V2 - 1].ljust(CHAPTER_TITLE_V2, b"\x00"))
        for img in images:
            f.write(struct.pack("<I", img.offset_in_text))
            f.write(struct.pack("<H", img.width))
            f.write(struct.pack("<H", img.height))
            f.write(struct.pack("<I", img.data_offset))  # type: ignore[attr-defined]
            f.write(struct.pack("<I", len(img.data)))
            f.write(struct.pack("<B", img.fmt))
            f.write(b"\x00" * 3)
        f.write(bytes(text_buf))
        if cover_data:
            f.write(cover_data)
        for img in images:
            f.write(img.data)


def convert(
    epub_path: str,
    out_path: Optional[str] = None,
    include_images: bool = True,
    fmt_version: int = 2,
) -> str:
    if out_path is None:
        base = os.path.splitext(os.path.basename(epub_path))[0]
        out_path = base + ".fbook"

    with zipfile.ZipFile(epub_path) as zf:
        opf_path = _find_opf(zf)
        book = _build_book_data(zf, opf_path, include_images)

        if fmt_version == 1:
            _write_v1(book, out_path)
        else:
            _write_v2(book, out_path)
    return out_path


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Convert EPUB to Flipper .fbook / .fbook2")
    parser.add_argument("epub", help="Path to input .epub file")
    parser.add_argument("-o", "--output", help="Output .fbook path")
    parser.add_argument("--no-images", action="store_true", help="Strip images")
    parser.add_argument("--v1", action="store_true",
                        help="Emit legacy v1 format (no cover, smaller images, no chapter word counts)")
    args = parser.parse_args(argv)

    fmt = 1 if args.v1 else 2
    out = convert(args.epub, args.output,
                  include_images=not args.no_images,
                  fmt_version=fmt)
    print(f"Wrote {out} (v{fmt})")
    return 0


if __name__ == "__main__":
    sys.exit(main())

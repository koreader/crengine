#!/usr/bin/env python3
"""Generate a set of fixed-layout (EPUB3 pre-paginated) test EPUBs for crengine.

Each output file exercises a distinct way a book can declare fixed-layout, so
the importer's detection paths (crengine/src/epubfmt.cpp) and the per-page
viewport capture (crengine/src/lvtinydom.cpp) can be verified end to end:

  fxl_epub3_viewport.epub
      EPUB3 with a global  <meta property="rendition:layout">pre-paginated
      Every spine document carries its own
          <meta name="viewport" content="width=W, height=H">
      and a single full-page <img> (the common "image per page" book).
      Alternating rendition:page-spread-left / -right itemref properties.
      Expected: fixed-layout mode = 1 (ALL), per-page Viewport stamped.

  fxl_mixed.epub
      Global layout is reflowable; two of five spine items opt in with
          properties="rendition:layout-pre-paginated"
      and one pre-paginated-by-default book would opt out with
          properties="rendition:layout-reflowable".
      Expected: fixed-layout mode = 2 (MIXED).

  fxl_kf8_legacy.epub
      Legacy Amazon/KF8-in-EPUB hints in the OPF metadata:
          <meta name="fixed-layout" content="true"/>
          <meta name="original-resolution" content="800x1200"/>
          <meta name="primary-writing-mode" content="horizontal-rl"/>
      No per-page viewport metas (tests the OPF viewport fallback and the
      writing-mode => rtl page progression).
      Expected: fixed-layout mode = 1, page-progression-direction = rtl.

  fxl_ibooks.epub
      No rendition:layout in the OPF at all; fixed-layout is declared only in
          META-INF/com.apple.ibooks.display-options.xml
      Expected: fixed-layout mode = 1 (via the iBooks fallback path).

  fxl_rtl_spread.epub
      EPUB3 pre-paginated manga: <spine page-progression-direction="rtl">
      with per-page rendition:page-spread-left/right.
      Expected: fixed-layout mode = 1, page-progression-direction = rtl,
      PageSpread stamped on each <DocFragment>.

  reflowable_control.epub
      An ordinary reflowable EPUB3 (no fixed-layout markers) used as a
      negative control. Expected: fixed-layout mode = 0 (NONE).

Usage:
    ./make_fixedlayout_test_epub.py [output_dir]

The images are solid-colour PNGs generated in pure Python (no Pillow needed),
so slice-2 scaling/pagination can still tell the pages apart by colour.
"""

import os
import struct
import sys
import zlib
import zipfile

MIMETYPE = b"application/epub+zip"

CONTAINER_XML = """\
<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>
"""

IBOOKS_DISPLAY_OPTIONS = """\
<?xml version="1.0" encoding="UTF-8"?>
<display_options>
  <platform name="*">
    <option name="fixed-layout">true</option>
    <option name="open-to-spread">false</option>
  </platform>
</display_options>
"""

# A handful of distinct page colours (R, G, B).
PAGE_COLORS = [
    (0xE6, 0x3B, 0x2E),  # red
    (0x2E, 0x8B, 0xE6),  # blue
    (0x3B, 0xB0, 0x4E),  # green
    (0xE6, 0xC3, 0x2E),  # yellow
    (0x8B, 0x3B, 0xE6),  # purple
    (0xE6, 0x7E, 0x2E),  # orange
]


def make_solid_png(width, height, rgb):
    """Return bytes of a minimal solid-colour RGB PNG (no external deps)."""
    r, g, b = rgb
    # Each scanline is prefixed with a filter-type byte (0 = none).
    row = bytes([0]) + bytes([r, g, b]) * width
    raw = row * height

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)  # 8-bit, colour type 2 (RGB)
    idat = zlib.compress(raw, 9)
    return sig + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")


def fxl_page_xhtml(title, vp_w, vp_h, img_href, with_viewport_meta=True):
    """A fixed-layout spine document: a single full-page image on a fixed canvas."""
    viewport = ""
    if with_viewport_meta:
        viewport = '  <meta name="viewport" content="width=%d, height=%d"/>\n' % (vp_w, vp_h)
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<!DOCTYPE html>\n'
        '<html xmlns="http://www.w3.org/1999/xhtml">\n'
        '<head>\n'
        '  <title>%s</title>\n'
        '%s'
        '  <style>\n'
        '    html, body { margin: 0; padding: 0; }\n'
        '    img { width: 100%%; height: 100%%; }\n'
        '  </style>\n'
        '</head>\n'
        '<body>\n'
        '  <img src="%s" alt="%s"/>\n'
        '</body>\n'
        '</html>\n'
    ) % (title, viewport, img_href, title)


def svg_page_xhtml(title, vp_w, vp_h, img_href):
    """A fixed-layout page that declares its canvas with an <svg viewBox> wrapping
    a single full-page image, and has NO <meta name="viewport"> (as many cover /
    title pages do). Exercises the SVG-viewBox viewport fallback and the full-page
    SVG fill: the image must fill the page edge to edge, not be letterboxed."""
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<!DOCTYPE html>\n'
        '<html xmlns="http://www.w3.org/1999/xhtml">\n'
        '<head>\n'
        '  <title>%s</title>\n'
        '  <style>html, body { margin: 0; padding: 0; text-align: center; }</style>\n'
        '</head>\n'
        '<body>\n'
        '  <div>\n'
        '    <svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink"'
        ' width="100%%" height="100%%" viewBox="0 0 %d %d" preserveAspectRatio="xMidYMid meet">\n'
        '      <image width="%d" height="%d" xlink:href="%s"/>\n'
        '    </svg>\n'
        '  </div>\n'
        '</body>\n'
        '</html>\n'
    ) % (title, vp_w, vp_h, vp_w, vp_h, img_href)


def positioned_page_xhtml(title, vp_w, vp_h):
    """A fixed-layout page whose content is CSS-absolutely-positioned coloured
    boxes on a white canvas, at coordinates a render test can assert pixels for
    (it exercises slice 3: position:absolute + top/left/right/bottom + width/
    height in px and %). No images, so the expected colours are exact.

    Layout on the %dx%d canvas:
      - green strip:  top:0 left:0   width:100%% height:40px   -> full-width top band
      - red box:      top:100 left:50 width:200 height:150     -> anchored by top/left
      - blue box:     bottom:0 right:0 width:120 height:90      -> anchored by bottom/right
      - a nested box inside the red one (top:20 left:20 40x40, yellow) checks that a
        positioned child is placed relative to its positioned ancestor, not the page.
    """
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<!DOCTYPE html>\n'
        '<html xmlns="http://www.w3.org/1999/xhtml">\n'
        '<head>\n'
        '  <title>%s</title>\n'
        '  <meta name="viewport" content="width=%d, height=%d"/>\n'
        '  <style>\n'
        '    html, body { margin: 0; padding: 0; background: #ffffff; }\n'
        '    #green { position: absolute; top: 0;    left: 0;  width: 100%%; height: 40px;  background: #3bb04e; }\n'
        '    #red   { position: absolute; top: 100px; left: 50px; width: 200px; height: 150px; background: #e63b2e; }\n'
        '    #blue  { position: absolute; bottom: 0;  right: 0;  width: 120px; height: 90px;  background: #2e8be6; }\n'
        '    #inner { position: absolute; top: 20px; left: 20px; width: 40px; height: 40px;  background: #e6c32e; }\n'
        '  </style>\n'
        '</head>\n'
        '<body>\n'
        '  <div id="green"></div>\n'
        '  <div id="red"><div id="inner"></div></div>\n'
        '  <div id="blue"></div>\n'
        '</body>\n'
        '</html>\n'
    ) % (title, vp_w, vp_h)


def transform_page_xhtml(title, vp_w, vp_h):
    """A fixed-layout page built like a PDFTron-generated book's "text layer"
    (slice 3b): a full-page container is scaled down with transform:scale(1/32)
    and transform-origin:left bottom, and its content is positioned in the
    un-scaled (32x) coordinate system with huge offsets and a huge font-size.
    After the engine folds the ancestor scale into each child's font-size and px
    offsets/size, everything lands at known viewport pixels a render test can
    assert.

    scale = 0.025 = 1/40, so a child coordinate C maps to viewport C/40. (0.025 is
    deliberately NOT a multiple of 1/256: it catches a scale stored at only 1/256
    precision, which would misplace the high-'bottom' green box by ~70px.) The
    container's origin is its bottom-left corner (at the viewport's bottom-left),
    so 'left' is measured from x=0 and 'bottom' up from y=%d. On the %dx%d canvas:
      - green: left 2000  bottom 46000  4000x2000  -> x[50,150]  y[0,50]    (top)
      - blue:  left 2000  bottom 24000  4000x4000  -> x[50,150]  y[500,600] (middle)
      - red:   left 26000 bottom 2000   4000x6000  -> x[650,750] y[1000,1150] (bottom)
      - text:  left 4000  bottom 4000   font 1280px (-> 32px), magenta background;
               its content-height box proves the font-size was scaled (a 1280px
               font would make the box ~1280px tall instead of ~32px).
      - kern:  an inline row (like a PDFTron page number) at y~330 of three
               background-coloured space spans; the 2nd is pushed right by a big
               margin-left and the 3rd pulled back by a negative margin-left. This
               checks that margins are folded by the scale (unfolded, the boxes fly
               off-screen) and that a negative inline margin is honoured (ignored,
               the red box would not move left of the blue one).
    """
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<!DOCTYPE html>\n'
        '<html xmlns="http://www.w3.org/1999/xhtml">\n'
        '<head>\n'
        '  <title>%s</title>\n'
        '  <meta name="viewport" content="width=%d, height=%d"/>\n'
        '  <style>\n'
        '    html, body { margin: 0; padding: 0; background: #ffffff; }\n'
        '    #layer { position: absolute; left: 0; top: 0; width: 100%%; height: 100%%;\n'
        '             transform-origin: left bottom; transform: scale(0.025); }\n'
        '    #layer div { position: absolute; margin: 0; white-space: pre; }\n'
        '    #green { left: 2000px;  bottom: 46000px; width: 4000px; height: 2000px; background: #3bb04e; }\n'
        '    #blue  { left: 2000px;  bottom: 24000px; width: 4000px; height: 4000px; background: #2e8be6; }\n'
        '    #red   { left: 26000px; bottom: 2000px;  width: 4000px; height: 6000px; background: #e63b2e; }\n'
        '    #text  { left: 4000px;  bottom: 4000px;  width: 4000px; font-size: 1280px; line-height: 1.0;\n'
        '             color: #000000; background: #c030c0; }\n'
        '    #kern  { left: 2000px;  bottom: 34000px; font-size: 1600px; line-height: 1.0; white-space: nowrap; }\n'
        '    #kg { color: #3bb04e; background: #3bb04e; }\n'
        '    #kb { color: #2e8be6; background: #2e8be6; margin-left: 12000px; }\n'
        '    #kr { color: #e63b2e; background: #e63b2e; margin-left: -11200px; }\n'
        '  </style>\n'
        '</head>\n'
        '<body>\n'
        '  <div id="layer">\n'
        '    <div id="green"></div>\n'
        '    <div id="blue"></div>\n'
        '    <div id="red"></div>\n'
        '    <div id="text">Mg</div>\n'
        '    <div id="kern"><span id="kg">&#9608;&#9608;</span><span id="kb">&#9608;&#9608;</span><span id="kr">&#9608;&#9608;</span></div>\n'
        '  </div>\n'
        '</body>\n'
        '</html>\n'
    ) % (title, vp_w, vp_h)


def reflowable_page_xhtml(title, text):
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<!DOCTYPE html>\n'
        '<html xmlns="http://www.w3.org/1999/xhtml">\n'
        '<head><title>%s</title></head>\n'
        '<body><h1>%s</h1><p>%s</p></body>\n'
        '</html>\n'
    ) % (title, title, text)


def nav_xhtml(entries):
    """A minimal EPUB3 navigation document."""
    lis = "\n".join('      <li><a href="%s">%s</a></li>' % (href, label)
                    for href, label in entries)
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">\n'
        '<head><title>Navigation</title></head>\n'
        '<body>\n'
        '  <nav epub:type="toc" id="toc">\n'
        '    <ol>\n'
        '%s\n'
        '    </ol>\n'
        '  </nav>\n'
        '</body>\n'
        '</html>\n'
    ) % lis


def build_opf(title, metadata_meta, manifest_items, spine_attrs, spine_items,
              uid="urn:uuid:fixedlayout-test-0001"):
    manifest = "\n".join(
        '    <item id="%s" href="%s" media-type="%s"%s/>'
        % (i["id"], i["href"], i["type"], (' properties="%s"' % i["properties"]) if i.get("properties") else "")
        for i in manifest_items)
    spine = "\n".join(
        '    <itemref idref="%s"%s/>'
        % (s["idref"], (' properties="%s"' % s["properties"]) if s.get("properties") else "")
        for s in spine_items)
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="pub-id">\n'
        '  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">\n'
        '    <dc:identifier id="pub-id">%s</dc:identifier>\n'
        '    <dc:title>%s</dc:title>\n'
        '    <dc:language>en</dc:language>\n'
        '%s'
        '  </metadata>\n'
        '  <manifest>\n'
        '%s\n'
        '  </manifest>\n'
        '  <spine%s>\n'
        '%s\n'
        '  </spine>\n'
        '</package>\n'
    ) % (uid, title, metadata_meta, manifest, spine_attrs, spine)


def write_epub(path, files, store_first_name="mimetype"):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zf:
        # The mimetype entry must come first and be stored uncompressed.
        zf.writestr(zipfile.ZipInfo("mimetype"), MIMETYPE, compress_type=zipfile.ZIP_STORED)
        for name, data in files:
            if isinstance(data, str):
                data = data.encode("utf-8")
            zf.writestr(name, data)
    print("wrote", path)


# --------------------------------------------------------------------------- #
# Individual book builders
# --------------------------------------------------------------------------- #

VP_W, VP_H = 800, 1200


def common_images(n):
    return [("OEBPS/img%02d.png" % (i + 1),
             make_solid_png(VP_W, VP_H, PAGE_COLORS[i % len(PAGE_COLORS)]))
            for i in range(n)]


def build_epub3_viewport(out_dir):
    n = 4
    images = common_images(n)
    pages = []
    manifest = [{"id": "nav", "href": "nav.xhtml", "type": "application/xhtml+xml", "properties": "nav"}]
    spine = []
    for i in range(n):
        href = "page%02d.xhtml" % (i + 1)
        pages.append(("OEBPS/" + href,
                      fxl_page_xhtml("Page %d" % (i + 1), VP_W, VP_H, "img%02d.png" % (i + 1))))
        manifest.append({"id": "p%d" % (i + 1), "href": href, "type": "application/xhtml+xml"})
        manifest.append({"id": "i%d" % (i + 1), "href": "img%02d.png" % (i + 1), "type": "image/png"})
        spread = "rendition:page-spread-left" if i % 2 == 0 else "rendition:page-spread-right"
        spine.append({"idref": "p%d" % (i + 1), "properties": spread})
    meta = '    <meta property="rendition:layout">pre-paginated</meta>\n' \
           '    <meta property="rendition:spread">both</meta>\n'
    opf = build_opf("FXL EPUB3 Viewport", meta, manifest, "", spine)
    files = [("META-INF/container.xml", CONTAINER_XML),
             ("OEBPS/content.opf", opf),
             ("OEBPS/nav.xhtml", nav_xhtml([("page01.xhtml", "Start")]))]
    files += pages + images
    write_epub(os.path.join(out_dir, "fxl_epub3_viewport.epub"), files)


def build_mixed(out_dir):
    n = 5
    images = common_images(n)
    pages = []
    manifest = [{"id": "nav", "href": "nav.xhtml", "type": "application/xhtml+xml", "properties": "nav"}]
    spine = []
    # Global layout left as the default (reflowable); items 2 and 4 opt in.
    for i in range(n):
        href = "page%02d.xhtml" % (i + 1)
        fixed = i in (1, 3)
        if fixed:
            pages.append(("OEBPS/" + href,
                          fxl_page_xhtml("Page %d" % (i + 1), VP_W, VP_H, "img%02d.png" % (i + 1))))
        else:
            pages.append(("OEBPS/" + href,
                          reflowable_page_xhtml("Page %d" % (i + 1), "Reflowable text page.")))
        manifest.append({"id": "p%d" % (i + 1), "href": href, "type": "application/xhtml+xml"})
        manifest.append({"id": "i%d" % (i + 1), "href": "img%02d.png" % (i + 1), "type": "image/png"})
        props = "rendition:layout-pre-paginated" if fixed else None
        spine.append({"idref": "p%d" % (i + 1), "properties": props})
    opf = build_opf("FXL Mixed", "", manifest, "", spine)
    files = [("META-INF/container.xml", CONTAINER_XML),
             ("OEBPS/content.opf", opf),
             ("OEBPS/nav.xhtml", nav_xhtml([("page01.xhtml", "Start")]))]
    files += pages + images
    write_epub(os.path.join(out_dir, "fxl_mixed.epub"), files)


def build_kf8_legacy(out_dir):
    n = 3
    images = common_images(n)
    pages = []
    manifest = [{"id": "nav", "href": "nav.xhtml", "type": "application/xhtml+xml", "properties": "nav"}]
    spine = []
    for i in range(n):
        href = "page%02d.xhtml" % (i + 1)
        # No per-page viewport meta here: exercises the OPF original-resolution fallback.
        pages.append(("OEBPS/" + href,
                      fxl_page_xhtml("Page %d" % (i + 1), VP_W, VP_H, "img%02d.png" % (i + 1),
                                     with_viewport_meta=False)))
        manifest.append({"id": "p%d" % (i + 1), "href": href, "type": "application/xhtml+xml"})
        manifest.append({"id": "i%d" % (i + 1), "href": "img%02d.png" % (i + 1), "type": "image/png"})
        spine.append({"idref": "p%d" % (i + 1)})
    meta = ('    <meta name="fixed-layout" content="true"/>\n'
            '    <meta name="original-resolution" content="800x1200"/>\n'
            '    <meta name="primary-writing-mode" content="horizontal-rl"/>\n'
            '    <meta name="orientation-lock" content="portrait"/>\n')
    opf = build_opf("FXL KF8 Legacy", meta, manifest, "", spine)
    files = [("META-INF/container.xml", CONTAINER_XML),
             ("OEBPS/content.opf", opf),
             ("OEBPS/nav.xhtml", nav_xhtml([("page01.xhtml", "Start")]))]
    files += pages + images
    write_epub(os.path.join(out_dir, "fxl_kf8_legacy.epub"), files)


def build_ibooks(out_dir):
    n = 3
    images = common_images(n)
    pages = []
    manifest = [{"id": "nav", "href": "nav.xhtml", "type": "application/xhtml+xml", "properties": "nav"}]
    spine = []
    for i in range(n):
        href = "page%02d.xhtml" % (i + 1)
        pages.append(("OEBPS/" + href,
                      fxl_page_xhtml("Page %d" % (i + 1), VP_W, VP_H, "img%02d.png" % (i + 1))))
        manifest.append({"id": "p%d" % (i + 1), "href": href, "type": "application/xhtml+xml"})
        manifest.append({"id": "i%d" % (i + 1), "href": "img%02d.png" % (i + 1), "type": "image/png"})
        spine.append({"idref": "p%d" % (i + 1)})
    # Deliberately NO rendition:layout in the OPF: detection must come from the
    # iBooks display-options file below.
    opf = build_opf("FXL iBooks", "", manifest, "", spine)
    files = [("META-INF/container.xml", CONTAINER_XML),
             ("META-INF/com.apple.ibooks.display-options.xml", IBOOKS_DISPLAY_OPTIONS),
             ("OEBPS/content.opf", opf),
             ("OEBPS/nav.xhtml", nav_xhtml([("page01.xhtml", "Start")]))]
    files += pages + images
    write_epub(os.path.join(out_dir, "fxl_ibooks.epub"), files)


def build_rtl_spread(out_dir):
    n = 4
    images = common_images(n)
    pages = []
    manifest = [{"id": "nav", "href": "nav.xhtml", "type": "application/xhtml+xml", "properties": "nav"}]
    spine = []
    for i in range(n):
        href = "page%02d.xhtml" % (i + 1)
        pages.append(("OEBPS/" + href,
                      fxl_page_xhtml("Page %d" % (i + 1), VP_W, VP_H, "img%02d.png" % (i + 1))))
        manifest.append({"id": "p%d" % (i + 1), "href": href, "type": "application/xhtml+xml"})
        manifest.append({"id": "i%d" % (i + 1), "href": "img%02d.png" % (i + 1), "type": "image/png"})
        # Manga spreads: right page first in RTL reading order.
        spread = "rendition:page-spread-right" if i % 2 == 0 else "rendition:page-spread-left"
        spine.append({"idref": "p%d" % (i + 1), "properties": spread})
    meta = '    <meta property="rendition:layout">pre-paginated</meta>\n'
    opf = build_opf("FXL RTL Manga", meta, manifest,
                    ' page-progression-direction="rtl"', spine)
    files = [("META-INF/container.xml", CONTAINER_XML),
             ("OEBPS/content.opf", opf),
             ("OEBPS/nav.xhtml", nav_xhtml([("page01.xhtml", "Start")]))]
    files += pages + images
    write_epub(os.path.join(out_dir, "fxl_rtl_spread.epub"), files)


def build_cover_spread(out_dir):
    # A lone cover (no page-spread hint, like most real covers) followed by
    # left/right pairs. Exercises spread alignment: the cover must stand alone
    # in a two-page view (not pair with page 2), so every later spread lines up.
    #   page1 cover (no hint)  -> alone
    #   page2 left, page3 right -> spread
    #   page4 left, page5 right -> spread
    n = 5
    images = common_images(n)
    pages = []
    manifest = [{"id": "nav", "href": "nav.xhtml", "type": "application/xhtml+xml", "properties": "nav"}]
    spine = []
    for i in range(n):
        href = "page%02d.xhtml" % (i + 1)
        pages.append(("OEBPS/" + href,
                      fxl_page_xhtml("Page %d" % (i + 1), VP_W, VP_H, "img%02d.png" % (i + 1))))
        manifest.append({"id": "p%d" % (i + 1), "href": href, "type": "application/xhtml+xml"})
        manifest.append({"id": "i%d" % (i + 1), "href": "img%02d.png" % (i + 1), "type": "image/png"})
        item = {"idref": "p%d" % (i + 1)}
        if i >= 1:  # page 1 (the cover) gets no page-spread hint
            item["properties"] = "rendition:page-spread-left" if i % 2 == 1 else "rendition:page-spread-right"
        spine.append(item)
    meta = '    <meta property="rendition:layout">pre-paginated</meta>\n'
    opf = build_opf("FXL Cover Spread", meta, manifest, "", spine)
    files = [("META-INF/container.xml", CONTAINER_XML),
             ("OEBPS/content.opf", opf),
             ("OEBPS/nav.xhtml", nav_xhtml([("page01.xhtml", "Start")]))]
    files += pages + images
    write_epub(os.path.join(out_dir, "fxl_cover_spread.epub"), files)


def build_svg_viewport(out_dir):
    # Fixed-layout pages with NO <meta name="viewport">: the canvas size comes
    # from an <svg viewBox="0 0 W H"> wrapping a single full-page image (a common
    # cover/title-page pattern). The image must fill the page edge to edge.
    n = 2
    images = common_images(n)
    pages = []
    manifest = [{"id": "nav", "href": "nav.xhtml", "type": "application/xhtml+xml", "properties": "nav"}]
    spine = []
    for i in range(n):
        href = "page%02d.xhtml" % (i + 1)
        pages.append(("OEBPS/" + href,
                      svg_page_xhtml("Page %d" % (i + 1), VP_W, VP_H, "img%02d.png" % (i + 1))))
        manifest.append({"id": "p%d" % (i + 1), "href": href, "type": "application/xhtml+xml"})
        manifest.append({"id": "i%d" % (i + 1), "href": "img%02d.png" % (i + 1), "type": "image/png"})
        spine.append({"idref": "p%d" % (i + 1)})
    meta = '    <meta property="rendition:layout">pre-paginated</meta>\n'
    opf = build_opf("FXL SVG Viewport", meta, manifest, "", spine)
    files = [("META-INF/container.xml", CONTAINER_XML),
             ("OEBPS/content.opf", opf),
             ("OEBPS/nav.xhtml", nav_xhtml([("page01.xhtml", "Start")]))]
    files += pages + images
    write_epub(os.path.join(out_dir, "fxl_svg_viewport.epub"), files)


def build_reflowable_control(out_dir):
    n = 3
    pages = []
    manifest = [{"id": "nav", "href": "nav.xhtml", "type": "application/xhtml+xml", "properties": "nav"}]
    spine = []
    for i in range(n):
        href = "page%02d.xhtml" % (i + 1)
        pages.append(("OEBPS/" + href,
                      reflowable_page_xhtml("Chapter %d" % (i + 1),
                                            "Just ordinary reflowable text. " * 20)))
        manifest.append({"id": "p%d" % (i + 1), "href": href, "type": "application/xhtml+xml"})
        spine.append({"idref": "p%d" % (i + 1)})
    opf = build_opf("Reflowable Control", "", manifest, "", spine)
    files = [("META-INF/container.xml", CONTAINER_XML),
             ("OEBPS/content.opf", opf),
             ("OEBPS/nav.xhtml", nav_xhtml([("page01.xhtml", "Start")]))]
    files += pages
    write_epub(os.path.join(out_dir, "reflowable_control.epub"), files)


def build_positioned(out_dir):
    """Fixed-layout book whose pages use CSS absolute positioning (slice 3),
    with coloured boxes at known coordinates for a render/pixel test."""
    n = 2
    pages = []
    manifest = [{"id": "nav", "href": "nav.xhtml", "type": "application/xhtml+xml", "properties": "nav"}]
    spine = []
    for i in range(n):
        href = "page%02d.xhtml" % (i + 1)
        pages.append(("OEBPS/" + href,
                      positioned_page_xhtml("Page %d" % (i + 1), VP_W, VP_H)))
        manifest.append({"id": "p%d" % (i + 1), "href": href, "type": "application/xhtml+xml"})
        spine.append({"idref": "p%d" % (i + 1)})
    meta = '    <meta property="rendition:layout">pre-paginated</meta>\n'
    opf = build_opf("FXL Positioned", meta, manifest, "", spine)
    files = [("META-INF/container.xml", CONTAINER_XML),
             ("OEBPS/content.opf", opf),
             ("OEBPS/nav.xhtml", nav_xhtml([("page01.xhtml", "Start")]))]
    files += pages
    write_epub(os.path.join(out_dir, "fxl_positioned.epub"), files)


def build_transform(out_dir):
    """Fixed-layout book exercising CSS transform:scale() on a fixed-layout "text
    layer" (slice 3b): huge coordinates + huge font-size in a scaled-down
    container, for a render/pixel test (see transform_page_xhtml)."""
    manifest = [{"id": "nav", "href": "nav.xhtml", "type": "application/xhtml+xml", "properties": "nav"}]
    pages = [("OEBPS/page01.xhtml", transform_page_xhtml("Page 1", VP_W, VP_H))]
    manifest.append({"id": "p1", "href": "page01.xhtml", "type": "application/xhtml+xml"})
    spine = [{"idref": "p1"}]
    meta = '    <meta property="rendition:layout">pre-paginated</meta>\n'
    opf = build_opf("FXL Transform", meta, manifest, "", spine)
    files = [("META-INF/container.xml", CONTAINER_XML),
             ("OEBPS/content.opf", opf),
             ("OEBPS/nav.xhtml", nav_xhtml([("page01.xhtml", "Start")]))]
    files += pages
    write_epub(os.path.join(out_dir, "fxl_transform.epub"), files)


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out_dir, exist_ok=True)
    build_epub3_viewport(out_dir)
    build_mixed(out_dir)
    build_kf8_legacy(out_dir)
    build_ibooks(out_dir)
    build_rtl_spread(out_dir)
    build_cover_spread(out_dir)
    build_svg_viewport(out_dir)
    build_reflowable_control(out_dir)
    build_positioned(out_dir)
    build_transform(out_dir)


if __name__ == "__main__":
    main()

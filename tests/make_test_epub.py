#!/usr/bin/env python3
"""Generate font-manager-test.epub — a visual test for font matching and synthesis."""

import io
import os
import zipfile

# ---------------------------------------------------------------------------
# Content
# ---------------------------------------------------------------------------

MIMETYPE = b"application/epub+zip"

CONTAINER_XML = """\
<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:schemas:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf"
              media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>
"""

CONTENT_OPF = """\
<?xml version="1.0" encoding="UTF-8"?>
<package version="2.0" xmlns="http://www.idpf.org/2007/opf"
         unique-identifier="bookid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>Font Manager Test</dc:title>
    <dc:creator>crengine test suite</dc:creator>
    <dc:identifier id="bookid">urn:uuid:font-manager-test-001</dc:identifier>
    <dc:language>en</dc:language>
  </metadata>
  <manifest>
    <item id="ncx"     href="toc.ncx"      media-type="application/x-dtbncx+xml"/>
    <item id="style"   href="style.css"    media-type="text/css"/>
    <item id="ch01"    href="ch01.html"    media-type="application/xhtml+xml"/>
    <item id="ch02"    href="ch02.html"    media-type="application/xhtml+xml"/>
    <item id="ch03"    href="ch03.html"    media-type="application/xhtml+xml"/>
    <item id="ch04"    href="ch04.html"    media-type="application/xhtml+xml"/>
  </manifest>
  <spine toc="ncx">
    <itemref idref="ch01"/>
    <itemref idref="ch02"/>
    <itemref idref="ch03"/>
    <itemref idref="ch04"/>
  </spine>
</package>
"""

TOC_NCX = """\
<?xml version="1.0" encoding="UTF-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head>
    <meta name="dtb:uid" content="urn:uuid:font-manager-test-001"/>
  </head>
  <docTitle><text>Font Manager Test</text></docTitle>
  <navMap>
    <navPoint id="ch01" playOrder="1">
      <navLabel><text>1. Font Weight</text></navLabel>
      <content src="ch01.html"/>
    </navPoint>
    <navPoint id="ch02" playOrder="2">
      <navLabel><text>2. Italic and Synthesis</text></navLabel>
      <content src="ch02.html"/>
    </navPoint>
    <navPoint id="ch03" playOrder="3">
      <navLabel><text>3. Optical Sizing</text></navLabel>
      <content src="ch03.html"/>
    </navPoint>
    <navPoint id="ch04" playOrder="4">
      <navLabel><text>4. Font Stretch</text></navLabel>
      <content src="ch04.html"/>
    </navPoint>
  </navMap>
</ncx>
"""

STYLE_CSS = """\
body { font-family: serif; margin: 1em; }
h1   { font-size: 1.4em; font-weight: bold; margin-bottom: 0.5em; }
h2   { font-size: 1.1em; font-weight: bold; margin: 1em 0 0.3em; }
p    { margin: 0.2em 0; }
.label { font-size: 0.75em; color: #555; font-style: italic; }
.sample { font-size: 1em; margin: 0.1em 0; }

/* Weight samples */
.w100 { font-weight: 100; }
.w200 { font-weight: 200; }
.w300 { font-weight: 300; }
.w400 { font-weight: 400; }
.w500 { font-weight: 500; }
.w550 { font-weight: 550; }
.w600 { font-weight: 600; }
.w650 { font-weight: 650; }
.w700 { font-weight: 700; }
.w800 { font-weight: 800; }
.w900 { font-weight: 900; }

/* Italic */
.roman     { font-style: normal; }
.italic    { font-style: italic; }
.oblique   { font-style: oblique; }

/* Optical sizing */
.opsz-auto { font-optical-sizing: auto; }
.opsz-none { font-optical-sizing: none; }

/* font-synthesis */
.synth-all  { font-synthesis: weight style; }
.synth-none { font-synthesis: none; }

/* Stretch */
.stretch-75  { font-stretch: condensed; }
.stretch-87  { font-stretch: semi-condensed; }
.stretch-100 { font-stretch: normal; }
.stretch-112 { font-stretch: semi-expanded; }
.stretch-125 { font-stretch: expanded; }

.sans  { font-family: sans-serif; }
.mono  { font-family: monospace; }
.serif { font-family: serif; }
"""

# Each chapter is plain XHTML

CH01 = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN"
  "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en">
<head><title>Font Weight</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>
<h1>Chapter 1 — Font Weight</h1>

<h2>Keyword and numeric weights (serif)</h2>
<p class="label">weight: 100</p>
<p class="sample serif w100">The quick brown fox jumps over the lazy dog. (100)</p>
<p class="label">weight: 200</p>
<p class="sample serif w200">The quick brown fox jumps over the lazy dog. (200)</p>
<p class="label">weight: 300</p>
<p class="sample serif w300">The quick brown fox jumps over the lazy dog. (300)</p>
<p class="label">weight: 400 (normal)</p>
<p class="sample serif w400">The quick brown fox jumps over the lazy dog. (400)</p>
<p class="label">weight: 500</p>
<p class="sample serif w500">The quick brown fox jumps over the lazy dog. (500)</p>
<p class="label">weight: 550 (CSS4 arbitrary)</p>
<p class="sample serif w550">The quick brown fox jumps over the lazy dog. (550)</p>
<p class="label">weight: 600</p>
<p class="sample serif w600">The quick brown fox jumps over the lazy dog. (600)</p>
<p class="label">weight: 650 (CSS4 arbitrary)</p>
<p class="sample serif w650">The quick brown fox jumps over the lazy dog. (650)</p>
<p class="label">weight: 700 (bold)</p>
<p class="sample serif w700">The quick brown fox jumps over the lazy dog. (700)</p>
<p class="label">weight: 800</p>
<p class="sample serif w800">The quick brown fox jumps over the lazy dog. (800)</p>
<p class="label">weight: 900</p>
<p class="sample serif w900">The quick brown fox jumps over the lazy dog. (900)</p>

<h2>Same weights, sans-serif</h2>
<p class="sample sans w300">Light sans-serif (300). The quick brown fox.</p>
<p class="sample sans w400">Normal sans-serif (400). The quick brown fox.</p>
<p class="sample sans w700">Bold sans-serif (700). The quick brown fox.</p>
<p class="sample sans w900">Black sans-serif (900). The quick brown fox.</p>

<h2>Expected behaviour</h2>
<p>Weights 100–900 should show a visual progression from thin to heavy.
CSS4 intermediate values (550, 650) should be handled by the nearest
available face for static fonts, or rendered precisely by variable fonts.</p>
</body>
</html>
"""

CH02 = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN"
  "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en">
<head><title>Italic and Synthesis</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>
<h1>Chapter 2 — Italic and Synthesis</h1>

<h2>Roman vs italic</h2>
<p class="label">Normal (roman)</p>
<p class="sample serif roman w400">The quick brown fox jumps over the lazy dog.</p>
<p class="label">Italic</p>
<p class="sample serif italic w400">The quick brown fox jumps over the lazy dog.</p>
<p class="label">Bold roman</p>
<p class="sample serif roman w700">The quick brown fox jumps over the lazy dog.</p>
<p class="label">Bold italic</p>
<p class="sample serif italic w700">The quick brown fox jumps over the lazy dog.</p>

<h2>Italic + arbitrary weight (CSS4)</h2>
<p class="label">weight: 550, italic</p>
<p class="sample serif italic w550">The quick brown fox — medium italic (550).</p>
<p class="label">weight: 650, italic</p>
<p class="sample serif italic w650">The quick brown fox — semi-bold italic (650).</p>

<h2>Sans-serif italic</h2>
<p class="sample sans roman w400">Sans normal roman (400).</p>
<p class="sample sans italic w400">Sans normal italic (400).</p>
<p class="sample sans roman w700">Sans bold roman (700).</p>
<p class="sample sans italic w700">Sans bold italic (700).</p>

<h2>font-synthesis</h2>
<p class="label">font-synthesis: weight style (default — synthesis allowed)</p>
<p class="sample serif italic w700 synth-all">Bold italic with synthesis allowed.</p>
<p class="label">font-synthesis: none — synthesis suppressed; may appear roman/light</p>
<p class="sample serif italic w700 synth-none">Bold italic with synthesis suppressed.</p>

<h2>Expected behaviour</h2>
<p>Italic text should be visually slanted or use a distinct italic face.
With font-synthesis:none and no native face, the text may appear
indistinguishable from roman — that is correct behaviour.</p>
</body>
</html>
"""

CH03 = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN"
  "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en">
<head><title>Optical Sizing</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>
<h1>Chapter 3 — Optical Sizing</h1>

<h2>font-optical-sizing: auto (default)</h2>
<p class="label">8pt — optical sizing auto</p>
<p class="sample serif opsz-auto" style="font-size: 0.6em;">
  Eight-point text. The quick brown fox jumps over the lazy dog.
  Glyphs may appear slightly bolder/wider than a pure scale of larger text.</p>
<p class="label">12pt — optical sizing auto</p>
<p class="sample serif opsz-auto">
  Twelve-point text. The quick brown fox jumps over the lazy dog.</p>
<p class="label">24pt — optical sizing auto</p>
<p class="sample serif opsz-auto" style="font-size: 2em;">
  Twenty-four-point. The quick brown fox.</p>

<h2>font-optical-sizing: none</h2>
<p class="label">8pt — optical sizing none (mechanical scale)</p>
<p class="sample serif opsz-none" style="font-size: 0.6em;">
  Eight-point text. The quick brown fox jumps over the lazy dog.
  Glyphs are a pure scale of the default design.</p>
<p class="label">12pt — optical sizing none</p>
<p class="sample serif opsz-none">
  Twelve-point text. The quick brown fox jumps over the lazy dog.</p>
<p class="label">24pt — optical sizing none</p>
<p class="sample serif opsz-none" style="font-size: 2em;">
  Twenty-four-point. The quick brown fox.</p>

<h2>Expected behaviour</h2>
<p>For variable fonts with an opsz axis, auto and none should produce
visibly different stroke weights at small sizes. For static fonts the
two settings look identical (no opsz axis to adjust).</p>
</body>
</html>
"""

CH04 = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN"
  "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en">
<head><title>Font Stretch</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>
<h1>Chapter 4 — Font Stretch</h1>

<h2>CSS keyword values (serif)</h2>
<p class="label">font-stretch: condensed (75%)</p>
<p class="sample serif stretch-75">
  The quick brown fox jumps over the lazy dog. Condensed.</p>
<p class="label">font-stretch: semi-condensed (87.5%)</p>
<p class="sample serif stretch-87">
  The quick brown fox jumps over the lazy dog. Semi-condensed.</p>
<p class="label">font-stretch: normal (100%)</p>
<p class="sample serif stretch-100">
  The quick brown fox jumps over the lazy dog. Normal.</p>
<p class="label">font-stretch: semi-expanded (112.5%)</p>
<p class="sample serif stretch-112">
  The quick brown fox jumps over the lazy dog. Semi-expanded.</p>
<p class="label">font-stretch: expanded (125%)</p>
<p class="sample serif stretch-125">
  The quick brown fox jumps over the lazy dog. Expanded.</p>

<h2>Same values, sans-serif</h2>
<p class="sample sans stretch-75">Sans condensed (75%). The quick brown fox.</p>
<p class="sample sans stretch-100">Sans normal (100%). The quick brown fox.</p>
<p class="sample sans stretch-125">Sans expanded (125%). The quick brown fox.</p>

<h2>Stretch + weight</h2>
<p class="sample serif stretch-75 w700">Bold condensed. The quick brown fox.</p>
<p class="sample serif stretch-125 w700">Bold expanded. The quick brown fox.</p>

<h2>Expected behaviour</h2>
<p>For variable fonts with a wdth axis, text should visibly compress or expand.
For static fonts without width variants, synthesis applies a horizontal scale
transform (if enabled) or the normal face is used unchanged.</p>

<h2>Regression check: baseline fonts</h2>
<p class="label">These should always appear in their default style</p>
<p class="sample serif w400 roman">Serif normal roman — this line is the baseline.</p>
<p class="sample sans  w400 roman">Sans normal roman — this line is the baseline.</p>
<p class="sample mono  w400 roman">Monospace roman — this line is the baseline.</p>
</body>
</html>
"""

# ---------------------------------------------------------------------------
# Build the EPUB
# ---------------------------------------------------------------------------

def build_epub(path):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        # mimetype must be first and uncompressed
        zf.writestr(zipfile.ZipInfo("mimetype"), MIMETYPE,
                    compress_type=zipfile.ZIP_STORED)
        zf.writestr("META-INF/container.xml", CONTAINER_XML)
        zf.writestr("OEBPS/content.opf",      CONTENT_OPF)
        zf.writestr("OEBPS/toc.ncx",          TOC_NCX)
        zf.writestr("OEBPS/style.css",        STYLE_CSS)
        zf.writestr("OEBPS/ch01.html",        CH01)
        zf.writestr("OEBPS/ch02.html",        CH02)
        zf.writestr("OEBPS/ch03.html",        CH03)
        zf.writestr("OEBPS/ch04.html",        CH04)
    with open(path, "wb") as f:
        f.write(buf.getvalue())
    print(f"Written: {path}  ({os.path.getsize(path)} bytes)")

if __name__ == "__main__":
    out = os.path.join(os.path.dirname(__file__), "font-manager-test.epub")
    build_epub(out)

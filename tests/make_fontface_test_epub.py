#!/usr/bin/env python3
"""Generate fontface-test.epub — a visual test for the @font-face registration
ordering invariant introduced by the @font-face parsing refactor. Separate
from font-manager-test.epub, which covers font *selection*
(weight/italic/stretch/fallback), not @font-face *registration timing*.

Each chapter is a separate EPUB spine item (DocFragment), specifically to
exercise per-DocFragment @font-face registration order:

  ch01 — font declared and used within the same fragment's own <head><style>
  ch02 — font declared in an external CSS file linked by ch02 (first parse)
  ch03 — same external CSS file, linked again by ch03 (stylesheet-cache hit;
         must still render correctly without re-registering)
  ch04 — references a font declared only later, in ch05 (malformed per the
         spine-order assumption this refactor relies on; must fall back
         gracefully, not crash)
  ch05 — declares the font ch04 references, and also uses it locally (proves
         the declaration itself is valid, isolating ch04's fallback as an
         ordering effect rather than a broken font)
  ch06 — regression test for koreader#10040 / koreader#12525: two @font-face
         rules under the SAME family, at two different numeric font-weight
         values (300 and 900), each pointing at its own distinct embedded
         file. This is a @font-face *descriptor-parsing* correctness check
         (did the numeric font-weight get read at registration time), not a
         font-*selection* check among already-correctly-registered weights
         — that's font-manager-test.epub's job. It belongs here rather than
         there because the bug is in @font-face parsing/registration, the
         same layer the rest of this fixture exercises.

Each @font-face rule references a DISTINCT embedded font file, read directly
from koreader/resources/fonts/ (crengine is always built as a thirdparty
component of koreader, never standalone, so this path is always available —
see load_fonts() below) and re-packaged into the EPUB under its own family
name.

This must NOT be collapsed to a single shared font file reused under
different family names: LVFontFace::id() (lvfntman.cpp) computes identity
from (file_path, face_index, documentId) only, ignoring the requested
typeface/family name. Registering the same file a second time under a
different @font-face family, in the same document, is treated by
tryRegisterFace()/hasFaceId() as an exact duplicate and silently dropped
*before* the family-name mapping is ever created — independent of and
unrelated to the registration-ordering behaviour this fixture exists to test.
Confirmed live via "font definition is duplicate" trace log entries when this
fixture mistakenly shared one file across all three families. Each chapter
needing its own distinct file works around that (pre-existing, unrelated)
font manager limitation so this fixture isolates only the ordering behaviour
it's meant to test. See FONTMANAGER_REFACTOR.md, "Future Work" for the
write-up of that bug.

Chapters 4 and 5's test paragraphs must NOT use byte-identical computed
styles either, for an unrelated reason: ldomNode::initNodeFont()
(lvtinydom.cpp) caches resolved fonts in _fontMap keyed by *style index*
(from the document-wide, content-deduplicated _styles cache, lvrefcache.h),
not by node. Two elements anywhere in the document — even in different
DocFragments — with an identical computed css_style_rec_t share one cache
entry, with no invalidation when the font registry changes between when the
first and second were resolved. Chapter 4's Test 4 paragraph and Chapter 5's
Test 5 paragraph originally used the identical inline
`style="font-family: 'OrderingTestFontC', serif;"`, so Chapter 4's
(correct, at that point) fallback-to-serif resolution got cached and silently
reused for Chapter 5's later, otherwise-correctly-registered, paragraph. The
near-imperceptible `letter-spacing: 1px` on Chapter 4's paragraph below exists
solely to keep the two style records distinct -- a sub-pixel value (e.g.
0.01px) was tried first and was NOT sufficient: crengine's layout engine
works in integer screen pixels, and the sub-pixel value rounded away to the
same computed style after all, so the collision (and its symptom -- whichever
chapter is viewed first "wins" and the other inherits its font resolution)
persisted across out-of-spine-order navigation between Chapters 4 and 5. This
is a third, separate, and more general bug than the file-reuse one above —
it isn't specific to this fixture's deliberately malformed Chapter 4 scenario;
it would also bite two *legitimately* ordered DocFragments sharing a CSS class
whose font becomes available between them -- not fixed, just documented here.
"""

import io
import os
import sys
import zipfile

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
    <dc:title>@font-face Test</dc:title>
    <dc:creator>crengine test suite</dc:creator>
    <dc:identifier id="bookid">urn:uuid:ordering-test-001</dc:identifier>
    <dc:language>en</dc:language>
  </metadata>
  <manifest>
    <item id="ncx"     href="toc.ncx"            media-type="application/x-dtbncx+xml"/>
    <item id="shared"  href="css/shared.css"     media-type="text/css"/>
    <item id="fontA"   href="fonts/ordering-test-mono.ttf"   media-type="application/x-font-ttf"/>
    <item id="fontB"   href="fonts/ordering-test-bold.ttf"   media-type="application/x-font-ttf"/>
    <item id="fontC"   href="fonts/ordering-test-italic.ttf" media-type="application/x-font-ttf"/>
    <item id="fontD"   href="fonts/ordering-test-weight-light.ttf" media-type="application/x-font-ttf"/>
    <item id="fontE"   href="fonts/ordering-test-weight-heavy.ttf" media-type="application/x-font-ttf"/>
    <item id="ch01"    href="ch01.html"          media-type="application/xhtml+xml"/>
    <item id="ch02"    href="ch02.html"          media-type="application/xhtml+xml"/>
    <item id="ch03"    href="ch03.html"          media-type="application/xhtml+xml"/>
    <item id="ch04"    href="ch04.html"          media-type="application/xhtml+xml"/>
    <item id="ch05"    href="ch05.html"          media-type="application/xhtml+xml"/>
    <item id="ch06"    href="ch06.html"          media-type="application/xhtml+xml"/>
  </manifest>
  <spine toc="ncx">
    <itemref idref="ch01"/>
    <itemref idref="ch02"/>
    <itemref idref="ch03"/>
    <itemref idref="ch04"/>
    <itemref idref="ch05"/>
    <itemref idref="ch06"/>
  </spine>
</package>
"""

TOC_NCX = """\
<?xml version="1.0" encoding="UTF-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head>
    <meta name="dtb:uid" content="urn:uuid:ordering-test-001"/>
  </head>
  <docTitle><text>@font-face Test</text></docTitle>
  <navMap>
    <navPoint id="ch01" playOrder="1">
      <navLabel><text>1. Declared and used in the same fragment</text></navLabel>
      <content src="ch01.html"/>
    </navPoint>
    <navPoint id="ch02" playOrder="2">
      <navLabel><text>2. Shared external CSS (first parse)</text></navLabel>
      <content src="ch02.html"/>
    </navPoint>
    <navPoint id="ch03" playOrder="3">
      <navLabel><text>3. Shared external CSS (stylesheet-cache hit)</text></navLabel>
      <content src="ch03.html"/>
    </navPoint>
    <navPoint id="ch04" playOrder="4">
      <navLabel><text>4. Font referenced before it is declared (malformed)</text></navLabel>
      <content src="ch04.html"/>
    </navPoint>
    <navPoint id="ch05" playOrder="5">
      <navLabel><text>5. Font declared (and used) here</text></navLabel>
      <content src="ch05.html"/>
    </navPoint>
    <navPoint id="ch06" playOrder="6">
      <navLabel><text>6. Numeric @font-face font-weight (koreader#10040 / #12525)</text></navLabel>
      <content src="ch06.html"/>
    </navPoint>
  </navMap>
</ncx>
"""

SHARED_CSS = """\
/* Linked by ch02 and ch03. @font-face is parsed on first link (ch02);
   ch03's link hits the StyleSheetCache and must not need to re-register. */
@font-face {
  font-family: "OrderingTestFontB";
  src: url("../fonts/ordering-test-bold.ttf");
}
"""

PAGE_TEMPLATE = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN"
  "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en">
<head><title>{title}</title>
{head_extra}
</head>
<body>
<h1>{heading}</h1>
{body}
</body>
</html>
"""

CH01 = PAGE_TEMPLATE.format(
    title="Same-fragment declaration",
    heading="Chapter 1 &#x2014; Declared and used in the same fragment",
    head_extra="""\
<style type="text/css">
@font-face {
  font-family: "OrderingTestFontA";
  src: url("fonts/ordering-test-mono.ttf");
}
</style>""",
    body="""\
<p>This is the simplest case: the @font-face rule is declared in this
chapter's own &lt;head&gt;&lt;style&gt;, and used by this same chapter.</p>
<p style="font-family: 'OrderingTestFontA', serif;">Test 1: this line should
render in the embedded monospace test font (Droid Sans Mono).</p>
<p style="font-family: serif;">Reference (default serif, for contrast): the
quick brown fox jumps over the lazy dog.</p>""",
)

CH02 = PAGE_TEMPLATE.format(
    title="Shared external CSS (first parse)",
    heading="Chapter 2 &#x2014; Shared external CSS (first parse)",
    head_extra='<link rel="stylesheet" type="text/css" href="css/shared.css"/>',
    body="""\
<p>This chapter links an external stylesheet declaring @font-face. This is
the first spine item to link it, so this is where registration actually
happens.</p>
<p style="font-family: 'OrderingTestFontB', serif;">Test 2: this line should
render in the embedded bold sans-serif test font (Noto Sans Bold).</p>
<p style="font-family: serif;">Reference (default serif, for contrast): the
quick brown fox jumps over the lazy dog.</p>""",
)

CH03 = PAGE_TEMPLATE.format(
    title="Shared external CSS (cache hit)",
    heading="Chapter 3 &#x2014; Shared external CSS (stylesheet-cache hit)",
    head_extra='<link rel="stylesheet" type="text/css" href="css/shared.css"/>',
    body="""\
<p>This chapter links the same external stylesheet as Chapter 2. The
stylesheet cache should serve a cached rule list here rather than
re-parsing the file, so @font-face registration does not run a second time
&#x2014; the font must still apply correctly.</p>
<p style="font-family: 'OrderingTestFontB', serif;">Test 3: this line should
render in the embedded bold sans-serif test font, identical to Chapter 2's
Test 2 line.</p>
<p style="font-family: serif;">Reference (default serif, for contrast): the
quick brown fox jumps over the lazy dog.</p>""",
)

CH04 = PAGE_TEMPLATE.format(
    title="Reference before declaration",
    heading="Chapter 4 &#x2014; Font referenced before it is declared",
    head_extra="<!-- no @font-face here: OrderingTestFontC is declared only in ch05.html -->",
    body="""\
<p>This paragraph references "OrderingTestFontC", which this EPUB declares
only later, in Chapter 5. A font referenced in spine item N but declared
only in spine item M &gt; N is malformed per the per-DocFragment
registration ordering this engine relies on. This is not expected to
work.</p>
<p>This chapter and Chapter 5 serve two purposes: (1) confirm
this malformed-ordering case fails gracefully on a fresh parse rather than
crashing, and (2) double as the manual reproduction case for an open,
unrelated bug in font-resolution caching (stale font-resolution cache found
via runtime verification) -- view both chapters in spine order, without
skipping around, for either purpose to be meaningful. See
tests/FONTFACE_TEST_PLAN.md, section 3, for the full procedure and expected
results.</p>
<!-- letter-spacing here is a near-imperceptible visual nudge, not a true
     no-op; it only keeps this paragraph's computed style distinct from
     Chapter 5's Test 5 paragraph, which would otherwise share a style index
     and silently reuse this resolution via _fontMap (lvtinydom.cpp) -- see
     this script's docstring. A whole 1px (not a sub-pixel value) is used
     deliberately: crengine's layout engine works in integer screen pixels,
     so a sub-pixel value risks rounding away to the same computed style
     after all, defeating the point. -->
<p style="font-family: 'OrderingTestFontC', serif; letter-spacing: 1px;">
Test 4: this line is expected to fall back to the default serif font
&#x2014; no crash, no missing glyphs, no italic rendering.</p>
<p style="font-family: serif;">Reference (default serif): the quick brown
fox jumps over the lazy dog. Test 4's line above should look identical to
this one.</p>""",
)

CH05 = PAGE_TEMPLATE.format(
    title="Declared (and used) here",
    heading="Chapter 5 &#x2014; Font declared (and used) here",
    head_extra="""\
<style type="text/css">
@font-face {
  font-family: "OrderingTestFontC";
  src: url("fonts/ordering-test-italic.ttf");
}
</style>""",
    body="""\
<p>This chapter declares "OrderingTestFontC" &#x2014; the same family
Chapter 4 referenced too early &#x2014; and uses it here, in the fragment
that declares it. See the note at the top of Chapter 4 for why this pair of
chapters exists and how to read the result.</p>
<p style="font-family: 'OrderingTestFontC', serif;">Test 5: this line should
render in the embedded italic sans-serif test font (Noto Sans Italic),
confirming the @font-face declaration itself is valid. Chapter 4's fallback
was specifically caused by ordering, not by a broken font file.</p>
<p style="font-family: serif;">Reference (default serif, for contrast): the
quick brown fox jumps over the lazy dog.</p>""",
)

CH06 = PAGE_TEMPLATE.format(
    title="Numeric @font-face font-weight",
    heading="Chapter 6 &#x2014; Numeric @font-face font-weight (koreader#10040 / #12525)",
    head_extra="""\
<style type="text/css">
@font-face {
  font-family: "OrderingTestWeightReg";
  font-weight: 300;
  src: url("fonts/ordering-test-weight-light.ttf");
}
@font-face {
  font-family: "OrderingTestWeightReg";
  font-weight: 900;
  src: url("fonts/ordering-test-weight-heavy.ttf");
}
</style>""",
    body="""\
<p>Two @font-face rules declare the SAME family, "OrderingTestWeightReg", at
two different numeric weights (300 and 900), each pointing at its own
distinct embedded file (FreeSans for 300, Noto Serif Bold Italic for 900).
This is a regression test for koreader#10040 / koreader#12525: the old
@font-face parser only recognised the keyword "bold", silently registering
any numeric font-weight (including 300 and 900 here) as 400. With that bug,
both rules above would collapse to indistinguishable same-weight faces in
one family, and the two lines below would be expected to render in the
<em>same</em> typeface as each other rather than each in its own distinct,
correctly-registered face.</p>
<p style="font-family: 'OrderingTestWeightReg', serif; font-weight: 300;">
Test 6a: this line should render in the embedded plain sans-serif test font
(FreeSans).</p>
<p style="font-family: 'OrderingTestWeightReg', serif; font-weight: 900;">
Test 6b: this line should render in the embedded bold italic serif test font
(Noto Serif Bold Italic) &#x2014; a completely different typeface from Test
6a above, not just a heavier weight of the same one.</p>
<p style="font-family: serif;">Reference (default serif, for contrast): the
quick brown fox jumps over the lazy dog.</p>""",
)

# ---------------------------------------------------------------------------
# Build the EPUB
# ---------------------------------------------------------------------------

def load_fonts(koreader_root):
    """Read the test fonts from koreader/resources/fonts. crengine is
    always built as a thirdparty component of koreader (never standalone),
    so these paths are expected to exist relative to this script regardless
    of which checkout/build is running it."""
    sources = {
        "ordering-test-mono.ttf":         "resources/fonts/droid/DroidSansMono.ttf",
        "ordering-test-bold.ttf":         "resources/fonts/noto/NotoSans-Bold.ttf",
        "ordering-test-italic.ttf":       "resources/fonts/noto/NotoSans-Italic.ttf",
        # ch06 (koreader#10040 / #12525): two more DISTINCT files, not reused
        # from above, for the same reason documented at the top of this file
        # (same file under two families in one document is treated as a
        # duplicate registration, independent of the weight-parsing bug ch06
        # actually tests).
        "ordering-test-weight-light.ttf": "resources/fonts/freefont/FreeSans.ttf",
        "ordering-test-weight-heavy.ttf": "resources/fonts/noto/NotoSerif-BoldItalic.ttf",
    }
    font_files = {}
    missing = []
    for dest_name, rel_path in sources.items():
        abs_path = os.path.join(koreader_root, rel_path)
        if not os.path.isfile(abs_path):
            missing.append(abs_path)
            continue
        with open(abs_path, "rb") as f:
            font_files[dest_name] = f.read()
    if missing:
        sys.exit(
            "make_fontface_test_epub.py: could not find required font file(s):\n"
            + "\n".join(f"  {p}" for p in missing)
            + f"\nExpected koreader checkout root: {koreader_root}\n"
            "This script assumes crengine is checked out as a thirdparty "
            "component of koreader (base/thirdparty/kpvcrlib/crengine) and "
            "reads its test fonts from koreader's resources/fonts/ directly, "
            "rather than keeping separate copies in this repo."
        )
    return font_files

def build_epub(path, font_files):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        # mimetype must be first and uncompressed
        zf.writestr(zipfile.ZipInfo("mimetype"), MIMETYPE,
                    compress_type=zipfile.ZIP_STORED)
        zf.writestr("META-INF/container.xml", CONTAINER_XML)
        zf.writestr("OEBPS/content.opf",       CONTENT_OPF)
        zf.writestr("OEBPS/toc.ncx",           TOC_NCX)
        zf.writestr("OEBPS/css/shared.css",    SHARED_CSS)
        zf.writestr("OEBPS/ch01.html",         CH01)
        zf.writestr("OEBPS/ch02.html",         CH02)
        zf.writestr("OEBPS/ch03.html",         CH03)
        zf.writestr("OEBPS/ch04.html",         CH04)
        zf.writestr("OEBPS/ch05.html",         CH05)
        zf.writestr("OEBPS/ch06.html",         CH06)
        for name, data in font_files.items():
            zf.writestr(zipfile.ZipInfo(f"OEBPS/fonts/{name}"),
                        data, compress_type=zipfile.ZIP_DEFLATED)
    with open(path, "wb") as f:
        f.write(buf.getvalue())
    print(f"Written: {path}  ({os.path.getsize(path)} bytes)")

if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    # tests/ -> crengine -> kpvcrlib -> thirdparty -> base -> koreader root
    koreader_root = os.path.normpath(os.path.join(here, "..", "..", "..", "..", ".."))
    out = os.path.join(here, "fontface-test.epub")
    build_epub(out, load_fonts(koreader_root))

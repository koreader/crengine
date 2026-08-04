#!/usr/bin/env python3
"""Generate fontface-import-precedence-test.epub — a visual test for whether
an `@font-face` rule brought in via `@import` is overridden by a same-family,
same-weight, same-style rule declared afterward in the *importing* file.

This is a follow-up to fontface-test.epub's Chapter 8 (see
FONTFACE_TEST_PLAN.md section 7): Chapter 8 established that
LVFontSelector::pickBestWeight() (lvfntman.cpp) resolves an exact tie among
same-family faces by returning the FIRST candidate found while walking the
family's face list in *registration* order, not the most recently declared
one -- the opposite of ordinary CSS cascade semantics for equal specificity.
That was confirmed for two rules in the SAME <style> block. This fixture
asks the same question for two rules that reach the font manager via
DIFFERENT routes -- one from an `@import`ed stylesheet, one declared directly
afterward in the importing file -- which is exactly the scenario raised in
review: "if a css `@import`s overrides.css which redefines a @font-face, I
would expect the last to win." Ordinary CSS cascade would agree. Whether
crengine's font manager actually works that way is what this fixture checks.

Why a SEPARATE epub rather than one more chapter in fontface-test.epub:
a genuine tie test needs two rules under one family, at the same (default)
weight/style, each pointing at its own DISTINCT embedded font file -- per the
note at the top of make_fontface_test_epub.py, registering the same font
FILE twice in one document, even under a different family name, is silently
dropped by RegisterDocumentFont()'s duplicate-face fast path
(tryRegisterFace()/hasFaceId() in lvfntman.cpp, keyed on file_path only,
before the family-name mapping is ever created) -- a real, pre-existing,
unrelated font-manager limitation. fontface-test.epub already spends all
eleven of KOReader's visually-distinct, Latin-legible shipped fonts (Droid
Sans Mono, FreeSans, FreeSerif, the four Noto Sans weights/styles, the four
Noto Serif weights/styles) across its existing chapters, so there is no
twelfth distinct Latin-legible file left to add another tie case into that
SAME document. (Checked directly: of the remaining shipped fonts, the
Arabic/Bengali/Devanagari "UI" faces and the Nerd Fonts symbols face all
lack Basic Latin glyph coverage per their cmap tables -- unusable for a
by-eye check of English test text. Only Noto Sans CJK SC covers Latin, and
that's one file, not two.) Putting this case in its own document sidesteps
the limit entirely: the duplicate-face fast path keys on (file_path,
documentId), so a fresh document can reuse fontface-test.epub Chapter 8's
own already-proven-visually-distinct pair (Noto Sans Regular / Noto Serif
Bold) with no risk of collision.

Expected result, based on reading LVImportStylesheetParser::Parse()
(lvtinydom.cpp): while a stylesheet's leading `@import` line(s) are present,
Parse() recursively parses each imported file into the SAME destination
LVStyleSheet (registering that file's `@font-face` rules as a side effect)
*before* calling dest.parse() on the importing file's own remaining content
(the trailing rules after its `@import` line, which is where this fixture's
"override" rule lives). So the imported/base rule always registers strictly
before the importing file's own same-family/weight/style rule -- there is no
cascade-style "later declaration wins" step for `@font-face` at all, only
registration order, same as Chapter 8. Test 1 below is therefore expected to
render in the BASE (imported) rule's font, not the override's -- confirming
`@import` does not get special override treatment, contrary to the ordinary
CSS cascade intuition that prompted this fixture.
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
    <dc:title>@font-face Import Precedence Test</dc:title>
    <dc:creator>crengine test suite</dc:creator>
    <dc:identifier id="bookid">urn:uuid:import-precedence-test-001</dc:identifier>
    <dc:language>en</dc:language>
  </metadata>
  <manifest>
    <item id="ncx"      href="toc.ncx"            media-type="application/x-dtbncx+xml"/>
    <item id="base"     href="css/base.css"       media-type="text/css"/>
    <item id="wrapper"  href="css/wrapper.css"    media-type="text/css"/>
    <item id="fontBase" href="fonts/import-test-base.ttf"     media-type="application/x-font-ttf"/>
    <item id="fontOver" href="fonts/import-test-override.ttf" media-type="application/x-font-ttf"/>
    <item id="ch01"     href="ch01.html"          media-type="application/xhtml+xml"/>
  </manifest>
  <spine toc="ncx">
    <itemref idref="ch01"/>
  </spine>
</package>
"""

TOC_NCX = """\
<?xml version="1.0" encoding="UTF-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head>
    <meta name="dtb:uid" content="urn:uuid:import-precedence-test-001"/>
  </head>
  <docTitle><text>@font-face Import Precedence Test</text></docTitle>
  <navMap>
    <navPoint id="ch01" playOrder="1">
      <navLabel><text>1. Import vs. local override tie</text></navLabel>
      <content src="ch01.html"/>
    </navPoint>
  </navMap>
</ncx>
"""

BASE_CSS = """\
/* Imported by wrapper.css, at the top of the file (the only place @import is
   valid). Declares the "base" rule: family OrderingTestImportPrecedence,
   default (tied) weight/style, pointing at Noto Sans Regular. */
@font-face {
  font-family: "OrderingTestImportPrecedence";
  src: url("../fonts/import-test-base.ttf");
}
"""

WRAPPER_CSS = """\
/* Linked by ch01. Imports base.css, then declares its OWN rule for the SAME
   family, at the SAME (default) weight/style -- an exact tie with base.css's
   rule, differing only in which file wins registers first. Per ordinary CSS
   cascade source-order semantics for equal specificity, a rule declared here
   -- after the @import -- would be expected to override the imported one.
   See this script's module docstring for why crengine's font manager is not
   actually expected to behave that way. */
@import url("base.css");
@font-face {
  font-family: "OrderingTestImportPrecedence";
  src: url("../fonts/import-test-override.ttf");
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
    title="Import vs. local override tie",
    heading="Chapter 1 &#x2014; Import vs. local override tie",
    head_extra='<link rel="stylesheet" type="text/css" href="css/wrapper.css"/>',
    body="""\
<p>This chapter links css/wrapper.css, which <code>@import</code>s
css/base.css and then declares its OWN <code>@font-face</code> rule under the
SAME family, "OrderingTestImportPrecedence", at the SAME (default)
weight/style as base.css's rule &#x2014; an exact tie, exactly like
fontface-test.epub's Chapter 8, except the two rules arrive via different
routes (one through an <code>@import</code>, one declared directly in the
importing file) instead of both sitting in the same &lt;style&gt; block.</p>
<p>Ordinary CSS cascade source-order rules for equal specificity would expect
the rule declared AFTER the <code>@import</code> line &#x2014; wrapper.css's
own rule, pointing at Noto Serif Bold &#x2014; to win over base.css's
imported rule, pointing at Noto Sans Regular, since it comes later in the
effective source order. See this fixture's generating script
(make_fontface_import_precedence_test_epub.py) for why that is NOT expected
to be what happens here: <code>LVImportStylesheetParser::Parse()</code>
(lvtinydom.cpp) fully parses and registers an imported file's
<code>@font-face</code> rules before parsing the importing file's own
trailing content, so the imported rule always registers first &#x2014; and
<code>LVFontSelector::pickBestWeight()</code>'s exact-tie resolution
(lvfntman.cpp) picks the first-registered candidate, same as Chapter 8.</p>
<p style="font-family: 'OrderingTestImportPrecedence', serif;">Test 1: this
line should render in the embedded plain sans-serif test font (Noto Sans
Regular) &#x2014; base.css's (imported, first-registered) rule &#x2014; NOT
wrapper.css's own (Noto Serif Bold) rule declared after the import. If it
instead renders in Noto Serif Bold, `@import`ed `@font-face` rules ARE being
overridden by a later same-tie local declaration &#x2014; update this
fixture's expectation (and FONTFACE_TEST_PLAN.md) to match, since nothing in
the CSS Fonts spec mandates first-wins here; this fixture just documents
current behaviour, the same way fontface-test.epub Chapter 8 does for the
same-file case.</p>
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
    of which checkout/build is running it.

    Reuses fontface-test.epub Chapter 8's exact pair (Noto Sans Regular /
    Noto Serif Bold) -- safe here specifically because this is a SEPARATE
    document/documentId, so RegisterDocumentFont()'s same-file duplicate
    fast path (keyed on file_path+documentId) never sees these files
    registered anywhere else in *this* document. See the module docstring
    for why that reuse isn't available within fontface-test.epub itself."""
    sources = {
        "import-test-base.ttf":     "resources/fonts/noto/NotoSans-Regular.ttf",
        "import-test-override.ttf": "resources/fonts/noto/NotoSerif-Bold.ttf",
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
            "make_fontface_import_precedence_test_epub.py: could not find "
            "required font file(s):\n"
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
        zf.writestr("OEBPS/css/base.css",      BASE_CSS)
        zf.writestr("OEBPS/css/wrapper.css",   WRAPPER_CSS)
        zf.writestr("OEBPS/ch01.html",         CH01)
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
    out = os.path.join(here, "fontface-import-precedence-test.epub")
    build_epub(out, load_fonts(koreader_root))

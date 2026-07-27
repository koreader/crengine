#!/usr/bin/env python3
"""Minimal EPUB reproducing three font-variant/small-caps bugs.

crengine maps all font-variant-* longhands into a single shared
style->font_features bitmap. This exercises three distinct bugs in how that
shared bitmap gets updated.

Cases A/B: a font-variant shorthand ("small-caps") followed by another
font-variant-* longhand reset to "normal", within the same rule on the same
node. Before the fix, any longhand set to "normal"/"none" wiped out the
whole bitmap rather than just its own bits, so the later
"font-variant-alternates: normal" erased the "small-caps" bit the shorthand
had just set. Case A omits !important (triggers the bug). Case B adds
!important (the manual workaround). On a fixed crengine build both should
render identically in small caps.

Case C: a lower-specificity rule sets small-caps on a node, and a
higher-specificity rule on that same node then says "font-variant:
inherit". Since nothing above sets font-variant, the node should inherit
plain "normal" (no small caps). Before the fix, "inherit" was applied by
OR'ing a zero value into the bitmap instead of replacing it, so the
small-caps bit set by the lower-specificity rule survived the explicit
inherit.

Case D: a lower-specificity rule sets small-caps (font-variant-caps), and a
higher-specificity rule on the same node sets a *different* longhand
(font-variant-numeric) to "initial". Since "initial" only concerns the
numeric sub-feature, the small-caps bit should be unaffected. Before the
fix, "initial" on any single longhand reset the whole shared bitmap
(hardcoded to clear every sub-feature), so it also wiped out the unrelated
small-caps bit.
"""

import io
import os
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
    <dc:title>font-variant small-caps !important repro</dc:title>
    <dc:creator>crengine test suite</dc:creator>
    <dc:identifier id="bookid">urn:uuid:font-variant-important-bug-001</dc:identifier>
    <dc:language>en</dc:language>
  </metadata>
  <manifest>
    <item id="ncx"   href="toc.ncx"   media-type="application/x-dtbncx+xml"/>
    <item id="style" href="style.css" media-type="text/css"/>
    <item id="ch01"  href="ch01.html" media-type="application/xhtml+xml"/>
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
    <meta name="dtb:uid" content="urn:uuid:font-variant-important-bug-001"/>
  </head>
  <docTitle><text>font-variant small-caps !important repro</text></docTitle>
  <navMap>
    <navPoint id="ch01" playOrder="1">
      <navLabel><text>1. Repro</text></navLabel>
      <content src="ch01.html"/>
    </navPoint>
  </navMap>
</ncx>
"""

STYLE_CSS = """\
body { font-family: serif; margin: 1em; }
p.label { font-style: italic; color: #555; margin: 0.3em 0 0.6em; }

/* Case A: no !important. Bug: font-variant-alternates: normal, coming
   after the shorthand, wipes out the small-caps bit it just set. */
p.case-a span.first-phrase {
    font-variant: small-caps;
    font-variant-alternates: normal;
}

/* Case B: identical, but with !important on font-variant (the manual
   workaround). */
p.case-b span.first-phrase {
    font-variant: small-caps !important;
    font-variant-alternates: normal;
}

/* Case C: a lower-specificity rule sets small-caps, then a higher-specificity
   rule on the same node says "font-variant: inherit". Nothing above sets
   font-variant, so the node should inherit plain "normal" (no small caps). */
span.inherit-phrase {
    font-variant: small-caps;
}
p.case-c span.inherit-phrase {
    font-variant: inherit;
}

/* Case D: a lower-specificity rule sets small-caps, then a higher-specificity
   rule on the same node sets a different longhand (font-variant-numeric) to
   "initial". Small-caps should be unaffected -- "initial" here only concerns
   the numeric sub-feature. */
span.initial-phrase {
    font-variant-caps: small-caps;
}
p.case-d span.initial-phrase {
    font-variant-numeric: initial;
}
"""

CH01 = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN"
  "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en">
<head><title>Repro</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>
<h1>font-variant / small-caps !important repro</h1>

<p class="label">Case A &#x2014; no !important (bug: opening phrase should be
small-caps but, before the fix, renders as plain text because the
"font-variant-alternates: normal" declaration that follows the shorthand
wipes out the small-caps bit).</p>
<p class="case-a"><span class="first-phrase">The first
phrase of the chapter</span> continues here as normal running text, well
past the point where the small-caps effect on the opening phrase should
have stopped, so the boundary between styled and unstyled text is easy to
see.</p>

<p class="label">Case B &#x2014; with !important (the manual workaround).
Should always render in small caps, both before and after the fix.</p>
<p class="case-b"><span class="first-phrase">The first
phrase of the chapter</span> continues here as normal running text, well
past the point where the small-caps effect on the opening phrase should
have stopped, so the boundary between styled and unstyled text is easy to
see.</p>

<p class="label">Expected after the fix: Case A and Case B look identical
&#x2014; both show "THE FIRST PHRASE OF THE CHAPTER" in small caps, with the
rest of the paragraph in normal case.</p>

<p class="label">Case C &#x2014; a lower-specificity rule sets small-caps on
the phrase, then a higher-specificity rule on the same element says
"font-variant: inherit". Nothing above sets font-variant, so the phrase
should inherit plain text (no small caps). Bug: before the fix, the
small-caps bit set by the lower-specificity rule survived the explicit
inherit.</p>
<p class="case-c"><span class="inherit-phrase">The first
phrase of the chapter</span> continues here as normal running text.</p>

<p class="label">Expected after the fix: Case C's opening phrase is NOT
small-caps (plain text), because the explicit "inherit" takes over
completely.</p>

<p class="label">Case D &#x2014; a lower-specificity rule sets small-caps on
the phrase, then a higher-specificity rule on the same element sets a
different longhand, "font-variant-numeric: initial". This should only reset
the numeric sub-feature, leaving small-caps untouched. Bug: before the fix,
"initial" on any single longhand reset the entire shared bitmap, wiping out
the unrelated small-caps bit.</p>
<p class="case-d"><span class="initial-phrase">The first
phrase of the chapter</span> continues here as normal running text.</p>

<p class="label">Expected after the fix: Case D's opening phrase IS
small-caps, because "font-variant-numeric: initial" only resets the numeric
sub-feature, not the unrelated small-caps bit.</p>
</body>
</html>
"""

def build_epub(path):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr(zipfile.ZipInfo("mimetype"), MIMETYPE,
                    compress_type=zipfile.ZIP_STORED)
        zf.writestr("META-INF/container.xml", CONTAINER_XML)
        zf.writestr("OEBPS/content.opf", CONTENT_OPF)
        zf.writestr("OEBPS/toc.ncx", TOC_NCX)
        zf.writestr("OEBPS/style.css", STYLE_CSS)
        zf.writestr("OEBPS/ch01.html", CH01)
    with open(path, "wb") as f:
        f.write(buf.getvalue())
    print(f"Written: {path} ({os.path.getsize(path)} bytes)")

if __name__ == "__main__":
    out = os.path.join(os.path.dirname(__file__), "font-variant-bug.epub")
    build_epub(out)

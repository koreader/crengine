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
  ch04 — references a font declared only in ch05's fragment, never its own
         (out of scope for ch04's DocFragment; must fall back gracefully,
         not crash)
  ch05 — declares the font ch04 references, and also uses it locally (proves
         the declaration itself is valid, isolating ch04's fallback as a
         fragment-scoping effect rather than a broken font)
  ch06 — regression test for koreader#10040 / koreader#12525: two @font-face
         rules under the SAME family, at two different numeric font-weight
         values (300 and 900), each pointing at its own distinct embedded
         file. This is a @font-face *descriptor-parsing* correctness check
         (did the numeric font-weight get read at registration time), not a
         font-*selection* check among already-correctly-registered weights
         — that's font-manager-test.epub's job. It belongs here rather than
         there because the bug is in @font-face parsing/registration, the
         same layer the rest of this fixture exercises.
  ch07 — regression test for koreader#15557: an unquoted, multi-word
         font-family name in the @font-face rule's font-family descriptor,
         referenced by the same unquoted name elsewhere. The old parser
         (EmbeddedFontStyleParser in epubfmt.cpp) tokenized descriptor values
         on whitespace and took only the first token, so an unquoted name
         like "Ordering Test Space Name" was truncated to "Ordering" at
         registration time, silently breaking the match against any rule
         referencing the font by its full name. A second rule under a quoted
         family name, pointing at its own distinct file, is a sanity control:
         quoting was always handled correctly even by the old parser, so
         Test 7b is expected to pass independently of whether Test 7a does.
  ch08 — two @font-face rules under the SAME family, in the SAME fragment,
         at the SAME (default) weight/style: an exact tie, pointing at two
         distinct embedded files. LVFontSelector::pickBestWeight() resolves
         an exact-match tie by returning the first candidate found while
         walking the family's face list in registration order, so this is
         expected to render in the FIRST-declared file's face, not the
         second/latest one -- the opposite of ordinary CSS cascade semantics,
         where a later declaration would win. Distinct from ch06, which uses
         two different explicit font-weight values specifically so weight
         disambiguates the two faces without ever exercising this tie.
  ch09 — the exact same @font-face rule (family, weight, style, url) declared
         TWICE in one <style> block. RegisterDocumentFont()'s fast path
         matches by file_path+documentId and treats an already-present
         DocFragment index as a no-op, so this is expected to be harmless:
         renders normally, no crash, no duplicate-glyph artefacts.
  ch10 — an external stylesheet that itself contains the same literal
         `@import url(...)` of one target CSS file TWICE. LVImportStylesheetParser
         only guards against *circular* imports (via _inProgress, cleared as
         soon as each import call returns), not repeat-within-one-pass
         imports, so the second @import re-enters LVStyleSheet::merge() and
         appends the target's selectors into the destination stylesheet a
         second time. That's real (if harmless) internal duplication -- not
         visually observable, since merging identical declarations twice
         doesn't change the computed style -- so this chapter can only assert
         the font still renders correctly; the duplication itself has to be
         confirmed by reading the code, not by eye. The embedded file is
         FreeSerif, which has a visibly smaller x-height/cap-height than most
         reading fonts at the same nominal size -- easy to mistake for a
         registration bug on first look -- so the chapter also declares a
         second family reaching the SAME FreeSerif typeface via
         `src: local("FreeSerif")` (a completely different, unaffected
         registration path: RegisterDocumentFontAlias, not
         RegisterDocumentFont) purely as a same-page reference line, so the
         "smaller than surrounding text" look has something to be checked
         against rather than relying on memory of what FreeSerif looks like
         elsewhere.
  ch11, ch12 — two DIFFERENT fragments, each declaring an @font-face rule
         under the SAME family name ("OrderingTestAliasScope") but with a
         different `src: local(...)` target (FreeSerif vs FreeSans, both
         shipped with KOReader). RegisterDocumentFontAlias()/resolveAlias()
         scope the alias mapping itself to the declaring DocFragment (see
         lvfntman.cpp), independently of ch01-ch07/ch08's per-file face
         scoping, since local() never registers a distinct embedded face --
         it only maps a family name to an already-registered system font,
         and that mapping must not leak between fragments that reuse the
         same alias name for different targets.

Each @font-face rule (other than ch11/ch12's local() aliases, which point at
already-installed system fonts rather than embedding a file) references a
DISTINCT embedded font file, read directly
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

For the same reason, ch08's two rules (the tie test) and ch09's rule
(deliberately declared twice) each point at a file not used anywhere else in
this fixture, even though ch09 repeats one rule verbatim rather than reusing
a file across two DIFFERENT families -- keeping every rule's file globally
unique in this fixture, regardless of which case it's testing, avoids ever
having to reason about whether a given reuse is the safe kind (identical
rule, same family) or the unsafe kind (same file, different family) while
reading this file. ch10 (duplicate @import) likewise gets its own file. ch11
and ch12 need no new files at all: `src: local(...)` never embeds a file, it
only maps a family name to an already-registered system font (FreeSerif /
FreeSans, both shipped with KOReader), so it isn't subject to this
per-document-per-file limitation either way.

Chapters 4 and 5's test paragraphs are DELIBERATELY byte-identical in
computed style -- `style="font-family: 'OrderingTestFontC', serif;"` on both
-- as a regression test for a third, separate bug that used to live here:
ldomNode::initNodeFont() (lvtinydom.cpp) caches resolved fonts in _fontMap
keyed by *style index* (from the document-wide, content-deduplicated _styles
cache, lvrefcache.h), not by node. Two elements anywhere in the document —
even in different DocFragments — with an identical computed css_style_rec_t
share one cache entry. Before commit f2c6db30 ("CSS: avoid walk up node tree
to get fragmentIdx") threaded fragmentIdx through getFont()/initNodeFont()
and had ldomNode::initNodeStyle() clear _fontMap on every el_DocFragment
boundary, that entry was never invalidated when the font registry changed
between when the first and second were resolved:
Chapter 4's Test 4 paragraph and Chapter 5's Test 5 paragraph, sharing this
identical style, meant Chapter 4's (correct, at that point) fallback-to-serif
resolution got cached and silently reused for Chapter 5's later,
otherwise-correctly-registered, paragraph, regardless of spine order. The
_fontMap clear on each DocFragment boundary fixes this: Chapter 5's styling
pass starts with an empty _fontMap, so its Test 5 paragraph always
re-resolves against Chapter 5's own @font-face scope instead of reusing
Chapter 4's cached miss. If this ever regresses, Test 5 below will render in
the default serif font (Chapter 4's stale resolution) instead of the
embedded italic test font.
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
    <item id="dupimporttarget"  href="css/dup-import-target.css"  media-type="text/css"/>
    <item id="dupimportwrapper" href="css/dup-import-wrapper.css" media-type="text/css"/>
    <item id="fontA"   href="fonts/ordering-test-mono.ttf"   media-type="application/x-font-ttf"/>
    <item id="fontB"   href="fonts/ordering-test-bold.ttf"   media-type="application/x-font-ttf"/>
    <item id="fontC"   href="fonts/ordering-test-italic.ttf" media-type="application/x-font-ttf"/>
    <item id="fontD"   href="fonts/ordering-test-weight-light.ttf" media-type="application/x-font-ttf"/>
    <item id="fontE"   href="fonts/ordering-test-weight-heavy.ttf" media-type="application/x-font-ttf"/>
    <item id="fontF"   href="fonts/ordering-test-space.ttf" media-type="application/x-font-ttf"/>
    <item id="fontG"   href="fonts/ordering-test-space-quoted.ttf" media-type="application/x-font-ttf"/>
    <item id="fontH"   href="fonts/ordering-test-tie-first.ttf" media-type="application/x-font-ttf"/>
    <item id="fontI"   href="fonts/ordering-test-tie-second.ttf" media-type="application/x-font-ttf"/>
    <item id="fontJ"   href="fonts/ordering-test-exact-dup.ttf" media-type="application/x-font-ttf"/>
    <item id="fontK"   href="fonts/ordering-test-dup-import.ttf" media-type="application/x-font-ttf"/>
    <item id="ch01"    href="ch01.html"          media-type="application/xhtml+xml"/>
    <item id="ch02"    href="ch02.html"          media-type="application/xhtml+xml"/>
    <item id="ch03"    href="ch03.html"          media-type="application/xhtml+xml"/>
    <item id="ch04"    href="ch04.html"          media-type="application/xhtml+xml"/>
    <item id="ch05"    href="ch05.html"          media-type="application/xhtml+xml"/>
    <item id="ch06"    href="ch06.html"          media-type="application/xhtml+xml"/>
    <item id="ch07"    href="ch07.html"          media-type="application/xhtml+xml"/>
    <item id="ch08"    href="ch08.html"          media-type="application/xhtml+xml"/>
    <item id="ch09"    href="ch09.html"          media-type="application/xhtml+xml"/>
    <item id="ch10"    href="ch10.html"          media-type="application/xhtml+xml"/>
    <item id="ch11"    href="ch11.html"          media-type="application/xhtml+xml"/>
    <item id="ch12"    href="ch12.html"          media-type="application/xhtml+xml"/>
  </manifest>
  <spine toc="ncx">
    <itemref idref="ch01"/>
    <itemref idref="ch02"/>
    <itemref idref="ch03"/>
    <itemref idref="ch04"/>
    <itemref idref="ch05"/>
    <itemref idref="ch06"/>
    <itemref idref="ch07"/>
    <itemref idref="ch08"/>
    <itemref idref="ch09"/>
    <itemref idref="ch10"/>
    <itemref idref="ch11"/>
    <itemref idref="ch12"/>
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
    <navPoint id="ch07" playOrder="7">
      <navLabel><text>7. Unquoted font-family with a space (koreader#15557)</text></navLabel>
      <content src="ch07.html"/>
    </navPoint>
    <navPoint id="ch08" playOrder="8">
      <navLabel><text>8. Tied same-family, same-fragment ordering</text></navLabel>
      <content src="ch08.html"/>
    </navPoint>
    <navPoint id="ch09" playOrder="9">
      <navLabel><text>9. Exact duplicate @font-face declaration</text></navLabel>
      <content src="ch09.html"/>
    </navPoint>
    <navPoint id="ch10" playOrder="10">
      <navLabel><text>10. Duplicate @import of the same CSS file</text></navLabel>
      <content src="ch10.html"/>
    </navPoint>
    <navPoint id="ch11" playOrder="11">
      <navLabel><text>11. Fragment-scoped alias (target A)</text></navLabel>
      <content src="ch11.html"/>
    </navPoint>
    <navPoint id="ch12" playOrder="12">
      <navLabel><text>12. Fragment-scoped alias (target B)</text></navLabel>
      <content src="ch12.html"/>
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

DUP_IMPORT_TARGET_CSS = """\
/* Imported (twice, from dup-import-wrapper.css) by ch10. Declares the
   @font-face rule that ch10 actually uses. */
@font-face {
  font-family: "OrderingTestDupImport";
  src: url("../fonts/ordering-test-dup-import.ttf");
}
"""

DUP_IMPORT_WRAPPER_CSS = """\
/* Linked by ch10. The SAME @import target, LITERALLY REPEATED: this is the
   fixture for case D (duplicate @import of the same CSS file within one CSS
   file). LVImportStylesheetParser's _inProgress set only guards circular
   imports -- it is cleared as soon as the first @import's recursive Parse()
   call returns, so by the time this second, identical @import line is
   processed, nothing stops it from re-entering Parse() too. That second call
   hits the StyleSheetCache (so the file itself is not re-read/re-parsed) but
   still unconditionally calls LVStyleSheet::merge(), which is a plain
   append with no dedup check -- so dup-import-target.css's selectors end up
   in ch10's stylesheet twice. Font registration itself stays a harmless
   no-op (same fast path as case C/ch09), so this is not expected to be
   visible by eye; see tests/FONTFACE_TEST_PLAN.md section 9 for how to
   confirm the actual duplication by reading the code instead. */
@import url("dup-import-target.css");
@import url("dup-import-target.css");
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
only in Chapter 5's fragment, not this one. @font-face declarations are
scoped to the DocFragment that declares them, so a font declared in one
fragment is not available in another, regardless of spine order &#x2014;
this fragment never declares "OrderingTestFontC" itself, so the reference
here is not expected to resolve.</p>
<p>This chapter and Chapter 5 serve two purposes: (1) confirm this
cross-fragment reference fails gracefully on a fresh parse rather than
crashing, and (2) double as a regression test for a previously-fixed,
unrelated bug in font-resolution caching -- view both chapters in spine
order, without skipping around, for either purpose to be meaningful. See
tests/FONTFACE_TEST_PLAN.md, section 3, for the full procedure and expected
results.</p>
<!-- This paragraph's style is deliberately byte-identical to Chapter 5's
     Test 5 paragraph below (same "font-family: 'OrderingTestFontC', serif;"
     inline style, nothing else differing) -- see this script's docstring
     for why that's the point rather than a bug. -->
<p style="font-family: 'OrderingTestFontC', serif;">
Test 4: this line is expected to fall back to the default serif font
&#x2014; no crash, no missing glyphs, no italic rendering &#x2014;
because "OrderingTestFontC" is out of scope for this fragment.</p>
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

CH07 = PAGE_TEMPLATE.format(
    title="Unquoted font-family with a space",
    heading="Chapter 7 &#x2014; Unquoted font-family with a space (koreader#15557)",
    head_extra="""\
<style type="text/css">
@font-face {
  font-family: Ordering Test Space Name;
  src: url(fonts/ordering-test-space.ttf);
}
@font-face {
  font-family: "Ordering Test Space Name Quoted";
  src: url("fonts/ordering-test-space-quoted.ttf");
}
</style>""",
    body="""\
<p>This chapter's @font-face rule gives its font-family descriptor as
"Ordering Test Space Name" &#x2014; multiple words, no quotes, and
deliberately different from the referenced file name
("ordering-test-space.ttf"). This is a regression test for
koreader#15557: the old @font-face parser (EmbeddedFontStyleParser in
epubfmt.cpp) tokenized descriptor values on whitespace and kept only the
first token, so this name would have been registered as just "Ordering",
silently breaking any rule that referenced the font by its full,
correct name (as Test 7a below does). The second rule, under a quoted
family name, is a sanity control pointing at its own distinct file
&#x2014; quoting was never broken by the old parser, so Test 7b is expected
to pass regardless of whether Test 7a does.</p>
<p style="font-family: Ordering Test Space Name, serif;">Test 7a
(unquoted, the regression case): this line should render in the embedded
bold italic sans-serif test font (Noto Sans Bold Italic).</p>
<p style="font-family: 'Ordering Test Space Name Quoted', serif;">Test 7b
(quoted, sanity control): this line should render in the embedded italic
serif test font (Noto Serif Italic) &#x2014; a different typeface from Test
7a, confirming this is a distinct, correctly-registered face rather than
Test 7a leaking through by coincidence.</p>
<p style="font-family: serif;">Reference (default serif, for contrast): the
quick brown fox jumps over the lazy dog.</p>""",
)

CH08 = PAGE_TEMPLATE.format(
    title="Tied same-family, same-fragment ordering",
    heading="Chapter 8 &#x2014; Tied same-family, same-fragment ordering",
    head_extra="""\
<style type="text/css">
@font-face {
  font-family: "OrderingTestTieReg";
  src: url("fonts/ordering-test-tie-first.ttf");
}
@font-face {
  font-family: "OrderingTestTieReg";
  src: url("fonts/ordering-test-tie-second.ttf");
}
</style>""",
    body="""\
<p>Two @font-face rules declare the SAME family, "OrderingTestTieReg", in
this SAME fragment, neither specifying font-weight or font-style &#x2014;
both register at the default weight (400) and style (roman), an exact tie.
Unlike Chapter 6, where two different explicit font-weight values let weight
matching disambiguate the two faces, nothing here disambiguates them: this
directly tests which one wins on a genuine tie, which Chapter 6 never
exercises.</p>
<p>LVFontSelector::pickBestWeight() (lvfntman.cpp) resolves an exact-weight
tie by returning the first candidate it finds while walking the family's
face list in registration (i.e. declaration) order &#x2014; NOT the most
recently declared one, unlike ordinary CSS cascade rules for equal
specificity. So the line below is expected to render in the FIRST rule's
font (Noto Sans Regular), not the second rule's (Noto Serif Bold).</p>
<p style="font-family: 'OrderingTestTieReg', serif;">Test 8: this line should
render in the embedded plain sans-serif test font (Noto Sans Regular) &#x2014;
the FIRST-declared rule's file, not the second-declared rule's (Noto Serif
Bold). If it renders in Noto Serif Bold instead, tie resolution has changed
to favour the latest declaration &#x2014; update this fixture's expectation
(and tests/FONTFACE_TEST_PLAN.md section 7) to match rather than treating
that as a silent regression, since nothing in the CSS spec mandates
first-wins here; this fixture just documents current behaviour.</p>
<p style="font-family: serif;">Reference (default serif, for contrast): the
quick brown fox jumps over the lazy dog.</p>""",
)

CH09 = PAGE_TEMPLATE.format(
    title="Exact duplicate @font-face declaration",
    heading="Chapter 9 &#x2014; Exact duplicate @font-face declaration",
    head_extra="""\
<style type="text/css">
@font-face {
  font-family: "OrderingTestExactDup";
  src: url("fonts/ordering-test-exact-dup.ttf");
}
@font-face {
  font-family: "OrderingTestExactDup";
  src: url("fonts/ordering-test-exact-dup.ttf");
}
</style>""",
    body="""\
<p>The exact same @font-face rule &#x2014; same family, same url, nothing
differing &#x2014; is declared TWICE in this chapter's &lt;head&gt;&lt;style&gt;.
RegisterDocumentFont()'s fast path (lvfntman.cpp) matches by file_path and
documentId and finds this fragment's index already present on the second
call, so the second declaration is expected to be a cheap no-op: no crash,
no duplicate-glyph artefacts, no change in rendering versus a single
declaration.</p>
<p style="font-family: 'OrderingTestExactDup', serif;">Test 9: this line
should render normally in the embedded test font (Noto Serif Regular),
exactly as if the duplicate rule above were not there at all.</p>
<p style="font-family: serif;">Reference (default serif, for contrast): the
quick brown fox jumps over the lazy dog.</p>""",
)

CH10 = PAGE_TEMPLATE.format(
    title="Duplicate @import of the same CSS file",
    heading="Chapter 10 &#x2014; Duplicate @import of the same CSS file",
    head_extra="""\
<link rel="stylesheet" type="text/css" href="css/dup-import-wrapper.css"/>
<style type="text/css">
/* A second, independent route to the SAME physical typeface (FreeSerif),
   registered via a completely different code path: src: local() maps a
   family name to an already-installed SYSTEM font (RegisterDocumentFontAlias
   in lvfntman.cpp), rather than embedding a copy of the file the way
   dup-import-target.css's rule does (RegisterDocumentFont). This is safe to
   declare alongside OrderingTestDupImport's embedded FreeSerif file in the
   same document -- unlike embedding the same *file* under two families
   (see the note at the top of this script), local() never touches
   RegisterDocumentFont()'s file-path-keyed fast path at all, so there is no
   risk of the second family being silently dropped here. Its only purpose
   is to give Test 10 a known-good comparison typeface on the same page. */
@font-face {
  font-family: "OrderingTestDupImportReference";
  src: local("FreeSerif");
}
</style>""",
    body="""\
<p>This chapter links an external stylesheet (css/dup-import-wrapper.css)
whose ENTIRE content is the same literal <code>@import url("dup-import-target.css");</code>
statement, repeated twice. See the comment at the top of
dup-import-wrapper.css (make_fontface_test_epub.py) for exactly why this is
not caught by the circular-import guard and results in dup-import-target.css's
@font-face rule and selectors being merged into this chapter's stylesheet
twice over. That duplication is real but not expected to be visible here
&#x2014; merging an identical declaration twice doesn't change the computed
style &#x2014; so this chapter can only confirm the font still renders
correctly; it cannot by itself prove the duplication happened or didn't.</p>
<p>The embedded test font here is FreeSerif, which has a noticeably smaller
x-height and cap-height, relative to its nominal size, than most reading
fonts &#x2014; at the SAME CSS font-size it can look smaller than the
surrounding paragraph text even though nothing here sets font-size. Don't
mistake that for a bug: compare Test 10 against the reference line
immediately below it, which reaches the exact same FreeSerif typeface
through an entirely separate registration path (<code>src: local("FreeSerif")</code>,
not the duplicated-@import file embed), so it isn't affected by anything
this chapter is actually testing. If the two lines match, the duplicated
import produced a correctly functioning font and the smaller appearance is
just FreeSerif's normal look at this size.</p>
<p style="font-family: 'OrderingTestDupImport', serif;">Test 10 (via the
doubled @import): this line should render in FreeSerif, despite the
doubled import.</p>
<p style="font-family: 'OrderingTestDupImportReference', serif;">Reference
(FreeSerif via local(), NOT via the doubled @import): this line should look
IDENTICAL to Test 10 above &#x2014; same typeface, same apparent size, same
smaller-than-surrounding-text look. If it doesn't match Test 10, that (not
the small size on its own) is the real signal something is wrong.</p>
<p style="font-family: serif;">Reference (default serif, for contrast with
both FreeSerif lines above): the quick brown fox jumps over the lazy dog.</p>""",
)

CH11 = PAGE_TEMPLATE.format(
    title="Fragment-scoped alias (target A)",
    heading="Chapter 11 &#x2014; Fragment-scoped alias (target A)",
    head_extra="""\
<style type="text/css">
@font-face {
  font-family: "OrderingTestAliasScope";
  src: local("FreeSerif");
}
</style>""",
    body="""\
<p>This chapter and Chapter 12 both declare an @font-face rule under the
SAME family name, "OrderingTestAliasScope", but each maps it via
<code>src: local(...)</code> to a DIFFERENT already-installed system font
&#x2014; FreeSerif here, FreeSans in Chapter 12. Unlike ch01-ch09's embedded
files, local() never registers a distinct document-embedded face; it only
maps a family name to an existing system font, via
RegisterDocumentFontAlias()/resolveAlias() (lvfntman.cpp). That alias
mapping is itself scoped to the declaring DocFragment, so this chapter's
"OrderingTestAliasScope" &#x2192; FreeSerif mapping must not be visible from
Chapter 12, and vice versa, even though both chapters use the identical
alias name.</p>
<p style="font-family: 'OrderingTestAliasScope', sans-serif;">Test 11: this
line should render in FreeSerif &#x2014; a serif face &#x2014; despite the
fallback family listed being sans-serif, confirming the alias resolved
correctly in this fragment.</p>
<p style="font-family: sans-serif;">Reference (default sans-serif, for
contrast): the quick brown fox jumps over the lazy dog.</p>""",
)

CH12 = PAGE_TEMPLATE.format(
    title="Fragment-scoped alias (target B)",
    heading="Chapter 12 &#x2014; Fragment-scoped alias (target B)",
    head_extra="""\
<style type="text/css">
@font-face {
  font-family: "OrderingTestAliasScope";
  src: local("FreeSans");
}
</style>""",
    body="""\
<p>This chapter re-declares "OrderingTestAliasScope" &#x2014; the same
family name Chapter 11 used &#x2014; but maps it to FreeSans instead of
FreeSerif. See Chapter 11 for the full explanation of what this pair of
chapters tests.</p>
<p style="font-family: 'OrderingTestAliasScope', serif;">Test 12: this line
should render in FreeSans &#x2014; a sans-serif face, visibly different from
Chapter 11's Test 11 line &#x2014; despite the fallback family listed being
serif, and despite both chapters using the exact same alias name. If this
line instead renders in FreeSerif (Chapter 11's target), the alias mapping
leaked across DocFragments.</p>
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
        # ch07 (koreader#15557): two more distinct files, for the same
        # duplicate-registration reason as ch06's two files above.
        "ordering-test-space.ttf":        "resources/fonts/noto/NotoSans-BoldItalic.ttf",
        "ordering-test-space-quoted.ttf":  "resources/fonts/noto/NotoSerif-Italic.ttf",
        # ch08 (tied same-family, same-fragment ordering): two more distinct
        # files, again for the same duplicate-registration reason.
        "ordering-test-tie-first.ttf":    "resources/fonts/noto/NotoSans-Regular.ttf",
        "ordering-test-tie-second.ttf":   "resources/fonts/noto/NotoSerif-Bold.ttf",
        # ch09 (exact duplicate @font-face declaration): one file, declared
        # twice by two byte-identical rules -- not reused from elsewhere.
        "ordering-test-exact-dup.ttf":    "resources/fonts/noto/NotoSerif-Regular.ttf",
        # ch10 (duplicate @import of the same CSS file): one file, referenced
        # from dup-import-target.css.
        "ordering-test-dup-import.ttf":   "resources/fonts/freefont/FreeSerif.ttf",
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
        zf.writestr("OEBPS/css/dup-import-target.css",  DUP_IMPORT_TARGET_CSS)
        zf.writestr("OEBPS/css/dup-import-wrapper.css", DUP_IMPORT_WRAPPER_CSS)
        zf.writestr("OEBPS/ch01.html",         CH01)
        zf.writestr("OEBPS/ch02.html",         CH02)
        zf.writestr("OEBPS/ch03.html",         CH03)
        zf.writestr("OEBPS/ch04.html",         CH04)
        zf.writestr("OEBPS/ch05.html",         CH05)
        zf.writestr("OEBPS/ch06.html",         CH06)
        zf.writestr("OEBPS/ch07.html",         CH07)
        zf.writestr("OEBPS/ch08.html",         CH08)
        zf.writestr("OEBPS/ch09.html",         CH09)
        zf.writestr("OEBPS/ch10.html",         CH10)
        zf.writestr("OEBPS/ch11.html",         CH11)
        zf.writestr("OEBPS/ch12.html",         CH12)
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

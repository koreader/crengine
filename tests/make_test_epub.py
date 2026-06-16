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
    <item id="ch05"    href="ch05.html"    media-type="application/xhtml+xml"/>
    <item id="ch06"    href="ch06.html"    media-type="application/xhtml+xml"/>
    <item id="ch07"    href="ch07.html"    media-type="application/xhtml+xml"/>
    <item id="padding" href="padding.bin"  media-type="application/octet-stream"/>
  </manifest>
  <spine toc="ncx">
    <itemref idref="ch01"/>
    <itemref idref="ch02"/>
    <itemref idref="ch03"/>
    <itemref idref="ch04"/>
    <itemref idref="ch05"/>
    <itemref idref="ch06"/>
    <itemref idref="ch07"/>
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
    <navPoint id="ch05" playOrder="5">
      <navLabel><text>5. Fallback Fonts</text></navLabel>
      <content src="ch05.html"/>
    </navPoint>
    <navPoint id="ch06" playOrder="6">
      <navLabel><text>6. Font-Family List</text></navLabel>
      <content src="ch06.html"/>
    </navPoint>
    <navPoint id="ch07" playOrder="7">
      <navLabel><text>7. Issue Regressions</text></navLabel>
      <content src="ch07.html"/>
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
<p class="label">weight: 550 (CSS4 arbitrary — not yet implemented; declaration ignored, renders as 400/normal)</p>
<p class="sample serif w550">The quick brown fox jumps over the lazy dog. (550)</p>
<p class="label">weight: 600</p>
<p class="sample serif w600">The quick brown fox jumps over the lazy dog. (600)</p>
<p class="label">weight: 650 (CSS4 arbitrary — not yet implemented; declaration ignored, renders as 400/normal)</p>
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
CSS4 intermediate values (550, 650) are not yet implemented: the CSS parser
only recognises the literal "100".."900" keywords, so a `font-weight: 550` or
`font-weight: 650` declaration is ignored entirely (not rounded) and the
element falls back to its inherited weight — 400/normal here, so 550 and 650
render identically to 400.</p>
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
<p class="label">&#x26A0; Arbitrary weights not yet implemented. The
`font-weight: 550`/`650` declarations below are ignored entirely (not
rounded), so both render at the inherited weight (400/normal), just italic.</p>
<p class="label">weight: 550, italic (declaration ignored, renders as 400/normal italic)</p>
<p class="sample serif italic w550">The quick brown fox — medium italic (550).</p>
<p class="label">weight: 650, italic (declaration ignored, renders as 400/normal italic)</p>
<p class="sample serif italic w650">The quick brown fox — semi-bold italic (650).</p>

<h2>Sans-serif italic</h2>
<p class="sample sans roman w400">Sans normal roman (400).</p>
<p class="sample sans italic w400">Sans normal italic (400).</p>
<p class="sample sans roman w700">Sans bold roman (700).</p>
<p class="sample sans italic w700">Sans bold italic (700).</p>

<h2>font-synthesis</h2>
<p class="label">&#x26A0; font-synthesis is not yet implemented. Both lines below
will render identically regardless of the font-synthesis value.</p>
<p class="label">font-synthesis: weight style (default — synthesis allowed)</p>
<p class="sample serif italic w700 synth-all">Bold italic with synthesis allowed.</p>
<p class="label">font-synthesis: none — synthesis suppressed; may appear roman/light</p>
<p class="sample serif italic w700 synth-none">Bold italic with synthesis suppressed.</p>

<h2>Expected behaviour</h2>
<p>Italic text should be visually slanted or use a distinct italic face.
font-synthesis is not yet implemented: the synthesis-allowed and
synthesis-none samples render identically.</p>
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

<p class="label">&#x26A0; Not yet implemented. The font-stretch CSS property is not
parsed. All lines in this chapter will render at normal width.</p>

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

CH05 = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN"
  "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en">
<head><title>Fallback Fonts</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>
<h1>Chapter 5 &#x2014; Fallback Fonts</h1>
<p>This chapter contains characters outside the coverage of most primary fonts,
requiring the fallback font mechanism (GetFallbackFont) to be exercised.
Each block should render legibly in an appropriate fallback font rather than
showing missing-glyph boxes.</p>

<h2>CJK Unified Ideographs</h2>
<p class="label">Simplified Chinese sample</p>
<p class="sample">&#x6C49;&#x5B57;&#x6D4B;&#x8BD5;&#xFF1A;&#x8FD9;&#x662F;&#x4E00;&#x6BB5;&#x4E2D;&#x6587;&#x6587;&#x672C;&#x3002;
The quick brown fox. &#x6C49;&#x5B57; mixed with Latin.</p>
<p class="label">Traditional Chinese sample</p>
<p class="sample">&#x6F22;&#x5B57;&#x6E2C;&#x8A66;&#xFF1A;&#x9019;&#x662F;&#x4E00;&#x6BB5;&#x4E2D;&#x6587;&#x6587;&#x672C;&#x3002;</p>
<p class="label">Japanese hiragana and katakana</p>
<p class="sample">&#x3053;&#x3093;&#x306B;&#x3061;&#x306F;&#x3002;&#x30B3;&#x30F3;&#x30CB;&#x30C1;&#x30CF;&#x3002;
Hello in Japanese: &#x3053;&#x3093;&#x306B;&#x3061;&#x306F;&#x4E16;&#x754C;&#x3002;</p>
<p class="label">Korean hangul</p>
<p class="sample">&#xC548;&#xB155;&#xD558;&#xC138;&#xC694;. &#xD55C;&#xAE00; &#xD14C;&#xC2A4;&#xD2B8;.</p>

<h2>Arabic</h2>
<p class="label">Arabic text (right-to-left)</p>
<p class="sample">&#x0645;&#x0631;&#x062D;&#x0628;&#x0627;&#x064B; &#x0628;&#x0627;&#x0644;&#x0639;&#x0627;&#x0644;&#x0645;.
&#x0647;&#x0630;&#x0627; &#x0627;&#x062E;&#x062A;&#x0628;&#x0627;&#x0631; &#x0627;&#x0644;&#x062E;&#x0637;&#x0648;&#x0637;.
Mixed: Hello &#x0645;&#x0631;&#x062D;&#x0628;&#x0627;&#x064B; world.</p>

<h2>Devanagari</h2>
<p class="label">Hindi sample</p>
<p class="sample">&#x0928;&#x092E;&#x0938;&#x094D;&#x0924;&#x0947; &#x0926;&#x0941;&#x0928;&#x093F;&#x092F;&#x093E;&#x0964;
&#x092F;&#x0939; &#x090F;&#x0915; &#x092B;&#x093E;&#x0928;&#x094D;&#x091F; &#x092A;&#x0930;&#x0940;&#x0915;&#x094D;&#x0937;&#x093E; &#x0939;&#x0948;&#x0964;
Mixed: Hello &#x0928;&#x092E;&#x0938;&#x094D;&#x0924;&#x0947; world.</p>

<h2>Bengali</h2>
<p class="label">Bengali sample</p>
<p class="sample">&#x09B9;&#x09CD;&#x09AF;&#x09BE;&#x09B2;&#x09CB; &#x09AC;&#x09BF;&#x09B6;&#x09CD;&#x09AC;&#x0964;
&#x098F;&#x099F;&#x09BF; &#x098F;&#x0995;&#x099F;&#x09BF; &#x09AB;&#x09A8;&#x09CD;&#x099F; &#x09AA;&#x09B0;&#x09C0;&#x0995;&#x09CD;&#x09B7;&#x09BE;&#x0964;</p>

<h2>Greek and Cyrillic</h2>
<p class="label">Greek (usually in primary font coverage)</p>
<p class="sample">&#x03B1;&#x03B2;&#x03B3;&#x03B4;&#x03B5; &#x0391;&#x0392;&#x0393;&#x0394;&#x0395;.
&#x03BA;&#x03B1;&#x03BB;&#x03B7;&#x03BC;&#x03AD;&#x03C1;&#x03B1; &#x03BA;&#x03CC;&#x03C3;&#x03BC;&#x03B5;.</p>
<p class="label">Cyrillic (usually in primary font coverage)</p>
<p class="sample">&#x041F;&#x0440;&#x0438;&#x0432;&#x0435;&#x0442; &#x043C;&#x0438;&#x0440;.
&#x042D;&#x0442;&#x043E; &#x0442;&#x0435;&#x0441;&#x0442; &#x0448;&#x0440;&#x0438;&#x0444;&#x0442;&#x0430;.</p>

<h2>Mixed-script paragraph</h2>
<p class="sample">English, &#x4E2D;&#x6587;, &#x65E5;&#x672C;&#x8A9E;, &#xD55C;&#xAD6D;&#xC5B4;,
&#x0645;&#x0631;&#x062D;&#x0628;&#x0627;&#x064B;, &#x0928;&#x092E;&#x0938;&#x094D;&#x0924;&#x0947; &#x2014;
all in one paragraph. Each non-Latin run should use an appropriate fallback font
while the Latin text uses the primary font.</p>

<h2>Expected behaviour</h2>
<p>All characters above should render as recognisable glyphs, not as empty boxes.
Missing boxes indicate the fallback font mechanism failed or the required fallback
font is not installed. Mixing scripts within one paragraph tests the per-character
fallback chain (GetFallbackFont with forFaceName).</p>
</body>
</html>
"""

CH06 = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN"
  "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en">
<head><title>Font-Family List</title>
<link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>
<h1>Chapter 6 &#x2014; CSS font-family List Selection</h1>

<p>This chapter verifies that a comma-separated CSS font-family list is
searched in order. Monospace text is visually distinctive regardless of
which specific fonts are installed, making it an unambiguous reference.</p>

<h2>Reference lines</h2>
<p class="label">A. font-family: monospace &#x2014; baseline monospace rendering</p>
<p class="sample mono">The quick brown fox jumps over the lazy dog. 0123456789</p>

<p class="label">B. font-family: serif &#x2014; baseline proportional rendering</p>
<p class="sample serif">The quick brown fox jumps over the lazy dog. 0123456789</p>

<h2>List with existing first entry</h2>
<p class="label">C. font-family: "Droid Sans Mono", serif &#x2014; first font exists;
must look identical to line A</p>
<p class="sample" style="font-family: 'Droid Sans Mono', serif;">
The quick brown fox jumps over the lazy dog. 0123456789</p>

<h2>List with missing first entry</h2>
<p class="label">D. font-family: "ZZZNotInstalled", "Droid Sans Mono", serif &#x2014;
first font absent, second exists; must look identical to lines A and C</p>
<p class="sample" style="font-family: 'ZZZNotInstalled', 'Droid Sans Mono', serif;">
The quick brown fox jumps over the lazy dog. 0123456789</p>

<p class="label">E. font-family: "ZZZNotInstalled1", "ZZZNotInstalled2", serif &#x2014;
both absent; must fall back to proportional reading font, matching line B</p>
<p class="sample" style="font-family: 'ZZZNotInstalled1', 'ZZZNotInstalled2', serif;">
The quick brown fox jumps over the lazy dog. 0123456789</p>

<h2>Expected behaviour</h2>
<p>Lines A, C, and D must be rendered in a monospace face and look
identical to each other. Lines B and E must be rendered in the proportional
reading font and look identical to each other. If D looks like B/E instead
of A/C, the font-family list is not being searched sequentially.</p>
</body>
</html>
"""

CH07 = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN"
  "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en">
<head><title>Issue Regressions</title>
<link rel="stylesheet" type="text/css" href="style.css"/>
<style type="text/css">
.italic-body { font-style: italic; }
</style>
</head>
<body>
<h1>Chapter 7 &#x2014; Issue Regressions</h1>
<p>Each section documents a specific past bug. All items should render
correctly; any failure indicates a regression.</p>

<h2>koreader#8791 &#x2014; Spurious document-wide italic</h2>
<p class="label">A CSS rule with font-style:italic scoped to a class must not
affect unstyled body text. The two lines below must look different.</p>
<p class="sample serif">This line is unstyled body text &#x2014; must be roman (upright).</p>
<p class="sample serif italic">This line has class="italic" &#x2014; must be italic (slanted).</p>
<p class="label">If both lines appear italic the document-wide italic bug has
regressed.</p>

<h2>koreader#10040 / koreader#12525 &#x2014; @font-face numeric font-weight ignored</h2>
<p class="label">&#x26A0; This bug cannot be tested from a static EPUB without
embedded fonts. The bug is in @font-face registration: when an EPUB declares
@font-face with a numeric font-weight (e.g. font-weight: 900), the weight is
ignored and the face is registered at 400. The two lines below use CSS
font-weight on regular elements, which works correctly and is unrelated to the
bug. Both lines will differ visually regardless of the fix.</p>
<p class="sample w400">CSS font-weight: 400 on an element &#x2014; normal weight (working correctly).</p>
<p class="sample w900">CSS font-weight: 900 on an element &#x2014; heavy weight (working correctly).</p>

<h2>koreader#11771 &#x2014; Ruby annotation horizontal alignment</h2>
<p class="label">The regression in #11771 was horizontal: annotations shifted
left when -epub-text-align-last was set. Check that the annotation is centred
horizontally above its base characters and that adjacent text has not shifted.
A vertical gap between annotation and base text is a known characteristic of
crengine's table-based ruby rendering and is not related to this regression.</p>
<p class="sample">
  Base text with ruby:
  <ruby>&#x6F22;&#x5B57;<rt>&#x304B;&#x3093;&#x3058;</rt></ruby>
  must have the annotation centred above the two base characters.
  Adjacent text must not shift.
</p>

<h2>koreader#8306 &#x2014; Unicode smart quotes</h2>
<p class="label">Unicode quotation marks must render as the correct glyph and
must not corrupt the surrounding characters.</p>
<p class="sample serif">Straight: "quoted text" and 'single quoted'.</p>
<p class="sample serif">Smart double: &#x201C;quoted text&#x201D; &#x2014; opening and closing curly quotes.</p>
<p class="sample serif">Smart single: &#x2018;quoted text&#x2019; &#x2014; opening and closing curly apostrophes.</p>
<p class="label">If any characters above appear as sequences like
&#xC3;&#xA2;&#xE2;&#x80;&#x9C; the Unicode encoding regression has
returned.</p>

<h2>Expected behaviour</h2>
<p>koreader#8791: body text is roman; only the classed line is italic.</p>
<p>koreader#10040/#12525: once embedded fonts are fixed, weight 900 renders
visibly heavier than weight 400.</p>
<p>koreader#11771: ruby annotation is centred above base characters with no
horizontal shift.</p>
<p>koreader#8306: all quotation mark characters render correctly with no
mojibake.</p>
</body>
</html>
"""

# ---------------------------------------------------------------------------
# Padding
# ---------------------------------------------------------------------------

# Unreferenced binary blob, stored uncompressed, purely to push the EPUB's
# total size past KOReader's partial-refresh threshold so the test exercises
# full-refresh-vs-partial-refresh behaviour on a realistically sized book.
PADDING_SIZE = 800_000
PADDING = bytes((i * 2654435761) & 0xFF for i in range(PADDING_SIZE))

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
        zf.writestr("OEBPS/ch05.html",        CH05)
        zf.writestr("OEBPS/ch06.html",        CH06)
        zf.writestr("OEBPS/ch07.html",        CH07)
        zf.writestr(zipfile.ZipInfo("OEBPS/padding.bin"), PADDING,
                    compress_type=zipfile.ZIP_STORED)
    with open(path, "wb") as f:
        f.write(buf.getvalue())
    print(f"Written: {path}  ({os.path.getsize(path)} bytes)")

if __name__ == "__main__":
    out = os.path.join(os.path.dirname(__file__), "font-manager-test.epub")
    build_epub(out)

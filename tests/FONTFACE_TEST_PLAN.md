# @font-face Registration Ordering — Test Plan

## Scope

This plan covers manual and visual testing of the `@font-face` registration
*ordering* invariant relied on by the `@font-face` parsing refactor. The
post-landing ordering-invariant audit confirmed there is no *structural*
bypass for EPUB — this plan is the runtime check that audit said it does
not replace.

This is deliberately a separate EPUB from `font-manager-test.epub`. The font
manager test EPUB covers font *selection* (weight, italic, stretch, fallback)
among already-correctly-registered faces, using only system-installed fonts
and no multi-fragment `@font-face` scenarios. This plan is primarily about
*when* an embedded `@font-face` font becomes available relative to the
per-DocFragment body styling that needs it (Chapters 1–5), plus two adjacent
checks that belong at the same `@font-face`-parsing layer rather than in the
selection-focused EPUB: whether a numeric `font-weight` descriptor is read
correctly at registration time (Chapter 6, section 4 below), and whether an
unquoted, multi-word `font-family` descriptor is read in full rather than
truncated (Chapter 7, section 5 below).

The test EPUB is at `tests/fontface-test.epub`. Regenerate it with
`python3 tests/make_fontface_test_epub.py` if needed — it reads the test
fonts directly from `koreader/resources/fonts/` (crengine is always
built as a thirdparty component of koreader, so this is always available;
the script fails with a clear error naming the missing path if it isn't).

Each `@font-face` rule in this EPUB points at a **distinct** embedded font
file (Chapters 1–3/5 additionally give each rule's file its own family name;
Chapter 6 uses two files under one shared family, at two different
`font-weight` values — see section 4 below; Chapter 7 uses two files under
two different family names, one quoted and one not — see section 5 below).
Files must stay distinct per rule — see the note at the top of `make_fontface_test_epub.py`: registering
the same font file twice in the same document, even under a different family
name, is silently dropped by the font manager's duplicate-face detection
(`LVFontFace::id()` in `lvfntman.cpp`). That's a real, pre-existing font
manager limitation, unrelated to and independent of the `@font-face` ordering
and weight-parsing behaviour this plan tests — confirmed live via "font
definition is duplicate" trace log entries when this fixture briefly shared
one file across all three families during development. Worth a separate bug
report, but out of scope for this plan.

A pass/fail only requires distinguishing each test font from the **default
serif** (or, in Chapters 6 and 7, from each other) by eye — no font identity
needs to be recognised — mirroring the monospace-as-unambiguous-marker
approach already used in `font-manager-test.epub` (Chapter 6). The seven test
fonts (Droid Sans Mono, Noto Sans Bold, Noto Sans Italic, FreeSans, Noto
Serif Bold Italic, Noto Sans Bold Italic, Noto Serif Italic) are also
visually distinct from each other, which is incidental rather than
load-bearing for Chapters 1–5 but is exactly the point for Chapters 6 and 7.

---

## 1. Same-fragment declaration (Chapter 1)

**Goal:** Confirm the simplest case — a font declared in a fragment's own
`<head><style>` is available to that same fragment's body.

| # | Action | Expected |
|---|--------|----------|
| 1.1 | Open `fontface-test.epub`, view Chapter 1 | Test 1 line renders in the embedded monospace test font |
| 1.2 | Compare Test 1 line to the reference serif line below it | The two lines must look visibly different (monospace vs. serif) |

---

## 2. Shared external CSS — first parse and cache hit (Chapters 2–3)

**Goal:** Confirm a font declared in an external CSS file is registered on
first link, and that a second DocFragment linking the same file (hitting the
in-memory stylesheet cache rather than re-parsing) still renders the font
correctly.

| # | Action | Expected |
|---|--------|----------|
| 2.1 | View Chapter 2 | Test 2 line renders in the embedded bold sans-serif test font (Noto Sans Bold) |
| 2.2 | View Chapter 3 | Test 3 line renders in the embedded bold sans-serif test font, identical to Chapter 2's Test 2 line |
| 2.3 | Compare both Test lines to their reference serif lines | Both Test lines must look visibly different from their adjacent reference line |

If Chapter 3 fails (renders as serif/fallback) while Chapter 2 succeeds, that
points specifically at the stylesheet-cache-hit path not correctly preserving
font availability for later DocFragments — see
`LVImportStylesheetParser::Parse()` (`lvtinydom.cpp:5009-5042`) and the
`dest.merge(*cached)` cache-hit branch.

---

## 3. Font referenced before it is declared (Chapters 4–5)

**Goal:** Confirm the one documented failure mode — a font referenced in an
earlier spine item than the one that declares it — fails *gracefully* on a
**fresh parse**, and confirm the declaration itself is valid by using it
correctly in its own fragment.

**Secondary purpose — reproduction case for an open bug.** This section also
doubles as the manual reproduction case for the `_fontMap`/style-content
caching bug found via runtime verification (stale font-resolution cache).
That bug is real, confirmed, and currently unfixed. Steps 3.1–3.4, run in spine order without
skipping around (see the navigation-order note further below), are this
bug's repro recipe: if the engine is ever changed such that Chapters 4 and 5
end up resolving the same style index again, this section will once more show
Chapter 4 picking up whichever font was resolved first regardless of spine
order — that's the symptom to look for when revisiting that bug.

**This section's Chapter 4 expectation only applies to a fresh parse** (first
open, or any open that follows a cache invalidation — magic-constant bump,
stale-cache detection, or simply no cache file yet). On a **reopen from an
existing cache**, Chapter 4 is expected to render in the *italic* test font
instead — see the note below the table. Confirm which scenario you're in
before reading the result.

| # | Action | Expected |
|---|--------|----------|
| 3.1 | View Chapter 4 on a fresh parse | Test 4 line renders in the **default serif font**, not italic — no crash, no missing glyphs |
| 3.2 | Compare Chapter 4's Test 4 line to its reference serif line (fresh parse) | The two lines must look **identical** (both fell back to default serif) |
| 3.3 | View Chapter 5 | Test 5 line renders in the embedded italic sans-serif test font (Noto Sans Italic) |
| 3.4 | Compare Chapter 5's Test 5 line to its reference serif line | The two lines must look visibly different (italic sans vs. serif) |
| 3.5 | Close the book, reopen it (cache reopen, not a fresh parse) | Test 4 line now renders in the **italic test font**, not serif — this is correct, see note below |

**Why Chapter 4 changes behaviour between fresh parse and cache reopen.**
`registerEmbeddedFonts()` (the cache-reopen path, `lvtinydom.cpp`) replays the
*complete*, already-finalized font list from the previous session's
`saveChanges()` in one bulk call, before any DocFragment is styled — there is
no incremental, per-rule discovery to be "too early" relative to, unlike a
fresh parse where `registerFontFace()` registers fonts one `@font-face` rule
at a time, interleaved with per-DocFragment styling. Confirmed directly in
`~/koreader.log`: on reopen, `RegisterDocumentFont` fires for all three test
fonts immediately during `loadCacheFileContent()`, before the subsequent
re-render's per-DocFragment `applyNodeStylesheet()` calls all hit "font
definition is duplicate." So on reopen, Chapter 5's declaration is already
known before Chapter 4 is ever styled — Chapter 4 rendering in italic on
reopen is the *correct* outcome, not a regression. To re-test the fresh-parse
behaviour (3.1/3.2), delete the book's cache (or use a copy you haven't
opened before) rather than just closing and reopening.

Step 3.3/3.4 passing while 3.1/3.2 fail **on a fresh parse** (i.e. Chapter 4
unexpectedly renders in the italic test font on first open, not on reopen)
would indicate font registration is not actually scoped to spine order —
worth investigating immediately, as it would mean the documented ordering
assumption doesn't hold as expected.

Step 3.1 rendering in the italic test font, or 3.3 unexpectedly falling back
to serif, both indicate a real problem. Before filing either as an ordering
bug, first rule out two other, unrelated issues this fixture's design has to
actively avoid:

- The font manager's duplicate-face-detection limitation described above
  (Scope section) — confirm Chapters 1/2/3's test fonts are rendering
  correctly first, since a shared-file mistake in this fixture would make
  Chapter 5 fail for a reason unrelated to ordering.
- A separate font-*resolution* caching issue: `ldomNode::initNodeFont()`
  (`lvtinydom.cpp`) caches resolved fonts keyed by a document-wide,
  content-deduplicated style index (`_fontMap`/`_styles`, not by node), with
  no invalidation when the font registry changes mid-pass. If Chapter 4's and
  Chapter 5's Test paragraphs ever end up with byte-identical computed styles
  again (the `letter-spacing: 1px` on Chapter 4's paragraph exists
  specifically to prevent this), whichever chapter is *viewed first* "wins"
  and the other silently inherits its font resolution, independent of
  registration ordering — a real, more general bug in its own right, not
  fixed, worth its own investigation regardless of whether this specific
  test ever trips over it again.

**Navigation order matters for this reason.** Because of the caching issue
above, view Chapters 1 through 5 **in spine order, front to back, in a single
session**, before drawing conclusions from Chapter 4 or 5. Viewing Chapter 5
before stepping back to Chapter 4 (or any other out-of-order navigation
between them) can make Chapter 4 incorrectly show the italic test font, or
vice versa — not because registration ordering broke, but because of the
*other* bug this section just described. If you see an unexpected result,
restart from Chapter 1 in a fresh reading session before filing it.

---

## 4. Numeric `@font-face` font-weight (Chapter 6)

**Goal:** Regression test for koreader#10040 / koreader#12525. The old
`@font-face` parser (`EmbeddedFontStyleParser` in `epubfmt.cpp`, removed by
this refactor) only recognised the keyword `bold` for the `font-weight`
descriptor; any numeric value (e.g. `font-weight: 900`) was silently dropped
and the face registered at 400. Downstream, an element requesting that
numeric weight would then synthesise it from the wrongly-registered 400-weight
face instead of using the face actually declared at that weight.

Chapter 6 declares two `@font-face` rules under one family,
`OrderingTestWeightReg`, at `font-weight: 300` and `font-weight: 900`, each
pointing at its own distinct embedded file chosen to be visually
unmistakable from the other (FreeSans vs. Noto Serif Bold Italic) — the same
identity-by-typeface technique used in Chapters 1–5, so a mismatch is
visible by eye rather than requiring a judgement call about synthetic vs.
genuine boldness.

| # | Action | Expected |
|---|--------|----------|
| 6.1 | View Chapter 6, Test 6a line (`font-weight: 300`) | Renders in the embedded plain sans-serif test font (FreeSans) |
| 6.2 | View Chapter 6, Test 6b line (`font-weight: 900`) | Renders in the embedded bold italic serif test font (Noto Serif Bold Italic) |
| 6.3 | Compare Test 6a and Test 6b | The two lines must be **completely different typefaces** from each other, not just different weights of one typeface |

If Test 6a and Test 6b render in the same typeface, numeric `font-weight` is
being dropped at `@font-face` registration time — see
`parse_font_face_rule()` in `lvstsheet.cpp`, the `cssff_font_weight` case.

---

## 5. Unquoted `font-family` with a space (Chapter 7)

**Goal:** Regression test for koreader#15557. The old `@font-face` parser
(`EmbeddedFontStyleParser` in `epubfmt.cpp`, removed by this refactor)
tokenized descriptor values on whitespace and kept only the first token, so
an unquoted, multi-word `font-family` value like `Ordering Test Space Name`
was registered as just `Ordering` — silently breaking any rule that
referenced the font by its full, correct name. Quoted values were never
affected, since quoting routed through a different code path
(`onQuotedText()`) that read the whole string.

Chapter 7 declares two `@font-face` rules, each pointing at its own distinct
embedded file: one under an unquoted multi-word family name (`Ordering Test
Space Name`, the regression case) and one under a quoted variant
(`"Ordering Test Space Name Quoted"`, a sanity control that was never
affected by the old parser's bug).

| # | Action | Expected |
|---|--------|----------|
| 7.1 | View Chapter 7, Test 7a line (unquoted family name) | Renders in the embedded bold italic sans-serif test font (Noto Sans Bold Italic), not the default serif |
| 7.2 | View Chapter 7, Test 7b line (quoted family name) | Renders in the embedded italic serif test font (Noto Serif Italic) — a different typeface from Test 7a |
| 7.3 | Compare Test 7a and Test 7b to the reference serif line | All three must look visibly different from each other |

If Test 7a falls back to the default serif font while Test 7b renders
correctly, the unquoted multi-word `font-family` descriptor is being
truncated at `@font-face` registration time — see `parse_font_face_rule()`
in `lvstsheet.cpp`, the `cssff_font_family` case and its use of
`splitPropertyValueList()`.

---

## 6. Manual step — `@font-face` in a styletweak

**Goal:** Confirm `@font-face` declared in a KOReader styletweak (merged
*after* the book's own user-agent stylesheet, not at the top of any file) is
available from the very first page of a book — the koreader#10604 scenario,
and the specific case raised in maintainer review about `@font-face`
positioning.

This cannot be packaged into the test EPUB itself — styletweaks are applied
by KOReader at the Lua layer, independent of any one book.

| # | Action | Expected |
|---|--------|----------|
| 6.1 | Create a custom styletweak containing:<br>`@font-face { font-family: "StyletweakTestFont"; src: local("FreeSans"); }`<br>`body { font-family: "StyletweakTestFont", serif; }` | Styletweak saved without error |
| 6.2 | Enable the styletweak, open `fontface-test.epub` (or any book) | **All** body text, starting from the very first page, renders in FreeSans — not the default serif, and not a fallback that only kicks in after the first page |
| 6.3 | Disable the styletweak, reopen the same book | Text reverts to the normal default font |

If 6.2 shows the first page in the wrong font while later pages are correct,
that's the ordering bug this whole plan exists to catch, manifesting via the
styletweak path specifically rather than the EPUB-internal path covered by
Chapters 1–5 above.

---

## Test environment notes

- This plan assumes the embedded test font (a monospace TrueType face) is
  visually unmistakable from the device's default serif reading font. If the
  default reading font is itself monospace (unusual), switch reading fonts
  before testing.
- Re-run section 3 (Chapters 4–5) after any change to spine-order processing,
  DOM construction (`ldomDocumentWriter`/`ldomDocumentFragmentWriter`), or the
  stylesheet cache — these are the most likely places for a regression in
  this specific ordering invariant to appear.
- Re-run section 4 (Chapter 6) after any change to `parse_font_face_rule()`
  or the `@font-face` descriptor tables in `lvstsheet.cpp`.
- Re-run section 5 (Chapter 7) after any change to `parse_font_face_rule()`'s
  `cssff_font_family` case or `splitPropertyValueList()` in `lvstsheet.cpp`.
- This plan only exercises the EPUB path. The ordering-invariant audit found
  a separate, pre-existing, unrelated gap in the CHM `DocFragment` path —
  out of scope here, tracked separately.

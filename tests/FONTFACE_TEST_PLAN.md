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
per-DocFragment body styling that needs it (Chapters 1–5), plus several
adjacent checks that belong at the same `@font-face`-parsing/registration
layer rather than in the selection-focused EPUB: whether a numeric
`font-weight` descriptor is read correctly at registration time (Chapter 6,
section 4), whether an unquoted, multi-word `font-family` descriptor is read
in full rather than truncated (Chapter 7, section 5), which face wins when
two rules under one family genuinely tie in the same fragment (Chapter 8,
section 7), whether an exact duplicate `@font-face` rule is a harmless no-op
(Chapter 9, section 8), whether a literally-repeated `@import` of the same
CSS file causes any visible problem (Chapter 10, section 9), and whether a
`src: local(...)` alias is correctly scoped per DocFragment when two
fragments reuse the same alias name for different targets (Chapters 11–12,
section 10), and whether an `@import`ed `@font-face` rule is overridden by a
same-family/weight/style rule declared afterward in the importing file, the
way ordinary CSS cascade source-order rules would suggest (section 11).

The test EPUB is at `tests/fontface-test.epub`. Regenerate it with
`python3 tests/make_fontface_test_epub.py` if needed — it reads the test
fonts directly from `koreader/resources/fonts/` (crengine is always
built as a thirdparty component of koreader, so this is always available;
the script fails with a clear error naming the missing path if it isn't).

Section 11 uses a second, separate EPUB, `tests/fontface-import-precedence-test.epub`
(regenerate with `python3 tests/make_fontface_import_precedence_test_epub.py`).
It is not a chapter in `fontface-test.epub` because it needs a fresh,
genuine same-family/weight/style tie — the same requirement as Chapter 8 —
but `fontface-test.epub` has already spent all eleven of KOReader's
visually-distinct, Latin-legible shipped fonts across its own chapters (see
below), leaving no twelfth distinct file to add another tie case into that
same document without hitting the same-file-per-document dedup limitation
described two paragraphs down. See the module docstring in
`make_fontface_import_precedence_test_epub.py` for the full reasoning,
including why the remaining shipped fonts (Arabic/Bengali/Devanagari "UI"
faces, Nerd Fonts symbols) aren't viable substitutes — none of them cover
Basic Latin.

Each `@font-face` rule in this EPUB points at a **distinct** embedded font
file (Chapters 1–3/5 additionally give each rule's file its own family name;
Chapter 6 uses two files under one shared family, at two different
`font-weight` values — see section 4 below; Chapter 7 uses two files under
two different family names, one quoted and one not — see section 5 below;
Chapter 8 uses two files under one shared family at the *same* default
weight — see section 7 below; Chapter 9 uses one file declared by two
byte-identical rules — see section 8 below; Chapter 10 uses one file reached
via a doubled `@import` — see section 9 below). Chapters 11–12 are the one
exception: their `src: local(...)` rules reference already-installed system
fonts (FreeSerif, FreeSans) by name rather than embedding a file — see
section 10 below.

Files must stay distinct per rule — see the note at the top of `make_fontface_test_epub.py`: registering
the same font file twice in the same document, even under a different family
name, is silently dropped by the font manager's duplicate-face-registration
fast path (`RegisterDocumentFont()`'s `addDocFragmentToFacesForFile()` short
circuit in `lvfntman.cpp`, which matches by file path only, ignoring the
requested family name). That's a real, pre-existing font manager limitation,
unrelated to and independent of the `@font-face` ordering and weight-parsing
behaviour this plan tests — confirmed live via "font definition is
duplicate" trace log entries when this fixture briefly shared one file
across all three families during development. Worth a separate bug report,
but out of scope for this plan. (Chapter 9's exact duplicate is *not* an
instance of this: it repeats one rule under its own single family, not two
different families sharing a file — see section 8.)

A pass/fail only requires distinguishing each test font from the **default
serif** (or, in Chapters 6, 7 and 8, from each other; in Chapters 11–12,
from the opposite chapter's line) by eye — no font identity needs to be
recognised — mirroring the monospace-as-unambiguous-marker approach already
used in `font-manager-test.epub` (Chapter 6). The eleven embedded test fonts
(Droid Sans Mono, Noto Sans Bold, Noto Sans Italic, FreeSans, Noto Serif
Bold Italic, Noto Sans Bold Italic, Noto Serif Italic, Noto Sans Regular,
Noto Serif Bold, Noto Serif Regular, FreeSerif) are also visually distinct
from each other, which is incidental rather than load-bearing for Chapters
1–5 but is exactly the point for Chapters 6, 7 and 8. Chapters 11–12 instead
rely on FreeSerif vs. FreeSans being visually distinct system fonts.

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

**Secondary purpose — regression test for a fixed bug.** This section also
doubles as the regression test for a `_fontMap`/style-content caching bug
found via runtime verification (stale font-resolution cache), fixed by
`ldomNode::initNodeStyle()` clearing `_fontMap` on every `el_DocFragment`
boundary ("CSS: avoid walk up node tree to get fragmentIdx",
`lvtinydom.cpp`). Chapter 4's Test 4 paragraph and Chapter 5's Test 5
paragraph are deliberately given byte-identical computed styles (see
`make_fontface_test_epub.py`'s docstring) so that steps 3.1–3.4 exercise this
fix directly: if the `_fontMap` boundary clear ever regresses, this section
will once more show Chapter 4 picking up whichever font was resolved first
regardless of spine order — that's the symptom to look for.

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
- A separate font-*resolution* caching issue, now fixed:
  `ldomNode::initNodeFont()` (`lvtinydom.cpp`) caches resolved fonts keyed by
  a document-wide, content-deduplicated style index (`_fontMap`/`_styles`,
  not by node). Chapter 4's and Chapter 5's Test paragraphs are deliberately
  given byte-identical computed styles specifically to exercise this: before
  the fix, whichever chapter was *viewed first* would "win" and the other
  would silently inherit its font resolution, independent of registration
  ordering. `ldomNode::initNodeStyle()` now clears `_fontMap` on every
  `el_DocFragment` boundary, so each fragment's styling pass always
  re-resolves against its own `@font-face` scope rather than reusing a
  resolution cached from a different fragment. If Chapter 4 and 5 diverge
  from this section's expected results, this is the first place to look.

**Navigation order should no longer matter, by design.** View Chapters 1
through 5 in spine order for a clean first pass, but the fix above means
viewing Chapter 5 before stepping back to Chapter 4 (or any other
out-of-order navigation between them) should **not** change either chapter's
result. If it does, that's a regression of the `_fontMap` boundary-clear fix
described above, not expected behaviour — restart from Chapter 1 in a fresh
reading session to confirm before filing it.

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

## 7. Tied same-family, same-fragment ordering (Chapter 8)

**Goal:** Confirm which face wins when two `@font-face` rules under the same
family, in the same fragment, genuinely tie — same (default) `font-weight`,
same (default, roman) `font-style`. Chapter 6 (section 4) already covers two
rules under one family in one fragment, but disambiguates them with two
different explicit `font-weight` values, so weight matching always has a
clear winner and the tie-break path is never exercised. This section is
specifically about what happens when nothing disambiguates the two faces.

Chapter 8 declares two `@font-face` rules under one family,
`OrderingTestTieReg`, neither specifying `font-weight` or `font-style` —
both register at weight 400, roman — each pointing at its own distinct
embedded file (Noto Sans Regular declared first, Noto Serif Bold declared
second).

| # | Action | Expected |
|---|--------|----------|
| 7.1 | View Chapter 8, Test 8 line | Renders in the embedded plain sans-serif test font (Noto Sans Regular) — the **first**-declared rule's file |
| 7.2 | Compare Test 8 to the reference serif line | The two lines must look visibly different |

If Test 8 instead renders in Noto Serif Bold (the **second**-declared
rule's file), tie resolution is picking the most-recently-declared face
rather than the first — see `LVFontSelector::pickBestWeight()` in
`lvfntman.cpp`: its exact-static-weight-match loop returns the first
candidate found while walking the family's face list in registration
(declaration) order, so a genuine tie currently resolves to whichever rule
was declared **first**, not last — the opposite of ordinary CSS cascade
semantics for equal specificity. That is current, intentional behaviour as
far as this plan is concerned (nothing in the CSS Fonts spec mandates
last-wins for an exact tie among embedded faces), so a change here isn't
automatically a regression — but it is a behaviour change worth confirming
was deliberate, and this section (plus Chapter 8's own in-page note) should
be updated to match if it ever changes.

---

## 8. Exact duplicate `@font-face` declaration (Chapter 9)

**Goal:** Confirm that declaring the exact same `@font-face` rule twice in
one CSS block — same family, same `src`, nothing differing — is a harmless
no-op rather than a crash, a double-registration artefact, or a corrupted
face list.

Chapter 9 declares one rule, `OrderingTestExactDup`, pointing at its own
distinct embedded file, verbatim twice in the same `<head><style>` block.

| # | Action | Expected |
|---|--------|----------|
| 8.1 | View Chapter 9, Test 9 line | Renders normally in the embedded test font (Noto Serif Regular), exactly as if the duplicate rule were not there |
| 8.2 | Compare Test 9 to the reference serif line | The two lines must look visibly different |
| 8.3 | No crash, hang, or log spam beyond a single (harmless) "font definition is duplicate" trace entry | — |

This case is expected to pass cleanly: `RegisterDocumentFont()`'s fast path
in `lvfntman.cpp` (`addDocFragmentToFacesForFile()`) matches the second,
identical registration by file path and finds this fragment's index already
present, so it returns immediately without reloading or re-registering
anything. This is a **different** code path from the same-file/different-family
limitation noted in the Scope section above — that one is a real, unrelated
gap; this one is the intended, documented no-op behaviour for a genuine
duplicate. If Test 9 falls back to the default serif font, or the book fails
to open, the fast path has stopped treating an exact duplicate as safe.

---

## 9. Duplicate `@import` of the same CSS file (Chapter 10)

**Goal:** Confirm that a CSS file containing the same literal
`@import url(...)` statement twice does not visibly break rendering, even
though (per code reading, not visually observable) it does cause the
imported file's rules to be merged into the destination stylesheet twice.

Chapter 10 links `css/dup-import-wrapper.css`, whose entire content is:

```css
@import url("dup-import-target.css");
@import url("dup-import-target.css");
```

`dup-import-target.css` declares the `OrderingTestDupImport` `@font-face`
rule, pointing at its own distinct embedded file: FreeSerif.

**Note on FreeSerif's apparent size.** FreeSerif has a visibly smaller
x-height and cap-height, relative to its nominal size, than most reading
fonts (x-height/em ≈ 0.45 vs. ≈ 0.54 for Noto Serif) — at the exact same CSS
`font-size` it can look smaller than the surrounding paragraph text even
though nothing in this fixture sets `font-size` anywhere. That's a property
of the typeface, unrelated to anything this section tests, and easy to
mistake for a registration bug on first look. To make that unambiguous,
Chapter 10 also declares a second family, `OrderingTestDupImportReference`,
reaching the exact same FreeSerif typeface via `src: local("FreeSerif")` —
a completely different registration path (`RegisterDocumentFontAlias`, not
`RegisterDocumentFont`) that never goes anywhere near the doubled-`@import`
chain — purely so Test 10 has a known-good, same-page comparison line.

| # | Action | Expected |
|---|--------|----------|
| 9.1 | View Chapter 10, Test 10 line (via the doubled `@import`) | Renders in FreeSerif |
| 9.2 | View Chapter 10's reference line (via `local("FreeSerif")`, immediately below Test 10) | Renders in FreeSerif |
| 9.3 | Compare Test 10 to its `local()` reference line | The two lines must look **identical** — same typeface, same apparent size (including both looking smaller than the surrounding paragraph text — that's FreeSerif's normal look, not a defect) |
| 9.4 | Compare both FreeSerif lines to the default-serif reference line at the bottom | Both FreeSerif lines must look visibly different from the default serif line |
| 9.5 | No crash, hang, or unbounded slowdown | — |

Step 9.3 is the actual pass/fail signal for this section — not whether the
FreeSerif lines look "small," since they're expected to. If Test 10 and its
`local()` reference diverge (e.g. Test 10 falls back to the default serif
font while the reference renders correctly in FreeSerif), that indicates the
doubled `@import` broke `OrderingTestDupImport`'s registration specifically,
since the reference line, being on a completely separate code path, would be
unaffected by anything wrong with the `@import` handling.

A pass here only confirms there's no *visible* problem — merging an
identical set of declarations into a stylesheet twice doesn't change the
computed style, so 9.1–9.5 passing does **not** prove the duplication isn't
happening internally. To confirm the duplication itself: `LVImportStylesheetParser::Parse()`
(`lvtinydom.cpp`) guards only against *circular* imports via its
`_inProgress` set, which is cleared as soon as each import's recursive
`Parse()` call returns — so a second, later `@import` of the same URL in the
same file is not recognised as "already imported this pass." It re-enters
`Parse()`, hits the `StyleSheetCache` (so the file itself is not re-read
from disk or re-parsed), but still calls `LVStyleSheet::merge()`
unconditionally, and `merge()` (`lvstsheet.cpp`) is a plain append with no
dedup check — so `dup-import-target.css`'s selectors end up in the
destination stylesheet twice. Font registration itself stays a harmless
no-op via the same fast path as section 8. This duplication is a real,
if minor, inefficiency (doubled memory for the imported ruleset, doubled
selector-matching work for every element in this fragment) rather than a
correctness bug — worth a follow-up fix, but out of scope for this plan,
which only asserts it doesn't break rendering.

---

## 10. Fragment-scoped `local()` alias (Chapters 11–12)

**Goal:** Confirm that a `src: local(...)` alias mapping is scoped to the
DocFragment that declares it, the same way embedded-file faces are (Chapters
1–5), so that two different fragments can map the **same** family name to
**different** local targets without either leaking into the other.

Chapter 11 declares `@font-face { font-family: "OrderingTestAliasScope";
src: local("FreeSerif"); }`. Chapter 12 declares the same family name,
`src: local("FreeSans")`. Both FreeSerif and FreeSans ship with KOReader, so
`local()` resolution should succeed in both chapters independently of any
one book's embedded fonts.

| # | Action | Expected |
|---|--------|----------|
| 10.1 | View Chapter 11, Test 11 line | Renders in FreeSerif (a serif face) despite its CSS fallback family being sans-serif |
| 10.2 | View Chapter 12, Test 12 line | Renders in FreeSans (a sans-serif face) despite its CSS fallback family being serif |
| 10.3 | Compare Test 11 and Test 12 | The two lines must look visibly different from each other (serif vs. sans-serif) |
| 10.4 | View Chapter 12 before Chapter 11 (out-of-order navigation) | Same results as 10.1/10.2 — order must not matter |

If Test 12 renders in FreeSerif instead of FreeSans (or vice versa for Test
11), Chapter 11's alias mapping leaked into Chapter 12 (or vice versa) — see
`resolveAlias()` in `lvfntman.cpp`: a `docFragmentIdx`-scoped alias is
supposed to be invisible outside its own DocFragment, falling back to a
document-wide (`docFragmentIdx == -1`) or global (`documentId == -1`) alias
only when no fragment-scoped match exists for the *current* fragment. Unlike
section 3's Chapter 4/5 pair, this isn't a spine-order-sensitive check —
`RegisterDocumentFontAlias()` scopes the mapping by fragment index at
registration time, not by when it's referenced — so 10.4 passing or failing
the same way as 10.1–10.3 is the expected result either way; it's included
as a sanity check, not because order is expected to matter here.

---

## 11. `@import` vs. local override precedence (fontface-import-precedence-test.epub, Chapter 1)

**Goal:** Confirm whether an `@font-face` rule reached via `@import` is
overridden by a same-family, same-(default-)weight, same-(default-)style
rule declared afterward in the *importing* file — the scenario raised in
review: "if a css `@import`s overrides.css which redefines a @font-face, I
would expect the last to win." This is section 7's tie question again (which
face wins a genuine tie), but for two rules that reach the font manager via
different routes instead of both sitting in one `<style>` block.

This uses the separate `fontface-import-precedence-test.epub` — see the
Scope section above for why. Its single chapter links `css/wrapper.css`,
which is:

```css
@import url("base.css");
@font-face {
  font-family: "OrderingTestImportPrecedence";
  src: url("../fonts/import-test-override.ttf");
}
```

`base.css` declares one rule under the same family, neither specifying
`font-weight` nor `font-style`:

```css
@font-face {
  font-family: "OrderingTestImportPrecedence";
  src: url("../fonts/import-test-base.ttf");
}
```

Both rules register at the default weight (400) and style (roman) — an exact
tie, differing only in which file's rule registers first. `base.css`'s file
is Noto Sans Regular; `wrapper.css`'s own rule (the "override," declared
after the `@import`) points at Noto Serif Bold.

| # | Action | Expected |
|---|--------|----------|
| 11.1 | View Chapter 1, Test 1 line | Renders in the embedded plain sans-serif test font (Noto Sans Regular) — `base.css`'s **imported** rule, not `wrapper.css`'s own rule declared after the `@import` |
| 11.2 | Compare Test 1 to the reference serif line | The two lines must look visibly different |

If Test 1 instead renders in Noto Serif Bold (`wrapper.css`'s own,
later-declared rule), an `@import`ed `@font-face` rule is being overridden by
a same-tie rule declared afterward in the importing file — contrary to
current behaviour, and worth confirming was deliberate before updating this
section (and the fixture's own in-page note) to match.

Current behaviour, per `LVImportStylesheetParser::Parse()` (`lvtinydom.cpp`):
while a stylesheet's leading `@import` line(s) are present, `Parse()`
recursively parses each imported file into the same destination
`LVStyleSheet` — registering that file's `@font-face` rules as a side effect
— *before* parsing the importing file's own trailing content. So the
imported rule always registers before the importing file's own rule; there
is no cascade-style "later declaration wins" step for `@font-face` at all,
only registration order. `LVFontSelector::pickBestWeight()`'s exact-tie
resolution (`lvfntman.cpp`) then picks the first-registered candidate, same
as section 7's Chapter 8 — so the imported rule wins here too, regardless of
which file's rule a reader might expect to "win" by CSS-cascade intuition.
That's current, intentional behaviour as far as this plan is concerned
(nothing in the CSS Fonts spec mandates last-wins for an exact tie among
embedded faces, `@import`ed or not), so a change here isn't automatically a
regression — but see section 7's note for the same caveat: it's a behaviour
change worth confirming was deliberate if it ever happens.

---

## 12. Manual step — `@font-face` in a styletweak

**Goal:** Confirm `@font-face` declared in a KOReader styletweak (merged
*after* the book's own user-agent stylesheet, not at the top of any file) is
available from the very first page of a book — the koreader#10604 scenario,
and the specific case raised in maintainer review about `@font-face`
positioning.

This cannot be packaged into the test EPUB itself — styletweaks are applied
by KOReader at the Lua layer, independent of any one book.

| # | Action | Expected |
|---|--------|----------|
| 12.1 | Create a custom styletweak containing:<br>`@font-face { font-family: "StyletweakTestFont"; src: local("FreeSans"); }`<br>`body { font-family: "StyletweakTestFont", serif; }` | Styletweak saved without error |
| 12.2 | Enable the styletweak, open `fontface-test.epub` (or any book) | **All** body text, starting from the very first page, renders in FreeSans — not the default serif, and not a fallback that only kicks in after the first page |
| 12.3 | Disable the styletweak, reopen the same book | Text reverts to the normal default font |

If 12.2 shows the first page in the wrong font while later pages are
correct, that's the ordering bug this whole plan exists to catch,
manifesting via the styletweak path specifically rather than the
EPUB-internal path covered by Chapters 1–5 above.

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
- Re-run section 7 (Chapter 8) after any change to
  `LVFontSelector::pickBestWeight()` or `matchFamily()` in `lvfntman.cpp` —
  these determine tie-break order among same-family, same-fragment faces.
- Re-run section 8 (Chapter 9) after any change to `RegisterDocumentFont()`'s
  fast path (`addDocFragmentToFacesForFile()`) or `tryRegisterFace()` in
  `lvfntman.cpp`.
- Re-run section 9 (Chapter 10) after any change to
  `LVImportStylesheetParser::Parse()` or `LVStyleSheet::merge()` — these
  determine both the (currently absent) dedup of repeated imports and the
  correctness of the resulting merged ruleset.
- Re-run section 10 (Chapters 11–12) after any change to
  `RegisterDocumentFontAlias()` or `resolveAlias()` in `lvfntman.cpp` — these
  determine per-DocFragment alias scoping.
- Re-run section 11 (`fontface-import-precedence-test.epub`) after any change
  to `LVImportStylesheetParser::Parse()` (`lvtinydom.cpp`) or
  `LVFontSelector::pickBestWeight()` (`lvfntman.cpp`) — these determine
  whether an imported rule still registers, and still wins an exact tie,
  before a same-family/weight/style rule declared later in the importing
  file.
- This plan only exercises the EPUB path. The ordering-invariant audit found
  a separate, pre-existing, unrelated gap in the CHM `DocFragment` path —
  out of scope here, tracked separately.

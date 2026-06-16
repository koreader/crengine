# Font Manager Refactor — Test Plan

## Scope

This plan covers manual and visual testing of the font manager refactor
(branch `refactor-font-manager`).  The refactor replaces `LVFontCache`/`CalcMatch`-based
font selection with an explicit `LVFontRegistry` + `LVFontSelector` +
`LVFontInstanceCache` architecture.

The test EPUB is at `tests/font-manager-test.epub`.  Regenerate it with
`python3 tests/make_test_epub.py` if needed.

---

## 1. Basic font rendering

**Goal:** Confirm fonts load and render correctly for a normal book.

| # | Action | Expected |
|---|--------|----------|
| 1.1 | Open any existing book in KOReader | Text renders without crashes or blank pages |
| 1.2 | Open `font-manager-test.epub` | All seven chapters display with visible text |
| 1.3 | Scroll through all chapters | No missing-glyph boxes on Latin text |

---

## 2. Font selection — weight (Chapter 1)

**Goal:** Verify weight matching and CSS4 arbitrary weights.

| # | Action | Expected |
|---|--------|----------|
| 2.1 | View Chapter 1 with a static font (e.g. Noto Serif) | Weights 100–900 show a clear visual progression from light to heavy |
| 2.2 | View Chapter 1 with a variable font (e.g. Literata) | Weights 100–900 show a smooth continuous progression; no sudden jumps |
| 2.3 | CSS4 intermediate weights 550 and 650 | **Not yet implemented** — the CSS parser only recognises the literal "100".."900" keywords, so the declaration is ignored entirely (not rounded) and the element renders at its inherited weight (400/normal) |
| 2.4 | Weight 700 on a variable font with wght range [300,700] | Renders at the font's maximum design weight — **no synthetic bold applied** |
| 2.5 | Weight 900 on a variable font with wght range [300,700] | Renders at the font's maximum (700), not synthetically emboldened |

---

## 3. Italic and bold synthesis (Chapter 2)

**Goal:** Verify italic selection, synthesis, and bold-italic combinations.

| # | Action | Expected |
|---|--------|----------|
| 3.1 | Normal roman text | No italic slant applied |
| 3.2 | Italic text with a font that has a real italic face | Uses the real italic face (no synthesis slant artefact) |
| 3.3 | Bold italic text | Uses bold italic face if available; otherwise synthesises from the appropriate base |
| 3.4 | Bold italic on a variable font with ital axis | Uses ital axis to produce italic; wght axis for bold — no software synthesis |
| 3.5 | CSS4 intermediate weight + italic (e.g. 550, italic) | **Not yet implemented** — the `font-weight` declaration is ignored entirely (not rounded); the element renders at its inherited weight (400/normal) but stays italic |
| 3.6 | `font-synthesis: none` | **Not yet implemented** — both synthesis-allowed and synthesis-none lines render identically |

---

## 4. Optical sizing (Chapter 3)

**Goal:** Verify opsz axis is applied correctly to variable fonts.

| # | Action | Expected |
|---|--------|----------|
| 4.1 | `font-optical-sizing: auto` at small size | Glyphs appear slightly heavier/wider than a mechanical scale (if font supports opsz) |
| 4.2 | `font-optical-sizing: none` at small size | Glyphs are a pure scale of the default design |
| 4.3 | `font-optical-sizing: auto` at large size | Glyphs appear lighter/narrower than at small size (if font supports opsz) |
| 4.4 | Static font (no opsz axis) | auto and none appear identical |

---

## 5. Font stretch (Chapter 4) — Not yet implemented

**Note:** The `font-stretch` CSS property is not yet parsed. All lines in
Chapter 4 render at normal width regardless of the declared stretch value.
This section records the intended behaviour for when it is implemented.

| # | Action | Expected (when implemented) |
|---|--------|----------|
| 5.1 | `font-stretch: condensed` on a variable font with wdth axis | Text visibly narrower than normal |
| 5.2 | `font-stretch: expanded` on a variable font with wdth axis | Text visibly wider than normal |
| 5.3 | `font-stretch` keywords on a static font (no wdth axis) | Synthesis applies horizontal scale; text visibly narrower/wider |
| 5.4 | Baseline paragraph at bottom of chapter | Shows normal roman/sans/mono — confirms stretch does not affect unstyled text |

---

## 6. Fallback fonts (Chapter 5)

**Goal:** Verify the fallback font mechanism for non-Latin scripts.

| # | Action | Expected |
|---|--------|----------|
| 6.1 | View Chapter 5 | CJK, Arabic, Devanagari, Bengali all render with recognisable glyphs (not boxes) |
| 6.2 | Mixed-script paragraph | Latin uses primary font; other scripts use appropriate fallback fonts |
| 6.3 | Greek and Cyrillic | Render correctly (usually within primary font coverage) |
| 6.4 | Disable one fallback font in settings | The script covered by that font now shows boxes; re-enabling restores glyphs |

---

## 7. CSS font-family list (Chapter 6)

**Goal:** Verify that a comma-separated `font-family` list is searched in
order and that the second entry is used when the first is absent.

| # | Action | Expected |
|---|--------|----------|
| 7.1 | Line C: `font-family: 'Droid Sans Mono', serif` | Renders in monospace — identical to line A |
| 7.2 | Line D: `font-family: 'ZZZNotInstalled', 'Droid Sans Mono', serif` | Renders in monospace — identical to lines A and C; second entry used because first is absent |
| 7.3 | Line E: `font-family: 'ZZZNotInstalled1', 'ZZZNotInstalled2', serif` | Renders in proportional reading font — identical to line B; falls through to generic family |
| 7.4 | Lines A/C/D are visually identical | Confirms first-entry and second-entry paths both resolve to the same monospace face |
| 7.5 | Lines B/E are visually identical | Confirms generic-family fallback is reached correctly when all named entries are absent |

---

## 8. Font picker — font list

**Goal:** Verify `getFaceList()` / `GetFontCount()` are correct after migration.

| # | Action | Expected |
|---|--------|----------|
| 8.1 | Open the font picker | All installed fonts appear in the list |
| 8.2 | Font names are correctly cased | Names show title-case (e.g. "Noto Serif", not "noto serif") |
| 8.3 | Count of fonts listed | Matches the number of font families registered at startup |
| 8.4 | No duplicate entries | Each family appears once |

---

## 9. Font change mid-session

**Goal:** Verify `SetAsPreferredFontWithBias` and `_instance_cache` invalidation.

| # | Action | Expected |
|---|--------|----------|
| 9.1 | Change document font in settings while book is open | New font applies immediately on re-render |
| 9.2 | Change to a font with different weight coverage | Weight rendering updates to match new font's capabilities |
| 9.3 | Change back to the original font | Original rendering restored |
| 9.4 | Change font with CSS-quoted name (e.g. `"Literata"`) | Font found correctly — quote stripping in `findFamily()` works |

---

## 10. Mode changes

**Goal:** Verify `SetAntialiasMode`, `SetHintingMode`, `SetKerningMode` propagate
to loaded instances via `_instance_cache.forEachFont()`.

| # | Action | Expected |
|---|--------|----------|
| 10.1 | Toggle antialiasing in settings | Text rendering changes immediately without reload |
| 10.2 | Toggle hinting mode | Glyph shapes adjust immediately |
| 10.3 | Toggle kerning mode | Letter spacing adjusts immediately |

---

## 11. Document-embedded fonts (EPUB @font-face)

**Goal:** Verify document fonts are registered, scoped, and cleaned up.

| # | Action | Expected |
|---|--------|----------|
| 11.1 | Open an EPUB that embeds custom fonts | Embedded fonts render on the pages that declare them |
| 11.2 | Close the document | Embedded fonts removed from registry (`removeFonts()` called) |
| 11.3 | Open a different document | No embedded fonts from the previous document appear |
| 11.4 | Two documents with same embedded font name | Each uses its own scoped instance |

---

## 12. Cache stability (reopening books)

**Goal:** Verify `GetFontListHash()` is stable and the rendering cache validates
correctly across sessions.

| # | Action | Expected |
|---|--------|----------|
| 12.1 | Open a book, close, reopen | Reopens from cache without full re-render (fast open) |
| 12.2 | Install a new font while the app is closed, then reopen | Font list hash changes → cache invalidated → book re-renders cleanly |
| 12.3 | Uninstall a font while the app is closed, then reopen | Same as above — cache invalidated cleanly |

---

## 13. Variable font specifics

**Goal:** Targeted tests for variable font axis handling.

| # | Action | Expected |
|---|--------|----------|
| 13.1 | Variable font, weight within axis range | Axis value applied; no synthesis |
| 13.2 | Variable font, weight above axis max | Renders at axis max; no synthetic bold over-darken |
| 13.3 | Variable font, weight below axis min | Renders at axis min; no synthetic thinning |
| 13.4 | Variable font with ital axis, italic requested | ital=1 applied via axis; no software slant |
| 13.5 | Variable font with slnt axis (no ital), italic requested | slnt=-12 applied; no software slant |
| 13.6 | Variable font with opsz axis | opsz value derived from size and DPI; varies between sizes |
| 13.7 | Variable font, multiple axes simultaneously | wght + opsz + ital all set correctly in one instance |

---

## 14. Issue regressions (Chapter 7)

**Goal:** Verify fixed bugs from issue history do not regress.

| # | Issue | Regression | Verification |
|---|-------|-----------|--------------|
| 14.1 | koreader#8791 | Spurious document-wide italic | Body text line must be roman; classed italic line must be slanted — they must look different |
| 14.2 | koreader#8306 | Unicode smart quotes corrupt surrounding text | Smart quote characters in Chapter 7 must render correctly with no mojibake |
| 14.3 | koreader#11771 | Ruby annotation shifted left by epub-text-align-last | Annotation must be centred horizontally above base characters; adjacent text must not shift. A vertical gap between annotation and base is a known crengine rendering characteristic and is not part of this regression. |
| 14.4 | koreader#10040 / koreader#12525 | @font-face numeric font-weight ignored | **Cannot be tested from a static EPUB** — the bug affects @font-face registration of embedded fonts, not CSS font-weight on elements. Requires an EPUB with embedded fonts declaring numeric font-weight in @font-face. |

---

## 15. Regression — font manager refactor

These were bugs found during the refactor; verify they do not regress.

| # | Regression | Verification |
|---|-----------|--------------|
| 15.1 | Literata displays as italic by default | Open a book using Literata; roman text should not be slanted |
| 15.2 | Noto Sans displays as bold by default | Open a book using Noto Sans; normal weight text should not be emboldened |
| 15.3 | All text uses monospace font | Open any book; confirm proportional fonts render proportionally |
| 15.4 | Font change mid-session has no effect | Change font, verify new font applies |
| 15.5 | Font names in picker are all lowercase | Open picker; names should be correctly cased |
| 15.6 | Variable font weight 700 gets synthetic bold | View Chapter 1 with Literata at weight 700; should not appear heavier than the axis max |
| 15.7 | Bold italic on Noto Sans shows as normal italic | View Chapter 2; bold italic should be visually heavier than normal italic |
| 15.8 | CSS font-family list second entry never used | View Chapter 6; lines C and D must render identically in monospace |

---

## Test environment notes

- Test with both a **static font family** (e.g. Noto Serif — separate Regular/Bold/Italic/BoldItalic files) and a **variable font** (e.g. Literata — single file with wght and opsz axes).
- The emulator screen DPI affects opsz values; test optical sizing on a device with `gRenderDPI >= 100`.
- For fallback font tests (Chapter 5), the CJK, Arabic, and South Asian fallback fonts must be installed; confirm via the fallback font setting before testing.
- For font-family list tests (Chapter 6), Droid Sans Mono must be installed (it is KOReader's default monospace font).

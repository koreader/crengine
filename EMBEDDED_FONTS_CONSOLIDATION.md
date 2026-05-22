# Embedded Font Handling — Proposed Consolidation

> **Repository:** [koreader/crengine](https://github.com/koreader/crengine)
> **Status: For consideration — not yet approved or implemented.**

---

## Context

crengine's CSS parser (`lvstsheet.cpp`) is actively maintained and handles a
wide range of CSS properties, selectors, and at-rules.  It is structurally
sound — the work needed there is additive (new properties, better coverage)
rather than architectural.

`@font-face` is the one significant gap in the CSS parser's coverage.  It is
explicitly skipped, with a comment pointing to a separate hand-written parser
in `epubfmt.cpp` that was written before the main CSS parser could absorb it.
That separation has aged poorly: the two parsers have diverged in capability,
and the workarounds required to compensate have become a source of bugs and
complexity.

This document proposes consolidating `@font-face` handling into the main CSS
parser — closing the gap in `lvstsheet.cpp`, removing the workarounds in
`epubfmt.cpp`, and fixing two known bugs as direct consequences.  It is not a
redesign of the CSS parser; it is bringing one missing feature into the system
that was always the right home for it.

A practical benefit of this approach is that `@font-face` rules in KOReader
styletweaks would work automatically, since styletweaks are processed by the
same CSS parser.  This allows users to reference locally-installed fonts by
a CSS family name without embedding them in an EPUB — a capability that
currently requires workarounds.

---

## Motivation

The current embedded font handling has two structural problems.

### Two-pass architecture with an expensive fallback

`epubfmt.cpp` contains a hand-written `EmbeddedFontStyleParser` state machine
that runs as a pre-pass over all CSS files listed in the OPF manifest before DOM
construction begins.  This pre-pass exists so that embedded fonts are registered
before `initNodeStyleRecursive` applies styles to the DOM.

However, `@font-face` rules can also appear in `<head><style>` blocks within
individual spine XHTML files.  These are not reachable by the pre-pass because
they are only available after each spine item's HTML is parsed.  A post-scan
runs after full DOM construction and, if it finds new fonts, calls
`forceReinitStyles()` — a full style re-initialisation pass — to ensure the
newly registered fonts are used.

```
Current flow:
  OPF manifest → pre-scan CSS files → registerEmbeddedFonts()
  ↓
  Build DOM (collect <head><style> text per spine item)
  ↓
  Post-scan <head><style> → if new fonts: forceReinitStyles() + re-register
  ↓
  initNodeStyleRecursive (parseStyleSheet per DocFragment)
  ↓
  Layout
```

The `forceReinitStyles()` path is acknowledged in the code with a `printf`
and a `// todo: we could avoid forceReinitStyles() when embedded fonts are
disabled` comment.  It is a workaround rather than an architectural choice.

### Separate, lower-quality parser

`EmbeddedFontStyleParser` is ~280 lines of hand-written character-by-character
state machine, separate from the main CSS parser in `lvstsheet.cpp`.  It has
known gaps versus the CSS `@font-face` spec:

- `font-weight` numeric values (100–900) are not parsed — only the keyword
  `"bold"` is recognised.  A declaration `font-weight: 100` silently registers
  the face at weight 400.  This is the root cause of
  [koreader#10040](https://github.com/koreader/koreader/issues/10040).
- `@font-face` in styletweaks and non-EPUB CSS is never processed, because
  `lvstsheet.cpp` explicitly skips `@font-face` blocks with the comment
  *"already quickly parsed in epubfmt.cpp, ignored at this point"*.  This is
  the root cause of
  [koreader#10604](https://github.com/koreader/koreader/issues/10604).
- `unicode-range`, `font-stretch`, and `font-display` descriptors are ignored.
- Multiple `src:` fallback entries are handled but only the first valid URL is
  used; `local()` vs `url()` distinction is partially handled.

---

## Proposed Architecture

Fonts are registered on first encounter as `@font-face` rules are parsed,
inline within the existing CSS processing pipeline.  No pre-scan is required
and no style reinitialisation is triggered.

```
Proposed flow:
  Build DOM
  ↓
  initNodeStyleRecursive:
    per DocFragment → parseStyleSheet() → @font-face → RegisterDocumentFont
  ↓
  Layout
```


### Why single-pass is sufficient

`parseStyleSheet` is called at the very start of each DocFragment's
initialisation in `initNodeStyleRecursive`, before any of that DocFragment's
body elements are styled.  A font declared in a shared external stylesheet is
registered on the first DocFragment that links to it and is available to all
subsequent DocFragments via the stylesheet cache.  A font declared in a spine
item's `<head><style>` block is registered before that item's body content is
styled.  There is no valid scenario in which a font is needed before its
`@font-face` declaration has been processed: a font referenced in spine item
N but declared only in spine item M where M > N would be a malformed EPUB.

### Stylesheet cache provides cross-DocFragment efficiency

The existing `StyleSheetCache` (keyed by CSS file path) means that a shared
external stylesheet referenced by ten DocFragments is parsed once.  `@font-face`
registration occurs on the first parse; subsequent DocFragments use the cached
result and skip re-parsing.  `hasFaceId()` in `tryRegisterFace()` makes
repeated registration of identical declarations idempotent.

For `<head><style>` blocks that repeat identical content across spine items
(uncommon but valid), duplicate registration is detected cheaply via `hasFaceId()`
and discarded without re-opening the font file.

### LVEmbeddedFontList and cache serialisation

`LVEmbeddedFontList` is the serialisable record of embedded fonts written to the
document cache (`CBT_FONT_DATA`).  On re-open from cache it is deserialised and
`registerEmbeddedFonts()` restores the font registry without any CSS parsing.

With the unified parser, `RegisterDocumentFont` appends each registered font to
`LVEmbeddedFontList` as a side-effect.  By the time `saveChanges()` serialises
the list, all DocFragments have been processed and the list is complete.  The
cache re-open path is unchanged.

### Unused CSS files

The pre-scan processes all CSS files listed in the OPF manifest, including files
not linked by any spine item.  The per-DocFragment approach processes only files
actually reachable from linked stylesheets and `@import` chains.  For a valid
EPUB these sets should be equivalent; for malformed EPUBs with orphaned CSS files
the per-DocFragment approach is more correct (it does not register unreachable
fonts).  Additionally, files reachable only via `@import` (not listed in the
manifest) are handled correctly by the per-DocFragment approach but are missed
by the current pre-scan.

---

## What Is Removed

| Removed | Replaced by |
|---------|-------------|
| `EmbeddedFontStyleParser` class (~280 lines) | `@font-face` parsing in `lvstsheet.cpp` |
| Pre-scan loop that opens and parses CSS files from the OPF manifest searching for `@font-face` rules | `@font-face` parsed inline by `lvstsheet.cpp` during normal CSS processing — no manifest pre-scan needed |
| Post-scan of `<head><style>` with `forceReinitStyles()` | Eliminated — timing handled naturally |
| `LVEmbeddedFontList::set()` bulk assignment | Incremental append via `RegisterDocumentFont` side-effect |
| `epubfmt.cpp` `fontList_nb_before_head_parsing` guard | Eliminated |

## What Is Preserved

- `LVEmbeddedFontList` struct and serialisation — unchanged; populated
  incrementally rather than in bulk.
- `registerEmbeddedFonts()` / `unregisterEmbeddedFonts()` — unchanged; still
  called on document open/close.
- Cache re-open path — unchanged; deserialise font list, call
  `registerEmbeddedFonts()`.
- `RegisterDocumentFont` — unchanged in signature; gains `LVEmbeddedFontList`
  append as side-effect.

---

## Issues Fixed

| Issue | Cause | Fixed by |
|-------|-------|----------|
| [#10040](https://github.com/koreader/koreader/issues/10040) `font-weight` numeric values dropped | `EmbeddedFontStyleParser` only recognises `"bold"` | `lvstsheet.cpp` already parses numeric weights |
| [#10604](https://github.com/koreader/koreader/issues/10604) `@font-face` in styletweaks ignored | `@font-face` only parsed by `epubfmt.cpp` | `lvstsheet.cpp` handles all CSS sources |
| `forceReinitStyles()` on EPUBs with `<head><style>` fonts | Post-scan needed to catch late-registered fonts | Eliminated — fonts registered before body styling |
| `@font-face` in `@import`-only files missed by pre-scan | Pre-scan limited to OPF manifest entries | `parseStyleSheet` follows `@import` recursively |

---

## Significant Assumptions

**Font file discovery from OPF manifest is complete.**
All font files in a valid EPUB are listed in the OPF manifest with a recognised
font media-type.  Font files referenced by `@font-face src: url()` that are not
in the manifest are considered malformed and will fail to register.

**`parseStyleSheet` is called before body element styling for each DocFragment.**
This is a structural invariant of `initNodeStyleRecursive` — lines 9013 and 9024
of `lvtinydom.cpp` execute before child body elements are processed.  The consolidation
depends on this ordering being maintained.

**`local()` src references do not require container access.**
`local()` resolves against installed system fonts via the font manager's existing
`GetFont` path.  Only `url()` references require the EPUB container.  The
container is already available at the `parseStyleSheet` call site via
`_document->getContainer()` (line 5022 of `lvtinydom.cpp`).

**Duplicate registration is idempotent and cheap.**
`hasFaceId()` detects duplicate faces in O(total registered faces).  For the
common case where the same external CSS file is referenced by many DocFragments,
the stylesheet cache ensures `@font-face` parsing runs only once.  Inline
`<head><style>` duplicates across spine items hit the `hasFaceId()` check and
are discarded without re-opening the font binary.

**`LVEmbeddedFontList` does not need to be complete before first layout.**
The list is only read during `saveChanges()` (serialisation to cache).  Populating
it incrementally during `initNodeStyleRecursive` is correct because serialisation
occurs after rendering, by which point all DocFragments have been processed.

---

## Implementation Plan (Incremental)

### Step 1 — Add `@font-face` parsing to `lvstsheet.cpp`

- Parse `@font-face { }` blocks in `lvstsheet.cpp` instead of skipping them.
- Extract `font-family`, `font-weight` (numeric and keyword), `font-style`,
  and `src` (both `url()` and `local()`) descriptors using the existing
  tokeniser.
- Call `document->RegisterDocumentFont(...)` with the parsed values.  The
  document reference is already available to the CSS parser.
- `RegisterDocumentFont` appends to `LVEmbeddedFontList` as a new side-effect.
- Remove the `"font-face" // already quickly parsed` skip in `lvstsheet.cpp`.

**Verify:** #10604 is fixed — a `@font-face` rule in a KOReader styletweak
applies correctly.  Existing EPUBs with embedded fonts continue to render
correctly.  `EmbeddedFontStyleParser` and the new path agree on which fonts
are registered (run both in parallel temporarily if needed).

### Step 2 — Remove `EmbeddedFontStyleParser` and the pre/post-scan

- Remove the pre-scan loop and post-scan `forceReinitStyles()` block from
  `epubfmt.cpp`.
- Remove the `EmbeddedFontStyleParser` class (~280 lines).
- Remove `fontList_nb_before_head_parsing` and associated guard.

**Verify:** Embedded fonts render correctly in EPUBs that declare `@font-face`
in external CSS, in `<head><style>`, and in styletweaks.  Open an EPUB with
embedded fonts, close it, and reopen from cache — fonts must render correctly
on re-open without re-parsing CSS, confirming `LVEmbeddedFontList` is
serialised and deserialised correctly.

---

## Files Changed

| File | Change |
|------|--------|
| `crengine/src/lvstsheet.cpp` | Parse `@font-face` blocks; call `RegisterDocumentFont` |
| `crengine/src/epubfmt.cpp` | Manifest font-file discovery; remove pre/post-scan and `EmbeddedFontStyleParser` |
| `crengine/src/lvtinydom.cpp` | `RegisterDocumentFont` appends to `LVEmbeddedFontList`; `registerEmbeddedFonts()` unchanged |
| `crengine/src/lvfntman.cpp` | `RegisterDocumentFont` — no signature change; minor side-effect addition |

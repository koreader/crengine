# Fixed-layout EPUB — test plan

Fixed-layout (EPUB3 "pre-paginated") books define an exact pixel canvas per
page and position their content on it, instead of reflowing text. Think manga,
comics, children's picture books, visual dictionaries and cookbooks. crengine
is a reflow engine, so historically these render broken (everything reflows into
a single column). This feature adds real fixed-layout rendering.

The work lands in slices; this document grows with them. **Slice 1 (detection +
per-page viewport capture) is implemented and testable now.** Slices 2–4 are
scaffolded at the bottom.

## Fixtures

`make_fixedlayout_test_epub.py [out_dir]` generates a corpus that exercises every
detection path (no Pillow / external deps needed):

| File | Declares fixed-layout via | Expected detection |
|------|---------------------------|--------------------|
| `fxl_epub3_viewport.epub` | `<meta property="rendition:layout">pre-paginated`, per-page `<meta name="viewport">`, alternating page-spread | mode = 1 (ALL) |
| `fxl_mixed.epub` | global reflowable, 2 of 5 itemrefs `properties="rendition:layout-pre-paginated"` | mode = 2 (MIXED) |
| `fxl_kf8_legacy.epub` | legacy `name="fixed-layout"`, `original-resolution`, `primary-writing-mode=horizontal-rl`; no per-page viewport (OPF fallback) | mode = 1, ppd = rtl |
| `fxl_ibooks.epub` | only `META-INF/com.apple.ibooks.display-options.xml` `<option name="fixed-layout">true` | mode = 1 |
| `fxl_rtl_spread.epub` | EPUB3 pre-paginated + `<spine page-progression-direction="rtl">` + page-spreads | mode = 1, ppd = rtl |
| `reflowable_control.epub` | nothing (negative control) | mode = 0 (NONE) |
| `fxl_positioned.epub` | EPUB3 pre-paginated, pages of CSS absolutely-positioned coloured boxes | mode = 1; used by the slice-3 render test |
| `fxl_transform.epub` | EPUB3 pre-paginated, a `transform:scale()` text layer (huge coords + huge font-size scaled down), like PDFTron output | mode = 1; used by the slice-3b render test |

`mode` is `CreDocument:getFixedLayoutMode()`: 0 = reflowable, 1 = fully
pre-paginated, 2 = mixed. `ppd` is `CreDocument:getPageProgressionDirection()`.

This was also validated against real-world pre-paginated EPUBs (picture books
and visual dictionaries; not committed, as they are copyrighted). They use
standard `rendition:layout: pre-paginated`, per-page `<meta name="viewport">`,
a full-page background `<img>` and `position:absolute` text overlays.

## Slice 1 — detection + viewport capture (implemented)

What it does:
- Parses the book's global `rendition:layout`, per-itemref `properties=`
  overrides (`rendition:layout-pre-paginated` / `-reflowable`,
  `rendition:page-spread-left/right/center`), legacy KF8 hints
  (`fixed-layout`, `original-resolution`, `primary-writing-mode`), and the
  iBooks `display-options.xml` fixed-layout option.
- Captures each spine document's own `<meta name="viewport" content="width=..,
  height=..">` while the fragment is parsed (falls back to an OPF-level
  viewport / `original-resolution`).
- Stamps each fixed-layout `<DocFragment>` with `LayoutMode="pre-paginated"`,
  `Viewport="WxH"` and, when present, `PageSpread="left|right|center"`.
- Records book-level `DOC_PROP_FIXED_LAYOUT` (0/1/2) and
  `DOC_PROP_PAGE_PROGRESSION_DIRECTION`, both serialized into the doc cache so
  they survive a later cached open (when OPF parsing is skipped).

Nothing in slice 1 changes rendering yet — it only records facts.

### Automated check (KOReader)

From the KOReader checkout, after building the emulator:

    ./kodev test front fixedlayout_spec.lua

`spec/unit/fixedlayout_spec.lua` generates the fixtures above and asserts the
expected `getFixedLayoutMode()` / `getPageProgressionDirection()` for each.

### Manual check (DOM inspection)

Open any fixture (or a real fixed-layout book) and dump the DOM
(`ReaderUI → tools`, or a `crengine` build with `cre.dumpDOM`); confirm each
page's `<DocFragment>` carries `LayoutMode="pre-paginated"` and a
`Viewport="WxH"` matching its `<meta name="viewport">`. A reflowable book must
have no such attributes and `DOC_PROP_FIXED_LAYOUT = 0`.

## Slice 2 — one page per spine item + scale-to-fit (implemented)

What it does:
- `setNodeStyle()` (lvrend.cpp) sizes each pre-paginated `<DocFragment>` to its
  `Viewport` (width/height in screen px), so its content lays out in native
  viewport coordinates.
- `ldomDocument::render()` (lvtinydom.cpp), for a fully fixed-layout book,
  replaces the flow-based page list with one `RN_PAGE_TYPE_FIXED` page per
  spine item (the flag serializes into the cache).
- `LVDocView::drawPageTo()` (lvdocview.cpp), for a fixed page, renders the
  fragment into an offscreen color buffer at native viewport resolution and
  draws it (as an image source) uniformly scaled and letterboxed to the page.

All of this is gated on fixed-layout: reflowable books are untouched (verified —
`./kodev test front document` still passes).

### Visual check

Open a fixed-layout book; each spine item should be its own page, scaled to fit
the screen with letterbox bars (the reader background colour). A single
full-page image fills the page; a page whose background image is positioned with
`position:absolute; width/height:100%` will not fill it until slice 3 lands
(the image sits in the flow instead). Validated by rendering pages to PNG with a
headless harness (solid-colour fixtures and real pre-paginated books).

## Slice 3 — CSS positioned layout (implemented)

What it does:
- Adds real CSS `position` (`static`/`relative`/`absolute`/`fixed`, `sticky`
  handled as `relative`) plus `top`/`right`/`bottom`/`left` to the engine
  (cssdef.h, lvstyles.h/.cpp, lvstsheet.cpp). These are parsed and stored for
  every document but only *acted on* for fully pre-paginated books, so
  reflowable rendering is unchanged.
- After normal flow has run, `renderFixedLayoutPositioning()` (lvrend.cpp) walks
  each pre-paginated `<DocFragment>` and rewrites the `RenderRectAccessor`
  (x/y/width/height) of every positioned element so it lands where CSS puts it
  on the page canvas (the viewport is the initial containing block; a positioned
  ancestor becomes the containing block for its absolute descendants). It uses
  the fact that `DrawDocument()` accumulates each node's offset down the tree, so
  only each node's offset relative to its parent needs to change.
- `setNodeStyle()` blockifies an inline element that is `position:absolute/fixed`
  (so it gets a block box the pass can move), and sizes images from their
  `width`/`height` attributes without the page-box cap, so a full-page
  background image fills the viewport.
- `LVDocView::drawPageTo()` renders only the fragment's own subtree into an
  offscreen buffer sized to the **viewport attribute** (not the flow-rendered
  box, which for a pre-paginated item is unreliable), then scales it to the page.

Not yet handled, as documented follow-ups:
- CSS `transform: scale()` — now implemented in **slice 3b** (below).
- `z-index` paint ordering (elements paint in DOM order, which is correct when
  z-index is absent or matches DOM order, as in these books).
- An in-flow sibling reclaiming the space of an out-of-flow absolute sibling
  (FXL content is almost always fully positioned, so this is seldom visible).
- Device-DPI independence of the fixed canvas (offsets resolve consistently with
  the content at the emulator's 96 DPI; honoring arbitrary `gRenderDPI` for the
  viewport box is shared with slice 2).

### Automated check (KOReader)

    ./kodev test front fxl_positioned

`spec/unit/fxl_positioned_spec.lua` renders `fxl_positioned.epub` (coloured
absolutely-positioned boxes at known coordinates) at its native viewport size,
so the page maps 1:1 to the output buffer, and asserts each box's pixels:
`top`/`left` anchoring, `bottom`/`right` anchoring, `width`/`height` in px and
`%`, a box nested inside another positioned box (placed relative to its
ancestor), and that an uncovered point keeps the page background.

### Visual check

Open a positioned fixed-layout book (e.g. a picture book): each
page's full-page background image fills the screen, scaled and letterboxed, with
no gap or half-fill. Validated by rendering pages to PNG with a headless harness.

## Slice 3b — CSS transform:scale() text layer (implemented)

PDFTron-generated fixed-layout books (picture books, visual
dictionaries) do not bake their text into the page image. Instead each
page has a full-page background `<img>` plus a "text layer": a container that is
scaled right down (`transform: scale(~0.03125)`, `transform-origin: left bottom`)
whose text is laid out in the un-scaled (≈32×) coordinate system — spans at
`left:2336px; bottom:29137px; font-size:1024px`. Without transform support that
text is formatted ~30000px off-canvas and clipped away, so slice 3 rendered the
background art correctly but the text was absent.

What it does:
- Adds the CSS `transform` property, parsing `transform: scale(sx[, sy])` (other
  transform functions and `transform-origin` are accepted and ignored). The
  scale is stored on the style (cssdef.h / lvstyles.h/.cpp / lvstsheet.cpp) for
  every document but only *acted on* for fully pre-paginated books.
- In `setNodeStyle()` (lvrend.cpp), for a fixed-layout element, folds the
  cumulative **ancestor** transform scale into the element's own font-size and all
  the px lengths that live in the scaled coordinate system — the offsets and size
  (`left/right/top/bottom/width/height`), and the text-layout lengths
  (`letter-spacing`, `text-indent`, `line-height`, `margin`, `padding`) — before
  the font is built and the content laid out. So the text is formatted at its
  final on-page size, and the existing slice-3 positioning pass (which reads the
  now-scaled offsets) places it correctly. An element's own transform is not
  applied to its own box; it only scales its descendants (each picks up the
  cumulative scale by walking its ancestors). The scale is stored in 16.16
  fixed-point: it multiplies coordinates up to ~25000px (one real-world book uses
  `scale(0.045327)`), where a coarser 1/256 representation would drift the text by
  tens of pixels.
- A fixed-layout book may rely on a **negative inline margin** to kern text —
  PDFTron output lays out a multi-digit page number as a run with a big
  `letter-spacing` spacer and pulls the digits back together with a negative
  `margin-left` on a nested span. crengine normally ignores negative inline
  margins ("No support for any negative value" in lvtextfm); for a fixed-layout
  book we honour them (lvrend adds the inline pad, lvtextfm keeps the negative
  value), so the digits land adjacent. Reflowable rendering is unchanged. `transform-origin` needs no explicit handling: the
  positioning pass anchors `left`/`top` against the container's left/top edge and
  `right`/`bottom` against its right/bottom edge, which reproduces a transform
  origin at any box corner — and these generators always pair the origin with the
  matching offsets (`origin:left bottom` ⇒ `left`/`bottom` offsets).
- Font-size is folded only when specified as a literal (absolute) size; an
  `em`/`%`/inherited size already carries the ancestor scale through its parent's
  (folded) font and must not be scaled twice.
- The positioning pass also anchors a fixed-layout absolute box with **auto**
  offsets at its containing block's origin (not the unreliable flow position), so
  a full-page text layer / background image lands at the page origin.

Not yet handled, as documented follow-ups:
- Non-uniform `scale(sx, sy)` stretches glyphs; text is folded by the vertical
  scale (crengine has one font size per run), so only uniform scale is exact.
- `transform-origin` other than a box corner, and transform functions other than
  `scale` (`translate`, `rotate`, `matrix`): not interpreted. In some real-world
  books this means rotated text (e.g. dense rotated index text previewed on some
  thumbnails) renders upright instead of rotated. The main per-section labels,
  which use the scaled text layer, render correctly.

### Automated check (KOReader)

    ./kodev test front fxl_transform

`spec/unit/fxl_transform_spec.lua` renders `fxl_transform.epub` at its native
viewport size (1:1) and asserts: coloured boxes whose huge `left`/`bottom` and
`width`/`height` are scaled by `scale(0.025)` land at their expected pixels
(anchored by the left-bottom origin); a text box whose `font-size:1280px` is
scaled to 32px is a short band low on the page — specifically that the middle of
the page stays the white background, which it would not if the font-size had not
been scaled (the box would be ~1280px tall); and an inline "kern" row of three
coloured boxes checks the margin handling — the box pushed right by a big
`margin-left` lands on-page (so the margin was folded, not left at ~12000px) and
the box pulled back by a negative `margin-left` lands left of it (so the negative
inline margin was honoured). The scale 0.025 is deliberately not a multiple of
1/256, so a scale stored at only 1/256 precision would misplace the high-`bottom`
green box by ~70px and fail the test.

### Visual check

Open a PDFTron-generated fixed-layout book (a picture book or a visual
dictionary): the selectable text now renders over the background art at
the right size and place. Validated by rendering pages to PNG with a headless
harness on the real-world test books, including a worst-case page of
11028 positioned spans.

## Slice 4a — KOReader frontend adaptation (implemented)

Pure KOReader (Lua) changes; no crengine code involved.

- **RTL page turns**: a book whose spine declares `page-progression-direction=
  "rtl"` (e.g. manga) defaults to right-to-left page turns for a fresh book
  (`ReaderView:onPreRenderDocument` sets `inverse_reading_order`), unless the
  reader already made a per-book choice.
- **Hide reflow-only settings** for fully fixed-layout books (font, CSS render
  style, typography/hyphenation, style tweaks): they have no effect, so their
  menus are hidden. Mixed-layout and reflowable books keep them.

### Automated check (KOReader)

`spec/unit/fxl_frontend_spec.lua` (5 tests, via a real `ReaderUI`): RTL default
on for `fxl_rtl_spread.epub`, off for `fxl_positioned.epub`; reflow menus absent
for a fully-fixed book, present for `fxl_mixed.epub` and a reflowable book.

## Slice 4b — two-page spreads (implemented)

In 2-page (landscape) mode crengine's `LVDocView::Draw` already renders two
facing pages into the left/right screen halves, and the fixed-layout branch of
`drawPageTo()` honours the half-rect it is given — so fixed-layout spreads work
once visible-page-count is 2 (driven by the existing KOReader setting; no new
frontend code). The one fix: for a **right-to-left** fixed-layout book the two
pages are placed with the current page on the **right** and the next on the
**left**, so the spread reads right-to-left and faces line up. Reflowable 2-page
mode is unchanged (the swap is gated on `RN_PAGE_TYPE_FIXED`). No cache/
formatting version bump (placement is decided at draw time, not serialized).

### Automated check (KOReader)

`spec/unit/fxl_spread_spec.lua` (rendered at 1600x1200): LTR
(`fxl_epub3_viewport.epub`) places page 1 on the left half, page 2 on the right;
RTL (`fxl_rtl_spread.epub`) places page 1 on the right half, page 2 on the left.

## Slice 4c — spread alignment (page-spread + lone cover) (implemented)

Each fixed page's `page-spread-left`/`page-spread-right`/`page-spread-center`
(from the spine itemref, both the prefixed `rendition:` form and the default
unprefixed form) is honoured, so a lone cover stands alone and the following
pages line up as facing-page spreads. `LVDocView::getFixedLayoutSpread()` groups
the pages into spreads (reading-direction aware), and both `goToPage()` (which
now aligns a fixed-layout view to a spread start) and `Draw()` (which places a
spread's pages on their physical halves, a standalone on its side) consult it.
Reflowable books are unaffected (gated on `RN_PAGE_TYPE_FIXED`). The cache
format version is bumped, because the page-spread side is stamped at import time
and cached opens must re-import to pick up the parser fix above.

### Automated check (KOReader)

`spec/unit/fxl_spread_spec.lua` also opens `fxl_cover_spread.epub` (a lone cover
then left/right pairs): the cover view shows the cover alone with a blank half,
and the next views are the real spreads page 2 | page 3 and page 4 | page 5.

Not yet handled, as a documented follow-up:
- **Several consecutive standalone pages** at the front (cover + title + …) can
  mis-step, as page turns advance by a fixed two pages; a single lone cover (the
  common case) is correct.

## Viewport-less SVG cover/title pages (implemented)

Some cover/title pages have no `<meta name="viewport">` and declare their canvas
with an `<svg width="100%" height="100%" viewBox="0 0 W H">` wrapping a single
full-page `<image>`. Two fixes (both in `setNodeStyle`, gated to fully
fixed-layout) make such a page fill its canvas: the fragment is sized from the
`<svg>`'s `viewBox` when no viewport meta is present, and a full-page `<svg>` is
laid out as a block at its exact viewBox pixel size with no margins (otherwise it
is an inline replaced element, sized to its DPI-scaled intrinsic size, and ends
up inset with white margins).

### Automated check (KOReader)

`spec/unit/fxl_svg_viewport_spec.lua` opens `fxl_svg_viewport.epub` (pages with
no viewport meta, an `<svg viewBox>` around a solid-colour full-page image) and
asserts all four corners of the rendered page are the page colour — i.e. the
image fills the page edge to edge rather than being letterboxed.

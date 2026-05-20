# Font Manager Refactor — Architecture & Implementation Plan

## Motivation

The current font manager has accumulated significant complexity over time:

- `LVFontCache` mixes registered font descriptors and loaded instances in the same
  structure, making every lookup ambiguous about what it is returning.
- `cache.find()` uses `CalcMatch()` to score every entry in the cache against every
  request, conflating font selection (which font?) with instance lookup (do we have
  this face loaded?).
- `CalcMatch()` encodes font selection rules as a scoring function whose behaviour
  emerges from interactions between a dozen numeric weights and special-case guards
  (`_real_weight`, `italic=2`, `useBias`, the 257 score, the +1 tie-breaker) rather
  than being stated explicitly.
- `GetFont()` makes 3–4 sequential cache lookups with progressively modified keys to
  work around the above, resulting in ~200 lines of logic that are hard to reason about.

The refactor separates these concerns explicitly and replaces the implicit scoring rules
with readable CSS Fonts Level 4 selection logic.

---

## Target Architecture

### Three distinct concerns

```
┌─────────────────────┐     ┌──────────────────┐     ┌───────────────────────┐
│   LVFontRegistry    │────▶│  LVFontSelector  │────▶│  LVFontInstanceCache  │
│  (what fonts exist) │     │  (which to use)  │     │  (loaded instances)   │
└─────────────────────┘     └──────────────────┘     └───────────────────────┘
```

### LVFontFace

One instance per physical font face. Replaces the registered-font role of `LVFontDef`.

```cpp
struct LVFontFace {
    lString8           file;
    int                face_index;
    int                weight;          // design weight (100–1000)
    bool               is_italic;
    css_font_family_t  family;          // monospace etc.
    bool               has_emojis;
    bool               has_ot_math;
    // Variable font axis ranges (min==max==def for static fonts)
    bool  _has_wght; float _wght_min, _wght_def, _wght_max;
    bool  _has_opsz; float _opsz_min, _opsz_def, _opsz_max;
    bool  _has_ital; float _ital_min,  _ital_def, _ital_max;
    bool  _has_slnt; float _slnt_min,  _slnt_def, _slnt_max;
    bool  _has_wdth; float _wdth_min,  _wdth_def, _wdth_max;
    // Document-embedded fonts
    int            documentId;
    LVByteArrayRef buf;

    bool     isVariable() const { return _has_wght && _wght_min < _wght_max; }
    lUInt32  id() const;  // stable hash of (file, face_index, documentId)
};
```

Static and variable fonts share the same struct. For static fonts all axis ranges have
min==max==def. This gives the selector a uniform representation and naturally supports
font families with more than 4 faces.

### LVFontFamily

Groups all faces under one name.

```cpp
class LVFontFamily {
    lString8             _name;
    LVArray<LVFontFace>  _faces;
public:
    void addFace(const LVFontFace&);
    const LVArray<LVFontFace>& faces() const;
};
```

### LVFontSynthesis

Explicit synthesis decision. Part of the cache key, so two instances with different
synthesis (e.g. with and without `font-synthesis: none`) coexist correctly.

```cpp
struct LVFontSynthesis {
    bool  bold;           // FT_EMBOLDEN or LVFontBoldTransform
    bool  italic;         // FreeType slant transform
    float width_scale;    // 1.0 = none; implements font-stretch synthesis

    bool    none()   const { return !bold && !italic && width_scale == 1.0f; }
    bool    operator==(const LVFontSynthesis&) const;
    lUInt32 hash()   const;
};
```

Adding small-caps synthesis in future is a one-field extension with no structural change.
The `font-synthesis` CSS property gates which fields the selector is allowed to set.

### LVFontRegistry

Registered faces only — no instances.

```cpp
class LVFontRegistry {
public:
    void               registerFace(const LVFontFace&);
    void               registerAlias(lString8 alias, lString8 canonical);
    const LVFontFamily* findFamily(lString8 name, int documentId = -1) const;
    void               removeFonts(int documentId);      // embedded font cleanup
    lUInt32            getHash(int documentId) const;    // for GetFontListHash()
private:
    LVHashTable<lString8, LVFontFamily>  _families;  // keyed by lowercase name
    LVHashTable<lString8, lString8>      _aliases;
};
```

### LVFontSelector

Pure function — no cache access, no loading.

```cpp
struct LVFontMatch {
    const LVFontFace*  face;        // nullptr if no candidates found
    LVFontSynthesis    synthesis;
    LVFontVariations   variations;  // effective axes after selection
};

class LVFontSelector {
public:
    LVFontMatch select(int weight, bool italic,
                       css_font_family_t family,
                       const lString8&   typeface,
                       int               documentId,
                       const LVFontVariations& requested,
                       const LVFontRegistry&   registry,
                       const lString8&   preferred_family) const;
private:
    const LVFontFace* pickFace(const LVFontFamily&,
                                int weight, bool italic) const;
    LVFontSynthesis   computeSynthesis(const LVFontFace&,
                                       int weight, bool italic) const;
    LVFontVariations  effectiveVariations(const LVFontFace&,
                                          const LVFontVariations&) const;
};
```

`pickFace()` implements CSS Fonts Level 4 §9 selection explicitly:
1. Filter candidates by italic preference; if none, use all (synthesis will cover the gap).
2. Apply the spec weight tiebreak: for weight < 400, try lower then higher; for weight
   > 500, try higher then lower; for 400–500, try in range first.

This replaces `CalcMatch()` entirely and is reviewable against the spec line by line.
The `useBias` mechanism becomes `preferred_family` — a string prepended to the lookup
rather than a scoring trick baked into cache entries.

### LVFontInstanceKey and LVFontInstanceCache

```cpp
struct LVFontInstanceKey {
    lUInt32          face_id;        // from LVFontFace::id()
    int              size;           // requested pixel size
    int              face_size;      // actual loaded size (monospace scaling)
    LVFontSynthesis  synthesis;
    lUInt32          features_hash;
    lUInt32          variations_hash;

    bool    operator==(const LVFontInstanceKey&) const;
    lUInt32 hash() const;
};

class LVFontInstanceCache {
public:
    LVFontRef get(const LVFontInstanceKey&) const;
    void      put(const LVFontInstanceKey&, LVFontRef);
    void      evict(lUInt32 face_id);   // embedded font cleanup
    void      gc();
private:
    LVHashTable<LVFontInstanceKey, LVFontRef> _table;
};
```

Lookup is an exact hash-map hit. No scoring, no ambiguity about whether a registered
entry or an instance is being returned.

Cache key notes:
- `face_id` encodes which physical file was selected; weight of static fonts is implicit.
- `synthesis` separates emboldened from non-emboldened instances of the same face.
- Multiple weight requests resolving to the same static face with the same synthesis
  share one cache entry — no duplication.
- Synthesis-from-synthesis is architecturally impossible: the selector reads from
  `LVFontRegistry` (physical faces only), never from the instance cache.

### GetFont() after refactor

```cpp
LVFontRef GetFont(int size, int weight, bool italic,
                  css_font_family_t family, lString8 typeface,
                  int features, int documentId, bool useBias,
                  const LVFontVariations* variations)
{
    // 1. Select face + synthesis
    LVFontVariations req_var = variations ? *variations : LVFontVariations();
    lString8 preferred = useBias ? _preferred_family : lString8();
    LVFontMatch m = _selector.select(weight, italic, family, typeface,
                                     documentId, req_var,
                                     _registry, preferred);
    if (!m.face) return LVFontRef(NULL);

    // 2. Build cache key
    int face_size = computeFaceSize(size, m.face->family);  // monospace scaling
    LVFontInstanceKey key {
        m.face->id(), size, face_size,
        m.synthesis,
        (lUInt32)features,
        m.variations.hash()
    };

    // 3. Cache hit
    LVFontRef cached = _instance_cache.get(key);
    if (!cached.isNull()) return cached;

    // 4. Load, synthesise, cache
    return loadAndCache(*m.face, size, face_size,
                        m.synthesis, features, m.variations, key);
}
```

### RegisterFont() after refactor

```cpp
bool RegisterFont(lString8 file, ...)
{
    for each face in file:
        LVFontFace f = inspectFace(file, index);
        // inspectFace reads weight, italic, family, axis ranges,
        // emoji/OT-math flags from the FreeType face.
        _registry.registerFace(f);
    return true;
}
```

Duplicate detection: check whether a face with the same `id()` is already registered.
`CalcDuplicateMatch()` is removed.

---

## What Is Removed

| Removed | Replaced by |
|---------|-------------|
| `LVFontDef` | `LVFontFace` (registry) + `LVFontInstanceKey` (cache) |
| `LVFontCacheItem` | `LVFontInstanceCache` entries |
| `LVFontCache` | `LVFontRegistry` + `LVFontInstanceCache` |
| `CalcMatch()` | `LVFontSelector::pickFace()` |
| `CalcDuplicateMatch()` | `face.id()` uniqueness check |
| `CalcFallbackMatch()` | Same selector, separate fallback family list |
| `useBias` scoring trick | `preferred_family` string passed to selector |
| `_real_weight` guard | Cache key encodes synthesis; synthesis-from-synthesis impossible |
| `italic=2` in CalcMatch | `LVFontSynthesis.italic` flag in key |
| 257 variable-font score | Selector prefers exact axis match over nearest-static naturally |
| Multi-phase GetFont lookups | Single select → lookup → load |

---

## What Is Preserved

- `LVFreeTypeFace` — the FreeType/HarfBuzz rendering class is unchanged. Changes stop
  at the boundary between selection and instantiation.
- `LVFontManager` public virtual API — same interface for callers (lvrend, lvdocview).
- CSS parsing (lvstsheet.cpp) — unchanged.
- All variable-font axis logic (opsz injection, ital/slnt handling) moves cleanly into
  `LVFontSelector::effectiveVariations()` and `computeSynthesis()`.

---

## OpenType Features

The current `int features` (32-bit bitmap) is a separate improvement opportunity.
Replacing it with an open-ended `LVFontFeatureSet` (vector of `hb_feature_t`) would:
- Remove the 32-feature limit
- Support numeric feature values (not just on/off)
- Pass through directly to HarfBuzz without the bitmap translation layer

This is compatible with the refactor but is a separate step. For now `features` remains
as `int` in the cache key.

---

## CSS Compliance Improvements

| Property | Current | After refactor |
|----------|---------|----------------|
| Font matching algorithm | CalcMatch scoring | CSS Fonts Level 4 §9 steps, explicit |
| Weight tiebreak | +1 hack for lower weight | Spec rules: direction depends on requested weight |
| `font-synthesis` | Not honoured | Selector gates synthesis per property |
| Italic vs oblique | Not distinguished | Separate handling possible |
| `font-feature-settings` | 32-bit bitmap | Open-ended (future step) |
| Family list fallback | CalcMatch side-channel | Explicit outer loop in selector |
| >4 faces per family | Not supported | Free: selector iterates all candidates |

---

## Known Boundary Cases

1. **Family name fragmentation** — fonts that split weights across multiple metadata
   family names appear as separate families. Requires CSS `@font-face` or a
   normalisation pass; not addressed in this refactor.

2. **Document-embedded font scoping** — `documentId` must be explicit in both the
   registry and the cache key to prevent cross-document contamination.

3. **Font list changes between sessions** — `face_id` should include a hash of the
   registration-time metadata so changes are detected on next open.

4. **Alias fonts** — `SetAlias()` registers a second name for an existing face.
   `LVFontRegistry` tracks aliases explicitly and resolves them at lookup time.

5. **Width synthesis and layout metrics** — `width_scale != 1.0` changes advance widths
   and must be included in the document rendering hash, not just the font instance hash.

6. **Monospace size scaling** — `face_size` may differ from `size`; both must be in the
   cache key.

---

## Implementation Plan (Incremental)

Each step leaves the codebase in a working state.

### Step 1 — Introduce LVFontFace and LVFontRegistry alongside existing cache ✓ DONE

- Add `LVFontFace`, `LVFontFamily`, `LVFontRegistry` as new types in `lvfntman.cpp`.
- Make `RegisterFont()` and `SetAlias()` write to both `_registry` and the existing
  `_cache`. The cache remains the authoritative path for `GetFont()`.
- Verify: all existing font registration tests pass; both structures contain the same
  fonts.

**As-built notes:**

- Three additional typed axis accessors were added to `LVFontDef`
  (`hasOpszAxis/getOpszAxisMin/Max`, `hasWdthAxis/getWdthAxisMin/Max`) to support
  `faceFromDef()`. The generic `getAxisMin(tag)` / `getAxisMax(tag)` were previously
  removed as unused; the typed variants are consistent with the existing
  `getWghtAxisMin/Max` pattern.

- `faceFromDef()` sets `_opsz_def = _opsz_min` (and similarly for ital, slnt, wdth)
  as a placeholder. For non-variable fonts min==max==def so this is correct; for
  variable fonts the real def comes from the FreeType axis descriptor (accessible at
  registration time but not currently stored separately in `LVFontDef`). This can be
  improved in a later step by reading `mm_var->axis[i].def` directly.

- The FontConfig registration path (Linux) was also wired up alongside the file-based
  and in-memory paths.

- `_preferred_family` (lString8) was added to `LVFreeTypeFontManager` alongside
  `_registry` as a placeholder for the `useBias` replacement in Step 5.

**Test EPUB:** `tests/font-manager-test.epub` — covers weight progression (100–900,
CSS4 intermediates 550/650), italic/bold-italic, font-synthesis, optical sizing
(auto vs none), and font-stretch keywords. Baseline regression samples on the
final page confirm default roman/sans/mono rendering is unchanged.

### Step 2 — Implement LVFontSelector as a standalone function ✓ DONE

- Add `LVFontSelector` with `select()` using CSS Fonts Level 4 §9 logic.
- Wire up a parallel path in `GetFont()` that calls the selector and logs its result
  alongside the existing CalcMatch result. Assert they agree.
- Fix any discrepancies; this surfaces edge cases safely.

**As-built notes:**

- `familyAt(int i)` added to `LVFontRegistry` to support generic-family fallback
  iteration in the selector.

- `LVFontSynthesis` was defined here (not in a separate step as originally planned),
  immediately before `LVFontMatch` which depends on it. It holds `bold` (bool),
  `italic` (bool), and `width_scale` (float, 1.0 = none).

- `LVFontMatch` carries the selected face, a `LVFontSynthesis`, and the effective
  `LVFontVariations` (axes to apply at instantiation time).

- The selector's four-tier fallback mirrors the current CalcMatch priority:
  (1) exact typeface name → (2) user preferred family → (3) generic family type
  (monospace/serif/sans-serif) → (4) any registered face.

- Parallel verification logs `CRLog::warn()` mismatches unconditionally (no special
  build flag needed). The comparison is skipped for instantiated cache entries since
  only registered entries have a directly comparable file+index. Any mismatch printed
  during `tests/font-manager-test.epub` review should be investigated before Step 4.

- `LVFontVariations` default constructor leaves all axes unset, so the `LVFontMatch`
  default constructor correctly produces an empty variations set.

**Test:** Open `tests/font-manager-test.epub` and check the KOReader log for any
`FontSelector mismatch` warnings. Expected: zero warnings for standard system fonts.

### Step 3 — Introduce LVFontSynthesis and LVFontInstanceCache ✓ DONE

- Add `LVFontSynthesis` and `LVFontInstanceKey`.
- Add `LVFontInstanceCache` populated alongside the existing cache.
- In `GetFont()`, check the new cache first; fall back to existing path if missed.
- Verify: hit rates are as expected; no stale instances returned.

**As-built notes:**

- `LVFontSynthesis` was already added in Step 2; Step 3 adds `LVFontInstanceKey`
  and `LVFontInstanceCache`.

- `LVFontInstanceKey` includes `requested_weight` (the original pre-snapping weight)
  alongside the snapped `variations_hash`. This is necessary because wght snapping
  maps both a w=400 and a w=700 request to the same 400-weight face, producing
  identical `variations_hash` values — but the w=700 request requires bold synthesis
  while the w=400 does not. Without `requested_weight` in the key, the instance cache
  would return the non-synthesised entry for the synthesised request (confirmed bug).
  `requested_weight` is a temporary stand-in; it will be replaced by a proper
  `LVFontSynthesis` field in Step 4.

- The instance cache check is placed AFTER all existing early-return paths (block 1,
  slnt translation, existing-instance return). Those paths are fast already (they
  hit `_cache` directly). The instance cache speeds up the main load path — the
  one that would otherwise call `loadFromFile`/`loadFromBuffer`. This placement will
  move earlier in Step 4.

- `LVFontInstanceCache::clear()` is called from `UnregisterDocumentFonts()` since
  per-face eviction is not yet implemented. This is conservative but correct; it
  also clears global font instances (wasteful but safe). Per-face eviction using
  `face_id` will be wired up in Step 4 or 5.

- `makeFaceId()` is a free static helper that produces a stable hash of
  (file, face_index, documentId) from a `LVFontDef*`, matching `LVFontFace::id()`.

**Test:** Open `tests/font-manager-test.epub`. With debug logging, verify that
repeated font requests within the document produce instance-cache hits (log
`FontSelector mismatch` count should remain zero).

### Step 4 — Switch GetFont() to the new path

- Replace `GetFont()` body with the three-step select → lookup → load flow.
- Remove the multi-phase lookup blocks.
- Keep `LVFontCache` alive temporarily for `GetFallbackFont()` and `GetFontList()`.

### Step 5 — Migrate fallback font and font list APIs

- Replace `GetFallbackFont()` with a selector call over the fallback family list.
- Replace `GetFontList()` / `GetFontListHash()` with registry-based equivalents.
- Remove `LVFontCache`.

### Step 6 — Remove LVFontDef and CalcMatch

- Delete `LVFontDef`, `LVFontCacheItem`, `LVFontCache`.
- Delete `CalcMatch()`, `CalcDuplicateMatch()`, `CalcFallbackMatch()`.
- Clean up any remaining references.

### Step 7 — OpenType features (separate, future)

- Replace `int features` with `LVFontFeatureSet`.
- Update CSS parsing, style hashing, and cache key.
- Cache version bump.

---

## Files Changed

| File | Change |
|------|--------|
| `crengine/src/lvfntman.cpp` | Core rewrite across steps 1–6 |
| `crengine/include/lvfntman.h` | New public types; `LVFontManager` interface unchanged |
| `crengine/src/lvstyles.cpp` | `calcHash(font_ref_t)` — likely unchanged |
| `crengine/src/lvrend.cpp` | `getFont()` call site — unchanged externally |
| `crengine/src/lvdocview.cpp` | `SetAsPreferredFontWithBias` → `SetPreferredFamily` |
| `crengine/src/lvtinydom.cpp` | Cache version bump at step 4 |

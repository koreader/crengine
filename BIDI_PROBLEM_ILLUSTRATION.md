# BiDi Text Selection Problem - Visual Illustration

## The Problem Explained Visually

### Example 1: Simple Mixed LTR/RTL Text

**Logical Text Order (HTML source):**
```
"Hello עברית World"
 ^^^^^  ^^^^^^ ^^^^^
 LTR    RTL    LTR
```

**Visual Rendering (on screen):**
```
Hello תירבע World
^^^^^       ^^^^^
LTR         LTR
      ^^^^^^
      RTL (rendered right-to-left)
```

**Current getSegmentRects() Behavior:**
```
[----Rect1----]         [----Rect3----]
Hello                   World
              (missing)
```
❌ **Problem**: The RTL segment "תירבע" is MISSING from the rects because the character-by-character iteration follows logical order, not visual order.

**Expected Behavior:**
```
[----Rect1----][--Rect2--][----Rect3----]
Hello          תירבע      World
```
✅ **Solution**: All three visual segments should be captured in the rects.

### Example 2: Logical Text with Visual Reordering

**Logical Order in HTML:**
```
Position:  0    5    10   15   20   25
Text:      "English ABC مرحبا DEF more text"
           ^^^^^^^ ^^^ ^^^^^ ^^^ ^^^^^^^^^
           LTR     LTR RTL   LTR LTR
```

**Visual Rendering (BiDi algorithm applied):**
```
Position:  0    5   8      13  16   20   25
Text:      English ABC FED ابحرم more text
           ^^^^^^^ ^^^ ^^^ ^^^^^ ^^^^^^^^^
           LTR     LTR LTR RTL   LTR
```

Note how "DEF" becomes "FED" and "مرحبا" becomes "ابحرم" (reversed) and they swap positions!

**Current Character-by-Char Iteration:**
```
Step 1: E-n-g-l-i-s-h → builds rect at visual position 0-7
Step 2: (space) → skipped
Step 3: A-B-C → builds rect at visual position 8-11
Step 4: (space) → skipped
Step 5: م-ر-ح-ب-ا → tries to build rect, but these characters
                     are rendered right-to-left at position 16-21
                     and in reverse visual order!
                     Current code gets confused, might build
                     incomplete or wrong rect
Step 6: (space) → skipped
Step 7: D-E-F → tries position 13-16, but these are rendered
                before the Arabic text!
Step 8: m-o-r-e... → continues at position 22+
```

❌ **Problem**: Steps 5-7 don't work correctly because:
- Characters are iterated in logical order (م then ر then ح...)
- But they appear in visual order (ا then ب then ح... - reversed!)
- The "DEF" appears BEFORE "مرحبا" visually, but AFTER it logically

**Word-Based Visual Order Processing:**
```
Line detected as BiDi: LTEXT_LINE_IS_BIDI flag set

Words in visual order (formatted_word_t array):
Word 0: "English" at x=0, width=50, src_text_index=0
Word 1: "ABC" at x=58, width=25, src_text_index=1  
Word 2: "FED" at x=90, width=25, src_text_index=3 (note: src index 3!)
Word 3: "ابحرم" at x=120, width=40, src_text_index=2 (note: src index 2!)
Word 4: "more" at x=165, width=30, src_text_index=4
Word 5: "text" at x=200, width=30, src_text_index=5

Build rects by checking which words belong to our range:
- If range includes src_text_index 0-5
- Process words in VISUAL order (0,58,90,120,165,200)
- Group consecutive visual positions into rects
```

✅ **Solution**: All words captured correctly in visual order!

### Example 3: Selection Within BiDi Text

**User selects text (visually selects from "ABC" to "more"):**

**Visual Selection:**
```
English [ABC FED ابحرم more] text
        ^---selection---^
```

**Logical Range:**
```
Text nodes in range:
- "ABC" (LTR) - src_index 1
- "مرحبا" (RTL) - src_index 2  
- "DEF" (LTR) - src_index 3
- "more" (LTR) - src_index 4
```

**Current getSegmentRects() Result:**
```
[Rect at ABC position]
(gap - missing RTL!)
(gap - missing DEF!)
[Rect at "more" position]
```

**Correct Result (word-based):**
```
Process line as BiDi:
- Collect words: src_index in {1,2,3,4}
- Word 1: "ABC" at x=58
- Word 2: "FED" at x=90  
- Word 3: "ابحرم" at x=120
- Word 4: "more" at x=165

Sort by visual position (already sorted):
Build rects from visual segments:
[Rect1: x=58 to x=115] covers "ABC FED"
[Rect2: x=120 to x=195] covers "ابحرم more"

(or single rect if threshold allows):
[Rect: x=58 to x=195] covers entire selection
```

## Key Data Structures

### formatted_line_t (per line)
```cpp
struct formatted_line_t {
    int y;                    // vertical position
    int height;               // line height
    int flags;                // includes LTEXT_LINE_IS_BIDI
    int word_count;           // number of words on this line
    formatted_word_t* words;  // array of words in VISUAL order
    // ... other fields
};
```

### formatted_word_t (per word/segment)
```cpp
struct formatted_word_t {
    int x;                 // horizontal position (VISUAL)
    int width;             // visual width
    int src_text_index;    // maps back to source text node (LOGICAL)
    int flags;             // includes LTEXT_WORD_DIRECTION_IS_RTL
    struct {
        int start;         // start offset in source text
        int len;           // length in source text
    } t;
    // ... other fields
};
```

## The Mapping Problem

**Current Approach (Character-by-Character):**
```
Logical Iteration → Character Positions → getRectEx() → Visual Rect

Problem: getRectEx() on individual chars in BiDi text may return:
- Correct positions for LTR chars
- Reversed positions for RTL chars  
- BUT: chars iterated in logical order don't correspond to visual order
- Result: gaps and missing segments
```

**Proposed Approach (Word-Based for BiDi):**
```
Range Nodes → Map to src_text_indices → Find words with those indices
            → Words already in VISUAL order → Build rects by visual position

Advantage: formatted_word_t array already has:
- Visual positions (x, width)
- Mapping to source (src_text_index)
- Direction flags (RTL/LTR)
```

## Summary

The core issue is that `getSegmentRects()` iterates through text in **logical order** (how it appears in the HTML/document tree), but BiDi text is rendered in **visual order** (how it appears on screen). 

The formatted text structures (`formatted_line_t` and `formatted_word_t`) already contain the visual order information, but `getSegmentRects()` doesn't use them. Instead, it tries to build rects character-by-character in logical order, which breaks down when logical and visual order differ.

The solution is to:
1. Detect BiDi lines using `LTEXT_LINE_IS_BIDI` flag
2. Access the formatted text structures
3. Process words in visual order (as they're already ordered in `formatted_word_t` array)
4. Build rects from the visual positions of words that belong to the range

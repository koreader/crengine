# BiDi Text Selection - Solution Ideas

## Problem Summary

`ldomXRange::getSegmentRects()` takes shortcuts when building selection rectangles:
1. When end of range is on same line, extends from start rect to end rect (skips all chars between)
2. When full text node is on one line, extends from start to end of node (skips all chars)
3. When iterating char-by-char, extends previous rect instead of building individual char rects

These shortcuts assume visual order = logical order, which breaks for BiDi text where characters from the same text node can appear in non-contiguous visual positions.

## Idea 1: Extend getRectEx() to Return BiDi Information

Enhance `ldomXPointer::getRectEx()` to provide additional context about BiDi rendering:

### Option A: Add output parameter to getRectEx()

```cpp
// In lvtinydom.h
bool getRectEx(lvRect & rect, bool adjusted=false, int * bidiFlags=NULL) const;

// BiDi flags that can be returned:
#define BIDI_FLAG_NONE           0x00  // Not in BiDi line
#define BIDI_FLAG_IN_BIDI_LINE   0x01  // Character is in a BiDi line
#define BIDI_FLAG_IS_RTL         0x02  // Character is in RTL word/segment
#define BIDI_FLAG_AT_VISUAL_EDGE 0x04  // Character is at edge where direction changes
```

**Usage in getSegmentRects():**
```cpp
int bidiFlags = BIDI_FLAG_NONE;
if (!curPos.getRectEx(curCharRect, true, &bidiFlags)) {
    // handle error...
}

if (bidiFlags & BIDI_FLAG_IN_BIDI_LINE) {
    // Don't take shortcuts - need to process char-by-char
    // and check each char's BiDi context
}
```

**Advantages:**
- Minimal API change (optional parameter, backward compatible)
- Provides just enough info to make decisions
- No need to access formatted text structures from getSegmentRects()

**Challenges:**
- Need to thread BiDi flag info through getRectEx() implementation
- getRectEx() already accesses formatted text internally, so this is extracting existing info

### Option B: Add separate query method

```cpp
// In lvtinydom.h
bool getRectEx(lvRect & rect, bool adjusted=false) const;  // unchanged
bool isInBidiContext() const;  // new method
bool isInRTLWord() const;      // new method
```

**Usage in getSegmentRects():**
```cpp
if (!curPos.getRectEx(curCharRect, true)) {
    // handle error...
}

if (curPos.isInBidiContext()) {
    // Don't take shortcuts
}
```

**Advantages:**
- Cleaner API separation
- Can query BiDi context independently of rect retrieval

**Challenges:**
- Multiple calls might be less efficient
- Need to ensure BiDi context queries use same state as rect retrieval

### Option C: Return BiDi info via reference parameter

```cpp
// In lvtinydom.h
struct RectContext {
    bool inBidiLine;
    bool isRTL;
    bool atVisualEdge;
};

bool getRectEx(lvRect & rect, bool adjusted=false, RectContext * context=NULL) const;
```

**Usage in getSegmentRects():**
```cpp
RectContext ctx;
if (!curPos.getRectEx(curCharRect, true, &ctx)) {
    // handle error...
}

if (ctx.inBidiLine) {
    // Don't take shortcuts
}
```

**Advantages:**
- Structured data, extensible
- Single call gets all context

**Challenges:**
- More complex API
- Need to define and maintain struct

## Idea 2: Use BiDi Info in getSegmentRects() to Avoid Shortcuts

Once we have BiDi information from getRectEx(), modify getSegmentRects() logic:

### Change 1: Skip shortcut when end is on same line (lines 12513-12518)

**Current code:**
```cpp
if (curCharRect.top == nodeStartRect.top) {
    // (Two offsets in a same text node with the same tops are on the same line)
    lineStartRect.extend(curCharRect);
    break; // SHORTCUT: assume start-to-end forms contiguous rect
}
```

**Modified code:**
```cpp
int startBidiFlags = BIDI_FLAG_NONE;
int endBidiFlags = BIDI_FLAG_NONE;
curPos.setOffset(startOffset);
curPos.getRectEx(nodeStartRect, true, &startBidiFlags);
curPos.setOffset(rangeEnd.getOffset() - 1);
curPos.getRectEx(curCharRect, true, &endBidiFlags);

if (curCharRect.top == nodeStartRect.top) {
    if ((startBidiFlags | endBidiFlags) & BIDI_FLAG_IN_BIDI_LINE) {
        // In BiDi line: cannot take shortcut, need to iterate chars
        // Fall through to char-by-char iteration (section 3)
    } else {
        // Not BiDi: safe to take shortcut
        lineStartRect.extend(curCharRect);
        break;
    }
}
```

### Change 2: Skip shortcut for full node on one line (lines 12531-12537)

**Current code:**
```cpp
if (curCharRect.top == nodeStartRect.top) {
    // Extend line up to the end of this node
    lineStartRect.extend(curCharRect);
    nodeStartRect = lvRect();
    go_on = curPos.nextText();
    continue; // SHORTCUT: assume node forms contiguous rect
}
```

**Modified code:**
```cpp
int startBidiFlags = BIDI_FLAG_NONE;
int endBidiFlags = BIDI_FLAG_NONE;
curPos.setOffset(startOffset);
curPos.getRectEx(nodeStartRect, true, &startBidiFlags);
curPos.setOffset(textLen-1);
curPos.getRectEx(curCharRect, true, &endBidiFlags);

if (curCharRect.top == nodeStartRect.top) {
    if ((startBidiFlags | endBidiFlags) & BIDI_FLAG_IN_BIDI_LINE) {
        // In BiDi line: cannot take shortcut, need char-by-char
        // Fall through to section 3
    } else {
        // Not BiDi: safe to take shortcut
        lineStartRect.extend(curCharRect);
        nodeStartRect = lvRect();
        go_on = curPos.nextText();
        continue;
    }
}
```

### Change 3: Enhanced char-by-char iteration (lines 12540-12580)

**Current code:**
```cpp
for (int i=startOffset+1; i<=textLen-1; i++) {
    // ...get curCharRect...
    if (curCharRect.top != nodeStartRect.top) {
        // Different line: extend prevCharRect and start new segment
        lineStartRect.extend(prevCharRect);
        rects.add(lineStartRect);
        nodeStartRect = curCharRect;
        lineStartRect = lvRect();
        break;
    }
    prevCharRect = curCharRect;  // ASSUMPTION: chars are contiguous on line
}
```

**Modified code:**
```cpp
int prevBidiFlags = BIDI_FLAG_NONE;
int curBidiFlags = BIDI_FLAG_NONE;
bool inBidiLine = false;

curPos.setOffset(startOffset);
curPos.getRectEx(prevCharRect, true, &prevBidiFlags);
inBidiLine = (prevBidiFlags & BIDI_FLAG_IN_BIDI_LINE) != 0;

for (int i=startOffset+1; i<=textLen-1; i++) {
    // ...skip spaces...
    
    if (!curPos.getRectEx(curCharRect, true, &curBidiFlags)) {
        continue;
    }
    
    if (curCharRect.top != nodeStartRect.top) {
        // Different line
        if (!prevCharRect.isEmpty()) {
            lineStartRect.extend(prevCharRect);
            rects.add(lineStartRect);
        }
        nodeStartRect = curCharRect;
        lineStartRect = lvRect();
        prevBidiFlags = curBidiFlags;
        inBidiLine = (curBidiFlags & BIDI_FLAG_IN_BIDI_LINE) != 0;
        break;
    }
    
    // Check if chars are visually contiguous
    if (inBidiLine) {
        // In BiDi line: check if current char continues previous segment
        bool isContiguous = false;
        
        if ((prevBidiFlags & BIDI_FLAG_IS_RTL) == (curBidiFlags & BIDI_FLAG_IS_RTL)) {
            // Same direction: check if visually adjacent
            if (prevBidiFlags & BIDI_FLAG_IS_RTL) {
                // RTL: current char should be to the LEFT of previous
                isContiguous = (curCharRect.right <= prevCharRect.left + BIDI_GAP_THRESHOLD);
            } else {
                // LTR: current char should be to the RIGHT of previous
                isContiguous = (curCharRect.left >= prevCharRect.right - BIDI_GAP_THRESHOLD &&
                                curCharRect.left <= prevCharRect.right + BIDI_GAP_THRESHOLD);
            }
        }
        
        if (!isContiguous) {
            // Start new segment
            if (!lineStartRect.isEmpty()) {
                rects.add(lineStartRect);
            }
            lineStartRect = curCharRect;
        } else {
            // Extend current segment
            lineStartRect.extend(curCharRect);
        }
    } else {
        // Not BiDi: use simple extend (existing logic)
        lineStartRect.extend(curCharRect);
    }
    
    prevCharRect = curCharRect;
    prevBidiFlags = curBidiFlags;
}
```

## Recommended Approach

**Option A (BiDi flags parameter) + Enhanced char-by-char iteration**

### Implementation Steps:

1. **Modify `ldomXPointer::getRectEx()` signature:**
   ```cpp
   bool getRectEx(lvRect & rect, bool adjusted=false, int * bidiFlags=NULL) const {
       return getRect(rect, true, adjusted, bidiFlags);
   }
   
   bool getRect(lvRect & rect, bool extended=false, bool adjusted=false, 
                int * bidiFlags=NULL) const;
   ```

2. **In `ldomXPointer::getRect()` implementation (around lines 9912-10489):**
   - When accessing formatted_line_t, check `line->flags & LTEXT_LINE_IS_BIDI`
   - When accessing formatted_word_t, check `word->flags & LTEXT_WORD_DIRECTION_IS_RTL`
   - If bidiFlags parameter is not NULL, populate it with:
     ```cpp
     if (bidiFlags) {
         *bidiFlags = BIDI_FLAG_NONE;
         if (line_is_bidi)
             *bidiFlags |= BIDI_FLAG_IN_BIDI_LINE;
         if (word_is_rtl)
             *bidiFlags |= BIDI_FLAG_IS_RTL;
     }
     ```

3. **In `ldomXRange::getSegmentRects()`:**
   - Add BiDi flags tracking variables
   - Check BiDi flags before taking shortcuts (sections 1 & 2)
   - Enhanced char-by-char loop that checks visual continuity for BiDi lines
   - Only create new segment when chars are not visually contiguous

### Benefits:

✅ **Minimal changes to API** - optional parameter, backward compatible
✅ **No architectural changes** - stays within rect-based approach
✅ **Leverages existing BiDi info** - getRectEx already has access to formatted text
✅ **Precise control** - getSegmentRects decides when to take shortcuts vs iterate
✅ **Efficient** - only affects BiDi lines, non-BiDi keeps existing optimizations

### Constants to Define:

```cpp
// In lvtinydom.cpp or lvtextfm.h
#define BIDI_FLAG_NONE           0x00
#define BIDI_FLAG_IN_BIDI_LINE   0x01
#define BIDI_FLAG_IS_RTL         0x02
#define BIDI_FLAG_AT_VISUAL_EDGE 0x04  // Reserved for future use

#define BIDI_GAP_THRESHOLD       3  // pixels - max gap to consider chars contiguous
```

## Testing Strategy

1. **Regression tests** - ensure non-BiDi text still works with shortcuts
2. **Pure RTL text** - test with Arabic/Hebrew text
3. **Mixed LTR/RTL on same line** - test "English עברית more English"
4. **Multiple BiDi lines** - test selection spanning several lines
5. **Edge cases** - partial word selection, inline images, nested elements

## Alternative: Simpler Heuristic Approach

If full BiDi flag propagation is too complex, could use simpler heuristic:

```cpp
// In getSegmentRects(), before taking shortcut:
bool mightBeBidi = false;
lString32 text = curPos.getText();
for (int j = 0; j < text.length() && !mightBeBidi; j++) {
    lChar32 ch = text[j];
    // Check for RTL characters (Hebrew, Arabic, etc.)
    mightBeBidi = (ch >= 0x0590 && ch <= 0x08FF) ||
                  (ch >= 0xFB1D && ch <= 0xFEFF);
}

if (mightBeBidi) {
    // Don't take shortcuts - do char-by-char
} else {
    // Safe to take shortcuts
}
```

**Advantages:**
- No API changes to getRectEx()
- Very simple implementation

**Disadvantages:**
- Less precise (may avoid shortcuts unnecessarily)
- Doesn't know actual visual direction of each character
- Can't detect visual contiguity in char-by-char loop

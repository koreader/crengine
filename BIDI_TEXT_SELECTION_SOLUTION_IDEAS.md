# BiDi Text Selection - Detailed Solution Ideas

## Problem Analysis

The `ldomXRange::getSegmentRects()` function in `crengine/src/lvtinydom.cpp` (lines 12362-12586) does not properly handle text selection for bidirectional (BiDi) text with mixed LTR (Left-To-Right) and RTL (Right-To-Left) segments.

### Current Implementation Issues

1. **Logical Order Processing**: The function iterates through text nodes character-by-character in logical (HTML source) order
2. **Missing BiDi Detection**: No check for `LTEXT_LINE_IS_BIDI` flag that indicates mixed directionality
3. **Visual vs Logical Mismatch**: In BiDi text, visual rendering order differs from logical source order
4. **Incomplete Rects**: Visual segments that are part of the selection may be skipped

### How getRectEx() Handles BiDi Correctly

The `ldomXPointer::getRectEx()` function (lines 10095-10327) already contains proper BiDi handling:

```cpp
// Lines 10083-10095
bool line_is_bidi = frmline->flags & LTEXT_LINE_IS_BIDI;

if (line_is_bidi) {
    // Special BiDi handling:
    // - Track nearestForwardSrcIndex for non-linear word search
    // - Use bestBidiRect as fallback for word reordering
    // - Process words in visual order from formatted_word_t structures
}
```

## Solution Ideas

### Idea 1: Refactor and Reuse BiDi Logic from getRectEx()

**Approach**: Extract common BiDi handling logic into helper functions that both `getRectEx()` and `getSegmentRects()` can use.

**Implementation Steps**:

1. **Create Helper Function for BiDi Line Processing**:
```cpp
// New helper function to extract words in visual order for a range
void collectBidiWordsForRange(
    LFormattedTextRef txtform,
    ldomNode* startNode, int startOffset,
    ldomNode* endNode, int endOffset,
    LVArray<lvRect>& rects,
    lvRect& finalNodeRect
);
```

2. **Modify getSegmentRects() Structure**:
```cpp
void ldomXRange::getSegmentRects(LVArray<lvRect>& rects, bool includeImages) {
    // Get final node and formatted text (similar to getRectEx)
    ldomNode* finalNode = getFinalNodeForRange();
    if (!finalNode) {
        // Fall back to current char-by-char logic
        return;
    }
    
    LFormattedTextRef txtform;
    // Render and get formatted text
    // (Need to determine how to get RenderRectAccessor and inner_width)
    
    // Check each line in the range
    for (int l = 0; l < txtform->GetLineCount(); l++) {
        const formatted_line_t* frmline = txtform->GetLineInfo(l);
        
        if (frmline->flags & LTEXT_LINE_IS_BIDI) {
            // Use word-based BiDi-aware processing
            processBidiLine(frmline, txtform, rects);
        } else {
            // Use existing optimized char-by-char logic
            processNormalLine(frmline, txtform, rects);
        }
    }
}
```

**Advantages**:
- Reuses proven BiDi logic
- Minimal code duplication
- Clear separation of concerns

**Challenges**:
- Need to refactor getRectEx() without breaking existing functionality
- Must handle different contexts (single point vs range)

### Idea 2: Word-Based Visual Order Processing for BiDi Lines

**Approach**: When BiDi is detected, switch from character-level to word-level processing using `formatted_word_t` structures.

**Implementation Details**:

1. **Access Formatted Text Information**:
```cpp
// Get the final node containing this range
ldomNode* finalNode = getStart().getFinalNode();
if (!finalNode) {
    // Use fallback to current logic
    return;
}

// Get formatted text (similar to getRectEx lines 9999-10001)
lvRect rc;
finalNode->getAbsRect(rc, true);
RenderRectAccessor fmt(finalNode);
int inner_width = fmt.getInnerWidth(); // or calculate from borders/padding

LFormattedTextRef txtform;
finalNode->renderFinalBlock(txtform, &fmt, inner_width);
```

2. **Build Source Text Node Map**:
```cpp
// Map which source text indices belong to our range
std::set<int> rangeSourceIndices;
for (int i = 0; i < txtform->GetSrcCount(); i++) {
    const src_text_fragment_t* src = txtform->GetSrcInfo(i);
    if (src->object >= getStart().getNode() && 
        src->object <= getEnd().getNode()) {
        rangeSourceIndices.insert(i);
    }
}
```

3. **Process Each Line with BiDi Awareness**:
```cpp
for (int l = 0; l < txtform->GetLineCount(); l++) {
    const formatted_line_t* frmline = txtform->GetLineInfo(l);
    bool line_is_bidi = frmline->flags & LTEXT_LINE_IS_BIDI;
    
    if (line_is_bidi) {
        // Collect all words from this line that belong to our range
        LVArray<const formatted_word_t*> rangeWords;
        
        for (int w = 0; w < frmline->word_count; w++) {
            const formatted_word_t* word = &frmline->words[w];
            
            // Skip padding words
            if (word->flags & LTEXT_WORD_IS_PAD) continue;
            
            // Check if word's source belongs to our range
            if (rangeSourceIndices.count(word->src_text_index) > 0) {
                rangeWords.add(word);
            }
        }
        
        // Build rects from collected words in visual order
        buildRectsFromWords(rangeWords, frmline, rc, rects);
    }
}
```

4. **Build Rectangles from Words**:
```cpp
void buildRectsFromWords(
    LVArray<const formatted_word_t*>& words,
    const formatted_line_t* frmline,
    lvRect& finalNodeRect,
    LVArray<lvRect>& rects
) {
    // Sort words by visual position (x coordinate)
    words.sortByX();
    
    lvRect currentSegment;
    int lastRight = -1;
    
    for (int i = 0; i < words.length(); i++) {
        const formatted_word_t* word = words[i];
        int wordLeft = word->x + finalNodeRect.left + frmline->x;
        int wordRight = wordLeft + word->width;
        
        // Check if word is visually continuous with previous
        if (currentSegment.isEmpty() || wordLeft > lastRight + WORD_GAP_THRESHOLD) {
            // Start new segment
            if (!currentSegment.isEmpty()) {
                rects.add(currentSegment);
            }
            currentSegment = lvRect(
                wordLeft,
                finalNodeRect.top + frmline->y,
                wordRight,
                finalNodeRect.top + frmline->y + frmline->height
            );
        } else {
            // Extend current segment
            currentSegment.right = wordRight;
        }
        lastRight = wordRight;
    }
    
    if (!currentSegment.isEmpty()) {
        rects.add(currentSegment);
    }
}
```

**Advantages**:
- Handles visual reordering correctly
- Leverages existing formatted text information
- Clear BiDi-specific code path

**Challenges**:
- Performance overhead of accessing formatted text
- Need to handle partial word selections (range starts/ends mid-word)
- Must handle ranges spanning multiple final nodes

### Idea 3: Hybrid Character and Word Processing

**Approach**: Keep fast character-by-character logic for non-BiDi text, only switch to word-based processing when BiDi is detected.

**Implementation Strategy**:

1. **Initial Fast Path Check**:
```cpp
void ldomXRange::getSegmentRects(LVArray<lvRect>& rects, bool includeImages) {
    // Try to detect if any part of range might contain BiDi text
    bool mayContainBidi = rangeCouldContainBidi();
    
    if (!mayContainBidi) {
        // Use existing optimized character-by-character logic
        getSegmentRectsCharByChar(rects, includeImages);
        return;
    }
    
    // Use BiDi-aware word-based processing
    getSegmentRectsWordBased(rects, includeImages);
}
```

2. **BiDi Detection Heuristic**:
```cpp
bool ldomXRange::rangeCouldContainBidi() {
    // Quick check: look at text direction of nodes in range
    ldomXPointerEx pos = getStart();
    ldomXPointerEx end = getEnd();
    
    while (pos.compare(end) < 0) {
        ldomNode* node = pos.getNode();
        if (node && node->isText()) {
            // Check if text contains RTL characters
            lString32 text = node->getText();
            for (int i = 0; i < text.length(); i++) {
                lChar32 ch = text[i];
                // Hebrew: 0x0590-0x05FF, Arabic: 0x0600-0x06FF, etc.
                if ((ch >= 0x0590 && ch <= 0x08FF) ||
                    (ch >= 0xFB1D && ch <= 0xFDFF) ||
                    (ch >= 0xFE70 && ch <= 0xFEFF)) {
                    return true;
                }
            }
        }
        if (!pos.nextText()) break;
    }
    return false;
}
```

**Advantages**:
- No performance impact for common non-BiDi case
- Progressive enhancement approach
- Easier to test and validate

**Challenges**:
- Maintaining two code paths
- RTL detection heuristic may have false positives/negatives

### Idea 4: Character Position to Visual Position Mapping

**Approach**: Create a mapping from logical character positions to visual coordinates, then build rects from sorted visual positions.

**Implementation Concept**:

1. **Build Position Map**:
```cpp
struct VisualPosition {
    int logicalOffset;
    ldomNode* node;
    lvRect visualRect;
    
    bool operator<(const VisualPosition& other) const {
        if (visualRect.top != other.visualRect.top)
            return visualRect.top < other.visualRect.top;
        return visualRect.left < other.visualRect.left;
    }
};

LVArray<VisualPosition> positions;

// For each character in range, get its visual position
ldomXPointerEx pos = getStart();
while (pos.compare(getEnd()) < 0) {
    lvRect charRect;
    if (pos.getRectEx(charRect, true)) {
        VisualPosition vp;
        vp.logicalOffset = pos.getOffset();
        vp.node = pos.getNode();
        vp.visualRect = charRect;
        positions.add(vp);
    }
    pos.nextChar();
}
```

2. **Sort and Group by Visual Position**:
```cpp
// Sort by visual position (top, then left)
positions.sort();

// Group consecutive visual positions into rects
lvRect currentSegment;
for (int i = 0; i < positions.length(); i++) {
    const VisualPosition& vp = positions[i];
    
    if (shouldStartNewSegment(currentSegment, vp)) {
        if (!currentSegment.isEmpty()) {
            rects.add(currentSegment);
        }
        currentSegment = vp.visualRect;
    } else {
        currentSegment.extend(vp.visualRect);
    }
}
```

**Advantages**:
- Conceptually simple and clear
- Handles arbitrary text reordering
- Works for all BiDi cases

**Challenges**:
- Performance: O(n) getRectEx calls for every character
- Memory: Storing position for every character
- May be too slow for large selections

## Recommended Approach

**Recommendation: Idea 2 with elements of Idea 3 (Word-Based with Hybrid)**

This combines:
- Word-based visual order processing for accuracy
- Hybrid approach for performance
- Leverages existing formatted text infrastructure

### Key Implementation Points

1. **Detect BiDi lines** using `LTEXT_LINE_IS_BIDI` flag
2. **Access formatted text** via `renderFinalBlock()`
3. **Process words in visual order** using `formatted_word_t` structures
4. **Handle edge cases**: partial word selection, multiple final nodes
5. **Maintain backward compatibility** with non-BiDi text

### Testing Strategy

1. Create test cases with:
   - Pure LTR text (regression test)
   - Pure RTL text (Arabic, Hebrew)
   - Mixed LTR/RTL on same line
   - Multiple BiDi lines
   - Partial word selections in BiDi text

2. Validate:
   - All visual segments are captured
   - No duplicate rects
   - Correct rect coordinates
   - Performance acceptable

## Additional Considerations

### Performance Optimization

- Cache formatted text access within single getSegmentRects() call
- Only process lines that intersect with the range
- Consider lazy evaluation of BiDi processing

### Edge Cases to Handle

1. **Range spanning multiple final nodes**: Each final node has separate formatted text
2. **Partial word selection**: Range starts/ends in middle of word in BiDi line
3. **Images in BiDi lines**: Handle LTEXT_WORD_IS_IMAGE flag
4. **Nested inline elements**: Multiple source text fragments in same visual position
5. **Empty ranges**: Gracefully handle empty or invalid ranges

### Code Structure Suggestions

Consider creating these helper functions:

```cpp
// Check if range might contain BiDi text
bool ldomXRange::containsBidiText();

// Get formatted text for a final node
bool getFormattedTextForNode(ldomNode* finalNode, LFormattedTextRef& txtform);

// Build rects for a BiDi line
void processBidiLineRects(
    const formatted_line_t* frmline,
    LFormattedTextRef txtform,
    ldomNode* startNode, int startOffset,
    ldomNode* endNode, int endOffset,
    lvRect& finalNodeRect,
    LVArray<lvRect>& rects
);

// Check if word belongs to range
bool wordInRange(
    const formatted_word_t* word,
    LFormattedTextRef txtform,
    ldomNode* startNode, int startOffset,
    ldomNode* endNode, int endOffset
);
```

## References

- `ldomXPointer::getRectEx()` implementation (lines 9912-10489) for BiDi handling pattern
- `formatted_line_t` and `formatted_word_t` structures in `include/lvtextfm.h`
- BiDi flags: `LTEXT_LINE_IS_BIDI`, `LTEXT_WORD_DIRECTION_IS_RTL`
- Existing text selection logic in `getSegmentRects()` (lines 12362-12586)

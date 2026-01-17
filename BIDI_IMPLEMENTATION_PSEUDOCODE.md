# BiDi Text Selection - Implementation Pseudocode

This document provides detailed pseudocode showing how to implement BiDi-aware text selection in `getSegmentRects()`.

## Recommended Implementation Approach

### High-Level Algorithm

```
function getSegmentRects(rects, includeImages):
    // Check if we can get formatted text info
    finalNodes = getFinalNodesInRange()
    
    for each finalNode in finalNodes:
        txtform = getFormattedText(finalNode)
        
        if txtform is null:
            // Fallback to current char-by-char implementation
            getSegmentRectsCharByChar(rects, includeImages)
            continue
        
        // Get the range boundaries relative to this final node
        rangeStart, rangeEnd = getRangeBoundariesInNode(finalNode)
        
        // Build source text index set for quick lookup
        sourceIndices = buildSourceIndexSet(txtform, rangeStart, rangeEnd)
        
        // Process each line
        for lineIndex in 0 to txtform.GetLineCount():
            line = txtform.GetLineInfo(lineIndex)
            
            if line.flags & LTEXT_LINE_IS_BIDI:
                // BiDi line: use word-based processing
                processBidiLine(line, txtform, sourceIndices, finalNode, rects)
            else:
                // Non-BiDi line: could use char-by-char or word-based
                // For consistency, use word-based
                processNormalLine(line, txtform, sourceIndices, finalNode, rects)
```

### Function: getFormattedText

```
function getFormattedText(finalNode):
    // Get the absolute rectangle of the final node
    rect = finalNode.getAbsRect(extended=true)
    
    // Get render format accessor
    fmt = RenderRectAccessor(finalNode)
    if fmt is null:
        return null
    
    // Get inner width (accounting for borders and padding)
    if fmt has INNER_FIELDS_SET:
        innerWidth = fmt.getInnerWidth()
    else:
        // Calculate from borders and padding
        paddingLeft = measureBorder(finalNode, 3) + 
                      lengthToPx(finalNode.style.padding[0], rect.width)
        paddingRight = measureBorder(finalNode, 1) + 
                       lengthToPx(finalNode.style.padding[1], rect.width)
        innerWidth = fmt.getWidth() - paddingLeft - paddingRight
    
    // Render and get formatted text
    txtform = LFormattedTextRef()
    finalNode.renderFinalBlock(txtform, fmt, innerWidth)
    
    return txtform
```

### Function: buildSourceIndexSet

```
function buildSourceIndexSet(txtform, rangeStart, rangeEnd):
    sourceIndices = new Set<int>()
    
    // Iterate through all source text fragments
    for i in 0 to txtform.GetSrcCount():
        src = txtform.GetSrcInfo(i)
        
        // Skip floats and padding
        if src.flags & LTEXT_SRC_IS_OBJECT:
            if src.o.objflags & LTEXT_OBJECT_IS_FLOAT:
                continue
        
        // Check if source node is in our range
        srcNode = src.object
        
        if includeImages:
            if isNodeInRange(srcNode, rangeStart, rangeEnd):
                sourceIndices.add(i)
        else:
            if srcNode.isText() and isNodeInRange(srcNode, rangeStart, rangeEnd):
                sourceIndices.add(i)
    
    return sourceIndices
```

### Function: processBidiLine

```
function processBidiLine(line, txtform, sourceIndices, finalNode, rects):
    // Collect words from this line that are in our range
    rangeWords = []
    
    for w in 0 to line.word_count:
        word = line.words[w]
        
        // Skip padding words (virtual, don't correspond to real text)
        if word.flags & LTEXT_WORD_IS_PAD:
            continue
        
        // Check if this word's source is in our range
        if word.src_text_index in sourceIndices:
            // Additional check: if range has specific offsets within a node,
            // verify word offsets overlap with range
            if wordOverlapsRangeOffsets(word, txtform, rangeStart, rangeEnd):
                rangeWords.append(word)
    
    // Build rects from collected words
    // Words are already in visual order (x-coordinate order in the array)
    buildRectsFromWords(rangeWords, line, finalNode, rects)
```

### Function: buildRectsFromWords

```
function buildRectsFromWords(words, line, finalNode, rects):
    if words is empty:
        return
    
    // Get final node absolute position
    finalRect = finalNode.getAbsRect(extended=true)
    
    // Start first segment
    currentSegment = null
    lastRight = -1
    
    // Gap threshold: if words are more than this far apart, start new segment
    GAP_THRESHOLD = 5  // pixels
    
    for word in words:
        // Calculate word's absolute visual position
        wordLeft = finalRect.left + line.x + word.x
        wordRight = wordLeft + word.width
        wordTop = finalRect.top + line.y
        wordBottom = wordTop + line.height
        
        // Check if this word continues the current segment
        if currentSegment is null:
            // Start new segment
            currentSegment = Rect(wordLeft, wordTop, wordRight, wordBottom)
        else if wordLeft <= lastRight + GAP_THRESHOLD:
            // Word is close enough to extend current segment
            currentSegment.left = min(currentSegment.left, wordLeft)
            currentSegment.right = max(currentSegment.right, wordRight)
            // top and bottom should be same for same line, but just in case:
            currentSegment.top = min(currentSegment.top, wordTop)
            currentSegment.bottom = max(currentSegment.bottom, wordBottom)
        else:
            // Gap too large, save current segment and start new one
            rects.add(currentSegment)
            currentSegment = Rect(wordLeft, wordTop, wordRight, wordBottom)
        
        lastRight = wordRight
    
    // Add final segment
    if currentSegment is not null:
        rects.add(currentSegment)
```

### Function: wordOverlapsRangeOffsets

```
function wordOverlapsRangeOffsets(word, txtform, rangeStart, rangeEnd):
    // Get source fragment info for this word
    src = txtform.GetSrcInfo(word.src_text_index)
    srcNode = src.object
    
    // If word's source node is entirely in range, include it
    if srcNode > rangeStart.node and srcNode < rangeEnd.node:
        return true
    
    // If source is object (image), check if it's in range
    if src.flags & LTEXT_SRC_IS_OBJECT:
        return srcNode >= rangeStart.node and srcNode <= rangeEnd.node
    
    // Check offsets for start node
    if srcNode == rangeStart.node:
        wordEnd = word.t.start + word.t.len
        if wordEnd <= rangeStart.offset:
            return false  // word ends before range starts
    
    // Check offsets for end node
    if srcNode == rangeEnd.node:
        if word.t.start >= rangeEnd.offset:
            return false  // word starts after range ends
    
    return true
```

### Function: processNormalLine (non-BiDi)

```
function processNormalLine(line, txtform, sourceIndices, finalNode, rects):
    // For non-BiDi lines, can use similar word-based approach
    // or fall back to char-by-char for optimization
    
    // Option 1: Use same word-based approach (simpler, consistent)
    processBidiLine(line, txtform, sourceIndices, finalNode, rects)
    
    // Option 2: Use optimized char-by-char (better performance)
    // (keep existing implementation for non-BiDi lines)
```

## Edge Cases to Handle

### 1. Range Spanning Multiple Final Nodes

```
function getSegmentRects(rects, includeImages):
    // Find all final nodes intersecting the range
    finalNodes = []
    currentNode = getStart().getNode()
    endNode = getEnd().getNode()
    
    while currentNode != null:
        finalNode = currentNode.getFinalNode()
        if finalNode not in finalNodes:
            finalNodes.append(finalNode)
        
        if currentNode == endNode:
            break
        
        currentNode = nextNode(currentNode)
    
    // Process each final node separately
    for finalNode in finalNodes:
        processRangeInFinalNode(finalNode, rects, includeImages)
```

### 2. Partial Word Selection

```
function wordOverlapsRangeOffsets(word, txtform, rangeStart, rangeEnd):
    // ... existing checks ...
    
    // For partial selection, we still include the whole word's visual rect
    // This matches typical selection behavior where partial word looks selected
    // Alternative: could calculate partial word width, but complex for BiDi
    
    return true  // if any overlap
```

### 3. Empty or Invalid Ranges

```
function getSegmentRects(rects, includeImages):
    if getStart() is null or getEnd() is null:
        return  // empty rects
    
    if getStart().compare(getEnd()) >= 0:
        return  // invalid range (start after end)
    
    // ... continue with normal processing
```

## Performance Considerations

### Optimization 1: Cache Formatted Text

```
function getSegmentRects(rects, includeImages):
    finalNodeCache = Map<ldomNode*, LFormattedTextRef>()
    
    for each finalNode in range:
        if finalNode in finalNodeCache:
            txtform = finalNodeCache[finalNode]
        else:
            txtform = getFormattedText(finalNode)
            finalNodeCache[finalNode] = txtform
        
        // process txtform...
```

### Optimization 2: Skip Lines Outside Range

```
function processLinesInRange(txtform, rangeStart, rangeEnd, rects):
    // Calculate which lines might contain range
    for lineIndex in 0 to txtform.GetLineCount():
        line = txtform.GetLineInfo(lineIndex)
        
        // Quick check: skip lines entirely before/after range
        if lineCompletelyBeforeRange(line, rangeStart):
            continue
        if lineCompletelyAfterRange(line, rangeEnd):
            break
        
        // Process this line
        processLine(line, txtform, rects)
```

### Optimization 3: Early Exit for Non-BiDi Documents

```
function getSegmentRects(rects, includeImages):
    // Check document/range properties
    if documentNeverContainsBidi():
        // Use fast char-by-char path
        getSegmentRectsCharByChar(rects, includeImages)
        return
    
    // Use BiDi-aware path
    // ...
```

## Testing Strategy

### Test Case 1: Pure LTR Text (Regression)
```
Input: "Hello World Selection Test"
Range: "World Selection"
Expected: Single rect covering "World Selection"
```

### Test Case 2: Pure RTL Text
```
Input: "שלום עולם בחירה מבחן"
Range: middle two words
Expected: Single rect covering the selected RTL words
```

### Test Case 3: Mixed LTR/RTL
```
Input: "Hello עולם World"
Range: entire text
Expected: Three rects or merged rects covering all three segments
```

### Test Case 4: Interleaved BiDi
```
Input: "English ABC مرحبا DEF test"
Range: "ABC مرحبا DEF"
Expected: Continuous rect(s) in visual order covering the selection
```

### Test Case 5: Multiple Lines with BiDi
```
Input: Multi-line text with BiDi on some lines
Range: spanning multiple lines
Expected: One rect per line segment, capturing all visual parts
```

## Integration Points

### Where to Add Code

1. **In lvtinydom.cpp**, modify `ldomXRange::getSegmentRects()` starting at line 12362

2. **Add helper functions** either as private methods of ldomXRange or as static functions in lvtinydom.cpp

3. **Consider refactoring** BiDi handling from `ldomXPointer::getRectEx()` (lines 10095-10327) into shared utilities

### Backward Compatibility

- Keep existing function signature: `void getSegmentRects(LVArray<lvRect>& rects, bool includeImages)`
- Maintain behavior for non-BiDi text (same rects as before)
- Only change behavior for BiDi text (fixing the bug)

### Error Handling

```
function getSegmentRects(rects, includeImages):
    try:
        // Attempt BiDi-aware processing
        getSegmentRectsWithBidiSupport(rects, includeImages)
    catch (any error):
        // Fallback to original implementation
        getSegmentRectsCharByChar(rects, includeImages)
```

## Summary

The key insight is that for BiDi text:
- **Don't iterate characters in logical order**
- **Do use formatted_word_t array in visual order**
- **Map source text indices to determine which words are in range**
- **Build rects from visual positions of those words**

This approach leverages existing data structures and follows the pattern already established in `getRectEx()`.

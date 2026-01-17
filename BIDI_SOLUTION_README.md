# BiDi Text Selection Fix - Solution Summary

## Quick Reference

This directory contains comprehensive documentation for fixing the BiDi (Bidirectional) text selection issue in `ldomXRange::getSegmentRects()`.

## Documentation Files

1. **BIDI_TEXT_SELECTION_SOLUTION_IDEAS.md** - Detailed analysis and four different solution approaches with pros/cons
2. **BIDI_PROBLEM_ILLUSTRATION.md** - Visual examples showing how BiDi text breaks the current implementation
3. **BIDI_IMPLEMENTATION_PSEUDOCODE.md** - Step-by-step implementation guide with pseudocode

## The Problem in One Sentence

`getSegmentRects()` iterates through text in logical (source) order, but BiDi text is rendered in visual order, causing visual segments to be missed in the selection rectangles.

## Recommended Solution

**Word-based visual order processing for BiDi lines:**

1. Detect BiDi lines using `LTEXT_LINE_IS_BIDI` flag
2. Access formatted text via `renderFinalBlock()`
3. Process `formatted_word_t` structures (already in visual order)
4. Build rects from visual positions of words in the range

## Key Files to Modify

- **crengine/src/lvtinydom.cpp** - `ldomXRange::getSegmentRects()` function (lines 12362-12586)
- Optionally refactor BiDi logic from `ldomXPointer::getRectEx()` (lines 10095-10327) into shared helpers

## Quick Start for Implementation

1. Read **BIDI_PROBLEM_ILLUSTRATION.md** to understand the issue visually
2. Review **BIDI_TEXT_SELECTION_SOLUTION_IDEAS.md** section "Idea 2: Word-Based Visual Order Processing"
3. Follow **BIDI_IMPLEMENTATION_PSEUDOCODE.md** for step-by-step implementation
4. Test with examples from the illustration document

## Key Data Structures

```cpp
// Line info with BiDi flag
formatted_line_t {
    int flags;  // check: flags & LTEXT_LINE_IS_BIDI
    formatted_word_t* words;  // array in VISUAL order
    int word_count;
}

// Word info with source mapping
formatted_word_t {
    int x, width;           // visual position
    int src_text_index;     // maps to source text node
    int flags;              // includes LTEXT_WORD_DIRECTION_IS_RTL
}
```

## Implementation Checklist

- [ ] Add function to get formatted text for final node
- [ ] Add function to build source index set from range
- [ ] Add BiDi line detection in getSegmentRects()
- [ ] Implement word-based rect building for BiDi lines
- [ ] Handle edge cases (multiple final nodes, partial selection)
- [ ] Test with pure LTR text (regression)
- [ ] Test with pure RTL text
- [ ] Test with mixed LTR/RTL text
- [ ] Test with multiple BiDi lines
- [ ] Performance testing for large documents

## Benefits of This Approach

✅ Fixes BiDi text selection gaps  
✅ Leverages existing formatted text infrastructure  
✅ Follows pattern from getRectEx() (proven to work)  
✅ Minimal performance impact (only BiDi lines affected)  
✅ Backward compatible (non-BiDi text unchanged)  

## References

- Issue: Text selection does not handle well lines with BiDi text
- Related code: `ldomXPointer::getRectEx()` already handles BiDi correctly
- BiDi flags defined in: `crengine/include/lvtextfm.h`
- Text formatting: `LFormattedText` class and `formatted_line_t`/`formatted_word_t` structures

## Questions or Issues?

Review the three documentation files in this directory for detailed explanations, visual examples, and implementation guidance.

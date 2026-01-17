# BiDi Text Selection - Architecture Diagram

## Current Implementation (Broken for BiDi)

```
┌─────────────────────────────────────────────────────────┐
│  ldomXRange::getSegmentRects()                          │
│  (lines 12362-12586 in lvtinydom.cpp)                   │
└─────────────────────────────────────────────────────────┘
                    │
                    │ Iterates in LOGICAL order
                    ▼
    ┌───────────────────────────────────┐
    │  Text Nodes in HTML Source Order  │
    │  Node1: "Hello"                   │
    │  Node2: "עברית" (RTL)             │
    │  Node3: "World"                   │
    └───────────────────────────────────┘
                    │
                    │ For each character position
                    ▼
    ┌───────────────────────────────────┐
    │  curPos.getRectEx(charRect)       │
    │  Get individual character rect    │
    └───────────────────────────────────┘
                    │
                    │ Extend rects on same line
                    ▼
    ┌───────────────────────────────────┐
    │  lineStartRect.extend(charRect)   │
    │  Build segment by line            │
    └───────────────────────────────────┘
                    │
                    ▼
    ❌ PROBLEM: In BiDi text, characters
       from same node appear non-contiguously!
       Visual segments are MISSED.
```

## Proposed Implementation (Fixed for BiDi)

```
┌─────────────────────────────────────────────────────────┐
│  ldomXRange::getSegmentRects() - ENHANCED               │
│  (proposed changes to lines 12362-12586)                │
└─────────────────────────────────────────────────────────┘
                    │
        ┌───────────┴──────────┐
        │                      │
        ▼                      ▼
  ┌─────────────┐      ┌─────────────────┐
  │ Get Final   │      │ Get Range       │
  │ Node(s)     │      │ Boundaries      │
  └─────────────┘      └─────────────────┘
        │                      │
        └───────────┬──────────┘
                    │
                    ▼
    ┌───────────────────────────────────────────┐
    │  finalNode->renderFinalBlock(txtform)     │
    │  Access formatted text structures         │
    └───────────────────────────────────────────┘
                    │
                    │ Get formatted text
                    ▼
    ┌───────────────────────────────────────────┐
    │  LFormattedTextRef txtform                │
    │  ├─ GetLineCount()                        │
    │  ├─ GetLineInfo(i) → formatted_line_t     │
    │  └─ GetSrcInfo(i) → src_text_fragment_t   │
    └───────────────────────────────────────────┘
                    │
                    │ For each line
                    ▼
    ┌───────────────────────────────────────────┐
    │  line = txtform->GetLineInfo(i)           │
    │  Check: line->flags & LTEXT_LINE_IS_BIDI  │
    └───────────────────────────────────────────┘
                    │
        ┌───────────┴──────────┐
        │                      │
        ▼                      ▼
  ┌─────────────────┐  ┌─────────────────────┐
  │ Non-BiDi Line   │  │ BiDi Line           │
  │ (keep fast      │  │ (NEW: word-based)   │
  │  char-by-char)  │  │                     │
  └─────────────────┘  └─────────────────────┘
                               │
                               ▼
           ┌───────────────────────────────────────┐
           │ Build source text index set:          │
           │ sourceIndices = {i | src[i].object    │
           │                   in range}           │
           └───────────────────────────────────────┘
                               │
                               ▼
           ┌───────────────────────────────────────┐
           │ For each word in line:                │
           │   word = line->words[w]               │
           │   Check: word->src_text_index         │
           │          in sourceIndices?            │
           └───────────────────────────────────────┘
                               │
                               ▼
           ┌───────────────────────────────────────┐
           │ Collect words in range:               │
           │ rangeWords = [word1, word2, ...]      │
           │ (already in VISUAL order!)            │
           └───────────────────────────────────────┘
                               │
                               ▼
           ┌───────────────────────────────────────┐
           │ Build rects from visual positions:    │
           │ - Sort by word->x (visual position)   │
           │ - Group consecutive words into rects  │
           │ - Add to output rects array           │
           └───────────────────────────────────────┘
                               │
                               ▼
           ✅ SUCCESS: All visual segments captured!
```

## Data Flow Comparison

### Current (Broken)
```
HTML Source Order → Char-by-Char → getRectEx() → Extend Rects
     (logical)        (logical)     (mixed!)      (broken!)
```

### Proposed (Fixed)
```
HTML Source       Formatted Text    Word Array    Visual Rects
  (logical)  →    (both orders) →   (visual)  →   (visual) ✓
     │               │    │             │
     │               │    │             └─ words[0..n] in x-order
     │               │    └─ src_text_index mapping
     │               └─ line->flags & LTEXT_LINE_IS_BIDI
     └─ Range nodes/offsets
```

## Key Data Structures

```
┌─────────────────────────────────────────────────────────────┐
│ LFormattedTextRef txtform                                   │
│                                                             │
│ ├─ Line 0: formatted_line_t                                │
│ │   ├─ y, height (vertical position)                       │
│ │   ├─ flags (includes LTEXT_LINE_IS_BIDI)                 │
│ │   ├─ word_count                                          │
│ │   └─ words[0..n] → formatted_word_t array ───┐          │
│ │                    (IN VISUAL ORDER!)         │          │
│ │                                               ▼          │
│ │      ┌────────────────────────────────────────────────┐ │
│ │      │ formatted_word_t (word 0)                      │ │
│ │      │  ├─ x = 0 (visual position)                    │ │
│ │      │  ├─ width = 50                                 │ │
│ │      │  ├─ src_text_index = 0 ───┐                    │ │
│ │      │  └─ t.start, t.len         │                    │ │
│ │      └────────────────────────────┼────────────────────┘ │
│ │                                   │                      │
│ │      ┌────────────────────────────┼────────────────────┐ │
│ │      │ formatted_word_t (word 1)  │                    │ │
│ │      │  ├─ x = 58                 │                    │ │
│ │      │  ├─ width = 40             │                    │ │
│ │      │  ├─ src_text_index = 1 ────┼──┐                 │ │
│ │      │  └─ flags & ...IS_RTL      │  │                 │ │
│ │      └────────────────────────────┼──┼─────────────────┘ │
│ │                                   │  │                   │
│ ├─ Line 1: formatted_line_t        │  │                   │
│ │   └─ words[...]                   │  │                   │
│ │                                   │  │                   │
│ └─ Sources: src_text_fragment_t    │  │                   │
│     ├─ [0]: object = Node1 ◄───────┘  │                   │
│     ├─ [1]: object = Node2 ◄──────────┘                   │
│     └─ [2]: object = Node3                                │
│           (LOGICAL order)                                  │
└─────────────────────────────────────────────────────────────┘

        ▲
        │ Maps visual words back to logical source nodes
        │
┌───────┴──────────────────────────────────────────┐
│ ldomXRange (input)                               │
│  ├─ start: Node1, offset 0                       │
│  └─ end: Node3, offset 5                         │
└──────────────────────────────────────────────────┘
```

## Processing Flow for BiDi Line

```
Step 1: Build Source Index Set
┌─────────────────────────────────┐
│ Range includes:                 │
│  - Node1 (all)                  │
│  - Node2 (all)                  │
│  - Node3 (partial)              │
│                                 │
│ Map to source indices:          │
│  sourceIndices = {0, 1, 2}      │
└─────────────────────────────────┘
                │
                ▼
Step 2: Collect Words
┌─────────────────────────────────┐
│ For line with LTEXT_LINE_IS_BIDI│
│                                 │
│ word[0]: src_index=0 ✓ in range│
│ word[1]: src_index=1 ✓ in range│
│ word[2]: src_index=2 ✓ in range│
│                                 │
│ rangeWords = [word0,word1,word2]│
└─────────────────────────────────┘
                │
                ▼
Step 3: Build Rects (Visual Order)
┌─────────────────────────────────┐
│ word0: x=0, width=50            │
│ word1: x=58, width=40           │
│ word2: x=105, width=35          │
│                                 │
│ Check gaps:                     │
│  0→50, 58→98 (gap: 8px ≤ thresh)│
│  → merge into one rect          │
│  98→105 (gap: 7px ≤ thresh)     │
│  → merge into one rect          │
│                                 │
│ Result: One rect (0, y, 140, y+h)│
│ OR separate rects if gaps large │
└─────────────────────────────────┘
```

## Why This Fixes the Problem

### Before (Broken)
```
Iteration: Node1 → Node2 → Node3
           (logical order)

For Node2 (RTL):
  charAt(0) → getRect → position ???
  charAt(1) → getRect → position ???
  (positions may be reversed, non-contiguous)
  
Result: GAPS in selection rects
```

### After (Fixed)
```
Iteration: word0 → word1 → word2
           (visual order, x=0 → x=58 → x=105)

word1 belongs to Node2:
  - Already positioned correctly in visual order
  - x and width give exact visual rect
  - No need to iterate characters
  
Result: COMPLETE selection rects
```

## Implementation Files

```
crengine/
├── src/
│   └── lvtinydom.cpp
│       ├── ldomXRange::getSegmentRects() [MODIFY: lines 12362-12586]
│       │   └── Add BiDi detection and word-based processing
│       │
│       └── ldomXPointer::getRectEx() [REFERENCE: lines 10095-10327]
│           └── Already has BiDi handling pattern to follow
│
└── include/
    ├── lvtinydom.h
    │   └── ldomXRange class [MAY ADD: helper method declarations]
    │
    └── lvtextfm.h
        ├── formatted_line_t (with LTEXT_LINE_IS_BIDI flag)
        └── formatted_word_t (with src_text_index, x, width)
```

## Success Criteria

✅ Pure LTR text: same rects as before (regression test)
✅ Pure RTL text: complete rects covering all text
✅ Mixed LTR/RTL: no gaps, all segments captured
✅ Multi-line BiDi: correct rects for each line
✅ Performance: minimal impact on non-BiDi text

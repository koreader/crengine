#include "crsetup.h"

#include "../include/pdbfmt.h"
#include "../include/crtxtenc.h"
#include <ctype.h>

// uncomment following line to save PDB content streams to /tmp
//#define DUMP_PDB_CONTENTS

// --- MOBI filepos fragment support ---
// MOBI uses two independent mechanisms for internal links:
// - <a filepos="NNNN"> points to byte offset NNNN in the uncompressed HTML.
//   Nothing in the source marks that offset; we must add an anchor there.
// - filepos-id="XXX" on an element marks it as a target (used by the NCX/TOC).
// We resolve both by rewriting them into plain href/id attributes, so the
// existing id->node map handles fragment resolution (and survives cache
// serialization). For each referenced byte offset, we inject an anchor: either
// a filepos-id attribute on the tag containing the offset, or (if the offset
// lands in text/whitespace) a standalone <a id="fileposNNNN"></a> element.

static const char * MOBI_FILEPOS_ID_PREFIX = "filepos";

// Maps a filepos byte offset to the id a link should target. Only populated
// when the target element already has its own id, so we point the link at that
// id instead of injecting a synthetic one (which would duplicate it).
struct MobiFileposResolver {
    LVHashTable<lUInt32, lString32> targetIds;
    MobiFileposResolver() : targetIds(256) {}
};

static int compareUInt32(const void * left, const void * right) {
    lUInt32 a = *(const lUInt32 *)left, b = *(const lUInt32 *)right;
    return (a < b) ? -1 : (a > b) ? 1 : 0;
}

// Case-insensitive match of a fixed-length ASCII string at data[pos].
static bool matchAscii(const lUInt8 * data, int pos, const char * str, int len) {
    for (int i = 0; i < len; i++) {
        lUInt8 ch = data[pos + i];
        lUInt8 expected = (lUInt8)str[i];
        if (ch != expected && (ch < 'A' || ch > 'Z' || ch != (expected - 32)))
            return false;
    }
    return true;
}

// Quick case-insensitive check: does data[pos..pos+6] match "filepos"?
static bool matchFileposBytes(const lUInt8 * data, int dataSize, int pos) {
    if (pos + 7 > dataSize) return false;
    return matchAscii(data, pos, "filepos", 7);
}

// Does the start tag spanning [tagStart, tagEnd) already declare a non-empty
// "id" or "filepos-id" attribute? If so, set *idValue to its value and return
// true (so a link can point at it instead of us injecting a duplicate id).
// Returns false if the tag has no such attribute, or if it is empty (id=""),
// in which case the caller injects our own id="fileposNNNN" to override it.
static bool tagGetIdAttr(const lUInt8 * data, int tagStart, int tagEnd, lString32 & idValue) {
    for (int i = tagStart; i < tagEnd; i++) {
        // An attribute name is preceded by whitespace (or the tag name).
        bool atBoundary = (i == tagStart) || data[i-1] == ' ' || data[i-1] == '\t' || data[i-1] == '\r' || data[i-1] == '\n';
        if (!atBoundary)
            continue;
        int remaining = tagEnd - i;
        int nameLen = 0;
        // "filepos-id" (9 chars)
        if (remaining >= 9 && matchAscii(data, i, "filepos-id", 9)) {
            nameLen = 9;
        }
        // "id" (2 chars)
        else if (remaining >= 2 && matchAscii(data, i, "id", 2)) {
            nameLen = 2;
        }
        if (nameLen == 0)
            continue;
        int afterName = i + nameLen;
        if (afterName < tagEnd && data[afterName] != ' ' && data[afterName] != '\t' && data[afterName] != '=' && data[afterName] != '>')
            continue; // e.g. "idle", "width": not the id attribute
        // Skip whitespace and an optional '=' and quote to reach the value
        int p = afterName;
        while (p < tagEnd && (data[p] == ' ' || data[p] == '\t' || data[p] == '\r' || data[p] == '\n')) {
            p++;
        }
        if (p < tagEnd && data[p] == '=') {
            p++;
            while (p < tagEnd && (data[p] == ' ' || data[p] == '\t' || data[p] == '\r' || data[p] == '\n')) {
                p++;
            }
            if (p < tagEnd && (data[p] == '"' || data[p] == '\''))
                p++;
        }
        int valStart = p;
        while (p < tagEnd && data[p] != '"' && data[p] != '\'' && data[p] != ' ' && data[p] != '\t' && data[p] != '\r' && data[p] != '\n' && data[p] != '>') {
            p++;
        }
        if (p > valStart)
            idValue = lString32((const lChar8 *)(data + valStart), p - valStart);
        else
            return false; // empty id="": let the caller inject its own id
        return true;
    }
    return false;
}

// Scan raw HTML bytes for filepos="NNNN" occurrences, record the numeric values.
static void collectMobiFileposData(const lUInt8 * data, int dataSize,
        LVArray<lUInt32> & fileposRefs) {
    for (int i = 0; i < dataSize - 8; ) {
        lUInt8 ch = data[i];
        if (ch != 'f' && ch != 'F') { i++; continue; }
        if (!matchFileposBytes(data, dataSize, i)) { i++; continue; }
        int pos = i + 7;
        // Skip whitespace before '='
        while (pos < dataSize && (data[pos] == ' ' || data[pos] == '\t')) {
            pos++;
        }
        if (pos >= dataSize || data[pos] != '=') { i++; continue; }
        pos++; // skip '='
        // Skip whitespace and an optional opening quote
        while (pos < dataSize && (data[pos] == ' ' || data[pos] == '"' || data[pos] == '\'')) {
            pos++;
        }
        // Parse the integer value
        lUInt64 val = 0;
        int numStart = pos;
        while (pos < dataSize && data[pos] >= '0' && data[pos] <= '9') {
            val = val * 10 + (data[pos] - '0');
            pos++;
        }
        if (pos > numStart && val > 0 && val <= (lUInt64)dataSize) {
            fileposRefs.add((lUInt32)val);
        }
        i = pos; // resume after the value
    }
}

// Emit an id="fileposNNNN" attribute into the stream, to be placed inside a start tag.
static void writeFileposIdAttr(LVStreamRef & out, lUInt32 filepos) {
    char buf[64];
    int len = snprintf(buf, sizeof(buf), " id=\"%s%u\"", MOBI_FILEPOS_ID_PREFIX, (unsigned)filepos);
    out->Write(buf, len, NULL);
}

// Emit a standalone <a id="fileposNNNN"></a> marker.
static void writeFileposMarker(LVStreamRef & out, lUInt32 filepos) {
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "<a id=\"%s%u\"></a>", MOBI_FILEPOS_ID_PREFIX, (unsigned)filepos);
    out->Write(buf, len, NULL);
}

// Does the start tag spanning [tagStart, tagEnd] (with data[tagEnd] == '>')
// close itself, i.e. end with '/>' possibly preceded by whitespace?
static bool isSelfClosingTag(const lUInt8 * data, int tagStart, int tagEnd) {
    int i = tagEnd - 1;
    while (i > tagStart && (data[i] == ' ' || data[i] == '\t'))
        i--;
    return i > tagStart && data[i] == '/';
}

// Locate a start tag we can attach a synthetic id to, given a byte offset pos.
// If pos is inside a start tag, return that tag's [<, >) span. Otherwise, if
// pos is immediately followed (after whitespace) by a start tag, return that
// tag's span. Returns false (leaving tagStart/tagEnd untouched) if neither
// applies, i.e. the offset lands in text/whitespace with no following start tag.
static bool findStartTagAt(const lUInt8 * data, int dataSize, int pos, int & tagStart, int & tagEnd) {
    // Is pos inside a start tag? Find the nearest '<' before pos that is not
    // already closed by a '>'.
    if (pos > 0 && pos < dataSize) {
        int prevLT = -1;
        for (int j = pos - 1; j >= 0; j--) {
            if (data[j] == '<') { prevLT = j; break; }
            if (data[j] == '>') break; // not inside a tag
        }
        if (prevLT >= 0 && data[prevLT + 1] != '/') {
            // Find the nearest '>' after pos (stopping at any '<').
            for (int j = pos; j < dataSize; j++) {
                if (data[j] == '>') {
                    // Skip self-closing void tags (e.g. <mbp:pagebreak/>): an
                    // id on them is useless and they get autoBoxed, so fall
                    // through to the following-tag scan instead.
                    if (!isSelfClosingTag(data, prevLT, j)) {
                        tagStart = prevLT; tagEnd = j; return true;
                    }
                    pos = j + 1; // skip past this self-closing tag
                    break;
                }
                if (data[j] == '<') break;
            }
        }
    }
    // Is pos followed (after whitespace) by a start tag? Skip self-closing
    // void tags (e.g. <mbp:pagebreak/>) and keep looking for a real element.
    int j = pos;
    // If we landed on '>' (end of a closing or start tag), advance past it so
    // we can look for the following start tag.
    if (j < dataSize && data[j] == '>')
        j++;
    while (true) {
        while (j < dataSize && (data[j] == ' ' || data[j] == '\t' || data[j] == '\r' || data[j] == '\n')) {
            j++;
        }
        if (!(j < dataSize && data[j] == '<' && j + 1 < dataSize && data[j + 1] != '/'))
            break;
        // Find the '>' closing this start tag.
        int k = j + 1;
        while (k < dataSize && data[k] != '>' && data[k] != '<')
            k++;
        if (k >= dataSize || data[k] != '>')
            break; // malformed
        if (!isSelfClosingTag(data, j, k)) {
            tagStart = j; tagEnd = k; return true;
        }
        // Self-closing void tag: skip past it and continue.
        j = k + 1;
    }
    return false;
}

// Pre-process the raw HTML stream: find all filepos=NNNN references and inject
// anchors at those byte offsets, so the existing id->node map can resolve them
// (and they survive cache serialization).
// Where the offset falls inside a start tag, we add an id="fileposNNNN"
// attribute to that tag, avoiding splitting any text node. Otherwise, if the
// offset is immediately followed (after whitespace) by a start tag, we attach
// the id to that tag too: an empty inline <a> anchor right after a page break
// would resolve to the previous page, whereas attaching the id to the following
// element keeps the target on the correct page. Only when the offset lands
// mid-text do we insert a standalone <a id="fileposNNNN"></a> marker (the same
// approach calibre uses).
// extraFileposRefs allows adding byte offsets to anchor that are not referenced
// by any filepos= link (used for the TOC/NCX index targets).
static LVStreamRef preprocessMobiHtmlStream(LVStreamRef stream, MobiFileposResolver & resolver, bool allowInjectStandaloneId, const LVArray<lUInt32> * extraFileposRefs = NULL) {
    stream->SetPos(0);
    LVByteArrayRef rawData = stream->GetData();
    stream->SetPos(0);
    if (rawData.isNull() || rawData->empty()) {
        return LVStreamRef();
    }
    const lUInt8 * data = rawData->get();
    int dataSize = rawData->length();

    LVArray<lUInt32> fileposRefs;
    collectMobiFileposData(data, dataSize, fileposRefs);
    if (extraFileposRefs) {
        for (int i = 0; i < extraFileposRefs->length(); i++) {
            lUInt32 fp = (*extraFileposRefs)[i];
            if (fp > 0 && fp <= (lUInt32)dataSize)
                fileposRefs.add(fp);
        }
    }
    if (fileposRefs.empty()) {
        return LVStreamRef();
    }

    // Sort for sequential insertion
    qsort(fileposRefs.get(), fileposRefs.length(), sizeof(lUInt32), compareUInt32);

    // Dedup in-place
    int n = 1;
    for (int i = 1; i < fileposRefs.length(); i++) {
        if (fileposRefs[i] != fileposRefs[n-1])
            fileposRefs[n++] = fileposRefs[i];
    }
    if (n < fileposRefs.length())
        fileposRefs.erase(n, fileposRefs.length() - n);

    // Build the rewritten byte array with markers inserted at filepos offsets.
    LVStreamRef rewritten = LVCreateMemoryStream(NULL, 0, false, LVOM_READWRITE);
    if (rewritten.isNull()) {
        stream->SetPos(0);
        return LVStreamRef();
    }
    int outPos = 0;
    for (int i = 0; i < fileposRefs.length(); i++) {
        lUInt32 filepos = fileposRefs[i];
        int insertPos = (int)filepos;
        // If the offset falls inside a tag (<...>), add an id attribute to that
        // tag rather than inserting a separate <a> element (which would split a
        // text node and break highlights).
        bool injectIntoTag = false;
        int tagEndPos = insertPos;
        int tagStart;
        if (findStartTagAt(data, dataSize, insertPos, tagStart, tagEndPos)) {
            lString32 existingId;
            if (tagGetIdAttr(data, tagStart, tagEndPos, existingId)) {
                // The tag already has an id: point the link at it.
                resolver.targetIds.set(filepos, existingId);
            } else {
                injectIntoTag = true;
            }
        }
        if (insertPos < outPos)
            insertPos = outPos;
        if (injectIntoTag) {
            // Copy up to (but not including) the closing '>', then emit the
            // attribute, then the '>'. For a self-closing tag (<div/>), the
            // attribute must go before the '/'.
            bool selfClosing = (tagEndPos > outPos && data[tagEndPos - 1] == '/');
            int copyEnd = selfClosing ? tagEndPos - 1 : tagEndPos;
            rewritten->Write(data + outPos, copyEnd - outPos, NULL);
            writeFileposIdAttr(rewritten, filepos);
            if (selfClosing)
                *rewritten << "/";
            outPos = tagEndPos;
        } else if (allowInjectStandaloneId) {
            // Only inject a standalone <a> marker (which may split a text node
            // and break highlights) when a recent DOM version is requested.
            rewritten->Write(data + outPos, insertPos - outPos, NULL);
            writeFileposMarker(rewritten, filepos);
            outPos = insertPos;
        } else {
            // Older DOM: skip the marker entirely to avoid splitting text nodes.
            rewritten->Write(data + outPos, insertPos - outPos, NULL);
            outPos = insertPos;
        }
    }
    rewritten->Write(data + outPos, dataSize - outPos, NULL);
    rewritten->SetPos(0);
    stream->SetPos(0);
    return rewritten;
}

// Custom callback filter that rewrites MOBI-specific attributes during HTML parsing:
// - filepos-id="XXX" -> id="XXX" (target marker, registered in the id->node map)
// - filepos="NNNN" -> href="#fileposNNNN" (link to a byte-offset target)
class MobiHtmlWriterFilter : public ldomDocumentWriterFilter {
    MobiFileposResolver & _resolver;
    bool _curTagHasId; // whether the current tag already got an id attribute
public:
    MobiHtmlWriterFilter(ldomDocument * document, MobiFileposResolver & resolver)
        : ldomDocumentWriterFilter(document, false, HTML_AUTOCLOSE_TABLE)
        , _resolver(resolver)
        , _curTagHasId(false) {}

    virtual ldomNode * OnTagOpen(const lChar32 * nsname, const lChar32 * tagname) {
        _curTagHasId = false;
        return ldomDocumentWriterFilter::OnTagOpen(nsname, tagname);
    }

    virtual void OnAttribute(const lChar32 * nsname, const lChar32 * attrname,
                             const lChar32 * attrvalue) {
        if (attrname && !lStr_cmp(attrname, U"filepos-id")) {
            // MOBI target marker: rewrite to a regular id attribute so it is
            // registered in the id->node map (and serialized to cache). But
            // skip it if the tag already has a plain id, to avoid a duplicate.
            if (!_curTagHasId) {
                ldomDocumentWriterFilter::OnAttribute(nsname, U"id", attrvalue);
                _curTagHasId = true;
            }
            return;
        }
        if (attrname && !lStr_cmp(attrname, U"id")) {
            // If a filepos-id already claimed this tag's id slot, drop this
            // plain id so the DOM's surviving id matches what tagGetIdAttr()
            // picks first (avoids a duplicate id attribute).
            if (_curTagHasId)
                return;
            _curTagHasId = true;
        }
        if (attrname && !lStr_cmp(attrname, U"filepos")) {
            // Link to a byte offset: rewrite to href="#fileposNNNN", or to the
            // target element's existing id if it already had one.
            lInt64 filepos = 0;
            if (lString32(attrvalue).atoi(filepos) && filepos >= 0 && filepos <= 0xFFFFFFFFLL) {
                lString32 targetId;
                if (_resolver.targetIds.get((lUInt32)filepos, targetId)) {
                    lString32 href(U"#");
                    href.append(targetId);
                    ldomDocumentWriterFilter::OnAttribute(nsname, U"href", href.c_str());
                    return;
                }
                lString32 href(U"#");
                href.append(MOBI_FILEPOS_ID_PREFIX);
                href.appendDecimal(filepos);
                ldomDocumentWriterFilter::OnAttribute(nsname, U"href", href.c_str());
                return;
            }
        }
        ldomDocumentWriterFilter::OnAttribute(nsname, attrname, attrvalue);
    }
};

// --- end MOBI filepos fragment support ---

// --- MOBI TOC (INDX/NCX index) support ---
// MOBI files store their TOC in a set of INDX records, referenced by the
// "ncxidx" field (offset 244) of the MOBI header in record 0:
//   [ncxidx]              INDX header record, containing a TAGX section that
//                         describes how entries are encoded
//   [ncxidx+1..+count]    index records, each holding entries listed in an
//                         IDXT table at the end of the record
//   [.. +ncncx]           CNCX records: a pool of <vwi length><utf-8 string>
//                         holding all entry titles
// The INDX/TAGX/IDXT parsing itself lives in PDBFile::readMobiToc() further
// below, as it needs access to the record table; only the standalone helpers
// (vwi decoding, CNCX string decoding) are defined here.
// Each entry starts with a <vwi len><ident string>, followed by control
// bytes (as many as TAGX says), then vwi-encoded values for the tags whose
// control bits are set. For the TOC ("NCX") index, the interesting tags are:
//   1 = filepos (byte offset into the uncompressed HTML)
//   3 = offset of the title string in the CNCX pool
//   4 = hierarchy level (0 = top level)
// (Also see calibre's calibre/ebooks/mobi/reader/ncx.py.)

struct MobiTocEntry {
    lUInt32 filepos;
    lString32 title;
    int level;
    MobiTocEntry() : filepos(0), level(0) {}
};

// Read a forward-encoded variable-width integer (7 bits per byte, big-endian,
// last byte has its high bit set). Returns false if the data runs out or the
// value doesn't fit in 32 bits — a silent truncation would corrupt offsets
// and lengths.
static bool readMobiVwi(const lUInt8 * data, int dataSize, int & pos, lUInt32 & value) {
    value = 0;
    while (pos < dataSize) {
        lUInt8 b = data[pos++];
        lUInt32 v = b & 0x7F;
        // Exact overflow check: value<<7 | v must stay within 32 bits
        if (value > (0xFFFFFFFFu - v) >> 7)
            return false;
        value = (value << 7) | v;
        if (b & 0x80)
            return true;
    }
    return false;
}

static int countSetBits(lUInt32 n) {
    int c = 0;
    while (n) {
        c += n & 1;
        n >>= 1;
    }
    return c;
}

// Decode a CNCX string (already sliced out of the pool) with the MOBI text
// encoding (65001 = UTF-8, 1252 = cp1252, other Windows codepages supported).
static lString32 decodeMobiCncxString(const lUInt8 * s, int len, lUInt32 encoding) {
    if (encoding == 65001)
        return Utf8ToUnicode(lString8((const char *)s, len));
    // Map the upper 128 chars via the codepage table (unknown codepages
    // fall back to cp1252 in GetCharsetByte2UnicodeTable())
    const lChar32 * table = GetCharsetByte2UnicodeTable((int)encoding);
    lString32 res;
    res.reserve(len);
    for (int i = 0; i < len; i++) {
        lUInt8 ch = s[i];
        res.append(1, ch < 128 ? (lChar32)ch : table[ch - 128]);
    }
    return res;
}

// --- end MOBI TOC support ---

struct PDBHdr
{
    lUInt8    name[32];
    lUInt16   attributes;
    lUInt16   version;
    lUInt32    creationDate;
    lUInt32    modificationDate;
    lUInt32    lastBackupDate;
    lUInt32    modificationNumber;
    lUInt32    appInfoID;
    lUInt32    sortInfoID;
    lUInt8     type[4];
    lUInt8     creator[4];
    lUInt32    uniqueIDSeed;
    lUInt32    nextRecordList;
    lUInt16    recordCount;
    lUInt16    firstEntry;
    bool read( LVStreamRef stream ) {
        // TODO: byte order support
        lvsize_t bytesRead = 0;
        if ( stream->Read(this, sizeof(PDBHdr), &bytesRead )!=LVERR_OK )
            return false;
        if ( bytesRead!=sizeof(PDBHdr) )
            return false;
        lvByteOrderConv cnv;
        if ( cnv.lsf() )
        {
            cnv.rev(&attributes);
            cnv.rev(&version);
            cnv.rev(&creationDate);
            cnv.rev(&modificationDate);
            cnv.rev(&lastBackupDate);
            cnv.rev(&modificationNumber);
            cnv.rev(&appInfoID);
            cnv.rev(&sortInfoID);
            cnv.rev(&uniqueIDSeed);
            cnv.rev(&nextRecordList);
            cnv.rev(&recordCount);
            cnv.rev(&firstEntry);
        }
        return true;
    }
    bool checkType( const char * str ) {
        return type[0]==str[0] && type[1]==str[1] && type[2]==str[2] && type[3]==str[3];
    }

    bool checkCreator( const char * str ) {
        return creator[0]==str[0] && creator[1]==str[1] && creator[2]==str[2] && creator[3]==str[3];
    }
};

struct PDBRecordEntry
{
    lUInt32 localChunkId;
    lUInt8  attributes[4];
    //lUInt8  uniqueID[3];
    bool read( LVStreamRef stream ) {
        // TODO: byte order support
        lvsize_t bytesRead = 0;
        if ( stream->Read(this, sizeof(PDBRecordEntry), &bytesRead )!=LVERR_OK )
            return false;
        if ( bytesRead!=sizeof(PDBRecordEntry) )
            return false;
        lvByteOrderConv cnv;
        if ( cnv.lsf() )
        {
            cnv.rev(&localChunkId);
        }
        return true;
    }
};

struct PalmDocPreamble
{
    lUInt16 compression; // 2  Compression   1 == no compression, 2 = PalmDOC compression (see below)
    lUInt16 unused;      // 2  Unused  Always zero
    lUInt32 textLength;  // 4  text length  Uncompressed length of the entire text of the book
    lUInt16 recordCount; // 2  record count  Number of PDB records used for the text of the book.
    lUInt16 recordSize;  // 2  record size  Maximum size of each record containing text, always 4096
    bool read( LVStreamRef stream ) {
        // TODO: byte order support
        lvsize_t bytesRead = 0;
        if ( stream->Read(this, sizeof(PalmDocPreamble), &bytesRead )!=LVERR_OK )
            return false;
        if ( bytesRead!=sizeof(PalmDocPreamble) )
            return false;
        lvByteOrderConv cnv;
        if ( cnv.lsf() )
        {
            cnv.rev(&compression); // 2  Compression   1 == no compression, 2 = PalmDOC compression (see below)
            cnv.rev(&textLength);  // 4  text length  Uncompressed length of the entire text of the book
            cnv.rev(&recordCount); // 2  record count  Number of PDB records used for the text of the book.
            cnv.rev(&recordSize);  // 2  record size  Maximum size of each record containing text, always 4096
        }
        if ( compression!=1 && compression!=2 )
            return false;
        return true;
    }
};

struct MobiPreamble : public PalmDocPreamble
{
    lUInt16 mobiEncryption;  // 2  Encryption Type	0 == no encryption, 1 = Old Mobipocket Encryption, 2 = Mobipocket Encryption
    lUInt16 unused2;     // 2  unknown, usually 0

    lUInt8  mobiSignature[4]; // 16	4	identifier	the characters M O B I
    lUInt32 hederLength; // 20	4	header length	the length of the MOBI header, including the previous 4 bytes
    lUInt32 mobiType;    //    24	4	Mobi type	The kind of Mobipocket file this is
            //    2 Mobipocket Book
            //    3 PalmDoc Book
            //    4 Audio
            //    257 News
            //    258 News_Feed
            //    259 News_Magazine
            //    513 PICS
            //    514 WORD
            //    515 XLS
            //    516 PPT
            //    517 TEXT
            //    518 HTML
    lUInt32 encoding; //    28	4	text Encoding	1252 = CP1252 (WinLatin1); 65001 = UTF-8
    lUInt32 uid; //    32	4	Unique-ID	Some kind of unique ID number (random?)
    lUInt32 fileVersion; //    36	4	File version	Version of the Mobipocket format used in this file.
    lUInt32 reserved[10]; //    40	40	Reserved	all 0xFF. In case of a dictionary, or some newer file formats, a few bytes are used from this range of 40 0xFFs
    lUInt32 firstNonBookIndex; //    80	4	First Non-book index?	First record number (starting with 0) that's not the book's text
    lUInt32 fullNameOffset; //    84	4	Full Name Offset	Offset in record 0 (not from start of file) of the full name of the book
    lUInt32 fullNameLength; //    88	4	Full Name Length	Length in bytes of the full name of the book
    lUInt32 locale; //    92	4	Locale	Book locale code. Low byte is main language 09= English, next byte is dialect, 08 = British, 04 = US. Thus US English is 1033, UK English is 2057.
    lUInt32 inputLanguage; //    96	4	Input Language	Input language for a dictionary
    lUInt32 outputLanguage; //    100	4	Output Language	Output language for a dictionary
    lUInt32 minVersion; //    104	4	Min version	Minimum mobipocket version support needed to read this file.
    lUInt32 firstImageIndex; //    108	4	First Image index?	First record number (starting with 0) that contains an image. Image records should be sequential.
    lUInt32 huffmanRecordOffset; //    112	4	Huffman Record Offset	The record number of the first huffman compression record.
    lUInt32 huffmanRecordCount; //    116	4	Huffman Record Count	The number of huffman compression records.
    lUInt32 reserved2[2]; //    120	8	?	eight bytes, often zeros
    lUInt32 mobiFlags; //    128	4	EXTH flags	bitfield. if bit 6 (0x40) is set, then there's an EXTH record
    lUInt32 unknown3[8]; //    132	32	?	32 unknown bytes, if MOBI is long enough
    lUInt32 drmOffset; //    164	4	DRM Offset	Offset to DRM key info in DRMed files. 0xFFFFFFFF if no DRM
    lUInt32 drmCount; //    168	4	DRM Count	Number of entries in DRM info. 0xFFFFFFFF if no DRM
    lUInt32 drmSize; //    172	4	DRM Size	Number of bytes in DRM info.
    lUInt32 drmFlags; //    176	4	DRM Flags	Some flags concerning the DRM info.


    bool read( LVStreamRef stream, lUInt16 & extraDataFlags ) {
        extraDataFlags = 0;
        lvsize_t bytesRead = 0;
        if ( stream->Read(this, sizeof(MobiPreamble), &bytesRead )!=LVERR_OK )
            return false;
        if ( bytesRead!=sizeof(MobiPreamble) )
            return false;
        lvByteOrderConv cnv;
        if ( cnv.lsf() )
        {
            cnv.rev(&compression); // 2  Compression   1 == no compression, 2 = PalmDOC compression (see below)
            cnv.rev(&textLength);  // 4  text length  Uncompressed length of the entire text of the book
            cnv.rev(&recordCount); // 2  record count  Number of PDB records used for the text of the book.
            cnv.rev(&recordSize);  // 2  record size  Maximum size of each record containing text, always 4096
            cnv.rev(&mobiEncryption);// 2  Encryption Type	0 == no encryption, 1 = Old Mobipocket Encryption, 2 = Mobipocket Encryption
            cnv.rev(&hederLength); // 20	4	header length	the length of the MOBI header, including the previous 4 bytes
            cnv.rev(&mobiType);    //    24	4	Mobi type	The kind of Mobipocket file this is
            cnv.rev(&encoding); //    28	4	text Encoding	1252 = CP1252 (WinLatin1); 65001 = UTF-8
            cnv.rev(&uid); //    32	4	Unique-ID	Some kind of unique ID number (random?)
            cnv.rev(&fileVersion); //    36	4	File version	Version of the Mobipocket format used in this file.
            cnv.rev(&firstNonBookIndex); //    80	4	First Non-book index?	First record number (starting with 0) that's not the book's text
            cnv.rev(&fullNameOffset); //    84	4	Full Name Offset	Offset in record 0 (not from start of file) of the full name of the book
            cnv.rev(&fullNameLength); //    88	4	Full Name Length	Length in bytes of the full name of the book
            cnv.rev(&locale); //    92	4	Locale	Book locale code. Low byte is main language 09= English, next byte is dialect, 08 = British, 04 = US. Thus US English is 1033, UK English is 2057.
            cnv.rev(&inputLanguage); //    96	4	Input Language	Input language for a dictionary
            cnv.rev(&outputLanguage); //    100	4	Output Language	Output language for a dictionary
            cnv.rev(&minVersion); //    104	4	Min version	Minimum mobipocket version support needed to read this file.
            cnv.rev(&firstImageIndex); //    108	4	First Image index?	First record number (starting with 0) that contains an image. Image records should be sequential.
            cnv.rev(&huffmanRecordOffset); //    112	4	Huffman Record Offset	The record number of the first huffman compression record.
            cnv.rev(&huffmanRecordCount); //    116	4	Huffman Record Count	The number of huffman compression records.
            cnv.rev(&mobiFlags); //    128	4	EXTH flags	bitfield. if bit 6 (0x40) is set, then there's an EXTH record
            cnv.rev(&drmOffset); //    164	4	DRM Offset	Offset to DRM key info in DRMed files. 0xFFFFFFFF if no DRM
            cnv.rev(&drmCount); //    168	4	DRM Count	Number of entries in DRM info. 0xFFFFFFFF if no DRM
            cnv.rev(&drmSize); //    172	4	DRM Size	Number of bytes in DRM info.
            cnv.rev(&drmFlags); //    176	4	DRM Flags	Some flags concerning the DRM info.
        }
        if ( compression!=1 && compression!=2 )
            return false;
        if ( mobiType!=2 && mobiType!=3 && mobiType!=517 && mobiType!=518
                 && mobiType!=257 && mobiType!=258 && mobiType!=259 )
            return false; // unsupported type
        if ( mobiEncryption!=0 )
            return false; // encryption is not supported
        if ( hederLength >= 0xE4 ) {
            stream->Seek(242-180, LVSEEK_CUR, NULL);
            stream->Read(&extraDataFlags);
            if ( cnv.lsf() )
                cnv.rev(&extraDataFlags);
//            if (extraDataFlags) {
//                CRLog::trace("extraDataFlags=%04x", (int)extraDataFlags);
//            }
        }
        return true;
    }
};

// format description from http://wiki.mobileread.com/wiki/EReader
struct EReaderHeader
{
    lUInt16 compression;    //    0-2	compression	Specifies compression and drm. 2 = palmdoc, 10 = zlib. 260 and 272 = DRM
    lUInt16 unknown1[2];    //    2-6	unknown	Value of 0 is used
    lUInt16 encoding;       //    6-8	encoding	Always 25152 (0x6240). All text must be encoded as Latin-1 cp1252
    lUInt16 smallPageCount; //    8-10	Number of small pages	The number of small font pages. If page index is not build in then 0.
    lUInt16 largePageCount; //    10-12	Number of large pages	The number of large font pages. If page index is not build in then 0.
    lUInt16 nonTextRecordStart; //12-14	Non-Text record start	The location of the first non text records. record 1 to this value minus 1 are all text records
    lUInt16 numberOfChapters;//    14-16	Number of chapters	The number of chapter index records contained in the file
    lUInt16 smallPageRecordCount; //    16-18	Number of small index	The number of small font page index records contained in the file
    lUInt16 largePageRecordCount; //    18-20	Number of large index	The number of large font page index records contained in the file
    lUInt16 imageCount;        //    20-22	Number of images	The number of images contained in the file
    lUInt16 linkCount;         //    22-24	Number of links	The number of links contained in the file
    lUInt16 metadataAvailable; //    24-26	Metadata avaliable	Is there a metadata record in the file? 0 = None, 1 = There is a metadata record
    lUInt16 unknown2; //    26-28	Unknown	Value of 0 is used
    lUInt16 footnoteRecordsCount; //    28-30	Number of Footnotes	The number of footnote records in the file
    lUInt16 sidebarRecordsCount; //    30-32	Number of Sidebars	The number of sidebar records in the file
    lUInt16 chapterIndexStart; //    32-34	Chapter index record start	The location of chapter index records. If there are no chapters use the value for the Last data record.
    lUInt16 unknown3; //    34-36	2560	Magic value that must be set to 2560
    lUInt16 smallPageIndexStart; //    36-38	Small page index start	The location of small font page index records. If page table is not built in use the value for the Last data record.
    lUInt16 largePageIndexStart; //    38-40	Large page index start	The location of large font page index records. If page table is not built in use the value for the Last data record.
    lUInt16 imageDataRecordStart; //    40-42	Image data record start	The location of the first image record. If there are no images use the value for the Last data record.
    lUInt16 linksRecordStart; //    42-44	Links record start	The location of the first link index record. If there are no links use the value for the Last data record.
    lUInt16 metadataRecordStart; //    44-46	Metadata record start	The location of the metadata record. If there is no metadata use the value for the Last data record.
    lUInt16 unknown4; //    46-48	Unknown	Value of 0 is used
    lUInt16 footnoteRecordStart; //    48-50	Footnote record start	The location of the first footnote record. If there are no footnotes use the value for the Last data record.
    lUInt16 sidebarRecordStart; //    50-52	Sidebar record start	The location of the first sidebar record. If there are no sidebars use the value for the Last data record.
    lUInt16 lastDataRecord; //    52-54	Last data record	The location of the last data record
    lUInt16 unknown5[39]; //    54-132	Unknown	Value of 0 is used
    bool read( LVStreamRef stream ) {
        lvsize_t bytesRead = 0;
        if ( stream->Read(this, sizeof(EReaderHeader), &bytesRead )!=LVERR_OK )
            return false;
        if ( bytesRead!=sizeof(EReaderHeader) )
            return false;
        lvByteOrderConv cnv;
        if ( cnv.lsf() )
        {
            cnv.rev(&compression);    //    0-2	compression	Specifies compression and drm. 2 = palmdoc, 10 = zlib. 260 and 272 = DRM
            cnv.rev(&encoding);       //    6-8	encoding	Always 25152 (0x6240). All text must be encoded as Latin-1 cp1252
            cnv.rev(&smallPageCount); //    8-10	Number of small pages	The number of small font pages. If page index is not build in then 0.
            cnv.rev(&largePageCount); //    10-12	Number of large pages	The number of large font pages. If page index is not build in then 0.
            cnv.rev(&nonTextRecordStart); //12-14	Non-Text record start	The location of the first non text records. record 1 to this value minus 1 are all text records
            cnv.rev(&numberOfChapters);//    14-16	Number of chapters	The number of chapter index records contained in the file
            cnv.rev(&smallPageRecordCount); //    16-18	Number of small index	The number of small font page index records contained in the file
            cnv.rev(&largePageRecordCount); //    18-20	Number of large index	The number of large font page index records contained in the file
            cnv.rev(&imageCount);        //    20-22	Number of images	The number of images contained in the file
            cnv.rev(&linkCount);         //    22-24	Number of links	The number of links contained in the file
            cnv.rev(&metadataAvailable); //    24-26	Metadata avaliable	Is there a metadata record in the file? 0 = None, 1 = There is a metadata record
            cnv.rev(&footnoteRecordsCount); //    28-30	Number of Footnotes	The number of footnote records in the file
            cnv.rev(&sidebarRecordsCount); //    30-32	Number of Sidebars	The number of sidebar records in the file
            cnv.rev(&chapterIndexStart); //    32-34	Chapter index record start	The location of chapter index records. If there are no chapters use the value for the Last data record.
            cnv.rev(&smallPageIndexStart); //    36-38	Small page index start	The location of small font page index records. If page table is not built in use the value for the Last data record.
            cnv.rev(&largePageIndexStart); //    38-40	Large page index start	The location of large font page index records. If page table is not built in use the value for the Last data record.
            cnv.rev(&imageDataRecordStart); //    40-42	Image data record start	The location of the first image record. If there are no images use the value for the Last data record.
            cnv.rev(&linksRecordStart); //    42-44	Links record start	The location of the first link index record. If there are no links use the value for the Last data record.
            cnv.rev(&metadataRecordStart); //    44-46	Metadata record start	The location of the metadata record. If there is no metadata use the value for the Last data record.
            cnv.rev(&footnoteRecordStart); //    48-50	Footnote record start	The location of the first footnote record. If there are no footnotes use the value for the Last data record.
            cnv.rev(&sidebarRecordStart); //    50-52	Sidebar record start	The location of the first sidebar record. If there are no sidebars use the value for the Last data record.
            cnv.rev(&lastDataRecord); //    52-54	Last data record	The location of the last data record
        }
        if ( compression!=1 && compression!=2 && compression!=10 )
            return false;
        return true;
    }
};

struct PluckerPreamble {
    lUInt32 signature; // 	4 	Numeric 	Must contain the value 0x6C6E6368.
    lUInt16 hdrVersion; // 	2 	Numeric 	Must have the value 3.
    lUInt16 hdrEncoding; // 	2 	Numeric 	Must have the value 0.
    lUInt16 verStrWords; // 	2 	Numeric 	The number of two-byte words following, containing the version string.
//    char  	2 * verStrWords 	String 	NUL-terminated ISO Latin-1 string, padded at end if necessary with a zero byte to an even-byte boundary, containing a version string to display to the user containing version information for the document.
//    pqaTitleWords 	2 	Numeric 	The number of two-byte words in the following pqaTitleStr.
//    pqaTitleStr 	2 * pqaTitleWords 	String 	NUL-terminated ISO Latin-1 string, padded at end if necessary with a zero byte to an even-byte boundary, containing a title string for iconic display of the document.
//    iconWords 	2 	Numeric 	Number of two-byte words in the following icon image.
//    icon 	2 * iconWords 	Image 	Image (32x32) in Palm image format to be used as an icon to represent the document on a desktop-style display. The image may not use a custom color map.
//    smIconWords 	2 	Numeric 	Number of two-byte words in the following icon image.
//    smIcon 	2 * smIconWords 	Image 	Small image (15x9) in Palm image format to be used as an icon to represent the document on a desktop-style display. The image may not use a custom color map.
};

class PDBFile;

class LVPDBContainerItem : public LVContainerItemInfo {
protected:
    LVStreamRef _stream;
    PDBFile * _file;
    int _startBlock;
    int _size;
    lString32 _name;
public:
    /// returns object size (file size or directory entry count)
    virtual lverror_t GetSize( lvsize_t * pSize ) {
        *pSize = _size;
		return LVERR_OK;
    }
    virtual lvsize_t        GetSize() const { return _size; }
    virtual const lChar32 * GetName() const { return _name.c_str(); }
    virtual lUInt32         GetFlags() const { return 0; }
    virtual bool            IsContainer() const { return false; }
    virtual LVStreamRef openStream() {
        // TODO: implement stream creation
        return LVStreamRef();
    }
    LVPDBContainerItem( LVStreamRef stream, PDBFile * file, lString32 name, int startBlockIndex, int size )
        : _stream(stream), _file(file), _startBlock(startBlockIndex), _size(size), _name(name) {
    }
};

class LVPDBRegionContainerItem : public LVPDBContainerItem {
public:
    /// returns object size (file size or directory entry count)
    virtual lUInt32         GetFlags() const { return 0; }
    virtual LVStreamRef openStream() {
        // return region of base stream
        return LVStreamRef( new LVStreamFragment( _stream, _startBlock, _size ) );
    }
    LVPDBRegionContainerItem( LVStreamRef stream, PDBFile * file, lString32 name, int startOffset, int size )
        : LVPDBContainerItem(stream, file, name, startOffset, size) {
    }
};

class LVPDBContainer : public LVContainer
{
    LVPtrVector<LVPDBContainerItem> _list;
    LVStreamRef _stream;
public:
    virtual LVContainer * GetParentContainer() { return NULL; }

    void addItem ( LVPDBContainerItem * item ) {
        _list.add(item);
    }

    //virtual const LVContainerItemInfo * GetObjectInfo(const lChar32 * pname);
    virtual const LVContainerItemInfo * GetObjectInfo(int index) {
        if ( index>=0 && index<_list.length() )
            return _list[index];
		return NULL;
    }
    virtual const LVContainerItemInfo * GetObjectInfo(lString32 name) { return NULL; }
    virtual int GetObjectCount() const { return _list.length(); }
    /// returns object size (file size or directory entry count)
    virtual lverror_t GetSize( lvsize_t * pSize ) {
        *pSize = _list.length();
		return LVERR_OK;
    }

    virtual LVStreamRef OpenStream( const lChar32 * fname, lvopen_mode_t mode ) {
        if ( mode!=LVOM_READ )
            return LVStreamRef();
        for ( int i=0; i<_list.length(); i++ ) {
            //CRLog::trace("OpenStream(%s) : %s", LCSTR(lString32(fname)), LCSTR(lString32(_list[i]->GetName())) );
            if ( !lStr_cmp(_list[i]->GetName(), fname) )
                return _list[i]->openStream();
        }
        return LVStreamRef();
    }

    void setStream( LVStreamRef stream ) {
        _stream = stream;
    }

    LVPDBContainer( ) {
        //_contentStream = LVStreamRef((LVStream*)file);
    }
    virtual ~LVPDBContainer() { }
};

static bool pattern_cmp( const lUInt8 * buf, const char * pattern ) {
    for ( int i=0; pattern[i]; i++ )
        if ( tolower(buf[i])!=pattern[i] )
            return false;
    return true;
}

class PDBFile : public LVNamedStream {
public:
    enum Format {
        UNKNOWN,
        PALMDOC,
        EREADER,
        PLUCKER,
        MOBI
    };
private:

    struct Record {
        lUInt32 offset;
        lUInt32 size;
        lUInt32 unpoffset;
        lUInt32 unpsize;
    };
    LVArray<Record> _records;
    LVStreamRef _stream;
    Format _format;
    int _compression;
    lUInt32 _textSize;
    int _recordCount; // text record count
    // read buffer
    LVArray<lUInt8> _buf;
    int     _bufIndex;
    lvpos_t _bufOffset;
    lvsize_t _bufSize;
    lvpos_t _pos;
    lUInt16 _mobiExtraDataFlags;
    int _mobiNcxIdx;      // PDB record number of the TOC (INDX/NCX) header, -1 if none
    lUInt32 _mobiEncoding; // MOBI text encoding (65001 = UTF-8, 1252 = cp1252)
    CRPropRef m_doc_props;

    // c.f., lvtinydom.cpp's legacy ldomUnpack
    bool zlibUnpack( const lUInt8 * compbuf, size_t compsize, lUInt8 * &dstbuf, lUInt32 & dstsize  )
    {
        lUInt8 tmp[UNPACK_BUF_SIZE]; // 256K buffer for uncompressed data
        int ret;
        z_stream z = { 0 };
        z.zalloc = Z_NULL;
        z.zfree = Z_NULL;
        z.opaque = Z_NULL;
        ret = inflateInit( &z );
        if ( ret != Z_OK )
            return false;
        z.avail_in = compsize;
        z.next_in = (unsigned char *)compbuf;
        lUInt32 uncompressed_size = 0;
        lUInt8 *uncompressed_buf = NULL;
        while (true) {
            z.avail_out = UNPACK_BUF_SIZE;
            z.next_out = tmp;
            ret = inflate( &z, Z_SYNC_FLUSH );
            if (ret != Z_OK && ret != Z_STREAM_END) { // some error occured while unpacking
                inflateEnd(&z);
                if (uncompressed_buf)
                    free(uncompressed_buf);
                // printf("inflate() error: %d (%d > %d)\n", ret, compsize, uncompressed_size);
                return false;
            }
            lUInt32 have = UNPACK_BUF_SIZE - z.avail_out;
            uncompressed_buf = cr_realloc(uncompressed_buf, uncompressed_size + have);
            memcpy(uncompressed_buf + uncompressed_size, tmp, have ); // cppcheck-suppress uninitvar
            uncompressed_size += have;
            if (ret == Z_STREAM_END) {
                break;
            }
            // printf("inflate() additional call needed (%d > %d)\n", compsize, uncompressed_size);
        }
        inflateEnd(&z);
        dstsize = uncompressed_size;
        dstbuf = uncompressed_buf;
        // printf("inflate() done %d > %d\n", compsize, uncompressed_size);
        return true;
    }

    //LVPDBContainer * _container;
    bool unpack( LVArray<lUInt8> & dst, LVArray<lUInt8> & src ) {
        int srclen = src.length();
        dst.reset();
        dst.reserve(srclen);

        if ( _compression==2 ) {
            // PalmDOC
            int pos = 0;
            lInt32 b;

            while (pos<srclen) {
                b = src[pos];
                pos++;
                if (b > 0 && b < 9) {
                    // 1..8 bytes follow
                    if (pos + b > srclen)
                        break;
                    for (int i=0; i<(int)b; i++)
                        dst.add(src[pos++]);
                } else if (b < 128) {
                    // unmodified single byte
                    dst.add((lUInt8)b);
                } else if (b >= 0xc0) {
                    dst.add(' ');
                    dst.add(b & 0x7f);
                } else {
                    if (pos >= srclen)
                        break;
                    lUInt32 z = ((b & 0x3f) << 8) + src[pos];
                    pos++;
                    int offset = z >> 3;
                    int size = (z & 7) + 3;
                    int srcpos = dst.length() - offset;
                    for (int i = 0; i < size; i++) {
                        if (srcpos >= 0) {
                            dst.add(dst[srcpos++]);
                        } else {
                            dst.add('?');
                            //CRLog::trace("wrong offset");
                        }
                    }
                }
            }
        } else if ( _compression==10 ) {
            // zlib
            /// unpack data from src to dst
            lUInt8 * dstbuf;
            lUInt32 dstsize;
            if ( !zlibUnpack( src.get(), src.size(), dstbuf, dstsize ) )
                return false;
            dst.add(dstbuf, dstsize);
            free(dstbuf);
        } else if ( _compression==17480 ) {
            // zlib
            // TODO: shouldn't it be HUFFMAN unpacker?
            /// unpack data from src to dst
            lUInt8 * dstbuf;
            lUInt32 dstsize;
            if ( !zlibUnpack( src.get(), src.size(), dstbuf, dstsize ) )
                return false;
            dst.add(dstbuf, dstsize);
            free(dstbuf);
        }
        return true;
    }

    void removeExtraData(int index, LVArray<lUInt8> & buf) {
        if (index >= _records.length() || !_mobiExtraDataFlags)
            return;
        for (int flag = 0x8000; flag; flag >>= 1) {
            if (!(_mobiExtraDataFlags & flag))
                continue;
            if (buf.length() == 0)
                return; // nothing to strip from an empty record
            lInt32 n = buf[buf.length()-1];
            if (flag == 1) {
                n &= 3;

                _records[index].size -= 1;
                buf.erase(buf.length()-1, 1);

                if (n>0) {
                    //CRLog::trace("block %d: removing %d bytes of multibyte character", index, n);

                    for (int i=n; i>0; i--) {
                        n = buf[buf.length() - 1];
                        if (!(n & 0x80))
                            break;
                        buf.erase(buf.length() - 1, 1);
                        if ((n & 0xC0) != 0x80)
                            break;
                    }
                }

            } else {
                if (!(n & 0x80)) {
                    lUInt32 n2 = buf[buf.length()-2];
                    n = (n & 0x7F) | ((n2 & 0x7F) << 16);
                } else {
                    n = n & 0x7F;
                }
                if (n > 0 && buf.length() >= n) {
                    //CRLog::trace("block %d: removing %d bytes of extra data type %d", index, n, flag);
                    _records[index].size -= n;
                    buf.erase(buf.length()-n, n);
                }
            }
//            if (n && buf.length() >= n) {
//                _records[index].size -= n;
//                buf.erase(buf.length()-n, n);

//                if (flag == 1 && n > 1) {
//                    CRLog::trace("block %d: removing %d bytes of multibyte character", index, n - 1);
//                    // remove extra utf-8 points
//                    while (buf.length()) {
//                        n = buf[buf.length() - 1];
//                        if (!(n & 0x80))
//                            break;
//                        buf.erase(buf.length() - 1, 1);
//                        if ((n & 0xC0) != 0x80)
//                            break;
//                    }
//                }
//            }
        }
    }

    bool readRecordNoUnpack(int index, LVArray<lUInt8> * dstbuf) {
        if (index >= _records.length())
            return false;
        dstbuf->reset();
        dstbuf->addSpace(_records[index].size);
        lvsize_t bytesRead = 0;
        _stream->SetPos(_records[index].offset);
        if (_stream->Read(dstbuf->get(), _records[index].size, &bytesRead) != LVERR_OK)
            return false;
        if (bytesRead != _records[index].size)
            return false;
        return true;
    }
    bool readRecord( int index, LVArray<lUInt8> * dstbuf ) {
        if (index >= _records.length())
            return false;
        LVArray<lUInt8> srcbuf;
        LVArray<lUInt8> * buf = _compression ? &srcbuf : dstbuf;
        if (!readRecordNoUnpack(index, buf))
            return false;

        if (_mobiExtraDataFlags && index < _recordCount)
            removeExtraData(index, *buf);

        if (!_compression)
            return true;
        // unpack
        return unpack(*dstbuf, srcbuf);
    }

    // --- MOBI TOC (INDX/NCX index) support ---

    // Read a big-endian u32 at byte offset off of an INDX record buffer.
    static lUInt32 indxWord(LVArray<lUInt8> & r, int off) {
        if (off < 0 || off + 4 > r.length())
            return 0;
        const lUInt8 * p = r.get() + off;
        return ((lUInt32)p[0] << 24) | ((lUInt32)p[1] << 16) | ((lUInt32)p[2] << 8) | p[3];
    }

    // Read a big-endian u16 at byte offset off of an INDX record buffer.
    static lUInt16 indxHalf(LVArray<lUInt8> & r, int off) {
        if (off < 0 || off + 2 > r.length())
            return 0;
        const lUInt8 * p = r.get() + off;
        return (lUInt16)((p[0] << 8) | p[1]);
    }

public:

    // Parse the INDX records starting at PDB record ncxidx and collect TOC
    // entries (filepos, title, level). See the "MOBI TOC support" comment
    // block above for the record layout. Returns false on any structural
    // error (caller then just gets no TOC from us).
    bool readMobiToc(int ncxidx, lUInt32 encoding, LVPtrVector<MobiTocEntry> & toc) {
        if (ncxidx < 0 || ncxidx + 1 >= _records.length())
            return false;
        LVArray<lUInt8> hdr;
        if (!readRecordNoUnpack(ncxidx, &hdr) || hdr.length() < 0xC0)
            return false;
        if (hdr[0] != 'I' || hdr[1] != 'N' || hdr[2] != 'D' || hdr[3] != 'X')
            return false;
        lUInt32 indxCount = indxWord(hdr, 4 + 5 * 4);  // word 5: number of index records
        lUInt32 ncncx = indxWord(hdr, 4 + 12 * 4);     // word 12: number of CNCX records
        lUInt32 tagxOff = indxWord(hdr, 4 + 44 * 4);   // word 44: offset of TAGX section
        if (indxCount < 1 || indxCount > 0xFFFF || ncncx > 0xFFFF)
            return false;
        // Compute in 64-bit: a crafted tagxOff near UINT32_MAX would wrap the
        // u32 sum and pass the bounds check, leading to OOB reads below.
        if ((lUInt64)tagxOff + 12 > (lUInt64)hdr.length())
            tagxOff = 0; // invalid: try the fallback below
        if (tagxOff == 0 || hdr[tagxOff] != 'T' || hdr[tagxOff+1] != 'A' || hdr[tagxOff+2] != 'G' || hdr[tagxOff+3] != 'X') {
            // Word 44 can be junk on some files; KindleUnpack instead starts
            // the TAGX section at the INDX header-length field (word 0).
            // Retry with that before giving up (as calibre's
            // get_tag_section_start() does with its own fallback).
            lUInt32 alt = indxWord(hdr, 4);
            if (alt != tagxOff && (lUInt64)alt + 12 <= (lUInt64)hdr.length()
                    && hdr[alt] == 'T' && hdr[alt+1] == 'A' && hdr[alt+2] == 'G' && hdr[alt+3] == 'X')
                tagxOff = alt;
            else
                return false;
        }
        lUInt32 firstEntryOff = indxWord(hdr, tagxOff + 4);
        lUInt32 controlByteCount = indxWord(hdr, tagxOff + 8);
        if (controlByteCount < 1 || controlByteCount > 32)
            return false;
        // TAGX entries: 4 bytes each (tag, num_of_values, bitmask, eof),
        // from offset 12 to firstEntryOff within the TAGX section.
        // (64-bit sum: firstEntryOff is attacker-controlled and could wrap a u32 sum.)
        if (firstEntryOff <= 12 || (lUInt64)tagxOff + firstEntryOff > (lUInt64)hdr.length())
            return false;
        struct TagxTag { lUInt8 tag; lUInt8 numOfValues; lUInt8 bitmask; lUInt8 eof; };
        TagxTag tagxTags[64];
        int tagxTagCount = 0;
        for (lUInt32 i = 12; i + 4 <= firstEntryOff && tagxTagCount < 64; i += 4) {
            const lUInt8 * p = hdr.get() + tagxOff + i;
            tagxTags[tagxTagCount].tag = p[0];
            tagxTags[tagxTagCount].numOfValues = p[1];
            tagxTags[tagxTagCount].bitmask = p[2];
            tagxTags[tagxTagCount].eof = p[3];
            tagxTagCount++;
        }
        if (12 + 4 * (lUInt64)tagxTagCount < firstEntryOff)
            CRLog::trace("MOBI TOC: TAGX section declares more than %d tag entries (firstEntryOff=%u); extra entries ignored",
                    tagxTagCount, firstEntryOff);

        // CNCX records: pool of <vwi len><string> entries. Build offset->string map.
        LVHashTable<lUInt32, lString32> cncxMap(1024);
        for (lUInt32 k = 0; k < ncncx; k++) {
            int recIdx = ncxidx + 1 + indxCount + k;
            if (recIdx >= _records.length())
                break;
            LVArray<lUInt8> cn;
            if (!readRecordNoUnpack(recIdx, &cn))
                break;
            lUInt32 base = k * 0x10000;
            int p = 0;
            while (p < cn.length()) {
                lUInt32 len;
                int lenStart = p;
                // Unsigned compare: a crafted 5-byte vwi can exceed INT_MAX.
                if (!readMobiVwi(cn.get(), cn.length(), p, len) || len > (lUInt32)(cn.length() - p))
                    break;
                cncxMap.set(base + lenStart, decodeMobiCncxString(cn.get() + p, len, encoding));
                p += len;
            }
        }

        // Index records: entries listed in the IDXT table at the end.
        for (lUInt32 ri = 0; ri < indxCount; ri++) {
            int recIdx = ncxidx + 1 + ri;
            if (recIdx >= _records.length())
                break;
            LVArray<lUInt8> r;
            if (!readRecordNoUnpack(recIdx, &r) || r.length() < 12)
                continue;
            if (r[0] != 'I' || r[1] != 'N' || r[2] != 'D' || r[3] != 'X')
                break;
            lUInt32 idxtOff = indxWord(r, 4 + 4 * 4); // word 4: offset of IDXT section
            lUInt32 entryCount = indxWord(r, 4 + 5 * 4); // word 5: number of entries
            // Cap entryCount (consistent with the other caps) and compute the
            // table end in 64-bit: a crafted entryCount >= 0x80000000 would
            // wrap the u32 sum and pass the bounds check.
            if (entryCount > 0xFFFF)
                continue;
            if ((lUInt64)idxtOff + 4 + 2 * (lUInt64)entryCount > (lUInt64)r.length())
                continue;
            if (r[idxtOff] != 'I' || r[idxtOff+1] != 'D' || r[idxtOff+2] != 'X' || r[idxtOff+3] != 'T')
                continue;
            // Entry start offsets from the IDXT table; the last entry ends at IDXT.
            for (lUInt32 j = 0; j < entryCount; j++) {
                int start = indxHalf(r, idxtOff + 4 + 2 * j);
                int end = (j + 1 < entryCount) ? indxHalf(r, idxtOff + 4 + 2 * (j + 1)) : idxtOff;
                if (start < 0 || end <= start || end > r.length())
                    continue;
                const lUInt8 * rec = r.get() + start;
                int recSize = end - start;
                int pos = 0;
                // Entry ident: <1-byte length><string> (unused for the TOC, skip it)
                if (pos >= recSize)
                    continue;
                lUInt32 identLen = rec[pos++];
                if (pos + (lUInt64)identLen > (lUInt64)recSize)
                    continue;
                pos += identLen;
                // Control bytes
                if (pos + (lUInt64)controlByteCount > (lUInt64)recSize)
                    continue;
                lUInt8 controlBytes[32] = { 0 };
                memcpy(controlBytes, rec + pos, controlByteCount);
                pos += controlByteCount;
                // Tag values
                lUInt32 filepos = 0;
                lUInt32 titleOff = (lUInt32)-1;
                int level = 0;
                bool haveFilepos = false;
                // Dispatch a decoded tag value to the TOC entry fields:
                // tag 1 = filepos, tag 3 = title offset in CNCX, tag 4 = level.
                auto assignTagValue = [&](lUInt8 tag, lUInt32 val) {
                    if (tag == 1 && !haveFilepos) { filepos = val; haveFilepos = true; }
                    else if (tag == 3 && titleOff == (lUInt32)-1) titleOff = val;
                    else if (tag == 4) level = (int)val;
                };
                int cbIndex = 0;
                for (int t = 0; t < tagxTagCount; t++) {
                    const TagxTag & tg = tagxTags[t];
                    if (cbIndex >= (int)controlByteCount)
                        break;
                    if (tg.eof == 0x01) {
                        cbIndex++; // header-terminating entry: consume one control byte
                        continue;
                    }
                    lUInt32 masked = controlBytes[cbIndex] & tg.bitmask;
                    if (masked == 0)
                        continue; // tag not present
                    int valueCount = 0;
                    int valueBytes = -1; // or byte-length of the value list
                    if (masked == tg.bitmask && countSetBits(tg.bitmask) > 1) {
                        // All bits set and multi-bit mask: a vwi byte-length follows
                        lUInt32 vb;
                        if (!readMobiVwi(rec, recSize, pos, vb))
                            break;
                        valueBytes = vb;
                    } else {
                        // Shift to get the count value from the masked bits
                        lUInt32 mask = tg.bitmask, v = masked;
                        while (mask && !(mask & 1)) {
                            mask >>= 1;
                            v >>= 1;
                        }
                        valueCount = (masked == tg.bitmask && countSetBits(tg.bitmask) == 1) ? 1 : (int)v;
                    }
                    // Read the values
                    if (valueBytes >= 0) {
                        int total = 0;
                        while (total < valueBytes) {
                            lUInt32 val;
                            int before = pos;
                            if (!readMobiVwi(rec, recSize, pos, val))
                                break;
                            total += pos - before;
                            assignTagValue(tg.tag, val);
                        }
                    } else {
                        for (int n = 0; n < valueCount * tg.numOfValues; n++) {
                            lUInt32 val;
                            if (!readMobiVwi(rec, recSize, pos, val))
                                break;
                            assignTagValue(tg.tag, val);
                        }
                    }
                }
                if (pos < recSize)
                    CRLog::trace("MOBI TOC: index entry %u has %d unconsumed trailing byte(s)", j, recSize - pos);
                if (!haveFilepos)
                    continue;
                lString32 title;
                if (titleOff != (lUInt32)-1) {
                    if (!cncxMap.get(titleOff, title))
                        CRLog::trace("MOBI TOC: index entry %u title offset %u not found in CNCX pool; entry skipped", j, titleOff);
                }
                if (title.empty()) {
                    // Skip blank TOC row (children, if any, will attach to this entry's parent).
                    continue;
                }
                MobiTocEntry * entry = new MobiTocEntry();
                entry->filepos = filepos;
                entry->level = level;
                entry->title = title;
                toc.add(entry);
            }
        }
        return toc.length() > 0;
    }

    bool readBlock( int index ) {
        if ( index<0 || index>=_recordCount )
            return false;
        if ( index==_bufIndex )
            return true; // already read
        bool res = readRecord( index+1, &_buf );
        if ( !res )
            return false;
        _bufIndex = index;
        _bufOffset = _records[index+1].unpoffset;
        _bufSize = _records[index+1].unpsize;
        return true;
    }

    int findBlock( lvpos_t pos ) {
        if ( pos==_textSize )
            return _recordCount-1;
        for ( int i=0; i<_recordCount; i++ ) {
            if ( pos>=_records[i+1].unpoffset && pos<_records[i+1].unpoffset+_records[i+1].unpsize )
                return i;
        }
        return -1;
    }

    bool seek( lvpos_t pos ) {
        int index = findBlock(pos);
        if ( index<0 )
            return false;
        bool res = readBlock( index );
        if ( !res )
            return false;
        _pos = pos;
        return true;
    }

public:

//    LVContainerRef getContainer() {
//        if ( !_container )
//            _container = new LVPDBContainer();
//        return LVContainerRef(&_container);
//    }


//    static PDBFile * create( LVStreamRef stream, int & format ) {
//        format = 0;
//        PDBFile * res = new PDBFile();
//        if ( res->open(stream, true, format) ) {
//            format = res->_format;
//            return res;
//        }
//        delete res;
//        return NULL;
//    }

    void detectFormat( doc_format_t & contentFormat ) {
        if ( contentFormat == doc_format_none ) {
            // autodetect format
            LVArray<lUInt8> buf;
            readRecord(1, &buf);
            int bytesRead = buf.length();
            if ( bytesRead>0 ) {
                int pmlCount = 0;
                int htmlCount = 0;
                lString32 pmlChars("pXxCcriuovtnsblaUBSmqQI");
                for ( int i=0; i<bytesRead-10; i++ ) {
                    const lUInt8 * p = buf.get() + i;
                    if ( p[0]=='\\' ) {
                        if ( pmlChars.pos(lString32((const lChar8 *)p+1, 1)) >=0 )
                            pmlCount++;
                    } else if (p[0]=='<') {
                        if ( pattern_cmp(p+1, "html") )
                            htmlCount+=100;
                        if ( pattern_cmp(p+1, "head") )
                            htmlCount+=50;
                        if ( pattern_cmp(p+1, "body") )
                            htmlCount+=50;
                        if ( pattern_cmp(p+1, "h1") || pattern_cmp(p+1, "h2") || pattern_cmp(p+1, "h3") || pattern_cmp(p+1, "h4"))
                            htmlCount+=5;
                        if ( pattern_cmp(p+1, "p>") || pattern_cmp(p+1, "b>") || pattern_cmp(p+1, "i>") || pattern_cmp(p+1, "li>") || pattern_cmp(p+1, "ul>"))
                            htmlCount+=10;
                    }
                }
                if ( pmlCount<5 && htmlCount<10 ) {
                    contentFormat = doc_format_txt;
                } else if ( pmlCount > htmlCount ) {
                    contentFormat = doc_format_fb2;
                } else {
                    contentFormat = doc_format_html;
                }
            }
            SetPos(0);
        }
    }

    CRPropRef getDocProps() {
        return m_doc_props;
    }

    bool open( LVStreamRef stream, LVPDBContainer * container, bool validateContent, doc_format_t & contentFormat ) {
        contentFormat = doc_format_none;
        _format = UNKNOWN;
        stream->SetPos(0);
        lUInt32 fsize = stream->GetSize();
        PDBHdr hdr;
        PDBRecordEntry entry;
        if ( !hdr.read(stream) )
            return false;
        if ( hdr.recordCount==0 )
            return false;

        if ( hdr.checkType("TEXt") && hdr.checkCreator("REAd") )
            _format = PALMDOC;
        if ( hdr.checkType("PNRd") && hdr.checkCreator("PPrs") )
            _format = EREADER;
        if ( hdr.checkType("BOOK") && hdr.checkCreator("MOBI") )
            _format = MOBI;
        if ( hdr.checkType("Data") && hdr.checkCreator("Plkr") )
            _format = PLUCKER;
//        if ( hdr.checkType("ToGo") && hdr.checkCreator("ToGo") )
//            _format = ISILO;
        if ( _format==UNKNOWN )
            return false; // UNKNOWN FORMAT

        stream->SetPos(0x4E);
        lUInt32 lastEntryStart = 0;
        _records.addSpace(hdr.recordCount);
        for ( int i=0; i<hdr.recordCount; i++ ) {
            if ( !entry.read(stream) )
                return false;
            lUInt32 pos = entry.localChunkId;
            if ( pos<lastEntryStart || pos>=fsize )
                return false;
            _records[i].offset = pos;
            if ( i>0 )
                _records[i-1].size = pos - _records[i-1].offset;
            lastEntryStart = pos;
        }
        _records[_records.length()-1].size = fsize - _records[_records.length()-1].offset;


        _stream = stream;

        if ( _format==EREADER ) {
            if ( _records[0].size<sizeof(EReaderHeader) )
                return false;
            EReaderHeader preamble = { 0 };
            stream->SetPos(_records[0].offset);
            if ( !preamble.read(stream) )
                return false; // invalid preamble
            _recordCount = preamble.nonTextRecordStart - 1;
            if ( _recordCount>=_records.length() )
                return false;
            _compression = preamble.compression;
            if ( _compression==1 )
                _compression = 0;
            _textSize = (lUInt32)-1;
            if ( preamble.imageCount && container ) {
                for ( int index=preamble.imageDataRecordStart; index<preamble.imageDataRecordStart+preamble.imageCount; index++ ) {
                    lUInt32 start = _records[index].offset + 62;
                    lUInt32 size = _records[index].size - 62;
                    if ( start<fsize && start+size<=fsize ) {
                        stream->SetPos(_records[index].offset);
                        if ( stream->ReadByte()=='P' && stream->ReadByte()=='N' && stream->ReadByte()=='G' && stream->ReadByte()==' ' ) {
                            // header ok, adding item
                            char name[33] = { 0 };
                            lvsize_t bytesRead = 0;
                            stream->Read(name, 32, &bytesRead);
                            if ( name[0] ) {
                                lString32 fname = lString32(name);
                                container->addItem( new LVPDBRegionContainerItem( stream, this, fname, start, size ) );
                            }
                        }
                    }
                }
            }
        } else if (_format==MOBI ) {
            if ( _records[0].size<sizeof(MobiPreamble) )
                return false;
            if (!validateContent)
                contentFormat = doc_format_pdb;

            MobiPreamble preamble = {};
            stream->SetPos(_records[0].offset);
            if ( !preamble.read(stream, _mobiExtraDataFlags) )
                return false; // invalid preamble
            if ( preamble.recordCount>=_records.length() )
                return false;
            _compression = preamble.compression;
            if ( _compression==1 )
                _compression = 0;
            _textSize = preamble.textLength;
            _recordCount = preamble.firstNonBookIndex - 1;
            _mobiEncoding = preamble.encoding;
            // TOC (INDX/NCX) header record number, at offset 244 of record 0.
            // The MOBI header starts at offset 16 and is hederLength bytes
            // long; ncxidx sits at 244..248, so the header must be at least
            // 232 bytes for those bytes to actually be the ncxidx field (on
            // short-header files they'd belong to EXTH/fullname data instead).
            // Note: on hybrid MOBI+KF8 files, the old (MOBI 6) part we render
            // usually has ncxidx == 0xFFFFFFFF, so no TOC index there — the
            // KF8 part (which we don't support) carries the real one.
            _mobiNcxIdx = -1;
            if (_records[0].size >= 248 && preamble.hederLength >= 232) {
                lUInt32 ncxidx = 0;
                stream->SetPos(_records[0].offset + 244);
                if (stream->Read(&ncxidx)) {
                    lvByteOrderConv cnv2;
                    cnv2.rev(&ncxidx);
                    if (ncxidx != 0xFFFFFFFF && ncxidx < (lUInt32)_records.length())
                        _mobiNcxIdx = (int)ncxidx;
                }
            }
            lUInt32 coverOffset = (lUInt32)-1;
            lUInt32 thumbOffset = 0;
            bool title_set = false;
            if (preamble.mobiFlags & 0x40) {
                // EXTH present
                stream->SetPos(_records[0].offset + 16 + preamble.hederLength);
                char exth_tag[4] = {0, 0, 0, 0};
                stream->Read(&exth_tag, 4, NULL);
                if (exth_tag[0] == 'E' && exth_tag[1] == 'X' && exth_tag[2] == 'T' && exth_tag[3] == 'H') {
                	CRLog::trace("EXTH record found");
                    lUInt32 hdrLen = 0;
                    lUInt32 recCount = 0;
                    lvByteOrderConv cnv;
                    stream->Read(&hdrLen);
                    stream->Read(&recCount);
                    if ( cnv.lsf() ) {
                        cnv.rev(&hdrLen);
                        cnv.rev(&recCount);
                    }
                    LVArray<lUInt8> buf2;
                    lString32 authors;
                    lString32 identifiers;
                    lString32 keywords;
                    bool authors_set = false;
                    bool identifiers_set = false;
                    bool keywords_set = false;
                    for (lUInt32 i=0; i<recCount; i++) {
                        lUInt32 recType = 0;
                        lUInt32 recLen = 0;
                        stream->Read(&recType);
                        stream->Read(&recLen);
                        if ( cnv.lsf() ) {
                            cnv.rev(&recType);
                            cnv.rev(&recLen);
                        }
                        buf2.reset();
                        if (recLen > 8) {
                            lvpos_t nextPos = stream->GetPos() + recLen - 8;
                            //================================
                            if (recLen == 12 && recType == 201) {
                                stream->Read(&coverOffset);
                                cnv.msf(&coverOffset);
                            } else if (recLen == 12 && recType == 202) {
                                stream->Read(&thumbOffset);
                                cnv.msf(&thumbOffset);
                            } else {
                                buf2.addSpace(recLen);
                                if (stream->Read(buf2.get(), recLen - 8, NULL) != LVERR_OK)
                                    break;
                                if (recType == 100) {
                                    lString8 s((const char *)buf2.get(), recLen - 8);
                                    CRLog::trace("MOBI author: %s", s.c_str());
                                    if (authors_set) {
                                        authors << "\n" << Utf8ToUnicode(s);
                                    } else {
                                        authors << Utf8ToUnicode(s);
                                        authors_set = true;
                                    }
                                } else if (recType == 103) {
                                    lString8 s((const char *)buf2.get(), recLen - 8);
                                    CRLog::trace("MOBI description: %s", s.c_str());
                                    m_doc_props->setString(DOC_PROP_DESCRIPTION, Utf8ToUnicode(s));
                                } else if (recType == 104) {
                                    lString8 s((const char *)buf2.get(), recLen - 8);
                                    CRLog::trace("MOBI ISBN: %s", s.c_str());
                                    if (identifiers_set) {
                                        identifiers << "\nISBN:" << Utf8ToUnicode(s);
                                    } else {
                                        identifiers << "ISBN:" << Utf8ToUnicode(s);
                                        identifiers_set = true;
                                    }
                                } else if (recType == 105) {
                                    lString8 s((const char *)buf2.get(), recLen - 8);
                                    CRLog::trace("MOBI subject: %s", s.c_str());
                                    if (keywords_set) {
                                        keywords << "\n" << Utf8ToUnicode(s);
                                    } else {
                                        keywords << Utf8ToUnicode(s);
                                        keywords_set = true;
                                    }
                                } else if (recType == 113) {
                                    lString8 s((const char *)buf2.get(), recLen - 8);
                                    CRLog::trace("MOBI ASIN: %s", s.c_str());
                                    if (identifiers_set) {
                                        identifiers << "\nASIN:" << Utf8ToUnicode(s);
                                    } else {
                                        identifiers << "ASIN:" << Utf8ToUnicode(s);
                                        identifiers_set = true;
                                    }
                                } else if (recType == 503) {
                                    lString8 s((const char *)buf2.get(), recLen - 8);
                                    CRLog::trace("MOBI updated title: %s", s.c_str());
                                    m_doc_props->setString(DOC_PROP_TITLE, Utf8ToUnicode(s));
                                    title_set = true;
                                } else if (recType == 524) {
                                    lString8 s((const char *)buf2.get(), recLen - 8);
                                    CRLog::trace("MOBI language: %s", s.c_str());
                                    m_doc_props->setString(DOC_PROP_LANGUAGE, Utf8ToUnicode(s));
                                }
                            }
                            //================================
                            stream->SetPos(nextPos);
                        }
                    }
                    if (authors_set) {
                        m_doc_props->setString(DOC_PROP_AUTHORS, authors);
                    }
                    if (identifiers_set) {
                        m_doc_props->setString(DOC_PROP_IDENTIFIERS, identifiers);
                    }
                    if (keywords_set) {
                        m_doc_props->setString(DOC_PROP_KEYWORDS, keywords);
                    }
                }
            }
            if (!title_set) {
                LVArray<lUInt8> buf3;
                buf3.addSpace(preamble.fullNameLength);
                stream->SetPos(_records[0].offset + preamble.fullNameOffset);
                if (stream->Read(buf3.get(), preamble.fullNameLength, NULL) == LVERR_OK) {
                    lString8 s((const char *)buf3.get(), preamble.fullNameLength);
                    CRLog::trace("MOBI title: %s", s.c_str());
                    m_doc_props->setString(DOC_PROP_TITLE, Utf8ToUnicode(s));
                }
            }
            if (container) {
                for ( int index=preamble.firstImageIndex; index<_records.length(); index++ ) {
                    stream->SetPos(_records[index].offset);
                    lUInt8 buf[256];
                    stream->Read(buf, 16, NULL);
                    //CRLog::debug("Image record %d [%02x %02x %02x %02x %02x]", index, buf[0], buf[1], buf[2], buf[3], buf[4]);
                    const char * fmt = NULL;
                    if (buf[0]==0xff && buf[1]==0xd8 && buf[2]==0xFF && buf[3]==0xe0)
                        fmt = "jpeg";
                    if (buf[0]==0x89 && buf[1]=='P' && buf[2]=='N' && buf[3]=='G')
                        fmt = "png";
                    if (buf[0]=='G' && buf[1]=='I' && buf[2]=='F')
                        fmt = "gif";
                    if (fmt) {
                        lString32 name = lString32(MOBI_IMAGE_NAME_PREFIX) + fmt::decimal((int) (index - preamble.firstImageIndex + 1));
                        //CRLog::debug("Adding image %s [%d] %s", LCSTR(name), _records[index].size, fmt);
                        container->addItem( new LVPDBRegionContainerItem( stream, this, name, _records[index].offset, _records[index].size ) );
                        if ((unsigned)index == preamble.firstImageIndex + coverOffset) {
                            m_doc_props->setString(DOC_PROP_COVER_FILE, name);
                            CRLog::trace("MOBI COVER: %s", LCSTR(name));
                        }
                    }
                }
            }
        } else if (_format==PALMDOC ) {
            if ( _records[0].size<sizeof(PalmDocPreamble) )
                return false;
            PalmDocPreamble preamble = { 0 };
            stream->SetPos(_records[0].offset);
            if ( !preamble.read(stream) )
                return false; // invalid preamble
            if ( preamble.recordCount>=_records.length() )
                return false;
            _compression = preamble.compression;
            if ( _compression==1 )
                _compression = 0;
            _textSize = preamble.textLength;
            _recordCount = preamble.recordCount;
        } else if (_format==PLUCKER ) {
            // TODO
            return false;
        }

//        if (_mobiExtraDataFlag) {
//            // remove extra data
//            for ( int k=1; k<_recordCount; k++ )
//                _records[k+1].size -= 6;
//        }

//#ifdef DUMP_PDB_CONTENTS
//        int unpoffset2 = 0;
//        FILE * out = fopen("/tmp/pdbout.txt", "wb" STDIO_CLOEXEC);
//        int k;
//        for (k=1; k <= _recordCount && unpoffset2 < this->_textSize; k++) {
//            LVArray<lUInt8> dst;
//            readRecordNoUnpack(k, &_buf);
//            if (_mobiExtraDataFlags) {
//                removeExtraData(k, _buf);
////                    int b = _buf[_buf.length()-1];
////                    CRLog::trace("Extra data: %d bytes", b);
////                    _records[k].size -= b;
////                    _buf.erase(_buf.length()-1-b, b);
//            }
//            if (_compression == 2) {
//                unpack(dst, _buf);
//                _records[k].unpoffset = unpoffset2;
//                _records[k].unpsize = dst.length();
//                unpoffset2 += dst.length();
//                fwrite(dst.get(), dst.length(), 1, out);
//                fprintf(out, "\n[block %d end]\n", k);
//            }
//            CRLog::trace("record[%d] : %06x %06x -  %06x %06x", k, _records[k].offset, _records[k].size, _records[k].unpoffset, _records[k].unpsize);
//        }
//        fclose(out);
//        CRLog::trace("totalUncompSizeHdr=%06x realUncompSize=%06x %d blocks of %d", this->_textSize, unpoffset2, k, _records.length());
//#endif

        if ( !validateContent )
            return true; // for simple format check

        LVArray<lUInt8> buf;
        lUInt32 unpoffset = 0;
        _crc = 0;
        for ( int k=0; k<_recordCount; k++ ) {

            readRecord(k+1, &buf);
            _records[k+1].unpoffset = unpoffset;
            _records[k+1].unpsize = buf.length();
            unpoffset += buf.length();
            _crc = lStr_crc32( _crc, buf.get(), buf.length() );
        }
        _mobiExtraDataFlags = 0;


        detectFormat( contentFormat );



        #ifdef DUMP_PDB_CONTENTS
        {
                int unpoffset2 = 0;
                FILE * out = fopen("/tmp/pdbout.txt", "wb" STDIO_CLOEXEC);
                int k;
                for (k=1; k <= _recordCount && unpoffset2 < this->_textSize; k++) {
                    LVArray<lUInt8> dst;
                    readRecordNoUnpack(k, &_buf);
//                    if (_mobiExtraDataFlags) {
//                        removeExtraData(k, _buf);
        //                    int b = _buf[_buf.length()-1];
        //                    CRLog::trace("Extra data: %d bytes", b);
        //                    _records[k].size -= b;
        //                    _buf.erase(_buf.length()-1-b, b);
//                    }
                    if (_compression == 2) {
                        unpack(dst, _buf);
                        _records[k].unpoffset = unpoffset2;
                        _records[k].unpsize = dst.length();
                        unpoffset2 += dst.length();
                        fwrite(dst.get(), dst.length(), 1, out);
                        fprintf(out, "\n[block %d end]\n", k);
                    }
                    CRLog::trace("record[%d] : %06x %06x -  %06x %06x", k, _records[k].offset, _records[k].size, _records[k].unpoffset, _records[k].unpsize);
                }
                fclose(out);
                CRLog::trace("totalUncompSizeHdr=%06x realUncompSize=%06x %d blocks of %d", this->_textSize, unpoffset2, k, _records.length());
        }
        #endif

        if (_textSize == (lUInt32)-1)
            _textSize = unpoffset;
        else if (unpoffset < _textSize) {
            CRLog::warn("PDB: Unpacked text size is %d but expected %d", unpoffset, _textSize);
            _textSize = unpoffset;
            //return false; // text size does not match
        }


        _bufIndex = -1;
        _bufSize = 0;
        _bufOffset = 0;

        SetName(_stream->GetName());
        m_mode = LVOM_READ;


        return true;
    }

    /// Seek (change file pos)
    /**
        \param offset is file offset (bytes) relateve to origin
        \param origin is offset base
        \param pNewPos points to place to store new file position
        \return lverror_t status: LVERR_OK if success
    */
    virtual lverror_t Seek( lvoffset_t offset, lvseek_origin_t origin, lvpos_t * pNewPos ) {
        lvpos_t npos = 0;
        lvpos_t currpos = _pos;
        switch (origin) {
        case LVSEEK_SET:
            npos = offset;
            break;
        case LVSEEK_CUR:
            npos = currpos + offset;
            break;
        case LVSEEK_END:
            npos = _textSize + offset;
            break;
        }
        if (npos > _textSize)
            return LVERR_FAIL;
        if (!seek(npos) )
            return LVERR_FAIL;
        if (pNewPos)
            *pNewPos =  _pos;
        return LVERR_OK;
    }

    /// Get file position
    /**
        \return lvpos_t file position
    */
    virtual lvpos_t GetPos()
    {
        return _pos;
    }

    /// Get file size
    /**
        \return lvsize_t file size
    */
    virtual lvsize_t  GetSize()
    {
        return _textSize;
    }

    virtual lverror_t GetSize( lvsize_t * pSize )
    {
        *pSize = _textSize;
        return LVERR_OK;
    }

    /// Set file size
    /**
        \param size is new file size
        \return lverror_t status: LVERR_OK if success
    */
    virtual lverror_t SetSize( lvsize_t size ) {
        CR_UNUSED(size);
        return LVERR_NOTIMPL;
    }

    /// Read
    /**
        \param buf is buffer to place bytes read from stream
        \param count is number of bytes to read from stream
        \param nBytesRead is place to store real number of bytes read from stream
        \return lverror_t status: LVERR_OK if success
    */
    virtual lverror_t Read( void * buf, lvsize_t count, lvsize_t * nBytesRead ) {
        lvsize_t bytesRead = 0;
        if ( nBytesRead )
            *nBytesRead = bytesRead;
        lUInt8 * dst = (lUInt8 *)buf;
        while ( count > 0 ) {
            if ( ! seek(_pos) ) {
                if ( _pos>=_textSize )
                    break;
                return LVERR_FAIL;
            }
            int bytesLeft = (int)(_bufOffset + _bufSize - _pos);
            if ( bytesLeft<=0 )
                break;
            int sz = count;
            if ( sz>bytesLeft )
                sz = bytesLeft;
            for ( int i=0; i<sz; i++ )
                dst[i] = _buf[_pos - _bufOffset + i];
            _pos += sz;
            dst += sz;
            count -= sz;
            bytesRead += sz;
        }
        if ( nBytesRead )
            *nBytesRead = bytesRead;
        return LVERR_OK;
    }

    /// Write
    /**
        \param buf is data to write to stream
        \param count is number of bytes to write
        \param nBytesWritten is place to store real number of bytes written to stream
        \return lverror_t status: LVERR_OK if success
    */
    virtual lverror_t Write( const void * buf, lvsize_t count, lvsize_t * nBytesWritten ) {
        CR_UNUSED3(buf, count, nBytesWritten);
        return LVERR_NOTIMPL;
    }

    /// Check whether end of file is reached
    /**
        \return true if end of file reached
    */
    virtual bool Eof() {
        return _pos>=_textSize;
    }

    Format getFormat() { return _format; }

    /// PDB record number of the MOBI TOC (INDX/NCX) header, -1 if none
    int getMobiNcxIdx() { return _mobiNcxIdx; }
    /// MOBI text encoding (65001 = UTF-8, 1252 = cp1252)
    lUInt32 getMobiEncoding() { return _mobiEncoding; }

    /// Constructor
    PDBFile() {
        //_container.AddRef();
        _bufIndex = -1;
        _mobiExtraDataFlags = 0;
        _mobiNcxIdx = -1;
        _mobiEncoding = 0;
        m_doc_props = LVCreatePropsContainer();
    }

    /// Destructor
    virtual ~PDBFile() { }

};

// open PDB stream from stream
//LVStreamRef LVOpenPDBStream( LVStreamRef srcstream, int &format )
//{
//    PDBFile * stream = PDBFile::create( srcstream, format );
//    srcstream->SetPos(0);
//    if ( stream!=NULL )
//    {
//        return LVStreamRef( stream );
//    }
//    return LVStreamRef();
//}

bool DetectPDBFormat( LVStreamRef stream, doc_format_t & contentFormat )
{
    PDBFile pdb;
    if ( !pdb.open(stream, NULL, false, contentFormat) )
        return false;
    return true;
}

bool isCorrectUtf8Text(LVStreamRef & stream) {
    char enc_name[32];
    lvpos_t oldpos = stream->GetPos();
    unsigned sz = 16384;
    stream->SetPos( 0 );
    if ( sz>stream->GetSize() )
        sz = stream->GetSize();
    if (sz < 8)
        return false;
    unsigned char * buf = new unsigned char[ sz ];
    lvsize_t bytesRead = 0;
    if ( stream->Read( buf, sz, &bytesRead )!=LVERR_OK ) {
        delete[] buf;
        stream->SetPos( oldpos );
        return false;
    }

    int res = 0;
    res = AutodetectCodePageUtf(buf, sz, enc_name);
    delete[] buf;
    return res != 0;
}

LVStreamRef GetPDBCoverpage(LVStreamRef stream)
{
    doc_format_t contentFormat = doc_format_none;
    PDBFile * pdb = new PDBFile();
    LVPDBContainer * container = new LVPDBContainer();
    if (!pdb->open(stream, container, false, contentFormat)) {
        delete container;
        delete pdb;
        return LVStreamRef();
    }
    stream = LVStreamRef(pdb);
    LVContainerRef cnt(container);
    container->setStream(stream);
    LVStreamRef coverStream;
    lString32 coverName = pdb->getDocProps()->getStringDef(DOC_PROP_COVER_FILE);
    if (!coverName.empty()) {
        coverStream = cnt->OpenStream(coverName.c_str(), LVOM_READ);
    }
    if (!coverStream.isNull()) {
        CRLog::trace("Found PDB coverpage image");
        return LVCreateMemoryStream(coverStream);
    }
    return LVStreamRef();
 }

bool ImportPDBDocument( LVStreamRef & stream, ldomDocument * doc, LVDocViewCallback * progressCallback, CacheLoadingCallback * formatCallback, doc_format_t & contentFormat )
{
    contentFormat = doc_format_none;
    PDBFile * pdb = new PDBFile();
    LVPDBContainer * container = new LVPDBContainer();
    pdb->getDocProps()->set(doc->getProps());
    if ( !pdb->open(stream, container, true, contentFormat) ) {
        delete container;
        delete pdb;
        return false;
    }
    stream = LVStreamRef(pdb);
    container->setStream(stream);
    doc->setContainer(LVContainerRef(container));

#if BUILD_LITE!=1
    if ( doc->openFromCache(formatCallback) ) {
        if ( progressCallback ) {
            progressCallback->OnLoadFileEnd( );
        }
        return true;
    }
#endif
    doc->getProps()->set(pdb->getDocProps());

    switch ( contentFormat ) {
    case doc_format_html:
        // HTML
        {
            LVStreamRef parserStream = stream;
            bool isMobiHtml = pdb->getFormat() == PDBFile::MOBI;

            // Injecting a standalone <a> marker into text could split a text node
            // and break highlights; only do it on recent DOM versions.
            bool allowInjectStandaloneId = doc->getDOMVersionRequested() >= 20260812;

            if (isMobiHtml) {
                // Read the TOC (INDX/NCX) index first: its entries target byte
                // offsets in the HTML, which we need to anchor (like filepos
                // links) before parsing, so we can then resolve each entry to
                // a DOM node via its id="fileposNNNN".
                LVPtrVector<MobiTocEntry> mobiToc;
                bool haveMobiToc = pdb->getMobiNcxIdx() >= 0 && pdb->readMobiToc(pdb->getMobiNcxIdx(), pdb->getMobiEncoding(), mobiToc);
                LVArray<lUInt32> tocFileposRefs;
                if (haveMobiToc) {
                    for (int i = 0; i < mobiToc.length(); i++)
                        tocFileposRefs.add(mobiToc[i]->filepos);
                }

                MobiFileposResolver mobiResolver;
                LVStreamRef rewrittenStream = preprocessMobiHtmlStream(stream, mobiResolver, allowInjectStandaloneId, haveMobiToc ? &tocFileposRefs : NULL);
                if (!rewrittenStream.isNull())
                    parserStream = rewrittenStream;

                MobiHtmlWriterFilter mobiWriterFilter(doc, mobiResolver);
                LVHTMLParser parser(parserStream, &mobiWriterFilter);
                parser.setProgressCallback(progressCallback);
                if ( !parser.CheckFormat() ) {
                    return false;
                } else {
                    if (isCorrectUtf8Text(parserStream))
                        parser.SetCharset(U"utf-8");
                    parserStream->SetPos(0);
                    if (!parser.Parse()) {
                        return false;
                    }
                }

                // Build the TOC from the index entries, resolving each filepos
                // target to the DOM node carrying the matching id="fileposNNNN".
                if (haveMobiToc) {
                    LVTocItem * toc = doc->getToc();
                    toc->clear();
                    // Stack of current parent items per level
                    // Arbitrary limit of 15 + 2 levels so that we only need a small fixed-size array.
                    // Root takes 0, 16 is there to avoid one more if branch when writing.
                    LVTocItem * parents[17];
                    for (int pi = 0; pi < 17; pi++) parents[pi] = toc;
                    int curLevel = 0;
                    int added = 0;
                    for (int i = 0; i < mobiToc.length(); i++) {
                        MobiTocEntry * e = mobiToc[i];
                        // The target element may already have had its own id
                        // (the pre-processor then pointed the offset at it via
                        // the resolver instead of injecting a synthetic
                        // id="fileposNNNN"), so look that up first.
                        lString32 id;
                        if (!mobiResolver.targetIds.get(e->filepos, id)) {
                            id = lString32(MOBI_FILEPOS_ID_PREFIX);
                            id.appendDecimal(e->filepos);
                        }
                        ldomNode * node = doc->getElementById(id.c_str());
                        if (!node)
                            continue; // target not anchored: skip entry
                        ldomXPointer ptr(node, 0);
                        int level = e->level;
                        if (level < 0)
                            level = 0;
                        if (level > 15)
                            level = 15;
                        if (level > curLevel + 1)
                            level = curLevel + 1; // no gaps in the hierarchy
                        LVTocItem * item = parents[level]->addChild(e->title, ptr, ptr.toString());
                        parents[level + 1] = item;
                        curLevel = level;
                        added++;
                    }
                    if (added > 0) {
                        CRLog::info("MOBI: TOC built from NCX index (%d entries)", added);
                        doc->setCacheFileStale(true); // cache must be updated with the TOC
                    } else {
                        toc->clear();
                    }
                }
            } else {
                ldomDocumentWriterFilter plainWriterFilter(doc, false, HTML_AUTOCLOSE_TABLE);
                LVHTMLParser parser(parserStream, &plainWriterFilter);
                parser.setProgressCallback(progressCallback);
                if ( !parser.CheckFormat() ) {
                    return false;
                } else {
                    parserStream->SetPos(0);
                    if (!parser.Parse()) {
                        return false;
                    }
                }
            }
        }
        break;
    default:
    //case doc_format_txt:
        // TXT
        {
            ldomDocumentWriter writer(doc);
            LVTextParser parser(stream, &writer, false);
            parser.setProgressCallback(progressCallback);
            if ( !parser.CheckFormat() ) {
                return false;
            } else {
                if (!parser.Parse()) {
                    return false;
                }
            }
        }
        break;
        // PML
        {
//            ldomDocumentWriterFilter writerFilter(*doc, false,
//                    HTML_AUTOCLOSE_TABLE);

//            LVHTMLParser parser(m_stream, &writerFilter);
//            parser->setProgressCallback(progressCallback);
//            if ( !parser->CheckFormat() ) {
//                return false;
//            } else {
//                if (!parser->Parse()) {
//                    return false;
//                }
//            }
        }
        break;
    }
#ifdef DUMP_PDB_CONTENTS
    for (int i=0; i<container->GetObjectCount(); i++) {
        const LVContainerItemInfo * item = container->GetObjectInfo(i);
        if (item->IsContainer())
            continue;
        lString32 fn = item->GetName();
        if (fn.empty())
            fn = cs32("pdb_item_") + lString32::itoa(i);
        fn = cs32("/tmp/") + fn;
        LVStreamRef in = container->OpenStream(item->GetName(), LVOM_READ);
        if (in.isNull())
            continue;
        LVStreamRef out = LVOpenFileStream(fn.c_str(), LVOM_WRITE);
        if (out.isNull())
            continue;
        CRLog::trace("Dumping stream %s (%d)", LCSTR(fn), (int)item->GetSize());
        LVPumpStream(out.get(), in.get());
    }
    {
        LVStreamRef out = LVOpenFileStream("/tmp/pdb_main.txt", LVOM_WRITE);
        if (!out.isNull()) {
            stream->SetPos(0);
            CRLog::trace("Dumping stream /tmp/pdb_main.txt (%d)", (int)stream->GetSize());
            LVPumpStream(out.get(), stream.get());
            stream->SetPos(0);
        }
    }
#endif

    return true;
}

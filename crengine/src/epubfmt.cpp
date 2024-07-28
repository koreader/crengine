#include "../include/epubfmt.h"
#include "../include/fb2def.h"

#if (USE_ZLIB==1)
#include <zlib.h>
#endif

class EpubItem {
public:
    lString32 href;
    lString32 mediaType;
    lString32 id;
    lString32 title;
    bool is_xhtml;
    bool nonlinear;
    EpubItem()
    { }
    EpubItem( const EpubItem & v )
        : href(v.href), mediaType(v.mediaType), id(v.id), is_xhtml(v.is_xhtml)
    { }
    EpubItem & operator = ( const EpubItem & v )
    {
        href = v.href;
        mediaType = v.mediaType;
        id = v.id;
        return *this;
    }
};

class EpubItems {
    LVPtrVector<EpubItem> _list;
    LVHashTable<lString32, int> _id2index;
public:
    EpubItems() : _id2index(16) {}
    int length() const {
        return _list.length();
    }
    void add( EpubItem * item ) {
        _list.add(item);
        // Keep indexed the first one met if we get a duplicated id
        int index;
        if ( ! _id2index.get(item->id, index) ) {
            _id2index.set(item->id, _list.length()-1);
        }
    }
    EpubItem * findById( const lString32 & id )
    {
        if ( id.empty() )
            return NULL;
        int index;
        if ( _id2index.get(id, index) )
            return _list[index];
        return NULL;
    }
};

//static void dumpZip( LVContainerRef arc ) {
//    lString32 arcName = LVExtractFilenameWithoutExtension( arc->GetName() );
//    if ( arcName.empty() )
//        arcName = "unziparc";
//    lString32 outDir = cs32("/tmp/") + arcName;
//    LVCreateDirectory(outDir);
//    for ( int i=0; i<arc->GetObjectCount(); i++ ) {
//        const LVContainerItemInfo * info = arc->GetObjectInfo(i);
//        if ( !info->IsContainer() ) {
//            lString32 outFileName = outDir + "/" + info->GetName();
//            LVCreateDirectory(LVExtractPath(outFileName));
//            LVStreamRef in = arc->OpenStream(info->GetName(), LVOM_READ);
//            LVStreamRef out = LVOpenFileStream(outFileName.c_str(), LVOM_WRITE);
//            if ( !in.isNull() && !out.isNull() ) {
//                CRLog::trace("Writing %s", LCSTR(outFileName));
//                LVPumpStream(out.get(), in.get());
//            }
//        }
//    }
//}

bool DetectEpubFormat( LVStreamRef stream )
{
    LVContainerRef m_arc = LVOpenArchieve( stream );
    if ( m_arc.isNull() )
        return false; // not a ZIP archive

    //dumpZip( m_arc );

    // read "mimetype" file contents from root of archive
    lString32 mimeType;
    {
        LVStreamRef mtStream = m_arc->OpenStream(U"mimetype", LVOM_READ );
        if ( !mtStream.isNull() ) {
            lvsize_t size = mtStream->GetSize();
            if ( size>4 && size<100 ) {
                LVArray<char> buf( size+1, '\0' );
                if ( mtStream->Read( buf.get(), size, NULL )==LVERR_OK ) {
                    for ( lvsize_t i=0; i<size; i++ )
                        if ( buf[i]<32 || ((unsigned char)buf[i])>127 )
                            buf[i] = 0;
                    buf[size] = 0;
                    if ( buf[0] )
                        mimeType = Utf8ToUnicode( lString8( buf.get() ) );
                }
            }
        }
    }

    if ( mimeType != U"application/epub+zip" )
        return false;
    return true;
}

void ReadEpubNcxToc( ldomDocument * doc, ldomNode * mapRoot, LVTocItem * baseToc, ldomDocumentFragmentWriter & appender ) {
    if ( !mapRoot || !baseToc)
        return;
    lUInt16 navPoint_id = mapRoot->getDocument()->getElementNameIndex(U"navPoint");
    lUInt16 navLabel_id = mapRoot->getDocument()->getElementNameIndex(U"navLabel");
    lUInt16 content_id = mapRoot->getDocument()->getElementNameIndex(U"content");
    lUInt16 text_id = mapRoot->getDocument()->getElementNameIndex(U"text");
    int nb_items = mapRoot->isElement() ? mapRoot->getChildCount() : 0;
    for (int i=0; i<nb_items; i++) {
        ldomNode * navPoint = mapRoot->getChildNode(i);
        if ( navPoint->getNodeId() != navPoint_id )
            continue;
        ldomNode * navLabel = navPoint->findChildElement(LXML_NS_ANY, navLabel_id, -1);
        if ( !navLabel )
            continue;
        ldomNode * text = navLabel->findChildElement(LXML_NS_ANY, text_id, -1);
        if ( !text )
            continue;
        ldomNode * content = navPoint->findChildElement(LXML_NS_ANY, content_id, -1);
        if ( !content )
            continue;
        lString32 href = content->getAttributeValue("src");
        lString32 title = text->getText(' ');
        title.trimDoubleSpaces(false, false, false);
        // Allow empty title (which is fine, and they may have sub items)
        if ( href.empty() )
            continue;
        //CRLog::trace("TOC href before convert: %s", LCSTR(href));
        href = DecodeHTMLUrlString(href);
        href = appender.convertHref(href);
        //CRLog::trace("TOC href after convert: %s", LCSTR(href));
        if ( href.empty() || href[0]!='#' )
            continue;
        ldomNode * target = doc->getNodeById(doc->getAttrValueIndex(href.substr(1).c_str()));
        if ( !target ) {
            // Let's not ignore entries with an invalid target, they may have children.
            // Also, if the anchor (ie. #top) is invalid, point to the docfragment itself.
            href = content->getAttributeValue("src");
            href = DecodeHTMLUrlString(href);
            int pos = href.pos(U'#');
            if ( pos > 0 ) {
                href = href.substr(0, pos);
                href = appender.convertHref(href);
                target = doc->getNodeById(doc->getAttrValueIndex(href.substr(1).c_str()));
            }
            // If still not found, let target be null (and lead us to page 0...), but allow
            // it to have children
            // (We might want to do as ReadEpubNavToc(), and report valid children target
            // to their parents or siblings that has none.)
        }
        ldomXPointer ptr(target, 0);
        LVTocItem * tocItem = baseToc->addChild(title, ptr, lString32::empty_str);
        ReadEpubNcxToc( doc, navPoint, tocItem, appender );
    }
}

void ReadEpubNcxPageList( ldomDocument * doc, ldomNode * mapRoot, LVPageMap * pageMap, ldomDocumentFragmentWriter & appender ) {
    // http://idpf.org/epub/20/spec/OPF_2.0.1_draft.htm#Section2.4.1.2
    // http://idpf.org/epub/a11y/techniques/techniques-20160711.html#refPackagesLatest
    //    <pageTarget id="p4" playOrder="6" type="normal" value="2">
    //      <navLabel><text>Page 8</text></navLabel>
    //      <content src="OEBPS/PL12.xhtml#page_8"/>
    //    </pageTarget>
    // http://blog.epubbooks.com/346/marking-up-page-numbers-in-the-epub-ncx/
    // type:value must be unique, and value can not be used as a short version of text...
    // Also see http://kb.daisy.org/publishing/docs/navigation/pagelist.html
    if ( !mapRoot || !pageMap)
        return;
    lUInt16 pageTarget_id = mapRoot->getDocument()->getElementNameIndex(U"pageTarget");
    lUInt16 navLabel_id = mapRoot->getDocument()->getElementNameIndex(U"navLabel");
    lUInt16 content_id = mapRoot->getDocument()->getElementNameIndex(U"content");
    lUInt16 text_id = mapRoot->getDocument()->getElementNameIndex(U"text");
    int nb_items = mapRoot->isElement() ? mapRoot->getChildCount() : 0;
    for (int i=0; i<nb_items; i++) {
        ldomNode * pageTarget = mapRoot->getChildNode(i);
        if ( pageTarget->getNodeId() != pageTarget_id )
            continue;
        ldomNode * navLabel = pageTarget->findChildElement(LXML_NS_ANY, navLabel_id, -1);
        if ( !navLabel )
            continue;
        ldomNode * text = navLabel->findChildElement(LXML_NS_ANY, text_id, -1);
        if ( !text )
            continue;
        ldomNode * content = pageTarget->findChildElement(LXML_NS_ANY, content_id, -1);
        if ( !content )
            continue;
        lString32 href = content->getAttributeValue("src");
        lString32 title = text->getText(' ');
        title.trimDoubleSpaces(false, false, false);
        // Empty titles wouldn't have much sense in page maps, ignore them
        if ( href.empty() || title.empty() )
            continue;
        href = DecodeHTMLUrlString(href);
        href = appender.convertHref(href);
        if ( href.empty() || href[0]!='#' )
            continue;
        ldomNode * target = doc->getNodeById(doc->getAttrValueIndex(href.substr(1).c_str()));
        if ( !target )
            continue;
        ldomXPointer ptr(target, 0);
        pageMap->addPage(title, ptr, lString32::empty_str);
    }
}

void ReadEpubNavToc( ldomDocument * doc, ldomNode * mapRoot, LVTocItem * baseToc, ldomDocumentFragmentWriter & appender ) {
    // http://idpf.org/epub/30/spec/epub30-contentdocs.html#sec-xhtml-nav-def
    if ( !mapRoot || !baseToc)
        return;
    lUInt16 ol_id = mapRoot->getDocument()->getElementNameIndex(U"ol");
    lUInt16 li_id = mapRoot->getDocument()->getElementNameIndex(U"li");
    lUInt16 a_id = mapRoot->getDocument()->getElementNameIndex(U"a");
    lUInt16 span_id = mapRoot->getDocument()->getElementNameIndex(U"span");
    int nb_items = mapRoot->isElement() ? mapRoot->getChildCount() : 0;
    for (int i=0; i<nb_items; i++) {
        ldomNode * li = mapRoot->getChildNode(i);
        if ( li->getNodeId() != li_id )
            continue;
        LVTocItem * tocItem = NULL;
        ldomNode * a = li->findChildElement(LXML_NS_ANY, a_id, -1);
        if ( a ) {
            lString32 href = a->getAttributeValue("href");
            lString32 title = a->getText(' ');
            if ( title.empty() ) {
                // "If the a element contains [...] that do not provide intrinsic text alternatives,
                // it must also include a title attribute with an alternate text rendition of the
                // link label."
                title = a->getAttributeValue("title");
            }
            title.trimDoubleSpaces(false, false, false);
            if ( !href.empty() ) {
                href = DecodeHTMLUrlString(href);
                href = appender.convertHref(href);
                if ( !href.empty() && href[0]=='#' ) {
                    ldomNode * target = doc->getNodeById(doc->getAttrValueIndex(href.substr(1).c_str()));
                    if ( target ) {
                        ldomXPointer ptr(target, 0);
                        tocItem = baseToc->addChild(title, ptr, lString32::empty_str);
                        // Report xpointer to upper parent(s) that didn't have
                        // one (no <a>) - but stop before the root node
                        LVTocItem * tmp = baseToc;
                        while ( tmp && tmp->getLevel() > 0 && tmp->getXPointer().isNull() ) {
                            tmp->setXPointer(ptr);
                            tmp = tmp->getParent();
                        }
                    }
                }
            }
        }
        // "The a element may optionally be followed by an ol ordered list representing
        // a subsidiary content level below that heading (e.g., all the subsection
        // headings of a section). The span element must be followed by an ol ordered
        // list: it cannot be used in "leaf" li elements."
        ldomNode * ol = li->findChildElement( LXML_NS_ANY, ol_id, -1 );
        if ( ol ) { // there are sub items
            if ( !tocItem ) {
                // Make a LVTocItem to contain sub items
                // There can be a <span>, with no href: children will set it to its own xpointer
                lString32 title;
                ldomNode * span = li->findChildElement(LXML_NS_ANY, span_id, -1);
                if ( span ) {
                    title = span->getText(' ');
                    title.trimDoubleSpaces(false, false, false);
                }
                // If none, let title empty
                tocItem = baseToc->addChild(title, ldomXPointer(), lString32::empty_str);
            }
            ReadEpubNavToc( doc, ol, tocItem, appender );
        }
    }
}

void ReadEpubNavPageMap( ldomDocument * doc, ldomNode * mapRoot, LVPageMap * pageMap, ldomDocumentFragmentWriter & appender ) {
    // http://idpf.org/epub/30/spec/epub30-contentdocs.html#sec-xhtml-nav-def
    if ( !mapRoot || !pageMap)
        return;
    lUInt16 li_id = mapRoot->getDocument()->getElementNameIndex(U"li");
    lUInt16 a_id = mapRoot->getDocument()->getElementNameIndex(U"a");
    int nb_items = mapRoot->isElement() ? mapRoot->getChildCount() : 0;
    for (int i=0; i<nb_items; i++) {
        ldomNode * li = mapRoot->getChildNode(i);
        if ( li->getNodeId() != li_id )
            continue;
        ldomNode * a = li->findChildElement(LXML_NS_ANY, a_id, -1);
        if ( a ) {
            lString32 href = a->getAttributeValue("href");
            lString32 title = a->getText(' ');
            if ( title.empty() ) {
                title = a->getAttributeValue("title");
            }
            title.trimDoubleSpaces(false, false, false);
            if ( !href.empty() ) {
                href = DecodeHTMLUrlString(href);
                href = appender.convertHref(href);
                if ( !href.empty() && href[0]=='#' ) {
                    ldomNode * target = doc->getNodeById(doc->getAttrValueIndex(href.substr(1).c_str()));
                    if ( target ) {
                        ldomXPointer ptr(target, 0);
                        pageMap->addPage(title, ptr, lString32::empty_str);
                    }
                }
            }
        }
    }
}

void ReadEpubAdobePageMap( ldomDocument * doc, ldomNode * mapRoot, LVPageMap * pageMap, ldomDocumentFragmentWriter & appender ) {
    // https://wiki.mobileread.com/wiki/Adobe_Digital_Editions#Page-map
    if ( !mapRoot || !pageMap)
        return;
    lUInt16 page_id = mapRoot->getDocument()->getElementNameIndex(U"page");
    int nb_items = mapRoot->isElement() ? mapRoot->getChildCount() : 0;
    for (int i=0; i<nb_items; i++) {
        ldomNode * page = mapRoot->getChildNode(i);
        if ( page->getNodeId() != page_id )
            continue;
        lString32 href = page->getAttributeValue("href");
        lString32 title = page->getAttributeValue("name");
        title.trimDoubleSpaces(false, false, false);
        // Empty titles wouldn't have much sense in page maps, ignore them
        if ( href.empty() || title.empty() )
            continue;
        href = DecodeHTMLUrlString(href);
        href = appender.convertHref(href);
        if ( href.empty() || href[0]!='#' )
            continue;
        ldomNode * target = doc->getNodeById(doc->getAttrValueIndex(href.substr(1).c_str()));
        if ( !target )
            continue;
        ldomXPointer ptr(target, 0);
        pageMap->addPage(title, ptr, lString32::empty_str);
    }
}

bool ExtractCoverFilenameFromCoverPageFragment( LVStreamRef stream, lString32 & cover_image_href,
            const elem_def_t * node_scheme, const attr_def_t * attr_scheme, const ns_def_t * ns_scheme)
{
    // We expect such a cover xhtml file to be small in size
    if ( stream.isNull() || stream->GetSize() > 5000 )
        return false;
    // We needed to get the *_scheme passed from lvdocview.cpp (they are only available there)
    // to this ImportEpubDocument() just for this: this HTML parser expects these hardcoded
    // elements id to properly handle auto-open-close tags (not passing them would result
    // in our parsed HTML being: <RootNode><body><html><body><html><body><div> ...).
    ldomDocument * coverDoc = LVParseHTMLStream( stream, node_scheme, attr_scheme, ns_scheme);
    if ( !coverDoc )
        return false;
    // We expect to find a single image, and no text, to consider the image as a possible cover.
    lString32 img_href;
    bool has_more_images = false;
    bool has_text = false;
    bool in_body = false;
    bool img_has_alt_attribute_equal_cover = false;
    ldomNode * coverDocRoot = coverDoc->getRootNode();
    /* For debugging:
    lString32Collection cssFiles;
    lString8 extra;
    lString8 html = ldomXPointer(coverDocRoot,0).getHtml(cssFiles, extra, 0);
    printf("HTML: %s\n", html.c_str());
    */
    // Use our usual non-recursive node walker
    ldomNode * n = coverDocRoot;
    if (n->isElement() && n->getChildCount() > 0) {
        int nextChildIndex = 0;
        n = n->getChildNode(nextChildIndex);
        while (true) {
            // Check only the first time we met a node (nextChildIndex == 0)
            // and not when we get back to it from a child to process next sibling
            if (nextChildIndex == 0) {
                if ( n->isElement() ) {
                    lUInt16 id = n->getNodeId();
                    if ( !in_body ) {
                        // We should ignore text outside <body>, ie. <title> or <style>
                        if (id == el_body ) {
                            in_body = true;
                        }
                    }
                    else if (id == el_img ) { // <img src=''>
                        if ( img_href.empty() ) {
                            img_href = n->getAttributeValue(attr_src);
                            lString32 img_alt = n->getAttributeValue(attr_alt);
                            if ( img_alt.lowercase() == U"cover" ) {
                                img_has_alt_attribute_equal_cover = true;
                            }
                        }
                        else {
                            // Too many images for a single cover: give up
                            has_more_images = true;
                            break;
                        }
                    }
                    else if (id == el_image ) { // <svg>...<image href=''>
                        if ( img_href.empty() ) {
                            img_href = n->getAttributeValue(attr_href);
                        }
                        else {
                            has_more_images = true;
                            break;
                        }
                    }
                }
                else if ( in_body && n->isText() ) {
                    lUInt16 id = n->getParentNode()->getNodeId();
                    if (id == el_stylesheet || id == el_script ) {
                        // We don't have styles here to know if this element is display:none, but
                        // skip commmon known ones that should be invisible and may contain text.
                    }
                    else {
                        // This won't ignore \n and &nbsp;, we'll see if we need to do more
                        lString32 text = n->getText().trim();
                        if ( text.length() > 0 ) {
                            // printf("TEXT: #%s#\n", LCSTR(text));
                            has_text = true;
                            break;
                        }
                    }
                }
            }
            // Process next child
            if (n->isElement() && nextChildIndex < n->getChildCount()) {
                n = n->getChildNode(nextChildIndex);
                nextChildIndex = 0;
                continue;
            }
            // No more child, get back to parent and have it process our sibling
            nextChildIndex = n->getNodeIndex() + 1;
            n = n->getParentNode();
            if (!n) // back to root node
                break;
            if (n == coverDocRoot && nextChildIndex >= n->getChildCount())
                // back to this node, and done with its children
                break;
        }
    }
    delete coverDoc;
    // printf("EPUB coverpage check: has_text:%d, has_more_images:%d, has_alt=cover:%d img_href:%d (%s)\n",
    //               has_text, has_more_images, img_has_alt_attribute_equal_cover, !img_href.empty(), LCSTR(img_href));
    if ( !img_href.empty() ) {
        if ( !has_text && !has_more_images ) {
            // Single image with no text in this fragment: it must be a cover image.
            cover_image_href = img_href;
            return true;
        }
        if ( img_has_alt_attribute_equal_cover ) {
            // If we have met an image with alt="cover" before any text or other images,
            // consider it valid.
            cover_image_href = img_href;
            return true;
        }
    }
    return false;
}

lString32 EpubGetRootFilePath(LVContainerRef m_arc)
{
    // check root media type
    lString32 rootfilePath;
    lString32 rootfileMediaType;
    // read container.xml
    {
        LVStreamRef container_stream = m_arc->OpenStream(U"META-INF/container.xml", LVOM_READ);
        if ( !container_stream.isNull() ) {
            ldomDocument * doc = LVParseXMLStream( container_stream );
            if ( doc ) {
                ldomNode * rootfile = doc->nodeFromXPath( cs32("container/rootfiles/rootfile") );
                if ( rootfile && rootfile->isElement() ) {
                    rootfilePath = rootfile->getAttributeValue("full-path");
                    rootfileMediaType = rootfile->getAttributeValue("media-type");
                }
                delete doc;
            }
        }
    }

    if (rootfilePath.empty() || rootfileMediaType != "application/oebps-package+xml")
        return lString32::empty_str;
    return rootfilePath;
}

// From https://github.com/CTrabant/teeny-sha1 , slightly shortened - MIT Licensed. Copyright (c) 2017 CTrabant
// Needed to demangle EPUB items (ie. fonts) obfuscated with the IDPF algorithm (http://www.idpf.org/2008/embedding)
static int sha1digest(uint8_t *digest, const uint8_t *data, size_t databytes) {
    #define SHA1ROTATELEFT(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))
    uint32_t W[80];
    uint32_t H[] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint32_t a, b, c, d, e;
    uint32_t f, k = 0;
    uint32_t idx, lidx, widx;
    uint32_t didx = 0;
    int32_t wcount;
    uint32_t temp;
    uint64_t databits = ((uint64_t)databytes) * 8;
    uint32_t loopcount = (databytes + 8) / 64 + 1;
    uint32_t tailbytes = 64 * loopcount - databytes;
    uint8_t datatail[128] = {0};
    if (!digest || !data)
        return -1;
    /* Pre-processing of data tail (includes padding to fill out 512-bit chunk):
         Add bit '1' to end of message (big-endian)
         Add 64-bit message length in bits at very end (big-endian) */
    datatail[0] = 0x80;
    datatail[tailbytes - 8] = (uint8_t) (databits >> 56 & 0xFF);
    datatail[tailbytes - 7] = (uint8_t) (databits >> 48 & 0xFF);
    datatail[tailbytes - 6] = (uint8_t) (databits >> 40 & 0xFF);
    datatail[tailbytes - 5] = (uint8_t) (databits >> 32 & 0xFF);
    datatail[tailbytes - 4] = (uint8_t) (databits >> 24 & 0xFF);
    datatail[tailbytes - 3] = (uint8_t) (databits >> 16 & 0xFF);
    datatail[tailbytes - 2] = (uint8_t) (databits >> 8 & 0xFF);
    datatail[tailbytes - 1] = (uint8_t) (databits >> 0 & 0xFF);
    /* Process each 512-bit chunk */
    for (lidx = 0; lidx < loopcount; lidx++) {
        /* Compute all elements in W */
        memset (W, 0, 80 * sizeof (uint32_t));
        /* Break 512-bit chunk into sixteen 32-bit, big endian words */
        for (widx = 0; widx <= 15; widx++) {
            wcount = 24;
            /* Copy byte-per byte from specified buffer */
            while (didx < databytes && wcount >= 0) {
                W[widx] += (((uint32_t)data[didx]) << wcount);
                didx++;
                wcount -= 8;
            }
            /* Fill out W with padding as needed */
            while (wcount >= 0) {
                W[widx] += (((uint32_t)datatail[didx - databytes]) << wcount);
                didx++;
                wcount -= 8;
            }
        }
        /* Extend the sixteen 32-bit words into eighty 32-bit words, with potential optimization from:
             "Improving the Performance of the Secure Hash Algorithm (SHA-1)" by Max Locktyukhin */
        for (widx = 16; widx <= 31; widx++) {
            W[widx] = SHA1ROTATELEFT ((W[widx - 3] ^ W[widx - 8] ^ W[widx - 14] ^ W[widx - 16]), 1);
        }
        for (widx = 32; widx <= 79; widx++) {
            W[widx] = SHA1ROTATELEFT ((W[widx - 6] ^ W[widx - 16] ^ W[widx - 28] ^ W[widx - 32]), 2);
        }
        /* Main loop */
        a = H[0];
        b = H[1];
        c = H[2];
        d = H[3];
        e = H[4];
        for (idx = 0; idx <= 79; idx++) {
            if (idx <= 19) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            }
            else if (idx >= 20 && idx <= 39) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            }
            else if (idx >= 40 && idx <= 59) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            }
            else if (idx >= 60 && idx <= 79) {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            temp = SHA1ROTATELEFT (a, 5) + f + e + k + W[idx];
            e = d;
            d = c;
            c = SHA1ROTATELEFT (b, 30);
            b = a;
            a = temp;
        }
        H[0] += a;
        H[1] += b;
        H[2] += c;
        H[3] += d;
        H[4] += e;
    }
    /* Store binary digest in supplied buffer */
    for (idx = 0; idx < 5; idx++) {
        digest[idx * 4 + 0] = (uint8_t) (H[idx] >> 24);
        digest[idx * 4 + 1] = (uint8_t) (H[idx] >> 16);
        digest[idx * 4 + 2] = (uint8_t) (H[idx] >> 8);
        digest[idx * 4 + 3] = (uint8_t) (H[idx]);
    }
    return 0;
}

/* Attempt to decompress the whole buffer, return the original data on error. */
static LVByteArrayRef try_buffer_decompress(LVByteArrayRef packed) {
#if (USE_ZLIB==1)
    if (!packed)
        return packed;

    const unsigned MINIMAL_CHUNK_SIZE = 1024;

    LVByteArrayRef unpacked(new LVByteArray());
    unpacked->reserve(MINIMAL_CHUNK_SIZE);

    z_stream_s zstrm;
    int        zerr;

    memset(&zstrm, 0, sizeof (zstrm));
    zstrm.avail_in = packed->length();
    zstrm.next_in = packed->get();
    zstrm.avail_out = unpacked->size();
    zstrm.next_out = unpacked->get();

    zerr = inflateInit2(&zstrm, -15);
    for (;;) {
        switch (zerr) {
        case Z_OK:
            break;
        case Z_BUF_ERROR:
            /* Not enough space in the output buffer. */
            break;
        case Z_STREAM_END:
            /* The end… */
            if (zstrm.total_out > (unsigned)unpacked->length())
                unpacked->addSpace(zstrm.total_out - unpacked->length());
            assert(zstrm.total_in == (unsigned)packed->length());
            packed = unpacked;
            [[fallthrough]];
        default:
            // The data was not compressed or is corrupted.
            goto end;
        }
        if (Z_BUF_ERROR == zerr || zstrm.avail_out < MINIMAL_CHUNK_SIZE) {
            if (zstrm.total_out > (unsigned)unpacked->length())
                unpacked->addSpace(zstrm.total_out - unpacked->length());
            unpacked->reserve(MINIMAL_CHUNK_SIZE + zstrm.total_out + 2 * zstrm.avail_in);
            zstrm.avail_out = unpacked->size() - zstrm.total_out;
            zstrm.next_out = unpacked->get() + zstrm.total_out;
        }
        zerr = inflate(&zstrm, Z_FINISH);
    }

end:
    inflateEnd(&zstrm);
#endif
    return packed;
}

// Adobe obfuscated item demangling proxy: XORs first 1024 bytes of source stream with key
class AdobeDemanglingStream : public StreamProxy {
    LVArray<lUInt8> & _key;
public:
    AdobeDemanglingStream(LVStreamRef baseStream, LVArray<lUInt8> & key) : StreamProxy(baseStream), _key(key) {
    }

    virtual lverror_t Read( void * buf, lvsize_t count, lvsize_t * nBytesRead ) {
        lvpos_t pos = _base->GetPos();
        lverror_t res = _base->Read(buf, count, nBytesRead);
        if (res != LVERR_OK)
            return res;
        if (_key.length() != 16)
            return LVERR_OK;
        if (pos >= 1024)
            return LVERR_OK;
        unsigned obfuscated_size = 1024 - pos;
        if (obfuscated_size > count)
            obfuscated_size = count;
        for (unsigned i = 0; i < obfuscated_size; ++i)
            ((lUInt8*)buf)[i] ^= _key[(i + pos) % 16];
        return LVERR_OK;
    }

    virtual LVByteArrayRef GetData()
    {
        return try_buffer_decompress(LVStream::GetData());
    }

};

// IDPF obfuscated item demangling proxy: XORs first 1040 bytes of source stream with key
// https://idpf.org/epub/20/spec/FontManglingSpec.html
// https://www.w3.org/submissions/epub-ocf/#obfus-algorithm
class IdpfDemanglingStream : public StreamProxy {
    LVArray<lUInt8> & _key;
public:
    IdpfDemanglingStream(LVStreamRef baseStream, LVArray<lUInt8> & key) : StreamProxy(baseStream), _key(key) {
    }

    virtual lverror_t Read( void * buf, lvsize_t count, lvsize_t * nBytesRead ) {
        lvpos_t pos = _base->GetPos();
        lverror_t res = _base->Read(buf, count, nBytesRead);
        if (res != LVERR_OK)
            return res;
        if (_key.length() != 20)
            return LVERR_OK;
        if (pos >= 1040)
            return LVERR_OK;
        unsigned obfuscated_size = 1040 - pos;
        if (obfuscated_size > count)
            obfuscated_size = count;
        for (unsigned i = 0; i < obfuscated_size; ++i)
            ((lUInt8*)buf)[i] ^= _key[(i + pos) % 20];
        return LVERR_OK;
    }

    virtual LVByteArrayRef GetData()
    {
        return try_buffer_decompress(LVStream::GetData());
    }

};

class EncryptedItemCallback {
public:
    virtual void addEncryptedItem(lString32 uri, lString32 algorithm) = 0;
    virtual ~EncryptedItemCallback() {}
};

class EncCallback : public LVXMLParserCallback {
    bool insideEncryption;
    bool insideEncryptedData;
    bool insideEncryptionMethod;
    bool insideCipherData;
    bool insideCipherReference;
public:
    /// called on opening tag <
    virtual ldomNode * OnTagOpen( const lChar32 * nsname, const lChar32 * tagname) {
        CR_UNUSED(nsname);
        if (!lStr_cmp(tagname, "encryption"))
            insideEncryption = true;
        else if (!lStr_cmp(tagname, "EncryptedData"))
            insideEncryptedData = true;
        else if (!lStr_cmp(tagname, "EncryptionMethod"))
            insideEncryptionMethod = true;
        else if (!lStr_cmp(tagname, "CipherData"))
            insideCipherData = true;
        else if (!lStr_cmp(tagname, "CipherReference"))
            insideCipherReference = true;
        return NULL;
    }
    /// called on tag close
    virtual void OnTagClose( const lChar32 * nsname, const lChar32 * tagname, bool self_closing_tag=false ) {
        CR_UNUSED(nsname);
        if (!lStr_cmp(tagname, "encryption"))
            insideEncryption = false;
        else if (!lStr_cmp(tagname, "EncryptedData") && insideEncryptedData) {
            if (!algorithm.empty() && !uri.empty()) {
                _container->addEncryptedItem(uri, algorithm);
            }
            insideEncryptedData = false;
        } else if (!lStr_cmp(tagname, "EncryptionMethod"))
            insideEncryptionMethod = false;
        else if (!lStr_cmp(tagname, "CipherData"))
            insideCipherData = false;
        else if (!lStr_cmp(tagname, "CipherReference"))
            insideCipherReference = false;
    }
    /// called on element attribute
    virtual void OnAttribute( const lChar32 * nsname, const lChar32 * attrname, const lChar32 * attrvalue ) {
        CR_UNUSED2(nsname, attrvalue);
        if (!lStr_cmp(attrname, "URI") && insideCipherReference) {
            uri = attrvalue;
        }
        else if (!lStr_cmp(attrname, "Algorithm") && insideEncryptionMethod) {
            algorithm = attrvalue;
        }
    }
    /// called on text
    virtual void OnText( const lChar32 * text, int len, lUInt32 flags ) {
        CR_UNUSED3(text,len,flags);
    }
    /// add named BLOB data to document
    virtual bool OnBlob(lString32 name, const lUInt8 * data, int size) {
        CR_UNUSED3(name,data,size);
        return false;
    }

    virtual void OnStop() { }
    /// called after > of opening tag (when entering tag body)
    virtual void OnTagBody() { }

    EncryptedItemCallback * _container;
    lString32 algorithm;
    lString32 uri;
    /// destructor
    EncCallback(EncryptedItemCallback * container) : _container(container) {
        insideEncryption = false;
        insideEncryptedData = false;
        insideEncryptionMethod = false;
        insideCipherData = false;
        insideCipherReference = false;
    }
    virtual ~EncCallback() {}
};

enum encryption_method_t {
    NOT_ENCRYPTED = 0,
    ADOBE_OBFUSCATION,
    IDPF_OBFUSCATION,
    UNSUPPORTED_ENCRYPTION
};

class EncryptedDataContainer : public LVContainer, public EncryptedItemCallback {
    LVContainerRef _container;
    LVHashTable<lString32, encryption_method_t> _encrypted_items;
    bool _has_unsupported_encrypted_items;
    bool _has_adobe_obfuscated_items;
    bool _has_idpf_obfuscated_items;
    LVArray<lUInt8> _adobeManglingKey;
    LVArray<lUInt8> _idpfFontManglingKey;
public:
    EncryptedDataContainer(LVContainerRef baseContainer) : _container(baseContainer), _encrypted_items(16),
            _has_unsupported_encrypted_items(false), _has_adobe_obfuscated_items(false), _has_idpf_obfuscated_items(false) { }

    virtual LVContainer * GetParentContainer() { return _container->GetParentContainer(); }
    //virtual const LVContainerItemInfo * GetObjectInfo(const lChar32 * pname);
    virtual const LVContainerItemInfo * GetObjectInfo(int index) { return _container->GetObjectInfo(index); }
    virtual const LVContainerItemInfo * GetObjectInfo(lString32 name) { return _container->GetObjectInfo(name); }
    virtual int GetObjectCount() const { return _container->GetObjectCount(); }
    /// returns object size (file size or directory entry count)
    virtual lverror_t GetSize( lvsize_t * pSize ) { return _container->GetSize(pSize); }

    virtual LVStreamRef OpenStream( const lChar32 * fname, lvopen_mode_t mode ) {
        LVStreamRef res = _container->OpenStream(fname, mode);
        if (res.isNull())
            return res;
        encryption_method_t encryption_method;
        if ( _encrypted_items.get(fname, encryption_method) ) {
            if ( encryption_method == ADOBE_OBFUSCATION )
                return LVStreamRef(new AdobeDemanglingStream(res, _adobeManglingKey));
            if ( encryption_method == IDPF_OBFUSCATION )
                return LVStreamRef(new IdpfDemanglingStream(res, _idpfFontManglingKey));
            // If unsupported, return stream as is, not decrypted (its reading may
            // fail, or not if encryption.xml was lying)
        }
        return res;
    }

    /// returns stream/container name, may be NULL if unknown
    virtual const lChar32 * GetName()
    {
        return _container->GetName();
    }
    /// sets stream/container name, may be not implemented for some objects
    virtual void SetName(const lChar32 * name)
    {
        _container->SetName(name);
    }

    virtual void addEncryptedItem(lString32 uri, lString32 algorithm) {
        encryption_method_t encryption_method;
        if (algorithm == U"http://ns.adobe.com/pdf/enc#RC") {
            encryption_method = ADOBE_OBFUSCATION;
            _has_adobe_obfuscated_items = true;
        }
        else if (algorithm == U"http://www.idpf.org/2008/embedding") {
            encryption_method = IDPF_OBFUSCATION;
            _has_idpf_obfuscated_items = true;
        }
        else {
            _has_unsupported_encrypted_items = true;
            encryption_method = UNSUPPORTED_ENCRYPTION;
            printf("CRE: encrypted (DRM) EPUB item: %s\n", UnicodeToUtf8(uri).c_str());
        }
        // Add the uri with and without a leading /, so we don't have to
        // add/remove one when looking up a uri
        _encrypted_items.set(uri, encryption_method);
        if (uri[0] == U'/')
            _encrypted_items.set(uri.substr(1), encryption_method);
        else
            _encrypted_items.set("/"+uri, encryption_method);
    }

    bool setAdobeManglingKey(lString32 key) {
        if (key.startsWith("urn:uuid:"))
            key = key.substr(9);
        _adobeManglingKey.clear();
        _adobeManglingKey.reserve(16);
        lUInt8 b = 0;
        int n = 0;
        for (int i=0; i<key.length(); i++) {
            int d = hexDigit(key[i]);
            if (d>=0) {
                b = (b << 4) | d;
                if (++n > 1) {
                    _adobeManglingKey.add(b);
                    n = 0;
                    b = 0;
                }
            }
        }
        return _adobeManglingKey.length() == 16;
    }

    void setIdpfManglingKey(lString32 key) {
        _idpfFontManglingKey.clear();
        lString8 utf = UnicodeToUtf8(key);
        sha1digest(_idpfFontManglingKey.addSpace(20), (lUInt8*)(utf.c_str()), utf.length());
    }

    bool hasAdobeObfuscatedItems() {
        return _has_adobe_obfuscated_items;
    }

    bool hasIdpfObfuscatedItems() {
        return _has_idpf_obfuscated_items;
    }

    bool hasUnsupportedEncryptedItems() {
        return _has_unsupported_encrypted_items;
    }

    bool open() {
        LVStreamRef stream = _container->OpenStream(U"META-INF/encryption.xml", LVOM_READ);
        if (stream.isNull())
            return false;
        EncCallback enccallback(this);
        LVXMLParser parser(stream, &enccallback, false, false);
        if (!parser.Parse())
            return false;
        if (_encrypted_items.length())
            return true;
        return false;
    }
};

void createEncryptedEpubWarningDocument(ldomDocument * m_doc) {
    CRLog::error("EPUB document contains encrypted items");
    ldomDocumentWriter writer(m_doc);
    writer.OnTagOpenNoAttr(NULL, U"body");
    writer.OnTagOpenNoAttr(NULL, U"h3");
    lString32 hdr("Encrypted content");
    writer.OnText(hdr.c_str(), hdr.length(), 0);
    writer.OnTagClose(NULL, U"h3");

    writer.OnTagOpenAndClose(NULL, U"hr");

    writer.OnTagOpenNoAttr(NULL, U"p");
    lString32 txt("This document is encrypted (has DRM protection).");
    writer.OnText(txt.c_str(), txt.length(), 0);
    writer.OnTagClose(NULL, U"p");

    writer.OnTagOpenNoAttr(NULL, U"p");
    lString32 txt2("Reading of DRM protected books is unsupported.");
    writer.OnText(txt2.c_str(), txt2.length(), 0);
    writer.OnTagClose(NULL, U"p");

    writer.OnTagOpenNoAttr(NULL, U"p");
    lString32 txt3("To read this book, please use the software recommended by the book seller.");
    writer.OnText(txt3.c_str(), txt3.length(), 0);
    writer.OnTagClose(NULL, U"p");

    writer.OnTagOpenAndClose(NULL, U"hr");

    writer.OnTagOpenNoAttr(NULL, U"p");
    lString32 txt4("");
    writer.OnText(txt4.c_str(), txt4.length(), 0);
    writer.OnTagClose(NULL, U"p");

    writer.OnTagClose(NULL, U"body");
}

LVStreamRef GetEpubCoverpage(LVContainerRef arc)
{
    // check root media type
    lString32 rootfilePath = EpubGetRootFilePath(arc);
    if ( rootfilePath.empty() )
        return LVStreamRef();

    EncryptedDataContainer * decryptor = new EncryptedDataContainer(arc);
    if (decryptor->open()) {
        CRLog::debug("EPUB: encrypted items detected");
    }

    LVContainerRef m_arc = LVContainerRef(decryptor);

    lString32 codeBase = LVExtractPath(rootfilePath, false);
    CRLog::trace("codeBase=%s", LCSTR(codeBase));

    LVStreamRef content_stream = m_arc->OpenStream(rootfilePath.c_str(), LVOM_READ);
    if ( content_stream.isNull() )
        return LVStreamRef();


    LVStreamRef coverPageImageStream;
    // reading content stream
    {
        lString32 coverId;
        ldomDocument * doc = LVParseXMLStream( content_stream );
        if ( !doc )
            return LVStreamRef();

        // Iterate all package/metadata/meta
        ldomNode * metadata = doc->nodeFromXPath(lString32("package/metadata"));
        int nb_metadata_items = (metadata && metadata->isElement()) ? metadata->getChildCount() : 0;
        lUInt16 meta_id = doc->getElementNameIndex(U"meta");
        for (int i=0; i<nb_metadata_items; i++) {
            ldomNode * item = metadata->getChildNode(i);
            if ( item->getNodeId() != meta_id )
                continue;
            lString32 name = item->getAttributeValue("name");
            if (name == "cover") {
                lString32 content = item->getAttributeValue("content");
                coverId = content;
                // We're done
                break;
            }
        }

        // Iterate all package/manifest/item
        ldomNode * manifest = doc->nodeFromXPath(lString32("package/manifest"));
        int nb_manifest_items = (manifest && manifest->isElement()) ? manifest->getChildCount() : 0;
        lUInt16 item_id = doc->getElementNameIndex(U"item");
        for (int i=0; i<nb_manifest_items; i++) {
            ldomNode * item = manifest->getChildNode(i);
            if ( item->getNodeId() != item_id )
                continue;
            lString32 href = item->getAttributeValue("href");
            lString32 id = item->getAttributeValue("id");
            if ( !href.empty() && !id.empty() ) {
                if (id == coverId) {
                    // coverpage file
                    href = DecodeHTMLUrlString(href);
                    lString32 coverFileName = LVCombinePaths(codeBase, href);
                    CRLog::info("EPUB coverpage file: %s", LCSTR(coverFileName));
                    coverPageImageStream = m_arc->OpenStream(coverFileName.c_str(), LVOM_READ);
                    // We're done
                    break;
                }
            }
        }
        delete doc;
    }

    return coverPageImageStream;
}


class EmbeddedFontStyleParser {
    LVEmbeddedFontList & _fontList;
    lString32 _basePath;
    int _state;
    lString8 _face;
    lString8 islocal;
    bool _italic;
    bool _bold;
    lString32 _url;
public:
    EmbeddedFontStyleParser(LVEmbeddedFontList & fontList) : _fontList(fontList) { }
    void onToken(char token) {
        // 4,5:  font-family:
        // 6,7:  font-weight:
        // 8,9:  font-style:
        //10,11: src:
        //   10   11    12   13
        //   src   :   url    (
        //CRLog::trace("state==%d: %c ", _state, token);
        switch (token) {
        case ':':
            if (_state < 2) {
                _state = 0;
            } else if (_state == 4 || _state == 6 || _state == 8 || _state == 10) {
                _state++;
            } else if (_state != 3) {
                _state = 2;
            }
            break;
        case ';':
            if (_state < 2) {
                _state = 0;
            } else if (_state != 3) {
                _state = 2;
            }
            break;
        case '{':
            if (_state == 1) {
                _state = 2; // inside @font {
                _face.clear();
                _italic = false;
                _bold = false;
                _url.clear();
            } else
                _state = 3; // inside other {
            break;
        case '}':
            if (_state == 2) {
                if (!_url.empty()) {
//                    CRLog::trace("@font { face: %s; bold: %s; italic: %s; url: %s", _face.c_str(), _bold ? "yes" : "no",
//                                 _italic ? "yes" : "no", LCSTR(_url));
                    if (islocal.length()==5 && _basePath.length()!=0)
                        _url = _url.substr((_basePath.length()+1), (_url.length()-_basePath.length()));
                    while (_fontList.findByUrl(_url))
                        _url.append(lString32(" ")); //avoid add() replaces existing local name
                    _fontList.add(_url, _face, _bold, _italic);
                }
            }
            _state = 0;
            break;
        case ',':
            if (_state == 2) {
                if (!_url.empty()) {
                    if (islocal.length() == 5 && _basePath.length()!=0)
                        _url=(_url.substr((_basePath.length()+1),(_url.length()-_basePath.length())));
                    while (_fontList.findByUrl(_url))
                        _url.append(lString32(" "));
                    _fontList.add(_url, _face, _bold, _italic);
                }
                _state = 11;
            }
            break;
        case '(':
            if (_state == 12) {
                _state = 13;
            } else {
                if (_state > 3)
                    _state = 2;
            }
            break;
        }
    }
    void onToken(lString8 & token) {
        if (token.empty())
            return;
        lString8 t = token;
        token.clear();
        //CRLog::trace("state==%d: %s", _state, t.c_str());
        if (t == "@font-face") {
            if (_state == 0)
                _state = 1; // right after @font
            return;
        }
        if (_state == 1)
            _state = 0;
        if (_state == 2) {
            if (t == "font-family")
                _state = 4;
            else if (t == "font-weight")
                _state = 6;
            else if (t == "font-style")
                _state = 8;
            else if (t == "src")
                _state = 10;
        } else if (_state == 5) {
            _face = t;
            _state = 2;
        } else if (_state == 7) {
            if (t == "bold")
                _bold = true;
            _state = 2;
        } else if (_state == 9) {
            if (t == "italic")
                _italic = true;
            else if (t == "oblique" || t.startsWith("oblique ") ) // oblique may be followed by angle values
                _italic = true;
            _state = 2;
        } else if (_state == 11) {
            if (t == "url") {
                _state = 12;
                islocal=t;
            }
            else if (t=="local") {
                _state=12;
                islocal=t;
            }
            else
                _state = 2;
        }
    }
    void onQuotedText(lString8 & token) {
        //CRLog::trace("state==%d: \"%s\"", _state, token.c_str());
        if (_state == 11 || _state == 13) {
            if (!token.empty()) {
                lString32 ltoken = Utf8ToUnicode(token);
                if (ltoken.startsWithNoCase(lString32("res://")) || ltoken.startsWithNoCase(lString32("file://")) )
                    _url = ltoken;
                else
                    _url = LVCombinePaths(_basePath, ltoken);
            }
            _state = 2;
        } else if (_state == 5) {
            if (!token.empty()) {
                _face = token;
            }
            _state = 2;
        }
        token.clear();
    }
    lString8 deletecomment(lString8 css) {
        int state;
        lString8 tmp=lString8("");
        tmp.reserve( css.length() );
        char c;
        state = 0;
        for (int i=0;i<css.length();i++) {
            c=css[i];
            if (state == 0 ) {
                if (c == ('/'))           // ex. [/]
                    state = 1;
                else if (c == ('\'') )    // ex. [']
                    state = 5;
                else if (c == ('\"'))     // ex. ["]
                    state = 7;
            }
            else if (state == 1 && c == ('*'))     // ex. [/*]
                    state = 2;
            else if (state == 1) {                // ex. [<secure/_stdio.h> or 5/3]
                    tmp<<('/');
                    if (c != ('/'))               // stay in state 1 if [//]
                        state = 0;
            }
            else if (state == 2 && c == ('*'))    // ex. [/*he*]
                    state = 3;
            else if (state == 2)                // ex. [/*heh]
                    state = 2;
            else if (state == 3 && c == ('/'))    // ex. [/*heh*/]
                    state = 0;
            else if (state == 3 && c == ('*'))    // ex. [/*heh**]
                    state = 3;
            else if (state == 3)                // ex. [/*heh*e]
                    state = 2;
            /* Moved up for faster normal path:
            else if (state == 0 && c == ('\'') )    // ex. [']
                    state = 5;
            */
            else if (state == 5 && c == ('\\'))     // ex. ['\]
                    state = 6;
            else if (state == 6)                // ex. ['\n or '\' or '\t etc.]
                    state = 5;
            else if (state == 5 && c == ('\'') )   // ex. ['\n' or '\'' or '\t' ect.]
                    state = 0;
            /* Moved up for faster normal path:
            else if (state == 0 && c == ('\"'))    // ex. ["]
                    state = 7;
            */
            else if (state == 8)                // ex. ["\n or "\" or "\t ect.]
                    state = 7;
            else if (state == 7 && c == ('\"'))    // ex. ["\n" or "\"" or "\t" ect.]
                    state = 0;
            if ((state == 0 && c != ('/')) || state == 5 || state == 6 || state == 7 || state == 8)
                    tmp<<c;
        }
        return tmp;
    }
    void parse(lString32 basePath, const lString8 & css) {
        _state = 0;
        _basePath = basePath;
        lString8 token;
        char insideQuotes = 0;
        lString8 css_ = deletecomment(css);
        for (int i=0; i<css_.length(); i++) {
            char ch = css_[i];
            if (insideQuotes || _state == 13) {
                if (ch == insideQuotes || (_state == 13 && ch == ')')) {
                    onQuotedText(token);
                    insideQuotes =  0;
                    if (_state == 13)
                        onToken(ch);
                } else {
                    if (_state == 13 && token.empty() && (ch == '\'' || ch=='\"')) {
                        insideQuotes = ch;
                    } else if (ch != ' ' || _state != 13)
                        token << ch;
                }
                continue;
            }
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                onToken(token);
            } else if (ch == '@' || ch=='-' || ch=='_' || ch=='.' || (ch>='a' && ch <='z') || (ch>='A' && ch <='Z') || (ch>='0' && ch <='9')) {
                token << ch;
            } else if (ch == ':' || ch=='{' || ch == '}' || ch=='(' || ch == ')' || ch == ';' || ch == ',') {
                onToken(token);
                onToken(ch);
            } else if (ch == '\'' || ch == '\"') {
                onToken(token);
                insideQuotes = ch;
            }
        }
    }
};

bool ImportEpubDocument( LVStreamRef stream, ldomDocument * m_doc, LVDocViewCallback * progressCallback,
            CacheLoadingCallback * formatCallback, bool metadataOnly,
            const elem_def_t * node_scheme, const attr_def_t * attr_scheme, const ns_def_t * ns_scheme )
{
    LVContainerRef arc = LVOpenArchieve( stream );
    if ( arc.isNull() )
        return false; // not a ZIP archive

    // check root media type
    lString32 rootfilePath = EpubGetRootFilePath(arc);
    if ( rootfilePath.empty() )
        return false;

    // Check for any encrypted/obfuscated resources
    EncryptedDataContainer * decryptor = new EncryptedDataContainer(arc);
    if (decryptor->open()) {
        CRLog::debug("EPUB: encrypted items detected");
    }

    // Make our archive container a proxy to the real container,
    // that can deobfuscate resources on-the-fly
    LVContainerRef m_arc = LVContainerRef(decryptor);
    if (decryptor->hasUnsupportedEncryptedItems()) {
        printf("CRE WARNING: EPUB contains unsupported encrypted items (DRM)\n");
        // Don't fail yet, it's possible only some fonts are obfuscated with
        // an unsupported algorithm, and the HTML text is readable.
    }
    m_doc->setContainer(m_arc);

    if ( progressCallback )
        progressCallback->OnLoadFileProgress(1);

    EpubItems epubItems;
    LVArray<EpubItem*> spineItems;

    lString32 codeBase = LVExtractPath(rootfilePath, false);
    // CRLog::trace("codeBase=%s", LCSTR(codeBase));

    LVStreamRef content_stream = m_arc->OpenStream(rootfilePath.c_str(), LVOM_READ);
    if ( content_stream.isNull() )
        return false;

    bool isEpub3 = false;
    lString32 epubVersion;
    lString32 navHref; // epub3 TOC
    lString32 ncxHref; // epub2 TOC
    lString32 pageMapHref; // epub2 Adobe page-map
    lString32 pageMapSource;
    lString32 coverId;

    LVEmbeddedFontList fontList;
    EmbeddedFontStyleParser styleParser(fontList);

    // Read OPF file
    {
        CRLog::debug("Parsing opf");
        ldomDocument * doc = LVParseXMLStream( content_stream );
        if ( !doc )
            return false;

        lString32 idpfUniqueIdentifier;
        ldomNode * package = doc->nodeFromXPath(lString32("package"));
        if ( package ) {
            epubVersion = package->getAttributeValue("version");
            if ( !epubVersion.empty() && epubVersion[0] >= '3' )
                isEpub3 = true;
            if ( decryptor->hasIdpfObfuscatedItems() )
                idpfUniqueIdentifier = package->getAttributeValue("unique-identifier");
        }

        // We will gather interesting things from <package><metadata> and <package><manifest>
        ldomNode * metadata = doc->nodeFromXPath(lString32("package/metadata"));
        int nb_metadata_items = (metadata && metadata->isElement()) ? metadata->getChildCount() : 0;
        ldomNode * manifest = doc->nodeFromXPath(lString32("package/manifest"));
        int nb_manifest_items = (manifest && manifest->isElement()) ? manifest->getChildCount() : 0;

        if ( decryptor->hasAdobeObfuscatedItems() ) {
            // There may be multiple <dc:identifier> tags, one of which (urn:uuid:...) being used
            // to make the key needed to deobfuscate obfuscated fonts. We need to parse it even
            // if we're going to load the book (and all its other metadata) from cache.
            // Iterate all package/metadata/identifier
            lUInt16 identifier_id = doc->getElementNameIndex(U"identifier");
            for (int i=0; i<nb_metadata_items; i++) {
                ldomNode * item = metadata->getChildNode(i);
                if ( item->getNodeId() != identifier_id )
                    continue;
                lString32 key = item->getText().trim();
                if (decryptor->setAdobeManglingKey(key)) {
                    CRLog::debug("Using font mangling key %s", LCSTR(key));
                    break;
                }
            }
        }
        if ( decryptor->hasIdpfObfuscatedItems() && !idpfUniqueIdentifier.empty() ) {
            lUInt16 identifier_id = doc->getElementNameIndex(U"identifier");
            for (int i=0; i<nb_metadata_items; i++) {
                ldomNode * item = metadata->getChildNode(i);
                if ( item->getNodeId() != identifier_id )
                    continue;
                if ( item->getAttributeValue("id") == idpfUniqueIdentifier ) {
                    lString32 key = item->getText().trim();
                    decryptor->setIdpfManglingKey(key);
                    break;
                }
            }
        }

#if BUILD_LITE!=1
        // If there is a cache file, it contains the fully built DOM document
        // made from the multiple html fragments in the epub, and also
        // m_doc_props which has been serialized.
        // No need to do all the below work, except if we are only
        // requesting metadata (parsing some bits from the EPUB is still
        // less expensive than loading the full cache file).
        if (!metadataOnly) {
            CRLog::debug("Trying loading from cache");
            if ( m_doc->openFromCache(formatCallback, progressCallback) ) {
                CRLog::debug("Loaded from cache");
                if ( progressCallback ) {
                    progressCallback->OnLoadFileEnd( );
                }
                delete doc;
                return true;
            }
            CRLog::debug("Not loaded from cache, parsing epub content");
        }
#endif

        CRPropRef m_doc_props = m_doc->getProps();
        // These are expected to be single, so pick the first one met
        lString32 title = doc->textFromXPath( cs32("package/metadata/title"));
        lString32 language = doc->textFromXPath( cs32("package/metadata/language"));
        lString32 description = doc->textFromXPath( cs32("package/metadata/description"));
        pageMapSource = doc->textFromXPath( cs32("package/metadata/source"));
        m_doc_props->setString(DOC_PROP_TITLE, title);
        m_doc_props->setString(DOC_PROP_LANGUAGE, language);
        m_doc_props->setString(DOC_PROP_DESCRIPTION, description);

        // Return possibly multiple <dc:creator> (authors) and <dc:subject> (keywords)
        // as a single doc_props string with values separated by \n.
        // (these \n can be replaced on the lua side for the most appropriate display)
        bool authors_set = false;
        lString32 authors;
        // Iterate all package/metadata/creator
        lUInt16 creator_id = doc->getElementNameIndex(U"creator");
        for (int i=0; i<nb_metadata_items; i++) {
            ldomNode * item = metadata->getChildNode(i);
            if ( item->getNodeId() != creator_id )
                continue;
            lString32 author = item->getText().trim();
            if (authors_set) {
                authors << "\n" << author;
            }
            else {
                authors << author;
                authors_set = true;
            }
        }
        m_doc_props->setString(DOC_PROP_AUTHORS, authors);

        // There may be multiple <dc:subject> tags, which are usually used for keywords, categories
        bool subjects_set = false;
        lString32 subjects;
        // Iterate all package/metadata/subject
        lUInt16 subject_id = doc->getElementNameIndex(U"subject");
        for (int i=0; i<nb_metadata_items; i++) {
            ldomNode * item = metadata->getChildNode(i);
            if ( item->getNodeId() != subject_id )
                continue;
            lString32 subject = item->getText().trim();
            if (subjects_set) {
                subjects << "\n" << subject;
            }
            else {
                subjects << subject;
                subjects_set = true;
            }
        }
        m_doc_props->setString(DOC_PROP_KEYWORDS, subjects);
        CRLog::info("Authors: %s Title: %s", LCSTR(authors), LCSTR(title));

        // Return possibly multiple <dc:identifier> (identifiers)
        // as a single doc_props string with values in a key-value format (scheme:identifier) separated by ;
        bool identifiers_set = false;
        lString32 identifiers;
        // Iterate all package/metadata/identifier
        lUInt16 identifier_id = doc->getElementNameIndex(U"identifier");
        for (int i=0; i<nb_metadata_items; i++) {
            ldomNode * item = metadata->getChildNode(i);
            if ( item->getNodeId() != identifier_id )
                continue;
            lString32 scheme = item->getAttributeValue(U"scheme");
            lString32 identifier;
            // In version 3, scheme is not set but the type is rather included in the text itself
            if (scheme.empty()) {
                identifier = item->getText().trim();
            }
            else {
                // In version 2, the scheme is only found as attribute
                identifier << scheme << ":" << item->getText().trim();
            }
            if (identifiers_set) {
                identifiers << "\n" << identifier;
            }
            else {
                identifiers << identifier;
                identifiers_set = true;
            }
        }
        m_doc_props->setString(DOC_PROP_IDENTIFIERS, identifiers);

        bool hasSeriesMeta = false;
        bool hasSeriesIdMeta = false;
        // Iterate all package/metadata/meta
        lUInt16 meta_id = doc->getElementNameIndex(U"meta");
        for (int i=0; i<nb_metadata_items; i++) {
            ldomNode * item = metadata->getChildNode(i);
            if ( item->getNodeId() != meta_id )
                continue;
            // If we've already got all of 'em, we're done
            if (hasSeriesIdMeta && !coverId.empty()) {
                break;
            }
            lString32 name = item->getAttributeValue("name");
            // Might come before or after the series stuff
            // (e.g., while you might think it'd come early, Calibre appends it during the Send To Device process).
            // Fun fact: this isn't part of *either* version of the ePub specs.
            // It's simply an agreed-upon convention, given how utterly terrible the actual specs are.
            if (coverId.empty() && name == "cover") {
                lString32 content = item->getAttributeValue("content");
                coverId = content;
                // Note: this is expected to be an id, found in the manifest.
                // Some bad EPUBs put there the path to the cover image file, which we then
                // won't find in the manifest.
                continue;
            }
            // Has to come before calibre:series_index
            if (!hasSeriesMeta && name == "calibre:series") {
                lString32 content = item->getAttributeValue("content");
                PreProcessXmlString(content, 0);
                m_doc_props->setString(DOC_PROP_SERIES_NAME, content);
                hasSeriesMeta = true;
                continue;
            }
            // Has to come after calibre:series
            if (hasSeriesMeta && name == "calibre:series_index") {
                lString32 content = item->getAttributeValue("content");
                PreProcessXmlString(content, 0);
                m_doc_props->setString(DOC_PROP_SERIES_NUMBER, content);
                hasSeriesIdMeta = true;
                continue;
            }
        }

        // Fallback to ePub 3 series metadata, c.f., https://www.w3.org/publishing/epub3/epub-packages.html#sec-belongs-to-collection
        // Because, yes, they're less standard than Calibre's ;D. Gotta love the ePub specs...
        // NOTE: This doesn't include the shittier variant where apparently a collection-type refines a dc:title's id,
        //       or something? Not in the specs, so, don't care.
        //       c.f., the first branch in https://github.com/koreader/crengine/issues/267#issuecomment-557507150
        //       The only similar thing buried deep in the original 3.0 specs is incredibly convoluted:
        //       http://idpf.org/epub/30/spec/epub30-publications.html#sec-opf-dctitle
        //       That thankfully seems to have been relegated to the past, despite title-type still supporting a collection type:
        //       https://www.w3.org/publishing/epub32/epub-packages.html#sec-title-type
        if (isEpub3 && !hasSeriesMeta) {
            lString32 seriesId;
            // Iterate all package/metadata/meta
            lUInt16 meta_id = doc->getElementNameIndex(U"meta");
            for (int i=0; i<nb_metadata_items; i++) {
                ldomNode * item = metadata->getChildNode(i);
                if ( item->getNodeId() != meta_id )
                    continue;

                lString32 property = item->getAttributeValue("property");

                // If we don't have a collection yet, try to find one
                // NOTE: The specs say that collections *MAY* be nested (i.e., a belongs-to-collection node may refine another one).
                //       For simplicity's sake, we only honor the first belongs-to-collection node here.
                //       If I had actual test data, I could have instead opted to specifically match on the "parent" collection,
                //       or the most deeply nested one, depending on what made the most sense, but I don't, so, KISS ;).
                if (!hasSeriesMeta) {
                    if (property == "belongs-to-collection") {
                        lString32 content = item->getText().trim();
                        PreProcessXmlString(content, 0);
                        m_doc_props->setString(DOC_PROP_SERIES_NAME, content);
                        hasSeriesMeta = true;
                        seriesId = item->getAttributeValue("id");
                        // Next!
                        continue;
                    }
                }

                // If we've got a collection, check if other properties refine it...
                if (hasSeriesMeta) {
                    // NOTE: We don't really handle series any differently than set, so we don't really care about this...
                    /*
                    if (property == "collection-type") {
                        // Only support valid types (series or set)
                        lString32 content = item->getText().trim();
                        if (content == "series" || content == "set") {
                            lString32 id = item->getAttributeValue("refines");
                            // Strip the anchor to match against seriesId
                            if (id.startsWith("#")) {
                                id = id.substr(1, id.length() - 1);
                            }
                            if (id == seriesId) {
                                // Next!
                                continue;
                            }
                        }
                    }
                    */
                    if (property == "group-position") {
                        lString32 id = item->getAttributeValue("refines");
                        // Strip the anchor to match against seriesId
                        if (id.startsWith("#")) {
                            id = id.substr(1, id.length() - 1);
                        }
                        // If we've got a match, that's our position in the series!
                        if (id == seriesId) {
                            lString32 content = item->getText().trim();
                            PreProcessXmlString(content, 0);
                            // NOTE: May contain decimal values (much like calibre:series_index).
                            //       c.f., https://github.com/koreader/crengine/pull/346#discussion_r436190907
                            m_doc_props->setString(DOC_PROP_SERIES_NUMBER, content);
                            // And we're done :)
                            break;
                        }
                    }
                }
            }
        }

        // We'll be reusing these later
        ldomNode * spine = doc->nodeFromXPath( cs32("package/spine") );
        lUInt16 itemref_id = doc->getElementNameIndex(U"itemref");

        // If no cover specified among the metadata by the publisher, or if it is
        // not found, there may still be one used (and usable by us) in the first
        // XHTML fragment, that we may want to look at.
        lString32 cover_xhtml_id;
        if ( spine && spine->getChildCount() > 1 ) {
            // We need at least 2 xhtml fragments to maybe have the 1st hold a cover
            int nb_itemrefs = spine->getChildCount();
            for (int i=0; i<nb_itemrefs; i++) {
                ldomNode * item = spine->getChildNode(i);
                if ( item->getNodeId() != itemref_id )
                    continue;
                cover_xhtml_id = item->getAttributeValue("idref");
                // We'll be looking for it later
                break;
            }
        }

        // If no cover to look for, we're done looking for metadata.
        // We still prefer finding an ePub3 cover (it may happen that even in an ePub3,
        // a ePub2 cover is specified, possibly some remnant metadata, and may not exist).
        // We'll have to find out that while iterating the manifest, and exit when found out.
        bool look_for_coverid = !coverId.empty();
        bool look_for_epub3_cover = isEpub3;
        // We'll also have to look for the href of the coverxhtml_id: if none of the previous
        // covers is found, we'll look inside the xhtml for a cover image.
        bool look_for_coverxhtml_id = !cover_xhtml_id.empty();
        lString32 cover_xhtml_href;
        bool look_at_cover_xhtml = true;

        if ( progressCallback )
            progressCallback->OnLoadFileProgress(2);

        // items
        CRLog::debug("opf: reading items");
        // Iterate all package/manifest/item
        lUInt16 item_id = doc->getElementNameIndex(U"item");
        for (int i=0; i<nb_manifest_items; i++) {
            if ( metadataOnly && !look_for_coverid && !look_for_epub3_cover && !look_for_coverxhtml_id) {
                // No more stuff to find, stop iterating
                break;
            }
            ldomNode * item = manifest->getChildNode(i);
            if ( item->getNodeId() != item_id )
                continue;
            lString32 href = item->getAttributeValue("href");
            lString32 mediaType = item->getAttributeValue("media-type");
            lString32 id = item->getAttributeValue("id");
            if ( !href.empty() && !id.empty() ) {
                href = DecodeHTMLUrlString(href);
                if ( look_for_epub3_cover ) {
                    // c.f. https://www.w3.org/publishing/epub3/epub-packages.html#sec-cover-image
                    lString32 props = item->getAttributeValue("properties"); // NOTE: Yes, plural, not a typo... -_-"
                    if (!props.empty() && props == "cover-image") {
                        lString32 coverFileName = LVCombinePaths(codeBase, href);
                        LVStreamRef stream = m_arc->OpenStream(coverFileName.c_str(), LVOM_READ);
                        if ( !stream.isNull() ) {
                            LVImageSourceRef img = LVCreateStreamImageSource(stream);
                            if ( !img.isNull() ) {
                                m_doc_props->setString(DOC_PROP_COVER_FILE, coverFileName);
                                // We found the epub3 cover, and it is a valid image: stop looking for any other
                                look_for_coverid = false;
                                look_for_coverxhtml_id = false;
                                look_at_cover_xhtml = false;
                            }
                        }
                        // We found the cover-image item: valid image or not, stop looking for it
                        look_for_epub3_cover = false;
                    }
                }
                if ( look_for_coverid && id==coverId ) {
                    lString32 coverFileName = LVCombinePaths(codeBase, href);
                    CRLog::info("EPUB coverpage file: %s", LCSTR(coverFileName));
                    LVStreamRef stream = m_arc->OpenStream(coverFileName.c_str(), LVOM_READ);
                    if ( !stream.isNull() ) {
                        LVImageSourceRef img = LVCreateStreamImageSource(stream);
                        if ( !img.isNull() ) {
                            CRLog::info("EPUB coverpage image is correct: %d x %d", img->GetWidth(), img->GetHeight() );
                            m_doc_props->setString(DOC_PROP_COVER_FILE, coverFileName);
                            // We found the epub2 cover, and it is a valid image: stop looking for the coverxhtml one.
                            look_for_coverxhtml_id = false;
                            look_at_cover_xhtml = false;
                        }
                    }
                    // We found the coverid: valid image or not, stop looking for it
                    look_for_coverid = false;
                }
                if ( look_for_coverxhtml_id && id==cover_xhtml_id ) {
                    // At this point, we just store the href. We will parse it if we end up
                    // not having found any of the epub3 cover or coverId.
                    cover_xhtml_href = href;
                    look_for_coverxhtml_id = false;
                }
                if (metadataOnly) {
                    continue;
                }
                EpubItem * epubItem = new EpubItem;
                epubItem->href = href;
                epubItem->id = id;
                epubItem->mediaType = mediaType;
                epubItem->is_xhtml = mediaType == "application/xhtml+xml";
                epubItems.add( epubItem );

                if ( isEpub3 && navHref.empty() ) {
                    lString32 properties = item->getAttributeValue("properties");
                    // We met properties="nav scripted"...
                    if ( properties == U"nav" || properties.startsWith(U"nav ")
                            || properties.endsWith(U" nav") || properties.pos(U" nav ") >= 0 ) {
                        navHref = href;
                    }
                }
            }
            if (metadataOnly) {
                continue;
            }
            if (mediaType == "text/css") {
                // Parse all CSS files to see if they specify some @font-face
                lString32 name = LVCombinePaths(codeBase, href);
                LVStreamRef cssStream = m_arc->OpenStream(name.c_str(), LVOM_READ);
                if (!cssStream.isNull()) {
                    lString8 cssFile = UnicodeToUtf8(LVReadTextFile(cssStream));
                    lString32 base = name;
                    LVExtractLastPathElement(base);
                    //CRLog::trace("style: %s", cssFile.c_str());
                    styleParser.parse(base, cssFile);
                }
                // Huge CSS files may take some time being parsed, so update progress
                // after each one to get a chance of it being displayed at this point.
                if ( progressCallback )
                    progressCallback->OnLoadFileProgress(3);
            }
        }
        CRLog::debug("opf: reading items done.");

        if ( look_at_cover_xhtml && !cover_xhtml_href.empty() ) {
            // No other cover found: look inside the cover xhtml fragment
            lString32 cover_xhtml_path = LVCombinePaths(codeBase, cover_xhtml_href);
            CRLog::info("EPUB cover xhtml page file: %s", LCSTR(cover_xhtml_path));
            LVStreamRef stream = m_arc->OpenStream(cover_xhtml_path.c_str(), LVOM_READ);
            lString32 cover_image_href;
            if ( ExtractCoverFilenameFromCoverPageFragment(stream, cover_image_href, node_scheme, attr_scheme, ns_scheme) ) {
                lString32 codeBase = LVExtractPath( cover_xhtml_path );
                if ( codeBase.length()>0 && codeBase.lastChar()!='/' )
                    codeBase.append(1, U'/');
                lString32 cover_image_path = LVCombinePaths(codeBase, cover_image_href);
                CRLog::info("EPUB cover image file: %s", LCSTR(cover_image_path));
                LVStreamRef stream = m_arc->OpenStream(cover_image_path.c_str(), LVOM_READ);
                if ( stream.isNull() ) {
                    // Try again in case cover_image_path is percent-encoded
                    cover_image_path = LVCombinePaths(codeBase, DecodeHTMLUrlString(cover_image_href));
                    CRLog::info("EPUB cover image file pct-decoded: %s", LCSTR(cover_image_path));
                    stream = m_arc->OpenStream(cover_image_path.c_str(), LVOM_READ);
                }
                if ( !stream.isNull() ) {
                    LVImageSourceRef img = LVCreateStreamImageSource(stream);
                    if ( !img.isNull() ) {
                        CRLog::info("EPUB coverpage image is correct: %d x %d", img->GetWidth(), img->GetHeight() );
                        m_doc_props->setString(DOC_PROP_COVER_FILE, cover_image_path);
                    }
                }
            }
        }

        if ( progressCallback )
            progressCallback->OnLoadFileProgress(4);

        // Gather EpubItems from <package><spine>, which specify which EpubItems,
        // and in which order, make out the book content
        if ( !metadataOnly && epubItems.length()>0 ) {
            CRLog::debug("opf: reading spine");
            if ( spine ) {
                // Some attributes specify that some EpubItems have a specific purpose
                // <spine toc="ncx" page-map="page-map">
                EpubItem * ncx = epubItems.findById( spine->getAttributeValue("toc") );
                if ( ncx!=NULL )
                    ncxHref = LVCombinePaths(codeBase, ncx->href);
                EpubItem * page_map = epubItems.findById( spine->getAttributeValue("page-map") );
                if ( page_map!=NULL )
                    pageMapHref = LVCombinePaths(codeBase, page_map->href);
                // Iterate all package/spine/itemref (each will make a book fragment)
                int nb_itemrefs = spine->getChildCount();
                for (int i=0; i<nb_itemrefs; i++) {
                    ldomNode * item = spine->getChildNode(i);
                    if ( item->getNodeId() != itemref_id )
                        continue;
                    EpubItem * epubItem = epubItems.findById( item->getAttributeValue("idref") );
                    if ( epubItem ) {
                        epubItem->nonlinear = lString32(item->getAttributeValue("linear")).lowercase() == U"no";
                        spineItems.add( epubItem );
                    }
                }
            }
            CRLog::debug("opf: reading spine done");
        }
        delete doc;
        CRLog::debug("opf: closed");
    }

    if ( metadataOnly ) {
        // We may have gathered some metadata, but the book may still not open if
        // it would have no valid spineItems. But best to pretend it is ok and
        // show some metadata to let the user know better which book is invalid.
        return true;
    }

    if ( spineItems.length()==0 )
        return false;

    if ( progressCallback )
        progressCallback->OnLoadFileProgress(5);

    lUInt32 saveFlags = m_doc->getDocFlags();
    m_doc->setDocFlags( saveFlags );
    m_doc->setContainer( m_arc );

    // Create a DocFragment for each and all items in the EPUB's <spine>
    bool relaxed_spine = true;
    if ( m_doc->getDOMVersionRequested() < 20240114 ) {
        // Only accept spine items with media-type="application/xhtml+xml
        relaxed_spine = false;
    }

    // Per EPUB specs, we should get XHTML content, HTML with proper XML balanced tags,
    // which is what ldomDocumentWriter expects. It will not fail if meeting unbalanced
    // HTML, but may auto-close any unclosed element when meeting a close tag for a parent,
    // and may generate a DOM different from the HTML expected one.
    // We can't really notice that, and we can't switch to using ldomDocumentWriterFilter
    // (which would do the right thing with unbalanced HTML).
    ldomDocumentWriter writer(m_doc);
#if 0
    m_doc->setNodeTypes( fb2_elem_table );
    m_doc->setAttributeTypes( fb2_attr_table );
    m_doc->setNameSpaceTypes( fb2_ns_table );
#endif
    //m_doc->setCodeBase( codeBase );

    int fontList_nb_before_head_parsing = fontList.length();
    if (!fontList.empty()) {
        // set document font list, and register fonts
        m_doc->getEmbeddedFontList().set(fontList);
        m_doc->registerEmbeddedFonts();
    }

    // Build a single DOM from all the spine items (each contained in a <DocFragment> internal element)
    ldomDocumentFragmentWriter appender(&writer, cs32("body"), cs32("DocFragment"), lString32::empty_str );
    writer.OnStart(NULL);
    writer.OnTagOpenNoAttr(U"", U"body");
    int fragmentCount = 0;
    size_t spineItemsNb = spineItems.length();
    for ( size_t i=0; i<spineItemsNb; i++ ) {
        if (relaxed_spine || spineItems[i]->is_xhtml) {
            // ldomDocumentFragmentWriter will get all id=, href=, src=... prefixed
            // with _doc_fragment_n, so they are unique in this single DOM.
            lString32 name = LVCombinePaths(codeBase, spineItems[i]->href);
            lString32 subst = cs32("_doc_fragment_") + fmt::decimal(i);
            appender.addPathSubstitution( name, subst );
            //CRLog::trace("subst: %s => %s", LCSTR(name), LCSTR(subst));
        }
    }
    int lastProgressPercent = 5;
    for ( size_t i=0; i<spineItemsNb; i++ ) {
        if ( progressCallback ) {
            int percent = 5 + 95 * i / spineItemsNb;
            if ( percent > lastProgressPercent ) {
                progressCallback->OnLoadFileProgress(percent);
                lastProgressPercent = percent;
            }
        }
        if (relaxed_spine || spineItems[i]->is_xhtml) {
            lString32 name = LVCombinePaths(codeBase, spineItems[i]->href);
            {
                // We want to make sure all spineItems get a DocFragment made,
                // even if we don't support or fail parsing them: we let this
                // fact be known, and if we later fix/support their handling,
                // we won't be inserting a new DocFragment between existing
                // ones and get all xpointers (highlights, last page) invalid
                // because their DocFragment index has been shifted.
                bool handled = false;
                appender.setCodeBase( name );
                appender.setNonLinearFlag(spineItems[i]->nonlinear);
                appender.setFragmentType(); // unset
                CRLog::debug("Checking fragment: %s", LCSTR(name));
                LVStreamRef stream = m_arc->OpenStream(name.c_str(), LVOM_READ);
                if ( !stream.isNull() ) {
                    lString32 base = name;
                    LVExtractLastPathElement(base);
                    //CRLog::trace("base: %s", LCSTR(base));
                    LVHTMLParser parser(stream, &appender);
                    if ( parser.CheckFormat() && parser.Parse() && appender.hasMetBaseTag() ) {
                        // CheckFormat() is not perfect (the two bytes "ul" in some encrypted stream
                        // will get CheckFormat() to succeed) and Parse() won't complain.
                        // Checking hasMetBaseTag() ensures we have met a <body> and that
                        // a <DocFragment> has been added into the DOM.
                        handled = true;
                        fragmentCount++;
                        // We may also meet @font-face in the html <head><style>
                        lString8 headCss = appender.getHeadStyleText();
                        //CRLog::trace("style: %s", headCss.c_str());
                        styleParser.parse(base, headCss);
                    }
                    if ( !handled && relaxed_spine ) {
                        // SVG are allowed in the <spine>
                        LVXMLParser svgparser(stream, &writer, false, false, true);
                        if (svgparser.CheckFormat()) {
                            appender.setFragmentType(U"SpineSvgWrapper");
                            // Alas, we can't easily have this svgparser drive writer or appender
                            // after we would ourselve OnTagOpen(body/html/div) as the parser would
                            // autoclose everything...
                            // SVGs in the spine are rare, so let's not hack these parser/writers,
                            // and get ugly and build some HTML as string that we will fed as
                            // a stream to a HTMLParser.
                            stream->SetPos(0);
                            LVStreamRef mstream = LVCreateMemoryStream();
                            // Make the SVG image horizontally centered (as for standalone SVG
                            // image documents, see top of ldomNode::initNodeRendMethod()).
                            lString8 s("<html><body><autoBoxing style='text-align: center'>");
                            mstream->Write(s.c_str(), s.length(), NULL);
                            LVPumpStream(mstream.get(), stream.get());
                            s = "</autoBoxing></body></html>";
                            mstream->Write(s.c_str(), s.length(), NULL);
                            LVHTMLParser mparser(mstream, &appender);
                            mparser.Parse();
                            handled = true;
                            fragmentCount++;
                        }
                    }
                }
                if ( !handled && relaxed_spine ) {
                    CRLog::error("Document type is not XML/XHTML for fragment %s", LCSTR(name));
                    appender.setFragmentType(U"SpineItemUnsupported");
                    // Create a dummy DocFragment with info about what we couldn't handle
                    LVStreamRef mstream = LVCreateMemoryStream();
                    lString8 s;
                    s << "<html><body><pre>Failed handling EPUB spine item '";
                    s << UnicodeToUtf8(name);
                    s << "' (";
                    s << UnicodeToUtf8(spineItems[i]->mediaType);
                    s << ", ";
                    s << UnicodeToUtf8(spineItems[i]->id);
                    s << ").";
                    s << "</pre></body></html>";
                    mstream->Write(s.c_str(), s.length(), NULL);
                    LVHTMLParser mparser(mstream, &appender);
                    mparser.Parse();
                    fragmentCount++;
                }
            }
        }
    }

    // Clear any toc items possibly added while parsing the HTML
    m_doc->getToc()->clear();
    bool has_toc = false;
    bool has_pagemap = false;

    // EPUB3 documents may contain both a toc.ncx and a nav xhtml toc.
    // We would have preferred to read first a toc.ncx if present, as it
    // is more structured than nav toc (all items have a href), but it
    // seems Sigil includes a toc.ncx for EPUB3, but does not keep it
    // up-to-date, while it does for the nav toc.
    if ( isEpub3 && !navHref.empty() ) {
        // Parse toc nav if epub3
        // http://idpf.org/epub/30/spec/epub30-contentdocs.html#sec-xhtml-nav-def
        navHref = LVCombinePaths(codeBase, navHref);
        LVStreamRef stream = m_arc->OpenStream(navHref.c_str(), LVOM_READ);
        lString32 codeBase = LVExtractPath( navHref );
        if ( codeBase.length()>0 && codeBase.lastChar()!='/' )
            codeBase.append(1, U'/');
        appender.setCodeBase(codeBase);
        if ( !stream.isNull() ) {
            ldomDocument * navDoc = LVParseXMLStream( stream );
            if ( navDoc!=NULL ) {
                // Find <nav epub:type="toc">
                lUInt16 nav_id = navDoc->getElementNameIndex(U"nav");
                ldomNode * navDocRoot = navDoc->getRootNode();
                ldomNode * n = navDocRoot;
                // Kobo falls back to other <nav type=> when no <nav type=toc> is found,
                // let's do the same.
                ldomNode * n_toc = NULL;
                ldomNode * n_landmarks = NULL;
                ldomNode * n_page_list = NULL;
                if (n->isElement() && n->getChildCount() > 0) {
                    int nextChildIndex = 0;
                    n = n->getChildNode(nextChildIndex);
                    while (true) {
                        // Check only the first time we met a node (nextChildIndex == 0)
                        // and not when we get back to it from a child to process next sibling
                        if (nextChildIndex == 0) {
                            if ( n->isElement() && n->getNodeId() == nav_id ) {
                                lString32 type = n->getAttributeValue("type");
                                if ( type == U"toc") {
                                    n_toc = n;
                                }
                                else if ( type == U"landmarks") {
                                    n_landmarks = n;
                                }
                                else if ( type == U"page-list") {
                                    n_page_list = n;
                                }
                            }
                        }
                        // Process next child
                        if (n->isElement() && nextChildIndex < n->getChildCount()) {
                            n = n->getChildNode(nextChildIndex);
                            nextChildIndex = 0;
                            continue;
                        }
                        // No more child, get back to parent and have it process our sibling
                        nextChildIndex = n->getNodeIndex() + 1;
                        n = n->getParentNode();
                        if (!n) // back to root node
                            break;
                        if (n == navDocRoot && nextChildIndex >= n->getChildCount())
                            // back to this node, and done with its children
                            break;
                    }
                }
                if ( !n_toc ) {
                    if ( n_landmarks ) {
                        n_toc = n_landmarks;
                    }
                    else if ( n_page_list ) {
                        n_toc = n_page_list;
                    }
                }
                if ( n_toc ) {
                    // "Each nav element may contain an optional heading indicating the title
                    // of the navigation list. The heading must be one of H1...H6."
                    // We can't do much with this heading (that would not resolve to anything),
                    // we could just add it as a top container item for the others, which will
                    // be useless (and bothering), so let's just ignore it.
                    // Get its first and single <OL> child
                    ldomNode * ol_root = n_toc->findChildElement( LXML_NS_ANY, navDoc->getElementNameIndex(U"ol"), -1 );
                    if ( ol_root )
                        ReadEpubNavToc( m_doc, ol_root, m_doc->getToc(), appender );
                }
                if ( n_page_list ) {
                    ldomNode * ol_root = n_page_list->findChildElement( LXML_NS_ANY, navDoc->getElementNameIndex(U"ol"), -1 );
                    if ( ol_root )
                        ReadEpubNavPageMap( m_doc, ol_root, m_doc->getPageMap(), appender );
                }
                delete navDoc;
            }
        }
    }

    has_toc = m_doc->getToc()->getChildCount() > 0;
    has_pagemap = m_doc->getPageMap()->getChildCount() > 0;

    // For EPUB2 (or EPUB3 where no nav toc was found): read ncx toc
    // We may also find in the ncx a <pageList> list
    if ( ( !has_toc || !has_pagemap ) && !ncxHref.empty() ) {
        LVStreamRef stream = m_arc->OpenStream(ncxHref.c_str(), LVOM_READ);
        lString32 codeBase = LVExtractPath( ncxHref );
        if ( codeBase.length()>0 && codeBase.lastChar()!='/' )
            codeBase.append(1, U'/');
        appender.setCodeBase(codeBase);
        if ( !stream.isNull() ) {
            ldomDocument * ncxdoc = LVParseXMLStream( stream );
            if ( ncxdoc!=NULL ) {
                if ( !has_toc ) {
                    ldomNode * navMap = ncxdoc->nodeFromXPath( cs32("ncx/navMap"));
                    if ( navMap!=NULL )
                        ReadEpubNcxToc( m_doc, navMap, m_doc->getToc(), appender );
                }
                // http://blog.epubbooks.com/346/marking-up-page-numbers-in-the-epub-ncx/
                if ( !has_pagemap ) {
                    ldomNode * pageList = ncxdoc->nodeFromXPath( cs32("ncx/pageList"));
                    if ( pageList!=NULL )
                        ReadEpubNcxPageList( m_doc, pageList, m_doc->getPageMap(), appender );
                }
                delete ncxdoc;
            }
        }
    }

    has_toc = m_doc->getToc()->getChildCount() > 0;
    has_pagemap = m_doc->getPageMap()->getChildCount() > 0;

    // If still no TOC, fallback to using the spine, as Kobo does.
    if ( !has_toc ) {
        LVTocItem * baseToc = m_doc->getToc();
        for ( size_t i=0; i<spineItemsNb; i++ ) {
            if (relaxed_spine || spineItems[i]->is_xhtml) {
                lString32 title = spineItems[i]->id; // nothing much else to use
                lString32 href = appender.convertHref(spineItems[i]->href);
                if ( href.empty() || href[0]!='#' )
                    continue;
                ldomNode * target = m_doc->getNodeById(m_doc->getAttrValueIndex(href.substr(1).c_str()));
                if ( !target )
                    continue;
                ldomXPointer ptr(target, 0);
                baseToc->addChild(title, ptr, lString32::empty_str);
            }
        }
    }

    // If no pagemap, parse Adobe page-map if there is one
    // https://wiki.mobileread.com/wiki/Adobe_Digital_Editions#Page-map
    if ( !has_pagemap && !pageMapHref.empty() ) {
        LVStreamRef stream = m_arc->OpenStream(pageMapHref.c_str(), LVOM_READ);
        lString32 codeBase = LVExtractPath( pageMapHref );
        if ( codeBase.length()>0 && codeBase.lastChar()!='/' )
            codeBase.append(1, U'/');
        appender.setCodeBase(codeBase);
        if ( !stream.isNull() ) {
            ldomDocument * pagemapdoc = LVParseXMLStream( stream );
            if ( pagemapdoc!=NULL ) {
                if ( !has_pagemap ) {
                    ldomNode * pageMap = pagemapdoc->nodeFromXPath( cs32("page-map"));
                    if ( pageMap!=NULL )
                        ReadEpubAdobePageMap( m_doc, pageMap, m_doc->getPageMap(), appender );
                }
                delete pagemapdoc;
            }
        }
    }

    if ( m_doc->getPageMap()->getChildCount() > 0 ) {
        m_doc->getPageMap()->setIsDocumentProvided(true);
        if ( !pageMapSource.empty() )
            m_doc->getPageMap()->setSource(pageMapSource);
    }

    writer.OnTagClose(U"", U"body");
    writer.OnStop();
    CRLog::debug("EPUB: %d documents merged", fragmentCount);

    if ( fontList.length() != fontList_nb_before_head_parsing ) {
        // New fonts met when parsing <head><style> of some DocFragments
        // Drop styles (before unregistering fonts, as they may reference them)
        m_doc->forceReinitStyles();
            // todo: we could avoid forceReinitStyles() when embedded fonts are disabled
            // (but being here is quite rare - and having embedded font disabled even more)
        m_doc->unregisterEmbeddedFonts();
        // set document font list, and register fonts
        m_doc->getEmbeddedFontList().set(fontList);
        m_doc->registerEmbeddedFonts();
        printf("CRE: document loaded, but styles re-init needed (cause: embedded fonts)\n");
    }

    // fragmentCount is not fool proof, best to check if we really made
    // some DocFragments children of <RootNode><body>
    if ( fragmentCount == 0 || m_doc->getRootNode()->getChildNode(0)->getChildCount() == 0 ) {
        if (decryptor->hasUnsupportedEncryptedItems()) {
            // No non-encrypted text: show the unsupported DRM page
            createEncryptedEpubWarningDocument(m_doc);
            return true;
        }
        return false;
    }

#if 0
    // set stylesheet
    //m_doc->getStyleSheet()->clear();
    m_doc->setStyleSheet( NULL, true );
    //m_doc->getStyleSheet()->parse(m_stylesheet.c_str());
    if ( !css.empty() && m_doc->getDocFlag(DOC_FLAG_ENABLE_INTERNAL_STYLES) ) {

        m_doc->setStyleSheet( "p.p { text-align: justify }\n"
            "svg { text-align: center }\n"
            "i { display: inline; font-style: italic }\n"
            "b { display: inline; font-weight: bold }\n"
            "abbr { display: inline }\n"
            "acronym { display: inline }\n"
            "address { display: inline }\n"
            "p.title-p { hyphenate: none }\n"
//abbr, acronym, address, blockquote, br, cite, code, dfn, div, em, h1, h2, h3, h4, h5, h6, kbd, p, pre, q, samp, span, strong, var
        , false);
        m_doc->setStyleSheet( UnicodeToUtf8(css).c_str(), false );
        //m_doc->getStyleSheet()->parse(UnicodeToUtf8(css).c_str());
    } else {
        //m_doc->getStyleSheet()->parse(m_stylesheet.c_str());
        //m_doc->setStyleSheet( m_stylesheet.c_str(), false );
    }
#endif
#if 0
    LVStreamRef out = LVOpenFileStream( U"c:\\doc.xml" , LVOM_WRITE );
    if ( !out.isNull() )
        m_doc->saveToStream( out, "utf-8" );
#endif

    // DONE!
    if ( progressCallback ) {
        progressCallback->OnLoadFileEnd( );
        m_doc->compact();
        m_doc->dumpStatistics();
    }

    // save compound XML document, for testing:
    //m_doc->saveToStream(LVOpenFileStream("/tmp/epub_dump.xml", LVOM_WRITE), NULL, true);

    return true;

}

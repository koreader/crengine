// Dump the computed style hash of every element of a document.
// Usage: dump_styles <file>
// Used to A/B compare style application (cascade order) between two builds:
// any difference in selector application shows up as a different style hash.
#include "crengine.h"
#include "lvdocview.h"
#include <stdio.h>

static void dumpNode(ldomNode * node, lString8 path) {
    if ( node->isElement() ) {
        lString8 line = path;
        line << " <" << UnicodeToUtf8(node->getNodeName()).c_str() << ">";
        lString32 cls = node->getAttributeValue("class");
        if ( !cls.empty() )
            line << " class=\"" << UnicodeToUtf8(cls).c_str() << "\"";
        css_style_ref_t st = node->getStyle();
        if ( !st.isNull() )
            line << " hash=" << fmt::hex(calcHash(*st));
        if ( st->pseudo_elem_before_style )
            line << " before=" << fmt::hex(calcHash(*st->pseudo_elem_before_style));
        if ( st->pseudo_elem_after_style )
            line << " after=" << fmt::hex(calcHash(*st->pseudo_elem_after_style));
        if ( st->pseudo_elem_first_letter_catcher_style )
            line << " fletter=" << fmt::hex(calcHash(*st->pseudo_elem_first_letter_catcher_style));
        if ( st->pseudo_elem_first_line_style )
            line << " fline=" << fmt::hex(calcHash(*st->pseudo_elem_first_line_style));
        printf("%s\n", line.c_str());
        int n = node->getChildCount();
        for ( int i=0; i<n; i++ ) {
            lString8 childPath = path;
            childPath << "/" << fmt::decimal(i);
            dumpNode( node->getChildNode(i), childPath );
        }
    }
}

int main(int argc, const char * argv[]) {
    if ( argc < 2 ) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return 2;
    }
    CRLog::setStdoutLogger();
    CRLog::setLogLevel(CRLog::LL_WARN);

    // With USE_FONTCONFIG=0 there's no automatic font enumeration: register
    // the DejaVu fonts explicitly.
    InitFontManager(lString8());
    static const char * fonts[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Italic.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSerif-BoldItalic.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        NULL
    };
    for ( int i=0; fonts[i]; i++ ) {
        if ( !fontMan->RegisterFont(lString8(fonts[i])) )
            fprintf(stderr, "Failed to register %s\n", fonts[i]);
    }
    if ( fontMan->GetFontCount() == 0 ) {
        fprintf(stderr, "No fonts found, cannot proceed\n");
        return 1;
    }

    LVDocView view;
    view.Resize(600, 800);
    if ( !view.LoadDocument(argv[1]) ) {
        fprintf(stderr, "Cannot open document %s\n", argv[1]);
        return 1;
    }
    view.goToPage(0);
    ldomDocument * doc = view.getDocument();
    dumpNode(doc->getRootNode(), lString8(""));
    return 0;
}

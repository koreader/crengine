#ifndef BOOKFORMATS_H
#define BOOKFORMATS_H

#include "lvtypes.h"
#include "lvstring.h"

/// source document formats
typedef enum {
    doc_format_none,
    doc_format_fb2,
    doc_format_fb3,
    doc_format_txt,
    doc_format_rtf,
    doc_format_epub,
    doc_format_html,
    doc_format_txt_bookmark, // coolreader TXT format bookmark
    doc_format_chm,
    doc_format_doc,
    doc_format_docx,
    doc_format_pdb,
    doc_format_odt,
    doc_format_svg,
    doc_format_max = doc_format_svg
    // don't forget update getDocFormatName() when changing this enum
} doc_format_t;

lString32 LVDocFormatName(int fmt);
int LVDocFormatFromExtension(lString32 &pathName);
lString8 LVDocFormatCssFileName(int fmt);


#endif // BOOKFORMATS_H

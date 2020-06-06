#ifndef EPUBFMT_H
#define EPUBFMT_H

#include "../include/crsetup.h"
#include "../include/lvtinydom.h"

// That's how many metadata nodes we parse before giving up
#define EPUB_META_MAX_ITER 50U

bool DetectEpubFormat( LVStreamRef stream );
bool ImportEpubDocument( LVStreamRef stream, ldomDocument * doc, LVDocViewCallback * progressCallback, CacheLoadingCallback * formatCallback, bool metadataOnly = false );
lString16 EpubGetRootFilePath( LVContainerRef m_arc );
LVStreamRef GetEpubCoverpage(LVContainerRef arc);


#endif // EPUBFMT_H

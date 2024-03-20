#ifndef EPUBFMT_H
#define EPUBFMT_H

#include "../include/crsetup.h"
#include "../include/lvtinydom.h"

bool DetectEpubFormat( LVStreamRef stream );
bool ImportEpubDocument( LVStreamRef stream, ldomDocument * doc, LVDocViewCallback * progressCallback,
    CacheLoadingCallback * formatCallback, bool metadataOnly = false,
    const elem_def_t * node_scheme=NULL, const attr_def_t * attr_scheme=NULL, const ns_def_t * ns_scheme=NULL);
lString32 EpubGetRootFilePath( LVContainerRef m_arc );
LVStreamRef GetEpubCoverpage(LVContainerRef arc);


#endif // EPUBFMT_H

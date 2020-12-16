#ifndef PDBFMT_H
#define PDBFMT_H

#include "../include/crsetup.h"
#include "../include/lvtinydom.h"

// Same as lvtinydom...
#include "zlib.h"
#define UNPACK_BUF_SIZE 0x40000

// creates PDB decoder stream for stream
//LVStreamRef LVOpenPDBStream( LVStreamRef srcstream, int &format );

bool DetectPDBFormat( LVStreamRef stream, doc_format_t & contentFormat );
bool ImportPDBDocument( LVStreamRef & stream, ldomDocument * doc, LVDocViewCallback * progressCallback, CacheLoadingCallback * formatCallback, doc_format_t & contentFormat );
LVStreamRef GetPDBCoverpage(LVStreamRef stream);


#endif // PDBFMT_H

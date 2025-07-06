/***************************************************************************
 *   crengine-ng                                                           *
 *   Copyright (C) 2022,2024 Aleksey Chernov <valexlin@gmail.com>          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU General Public License           *
 *   as published by the Free Software Foundation; either version 2        *
 *   of the License, or (at your option) any later version.                *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the Free Software           *
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,            *
 *   MA 02110-1301, USA.                                                   *
 ***************************************************************************/

#include "mdfmt.h"

#if (USE_MD4C == 1)

#include <lvtinydom.h>

#include <string.h>

#include <md4c-html.h>

#define TEXT_PARSER_CHUNK_SIZE 16384

bool DetectMarkdownFormat(LVStreamRef stream, const lString32& fileName) {
    // Check file extension
    lString32 nm = fileName;
    nm = nm.lowercase();
    if (!nm.endsWith(".md"))
        return false;
    // Check file size
    lvsize_t sz = stream->GetSize();
    if (sz < 5 || sz > MARKDOWN_MAX_FILE_SIZE)
        return false;
    // Checking for compliance with the text format
    LVTextParser textParser(stream, NULL, true);
    bool res = textParser.CheckFormat();
    stream->SetPos(0);
    return res;
}

typedef struct cre_md4c_parse_data_tag
{
    lString8* htmlData;
} cre_md4c_parse_data;

static void my_md4c_process_output(const MD_CHAR* chunk, MD_SIZE sz, void* userData) {
    cre_md4c_parse_data* data = (cre_md4c_parse_data*)userData;
    data->htmlData->append(chunk, sz);
}

bool ImportMarkdownDocument(LVStreamRef stream, const lString32& fileName, ldomDocument* doc, LVDocViewCallback* progressCallback, CacheLoadingCallback* formatCallback) {
    if (doc->openFromCache(formatCallback)) {
        if (progressCallback) {
            progressCallback->OnLoadFileEnd();
        }
        return true;
    }
    bool res = false;
    // Read stream
    lString8 rawData;
    lString8 htmlData;
    char buffer[TEXT_PARSER_CHUNK_SIZE];
    lvsize_t bytesRead = 0;
    stream->SetPos(0);
    while (stream->Read(buffer, TEXT_PARSER_CHUNK_SIZE, &bytesRead) == LVERR_OK) {
        rawData.append(buffer, bytesRead);
        if (bytesRead < TEXT_PARSER_CHUNK_SIZE)
            break;
    }
    // Parse and convert to html
    cre_md4c_parse_data parseData;
    parseData.htmlData = &htmlData;
    int parse_res = md_html(rawData.c_str(), rawData.length(), my_md4c_process_output, (void*)&parseData,
                            MD_FLAG_COLLAPSEWHITESPACE | MD_FLAG_TABLES | MD_FLAG_TASKLISTS |
                                    MD_FLAG_STRIKETHROUGH | MD_FLAG_PERMISSIVEURLAUTOLINKS |
                                    MD_FLAG_PERMISSIVEEMAILAUTOLINKS | MD_FLAG_PERMISSIVEWWWAUTOLINKS |
                                    MD_FLAG_LATEXMATHSPANS,
                            MD_HTML_FLAG_XHTML);
    rawData.clear();
    if (0 != parse_res) {
        // Parse failed
        CRLog::error("MD4C: Failed to parse Markdown document!");
        return res;
    }
    // Write document content to stream to parse them
    lvsize_t result_len = htmlData.length();
    lString32 title = LVExtractFilenameWithoutExtension(fileName);
    lString8 gen_preamble = cs8(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" \"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\">"
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>"
    ) + UnicodeToUtf8(title) + cs8("</title></head><body>");
    lString8 gen_tail = cs8("</body></html>");
    lvsize_t dw;
    LVStreamRef memStream = LVCreateMemoryStream();
    res = !memStream.isNull();
    if (res)
        res = LVERR_OK == memStream->Write(gen_preamble.c_str(), gen_preamble.length(), &dw);
    if (res)
        res = dw == (lvsize_t)gen_preamble.length();
    if (res) {
        res = LVERR_OK == memStream->Write(htmlData.data(), result_len, &dw);
    }
    htmlData.clear();
    if (res)
        res = dw == result_len;
    if (res)
        res = LVERR_OK == memStream->Write(gen_tail.c_str(), gen_tail.length(), &dw);
    if (res)
        res = dw == (lvsize_t)gen_tail.length();
    if (res) {
        // Parse stream to document
        ldomDocumentWriter writer(doc);
        LVHTMLParser parser(memStream, &writer);
        parser.setProgressCallback(progressCallback);
        res = parser.CheckFormat() && parser.Parse();
    }
    if (res) {
        doc->getProps()->setString(DOC_PROP_TITLE, title);
        doc->buildTocFromHeadings();
    }
    return res;
}

#endif

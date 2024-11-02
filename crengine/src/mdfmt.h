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

#ifndef __MDFMT_H_INCLUDED__
#define __MDFMT_H_INCLUDED__

#include <crsetup.h>

#if (USE_MD4C == 1)

#include <lvstream.h>
#include <lvstring.h>

class ldomDocument;
class LVDocViewCallback;
class CacheLoadingCallback;

#define MARKDOWN_MAX_FILE_SIZE 10 * 1024 * 1024 // 10M

bool DetectMarkdownFormat(LVStreamRef stream, const lString32& fileName);
bool ImportMarkdownDocument(LVStreamRef stream, const lString32& fileName, ldomDocument* doc, LVDocViewCallback* progressCallback, CacheLoadingCallback* formatCallback);

#endif

#endif // __MDFMT_H_INCLUDED__

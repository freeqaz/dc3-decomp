#pragma once
// Song browser data - the JSON GET /api/songs answers with
// (docs/native/reference/SONG_BROWSER_DATA.md, docs/tools/HTTP_DEBUG_SERVER.md).
// Standard C++ only, so the unit tests check the text without an engine.

#include "SongCatalog.h"
#include "SongSearch.h"
#include "SongSections.h"

#include <string>

namespace songbrowser {

// A JSON string body: quotes, backslashes and control characters escaped,
// UTF-8 passed through, bytes that are not UTF-8 replaced by U+FFFD.
std::string JsonEscape(const std::string &text);

// {"count":N,"songs":[{every SongEntry field}...],"history":[names],
//  "recents":[names],"labels":{token:text}}
// `history` is the play history as kept; `recents` is the part of it that is
// in the catalog (SongCatalog::Recents, at most `recentRows`; 0 = all).
std::string CatalogJson(const SongCatalog &catalog, int recentRows = 0);

// {"query":"..","searching":bool,"words":[..],"count":N,"rows":[{"shortName",
//  "title","artist","album","match":"title_prefix","field":"title",
//  "ranges":[{"field":"title","begin":0,"length":4,"text":"Y.M."}]}...]}
// begin/length are bytes of the field's UTF-8; "text" is that run.
std::string SearchJson(
    const SongCatalog &catalog, const SearchResult &result, const std::string &query
);

// {"arrangement":"artist","count":N,"sections":[{"token","title","firstRow",
//  "count","recents"}...],"rows":[names]}
std::string ArrangementJson(const SongCatalog &catalog, const SongList &list);

} // namespace songbrowser

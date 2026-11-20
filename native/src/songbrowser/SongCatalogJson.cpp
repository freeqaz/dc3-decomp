// Song browser data - JSON.  See SongCatalogJson.h.

#include "SongCatalogJson.h"

#include "SongText.h"

#include <cstdio>

namespace songbrowser {

std::string JsonEscape(const std::string &text) {
    std::string out;
    size_t at = 0;
    while (at < text.size()) {
        unsigned cp;
        int length = DecodeUtf8(text, at, cp);
        if (cp == '"') {
            out += "\\\"";
        } else if (cp == '\\') {
            out += "\\\\";
        } else if (cp == '\n') {
            out += "\\n";
        } else if (cp == '\t') {
            out += "\\t";
        } else if (cp < 0x20 || cp == 0x7F) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", cp);
            out += buf;
        } else if (cp == 0xFFFD && length == 1) {
            out += "\xEF\xBF\xBD"; // a byte that was not UTF-8
        } else {
            out.append(text, at, length);
        }
        at += length;
    }
    return out;
}

namespace {

void Key(std::string &json, const char *key) {
    json += '"';
    json += key;
    json += "\":";
}

void Str(std::string &json, const char *key, const std::string &value, bool comma = true) {
    Key(json, key);
    json += '"';
    json += JsonEscape(value);
    json += '"';
    if (comma) json += ',';
}

void Int(std::string &json, const char *key, int value, bool comma = true) {
    Key(json, key);
    json += std::to_string(value);
    if (comma) json += ',';
}

void Bool(std::string &json, const char *key, bool value, bool comma = true) {
    Key(json, key);
    json += value ? "true" : "false";
    if (comma) json += ',';
}

void Names(std::string &json, const std::vector<std::string> &names) {
    json += '[';
    for (size_t i = 0; i < names.size(); i++) {
        if (i) json += ',';
        json += '"';
        json += JsonEscape(names[i]);
        json += '"';
    }
    json += ']';
}

} // namespace

std::string CatalogJson(const SongCatalog &catalog, int recentRows) {
    std::string json = "{";
    Int(json, "count", catalog.Size());
    Key(json, "songs");
    json += '[';
    for (int i = 0; i < catalog.Size(); i++) {
        const SongEntry &song = catalog.At(i);
        if (i) json += ',';
        json += '{';
        Int(json, "id", song.id);
        Str(json, "shortName", song.shortName);
        Str(json, "title", song.title);
        Str(json, "artist", song.artist);
        Str(json, "album", song.album);
        Int(json, "year", song.year);
        Int(json, "bpm", song.bpm);
        Int(json, "lengthMs", song.lengthMs);
        char rank[32];
        snprintf(rank, sizeof(rank), "%g", (double)song.rank);
        Key(json, "rank");
        json += rank;
        json += ',';
        Int(json, "difficultyTier", song.difficultyTier);
        Str(json, "origin", song.origin);
        Str(json, "source", SourceToken(song.source));
        Bool(json, "onDisc", song.onDisc);
        Bool(json, "isCover", song.isCover);
        Str(json, "albumArtPath", song.albumArtPath);
        Int(json, "bestStars", song.bestStars);
        Int(json, "bestScore", song.bestScore);
        Bool(json, "played", song.played);
        Bool(json, "locked", song.locked);
        Int(json, "recentRank", song.recentRank, false);
        json += '}';
    }
    json += "],";
    Key(json, "history");
    Names(json, catalog.History());
    json += ',';
    Key(json, "recents");
    std::vector<int> recents = catalog.Recents(recentRows);
    std::vector<std::string> recentNames;
    for (size_t i = 0; i < recents.size(); i++) {
        recentNames.push_back(catalog.At(recents[i]).shortName);
    }
    Names(json, recentNames);
    json += ',';
    Key(json, "labels");
    json += '{';
    bool first = true;
    for (std::map<std::string, std::string>::const_iterator it = catalog.Labels().begin();
         it != catalog.Labels().end(); ++it) {
        if (!first) json += ',';
        first = false;
        json += '"';
        json += JsonEscape(it->first);
        json += "\":\"";
        json += JsonEscape(it->second);
        json += '"';
    }
    json += "}}";
    return json;
}

std::string SearchJson(
    const SongCatalog &catalog, const SearchResult &result, const std::string &query
) {
    std::string json = "{";
    Str(json, "query", query);
    Bool(json, "searching", result.searching);
    Key(json, "words");
    Names(json, result.words);
    json += ',';
    Int(json, "count", (int)result.rows.size());
    Key(json, "rows");
    json += '[';
    for (size_t i = 0; i < result.rows.size(); i++) {
        const SearchRow &row = result.rows[i];
        const SongEntry &song = catalog.At(row.song);
        if (i) json += ',';
        json += '{';
        Str(json, "shortName", song.shortName);
        Str(json, "title", song.title);
        Str(json, "artist", song.artist);
        Str(json, "album", song.album);
        Str(json, "match", MatchClassName(row.matchClass));
        Str(json, "field", MatchFieldName(row.field));
        Key(json, "ranges");
        json += '[';
        for (size_t r = 0; r < row.ranges.size(); r++) {
            const MatchRange &range = row.ranges[r];
            const std::string &text = range.field == kFieldTitle
                ? song.title
                : range.field == kFieldArtist ? song.artist : song.album;
            if (r) json += ',';
            json += '{';
            Str(json, "field", MatchFieldName(range.field));
            Int(json, "begin", range.begin);
            Int(json, "length", range.length);
            Str(json, "text", text.substr(range.begin, range.length), false);
            json += '}';
        }
        json += "]}";
    }
    json += "]}";
    return json;
}

std::string ArrangementJson(const SongCatalog &catalog, const SongList &list) {
    std::string json = "{";
    Str(json, "arrangement", ArrangementName(list.arrangement));
    Int(json, "count", (int)list.rows.size());
    Key(json, "sections");
    json += '[';
    for (size_t i = 0; i < list.sections.size(); i++) {
        const Section &section = list.sections[i];
        if (i) json += ',';
        json += '{';
        Str(json, "token", section.token);
        Str(json, "title", section.title);
        Int(json, "firstRow", section.firstRow);
        Int(json, "count", section.count);
        Bool(json, "recents", section.recents, false);
        json += '}';
    }
    json += "],";
    Key(json, "rows");
    std::vector<std::string> names;
    for (size_t i = 0; i < list.rows.size(); i++) {
        names.push_back(catalog.At(list.rows[i]).shortName);
    }
    Names(json, names);
    json += '}';
    return json;
}

} // namespace songbrowser

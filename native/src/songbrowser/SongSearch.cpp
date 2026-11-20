// Song browser data - incremental search.  See SongSearch.h.

#include "SongSearch.h"

#include <algorithm>
#include <climits>

namespace songbrowser {

const char *MatchFieldName(MatchField field) {
    switch (field) {
    case kFieldTitle:
        return "title";
    case kFieldArtist:
        return "artist";
    default:
        return "album";
    }
}

const char *MatchClassName(MatchClass matchClass) {
    switch (matchClass) {
    case kMatchTitlePrefix:
        return "title_prefix";
    case kMatchTitleWord:
        return "title_word";
    case kMatchArtistPrefix:
        return "artist_prefix";
    case kMatchArtistWord:
        return "artist_word";
    case kMatchAlbum:
        return "album";
    case kMatchSubstring:
        return "substring";
    default:
        return "fuzzy";
    }
}

std::vector<std::string> QueryWords(const std::string &query) {
    std::vector<std::string> words;
    std::string folded = FoldForSearch(query, kFoldTight).text;
    size_t at = 0;
    while (at < folded.size()) {
        size_t end = folded.find(' ', at);
        if (end == std::string::npos) end = folded.size();
        if (end > at) words.push_back(folded.substr(at, end - at));
        at = end + 1;
    }
    return words;
}

namespace {

// Where in a field a term was found, best first.
enum Place { kPlacePrefix = 0, kPlaceWord = 1, kPlaceInside = 2, kPlaceNone = 3 };

MatchClass ClassFor(MatchField field, Place place) {
    if (place == kPlaceInside) return kMatchSubstring;
    switch (field) {
    case kFieldTitle:
        return place == kPlacePrefix ? kMatchTitlePrefix : kMatchTitleWord;
    case kFieldArtist:
        return place == kPlacePrefix ? kMatchArtistPrefix : kMatchArtistWord;
    default:
        return kMatchAlbum;
    }
}

// The best place `term` occurs in `folded`, and where (a folded position).
Place BestPlace(const FoldedText &folded, const std::string &term, size_t &where) {
    Place best = kPlaceNone;
    size_t skipThe = LeadingThe(folded.text);
    size_t at = folded.text.find(term);
    while (at != std::string::npos) {
        Place place = kPlaceInside;
        if (at == 0 || (skipThe != 0 && at == skipThe)) {
            place = kPlacePrefix;
        } else if (folded.WordStartsAt(at)) {
            place = kPlaceWord;
        }
        if (place < best) {
            best = place;
            where = at;
            if (best == kPlacePrefix) break;
        }
        at = folded.text.find(term, at + 1);
    }
    return best;
}

// Equal, or one character wrong, missing, extra, or two neighbours swapped.
bool WithinOneEdit(const std::string &a, const std::string &b) {
    size_t la = a.size(), lb = b.size();
    if (la > lb + 1 || lb > la + 1) return false;
    size_t i = 0;
    while (i < la && i < lb && a[i] == b[i]) {
        i++;
    }
    if (i == la && i == lb) return true;
    if (la == lb) {
        if (a.compare(i + 1, std::string::npos, b, i + 1, std::string::npos) == 0) {
            return true;
        }
        return i + 1 < la && a[i] == b[i + 1] && a[i + 1] == b[i]
            && a.compare(i + 2, std::string::npos, b, i + 2, std::string::npos) == 0;
    }
    if (la + 1 == lb) return a.compare(i, std::string::npos, b, i + 1, std::string::npos) == 0;
    return a.compare(i + 1, std::string::npos, b, i, std::string::npos) == 0;
}

// "ft", or the start of "feat"/"featuring" from two letters on: the word a
// person types between two credited artists ("usher feat will i am").
bool IsFeaturingWord(const std::string &word) {
    static const std::string featuring = "featuring";
    if (word == "ft") return true;
    return word.size() >= 2 && word.size() <= featuring.size()
        && featuring.compare(0, word.size(), word) == 0;
}

bool HasWord(const FoldedText &folded, const char *word) {
    std::string padded = " " + folded.text + " ";
    return padded.find(std::string(" ") + word + " ") != std::string::npos;
}

bool RangeLess(const MatchRange &a, const MatchRange &b) {
    if (a.field != b.field) return a.field < b.field;
    if (a.begin != b.begin) return a.begin < b.begin;
    return a.length < b.length;
}

} // namespace

SongSearch::Hit SongSearch::FindExact(const Song &song, const std::string &term) {
    Hit best;
    if (term.empty()) return best;
    for (int field = 0; field < 3; field++) {
        for (int mode = 0; mode < 2; mode++) {
            const FoldedText &folded = song.text[field][mode];
            size_t where = 0;
            Place place = BestPlace(folded, term, where);
            if (place == kPlaceNone) continue;
            MatchClass matchClass = ClassFor((MatchField)field, place);
            if (best.found && matchClass >= best.matchClass) continue;
            best.found = true;
            best.matchClass = matchClass;
            best.field = (MatchField)field;
            best.begin = folded.begin[where];
            best.end = folded.end[where + term.size() - 1];
        }
    }
    return best;
}

SongSearch::Hit SongSearch::FindFuzzy(const Song &song, const std::string &word) {
    Hit hit;
    int length = (int)word.size();
    if (length < kMinFuzzyWord) return hit;
    // Letters left out: the word's characters, in order, from the start of a
    // word of the text, within a span of at most twice the word.
    int maxSpan = 2 * length;
    for (int field = 0; field < 3; field++) {
        for (int mode = 0; mode < 2; mode++) {
            const FoldedText &folded = song.text[field][mode];
            const std::string &text = folded.text;
            int size = (int)text.size();
            for (int start = 0; start < size; start++) {
                // The first character is taken to be right.
                if (!folded.WordStartsAt(start) || text[start] != word[0]) continue;
                int last = -1;
                if (length >= kMinEditWord) {
                    static const int kDelta[3] = { 0, 1, -1 };
                    for (int d = 0; d < 3 && last < 0; d++) {
                        int candidate = length + kDelta[d];
                        if (start + candidate > size) continue;
                        if (WithinOneEdit(word, text.substr(start, candidate))) {
                            last = start + candidate - 1;
                        }
                    }
                }
                if (last < 0) {
                    int matched = 1;
                    int at = start + 1;
                    int lastMatched = start;
                    while (matched < length && at < size && at - start < maxSpan) {
                        if (text[at] == word[matched]) {
                            matched++;
                            lastMatched = at;
                        }
                        at++;
                    }
                    if (matched == length) last = lastMatched;
                }
                if (last < 0) continue;
                hit.found = true;
                hit.matchClass = kMatchFuzzy;
                hit.field = (MatchField)field;
                hit.begin = folded.begin[start];
                hit.end = folded.end[last];
                return hit;
            }
        }
    }
    return hit;
}

SongSearch::Hit SongSearch::Find(const Song &song, const std::string &term) {
    Hit hit = FindExact(song, term);
    if (!hit.found) hit = FindFuzzy(song, term);
    return hit;
}

void SongSearch::Rebuild(const SongCatalog &catalog) {
    int count = catalog.Size();
    mSongs.assign(count, Song());
    std::vector<std::pair<std::string, int> > order;
    for (int i = 0; i < count; i++) {
        const SongEntry &entry = catalog.At(i);
        const std::string *fields[3] = { &entry.title, &entry.artist, &entry.album };
        for (int field = 0; field < 3; field++) {
            mSongs[i].text[field][kFoldSpaced] = FoldForSearch(*fields[field], kFoldSpaced);
            mSongs[i].text[field][kFoldTight] = FoldForSearch(*fields[field], kFoldTight);
        }
        mSongs[i].recentRank = entry.recentRank;
        mSongs[i].titleOrder = 0;
        // Title order, as SongSections' by-title list has it; the short name
        // makes it a total order.
        order.push_back(std::make_pair(
            SortKey(entry.title, false) + '\x01' + SortKey(entry.artist, true) + '\x01'
                + entry.shortName,
            i
        ));
    }
    std::sort(order.begin(), order.end());
    for (int i = 0; i < count; i++) {
        mSongs[order[i].second].titleOrder = i;
    }
}

SearchResult SongSearch::Search(const std::string &query, int maxRows) const {
    SearchResult result;
    result.words = QueryWords(query);
    result.searching = !result.words.empty();
    if (!result.searching) return result;

    const std::vector<std::string> &words = result.words;
    std::string phrase;
    for (size_t i = 0; i < words.size(); i++) {
        if (i) phrase += ' ';
        phrase += words[i];
    }

    for (size_t s = 0; s < mSongs.size(); s++) {
        const Song &song = mSongs[s];
        bool hasFeaturing = HasWord(song.text[kFieldArtist][kFoldSpaced], "ft")
            || HasWord(song.text[kFieldArtist][kFoldSpaced], "feat")
            || HasWord(song.text[kFieldArtist][kFoldSpaced], "featuring");
        // Every word, one at a time.
        std::vector<Hit> hits;
        bool allFound = true;
        for (size_t w = 0; w < words.size() && allFound; w++) {
            Hit hit = Find(song, words[w]);
            if (hit.found) {
                hits.push_back(hit);
            } else if (!(words.size() > 1 && hasFeaturing && IsFeaturingWord(words[w]))) {
                // (A "feat" between two artists is satisfied by the credit
                // itself, however the song spells it.)
                allFound = false;
            }
        }
        if (!allFound || hits.empty()) continue;

        SearchRow row;
        row.song = (int)s;
        Hit worst = hits[0];
        for (size_t i = 1; i < hits.size(); i++) {
            if (hits[i].matchClass > worst.matchClass) worst = hits[i];
        }
        // The whole query as one run, when that is at least as good.
        Hit whole;
        if (words.size() > 1) whole = FindExact(song, phrase);
        if (whole.found && whole.matchClass <= worst.matchClass) {
            hits.assign(1, whole);
            worst = whole;
        }
        row.matchClass = worst.matchClass;
        row.field = worst.field;
        for (size_t i = 0; i < hits.size(); i++) {
            MatchRange range = { hits[i].field, hits[i].begin, hits[i].end - hits[i].begin };
            row.ranges.push_back(range);
        }
        std::sort(row.ranges.begin(), row.ranges.end(), RangeLess);
        std::vector<MatchRange> merged;
        for (size_t i = 0; i < row.ranges.size(); i++) {
            const MatchRange &range = row.ranges[i];
            if (!merged.empty() && merged.back().field == range.field
                && range.begin <= merged.back().begin + merged.back().length) {
                int end = std::max(
                    merged.back().begin + merged.back().length, range.begin + range.length
                );
                merged.back().length = end - merged.back().begin;
            } else {
                merged.push_back(range);
            }
        }
        row.ranges.swap(merged);
        result.rows.push_back(row);
    }

    // How it matched, then played recently, then title order.  titleOrder is
    // different for every song, so the order does not depend on the sort.
    const std::vector<Song> &songs = mSongs;
    std::sort(
        result.rows.begin(),
        result.rows.end(),
        [&songs](const SearchRow &a, const SearchRow &b) {
            if (a.matchClass != b.matchClass) return a.matchClass < b.matchClass;
            int recentA = songs[a.song].recentRank < 0 ? INT_MAX : songs[a.song].recentRank;
            int recentB = songs[b.song].recentRank < 0 ? INT_MAX : songs[b.song].recentRank;
            if (recentA != recentB) return recentA < recentB;
            return songs[a.song].titleOrder < songs[b.song].titleOrder;
        }
    );
    if (maxRows > 0 && (int)result.rows.size() > maxRows) result.rows.resize(maxRows);
    return result;
}

} // namespace songbrowser

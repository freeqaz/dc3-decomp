// DC3 Native Port - a song id for a song whose songs.dta gives a NAME.
// See SongId_Native.h.

#include "platform/SongId_Native.h"

#include "obj/Data.h"
#include "utl/Symbol.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace NativeSongId {

unsigned int Fnv1a32(const char *name) {
    unsigned int hash = 0x811C9DC5u; // the FNV offset basis
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        hash ^= *p;
        hash *= 0x01000193u; // the FNV prime; unsigned, so it wraps at 32 bits
    }
    return hash;
}

int FromName(const char *name) {
    if (!name || !*name)
        return 0;
    unsigned int hash = Fnv1a32(name);
    unsigned int folded = (hash >> 30) ^ (hash & 0x3FFFFFFFu);
    return (int)((unsigned int)kCustomMin | folded);
}

bool IsCustom(int id) { return id >= kCustomMin; }

int FromNode(const DataNode &node, const DataArray *source) {
    DataType type = node.Type();
    if (type != kDataSymbol && type != kDataString)
        return node.Int(source);

    const char *name = node.Str(source);
    int id = FromName(name);

    // Called from the main thread only (SongMgr::ContentLoaded), and twice
    // per song (the manager, then the metadata), so the line is per name.
    static std::set<std::string> sAnnounced;
    if (sAnnounced.insert(name ? name : "").second) {
        printf(
            "DC3 SongId: song_id '%s' is a name, not a number: using id %d (%s)\n",
            name ? name : "",
            id,
            source ? source->File() : "?"
        );
    }
    return id;
}

bool Find(const DataArray *songData, Symbol key, int &songID) {
    DataArray *entry = songData->FindArray(key, false);
    if (!entry)
        return false;
    songID = FromNode(entry->Node(1), entry);
    return true;
}

int FindWithBackup(const DataArray *songData, const DataArray *backup, Symbol key) {
    int songID = 0;
    Find(songData, key, songID);
    if (songID == 0 && backup)
        Find(backup, key, songID);
    return songID;
}

bool Collides(int songID, const char *knownShortName, const char *newShortName) {
    if (!IsCustom(songID) || !knownShortName || !newShortName)
        return false;
    if (strcmp(knownShortName, newShortName) == 0)
        return false;
    printf(
        "DC3 SongId: COLLISION: song '%s' is NOT loaded: its song id %d already belongs to song '%s'. "
        "An id from %d up is the hash of a song_id name; give one of the two songs another song_id.\n",
        newShortName,
        songID,
        knownShortName,
        kCustomMin
    );
    return true;
}

} // namespace NativeSongId

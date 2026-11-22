#pragma once
// DC3 Native Port - a song id for a song whose songs.dta gives a NAME.
//
// An official song carries an integer: `(song_id 7001)`.  A community package
// carries a name: `(song_id thefox)`.  The image reads the value with
// DataNode::Int, which on a symbol logs a MILO_FAIL and returns the low 32
// bits of the symbol's address: a different number each time the file is
// read in a new process (measured: -1364816982 and -1825805398 in two
// launches, each with an empty song cache).  Scores are saved under the id,
// so they were lost whenever the song cache was rebuilt.
//
// Here a name becomes a number that depends on the name alone:
//
//     id = 0x40000000 + fold30(FNV-1a-32(name))      1073741824..2147483647
//
// The range is what no real song uses.  Ids in the 138 songs this checkout has
// (disc, the DC2 export, the DC2 DLC pack): 6001..6064, 7001..7075 and
// 10001002..10001040.  The game's sentinels are 0 (invalid), -1 (any) and -2
// (random), so an id must be positive.  Every holder of an id is a 32-bit int
// (SongStatus and its save stream, CustomPlaylist, SkillsAwardList, the song
// cache, the net jobs), so 30 bits of hash survive everywhere.
//
// An integer is passed through unchanged, whatever its value.

class DataArray;
class DataNode;
class Symbol;

namespace NativeSongId {

// The ids a name can become: bit 30 set, bit 31 clear.
const int kCustomMin = 0x40000000; // 1073741824
const int kCustomMax = 0x7FFFFFFF; // 2147483647

// FNV-1a, 32 bit, over the bytes of `name` up to its NUL.  Written out here so
// it is the same on Linux, macOS and wasm: no std::hash, no pointer.
unsigned int Fnv1a32(const char *name);

// The id of a song named `name`: the hash folded to 30 bits (the top two bits
// xor'd into the bottom two), plus kCustomMin.  0 (kSongID_Invalid) for a null
// or empty name.  Case counts: `thefox` and `TheFox` are two songs.
int FromName(const char *name);

// Whether `id` is in the range FromName answers in.
bool IsCustom(int id);

// The id of a `song_id` value node.  An integer node: its value.  A symbol or
// string node: FromName of it, and one line of log the first time that name
// is seen.  Anything else (a float, a command) goes to DataNode::Int, which
// is what the image does with it.  `source` is the array the node is in, for
// the file name in the log.
int FromNode(const DataNode &node, const DataArray *source);

// DataArray::FindData(key, int &, false) for a song's id: looks `key` up in
// `songData` and reads its value with FromNode.  False, and `songID` left
// alone, when the song has no such entry.
bool Find(const DataArray *songData, Symbol key, int &songID);

// Whether the song `newShortName` must be refused because its id is taken:
// `songID` is a custom id and already belongs to `knownShortName`, a different
// song.  Logs one line naming both when it answers true.  `knownShortName` is
// null when nothing has the id yet.
//
// Either two names hashed to one id, or a name hashed to an id that another
// package wrote as an integer.  The song that came second is not loaded: two
// songs on one id would share one set of saved scores and one metadata slot,
// and moving the second to a free id would make its id depend on which
// packages are installed, which is the instability this file removes.
bool Collides(int songID, const char *knownShortName, const char *newShortName);

} // namespace NativeSongId

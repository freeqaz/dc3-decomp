#pragma once

#include "utl/Symbol.h"

class Playlist;

namespace NativePgSongs {

// A curated free-play song list, off by default (the full catalog).
// DC3_PG_SONGS=1 turns it on; the Mac launcher (scripts/mac/choose-song-mode.sh)
// asks which one on every launch.
bool Enabled();
bool AllowsSong(Symbol shortName);
bool AllowsPlaylist(const Playlist &playlist);

} // namespace NativePgSongs

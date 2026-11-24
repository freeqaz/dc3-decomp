#pragma once

#include "utl/Symbol.h"

class Playlist;

namespace NativePgSongs {

// The default song selection policy. DC3_PG_SONGS=0 restores the full catalog.
bool Enabled();
bool AllowsSong(Symbol shortName);
bool AllowsPlaylist(const Playlist &playlist);

} // namespace NativePgSongs

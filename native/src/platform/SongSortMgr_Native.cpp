// SongSortMgr/MQSongSortMgr global definitions for native port.
// The real classes are compiled in the native build; these are just the
// properly-typed nullptr definitions. MetaPanel::Init() constructs them via new.

#ifdef HX_NATIVE

#include "meta_ham/SongSortMgr.h"
#include "meta_ham/MQSongSortMgr.h"

SongSortMgr *TheSongSortMgr = nullptr;
MQSongSortMgr *TheMQSongSortMgr = nullptr;

#endif // HX_NATIVE

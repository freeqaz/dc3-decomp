// Order matters and is observable in the image: __FILE__ records the path
// by which a header was FIRST reached, and MSVC bakes that into the
// MEM_OVERLOAD / MILO_ASSERT strings. This TU's target strings spell
// `...\src\movie\MovieImpl_p.h` (backslash, i.e. reached bare from this
// directory); BinkMovieSys.cpp's spell `...\src\movie/MovieImpl_p.h`
// (forward slash, i.e. reached through the -I path as `movie/...`).
#include "MovieImpl_p.h"
#include "movie\MovieSys.h"
#include "MovieSys.h"
#include "TexMovie.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "utl\MemMgr.h"

#ifdef HX_NATIVE
#include "moviebink\BinkMovieSys.h"
extern BinkMovieSys gBinkMovieSys;
MovieSys &TheMovieSys = gBinkMovieSys;
#endif

MovieSys::MovieSys() : isInitalized(false) {}

MovieSys::~MovieSys() {}

void MovieSys::Init() {
    if (isInitalized == false) {
        isInitalized = true;
        TexMovie::Init();
        TheDebug.AddExitCallback(Movie::Terminate);
    }
}

void MovieSys::Terminate() {
    if (isInitalized == false) {
        return;
    }
    isInitalized = false;
}

MovieImpl *MovieSys::CreateMovieImpl() { return new MovieImpl(); }

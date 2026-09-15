#include "rnddx9\TexMgr.h"
#include "Rnd.h"
#include "rnddx9\Rnd.h"
#include "rndobj\TexMgr.h"
#include "utl\MemTrack.h"
#include "xdk\D3D9.h"
#include "xdk\d3d9i\d3d9.h"

DxRndTexMgr TheDxTexMgr;
// w8-l: the std::map<Hmx::CRC, RefRes<void> > insert_unique instantiated from
// this TU is 99.967220 normalized and is the only function short of 100% in
// this unit (15/16).  Four charged rows, [13]/[14] and [65]/[66], and each
// pair is ONE exchange: the two lwz's swap BOTH their destination register and
// their base register together (r11<->r27 with r30<->r6, and r26<->r27 with
// r30<->r29), the offsets following at -/+0x10.  objdiff's offset resolver
// says so explicitly -- "4 excluded as non-field (4 different base register on
// each side)" -- so this is a register permutation inside stlport's tree
// insert, not a wrong struct field.  Nothing in this file selects it.
TexMgr &TheTexMgr = TheDxTexMgr;

void DxRndTexMgr::OnReleaseResource(void *v) {
    D3DResource *resource = static_cast<D3DResource *>(v);
    TheDxRnd.AutoRelease(resource);
}

bool DxRndTexMgr::
    CreateSurface(const char *filename, Hmx::CRC key, UINT w, UINT h, UINT levels, DWORD, D3DFORMAT fmt, DWORD pool, D3DTexture **pTex, void **) {
    void *data = Get(key);
    if (data) {
        *pTex = (D3DTexture *)data;
        return true;
    } else {
        BeginMemTrackFileName(filename);
        *pTex = (D3DTexture *)D3DDevice_CreateTexture(
            w, h, 1, levels, 0, fmt, pool, D3DRTYPE_TEXTURE
        );
        DX_ASSERT(*pTex, 0x23);
        EndMemTrackFileName();
        ReserveRes(key, *pTex);
        return false;
    }
}

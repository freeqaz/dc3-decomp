#include "rndobj/ShaderProgram.h"
#include "Memory.h"
#include "ShaderMgr.h"
#include "os\Debug.h"
#include "os\File.h"
#include "os\OSFuncs.h"
#include "os\System.h"
#include "os\Timer.h"
#include "rndobj\Env.h"
#include "rndobj\Mat_NG.h"
#include "rndobj\ShaderOptions.h"
#include "utl/BinStream.h"
#include "utl\DataPointMgr.h"
#include "utl\FileStream.h"
#include "utl/Loader.h"
#include "utl\MemMgr.h"
#include "math\Utl.h"
#include "obj\Data.h"

void RndShaderProgram::SaveShaderBuffer(const char *file, RndShaderBuffer &buffer) {
    FileMkDir(FileGetPath(file));
    File *f = NewFile(file, 0x301);
    f->Write(buffer.Storage(), buffer.Size());
    delete f;
}

void RndShaderProgram::LoadShaderBuffer(
    BinStream &bs, int size, RndShaderBuffer *&buffer
) {
    MemDoTempAllocations tmp;
    buffer = NewBuffer(size);
    bs.Read(buffer->Storage(), size);
}

void RndShaderProgram::LoadShaderBuffer(const char *cc, RndShaderBuffer *&buffer) {
    FileStream stream(cc, FileStream::kReadNoArk, true);
    LoadShaderBuffer(stream, stream.Size(), buffer);
}

unsigned long gModTime;

void ShaderRecurseCB(const char *dir, const char *file) {
    FileStat stat;
    MILO_ASSERT(FileGetStat(MakeString("%s/%s", dir, file), &stat) == 0, 0x1B);
    if (stat.st_mtime > gModTime) {
        gModTime = stat.st_mtime;
    }
}

unsigned long RndShaderProgram::InitModTime() {
    gModTime = 0;
    if (TheShaderMgr.CacheShaders()) {
        FileRecursePattern(
            MakeString("%s/shaders/*.fx", FileSystemRoot()), ShaderRecurseCB, false
        );
    }
    return gModTime;
}

void RndShaderProgram::CopyErrorShader(ShaderType shader, const ShaderOptions &opts) {
    if (!MainThread()) {
        MILO_NOTIFY(
            "missing shader %s_%llx cannot be cached (not used in main thread).",
            ShaderTypeName(shader),
            opts.flags
        );
    }
    MILO_ASSERT(shader != kErrorShader && shader != kPostprocessErrorShader, 0x12F);

    // Determine the appropriate error shader type
    ShaderType errorType;
    if (shader == kPostprocessShader) {
        errorType = kPostprocessErrorShader;
    } else {
        errorType = kErrorShader;
    }

    // Build options mask for error shader, preserving specific flags
    u64 mask = 0;
    if (errorType == kErrorShader && (opts.flags & 0x1000)) {
        mask = 0x1000;
    }
    u64 display = TheShaderMgr.GetShaderErrorDisplay();
    mask = ((display & 1) << 0x23) | (mask & 0xfffffff7ffffffff);

    ShaderOptions newOpts(mask);
    RndShaderProgram &program = TheShaderMgr.FindShader(errorType, newOpts);
    if (!program.Cached()) {
        if (!TheShaderMgr.CacheShaders()) {
            const char *msg =
                "FAILURE: Error shader cannot be cached. Unable to handle missing shaders!\n";
            {
                FormatString fs(msg);
                TheDebug << fs.Str();
            }
            {
                FormatString fs(msg);
                TheDebug.Fail(fs.Str(), nullptr);
            }
        }
        Cache(errorType, newOpts, nullptr, nullptr);
    }
    Copy(program);
}

bool RndShaderProgram::Cache(
    ShaderType shaderType,
    const ShaderOptions &opts,
    RndShaderBuffer *vsBuffer,
    RndShaderBuffer *psBuffer
) {
    if (mCached)
        return true;
    mCached = true;
    Platform platform = TheLoadMgr.GetPlatform();
    if (platform != kPlatformNone && platform != kPlatformWii && GetGfxMode() != kOldGfx) {
        PhysMemTypeTracker tracker("D3D(phys):Shader");
        if (vsBuffer != nullptr && vsBuffer->Size() != 0 && psBuffer != nullptr &&
            psBuffer->Size() != 0) {
            CreateVertexShader(*vsBuffer);
            CreatePixelShader(*psBuffer, shaderType);
            // w7-al: an EXPLICIT return, not a fall-through into the shared
            // `return true` at the bottom. The image tail-merges this exit with
            // the two `return false` exits into one destructor block
            // (0x82732054: addi r3,r31,0x70 / bl ~PhysMemTypeTracker /
            // mr r3, r30), while the function's own fall-off gets a SECOND,
            // unmerged copy at 0x82732574 ending in `li r3, 1`. A fall-through
            // here would give this path the second block, not the first.
            return true;
        } else {
            if (!TheShaderMgr.CacheShaders()) {
                CopyErrorShader(shaderType, opts);
                String optsStr;
                ShaderMakeOptionsString(shaderType, opts, optsStr);
                // w7-al: `matPath` used to be hoisted into a named local here.
                // MSVC evaluates these arguments RIGHT-TO-LEFT, and the image
                // does exactly that: optsStr.c_str() (0x82732?A4), then the
                // RndEnviron ternary + PathName (0x827320D0), then
                // PathName(NgMat::Current()) (0x827320E0), then
                // ShaderTypeName (0x827320EC).  A local forced NgMat's PathName
                // to run FIRST.  Inlined back into the argument list.
                // BEHAVIOURAL GAP, not closable from this file (w7-ab).  The
                // image builds this message with TWO MakeString calls back to
                // back -- `bl ??$MakeString@PBD_KPBDPBDPBD@@...` at 0x82732110
                // immediately followed by `bl ?MakeString@@YAPBDPBD@Z` at
                // 0x82732114, r3 flowing straight through -- i.e. the notify
                // argument is itself a MakeString and MILO_NOTIFY wraps it a
                // second time.  Spelling that faithfully as
                // `MILO_NOTIFY(MakeString(fmt, ...))` REGRESSES this function
                // 86.9 -> 85.9 because utl/MakeString.h declares the
                // single-argument overload `inline`, so MSVC expands
                // FormatString's 4 KB buffer into our frame (frame delta
                // +0x1010) where the image keeps it out of line as a COMDAT in
                // App.obj (`.fn "?MakeString@@YAPBDPBD@Z"` in asm/App.s, 30
                // call sites binary-wide).  Closing it needs MakeString.h to
                // stop inlining that overload -- a shared-header change with a
                // binary-wide blast radius, not a change this call site can
                // make.  Left unfaithful deliberately; the missing call is a
                // logged-message-only path, so no gameplay behaviour differs.
                MILO_NOTIFY(
                    "Missing shader %s_%llx\n(material: %s)\n(environment: %s)\n(compile options: %s)",
                    ShaderTypeName(shaderType),
                    opts.flags,
                    PathName(NgMat::Current()),
                    // BEHAVIOURAL FIX (w7-al): this was
                    // `Current() ? PathName(Current()) : nullptr`, which skips
                    // the call entirely when there is no environ. The image
                    // calls PathName UNCONDITIONALLY -- the null test at
                    // 0x827320B0 selects between `li r3, 0` and the vbtable
                    // adjustment and then falls into the single
                    // `bl PathName` at 0x827320D0, i.e. the test IS the
                    // virtual-base conversion of RndEnviron* to Hmx::Object*,
                    // not a user ternary. PathName(nullptr) does not return
                    // nullptr, so the notify text differed.
                    PathName(RndEnviron::Current()),
                    optsStr.c_str()
                );
                if (UsingCD()) {
                    // w7-al: the virtual-base conversion of RndEnviron::Current()
                    // to Hmx::Object* (the null test + vbtable load + addi 4) is
                    // the FIRST thing the image does in this block -- 0x82732134,
                    // immediately after the UsingCD branch and before
                    // ShaderTypeName -- and it keeps the converted pointer in a
                    // callee-saved register across SystemConfig/Node/Str. Writing
                    // the conversion at the point of `envPath` sank it below
                    // Str(), which is nine rows out of place.
                    Hmx::Object *envObj = RndEnviron::Current();
                    const char *shaderTypeName = ShaderTypeName(shaderType);
                    DataArray *cfg = SystemConfig("rnd", "title");
                    // BEHAVIOURAL FIX (w7-al): the image passes the array itself
                    // as DataNode::Str's parent, not null -- `mr r4, r26` at
                    // 0x827321A0, where r26 is SystemConfig's return value saved
                    // by `mr r26, r3` at 0x82732198. We passed nullptr, which
                    // changes how a variable/property node resolves.
                    char *dataRoot = (char *)cfg->Node(1).Str(cfg);
                    const char *envPath = PathName(envObj);
                    const char *matPath2 = PathName(NgMat::Current());
                    const char *shaderHex = MakeString("%s_%llx", shaderTypeName, opts.flags);
                    const char *flagsHex = MakeString("%llx", opts.flags);
                    shaderTypeName = ShaderTypeName(shaderType);
                    const char *reportPath =
                        MakeString("debug/%s/rnd/missing_shaders", dataRoot);
                    SendDebugDataPoint(
                        reportPath,
                        "type", shaderTypeName,
                        "flags", flagsHex,
                        "shader", shaderHex,
                        "mat", matPath2,
                        "environ", envPath
                    );
                }
                return false;
            }
            AutoSlowFrame slowFrame("RndShaderProgram::Cache", 5.0f);
            // w7-al: one 64-bit local, not four re-reads of opts.flags. The
            // image loads it ONCE into a callee-saved register and homes it in
            // a stack temp immediately after the AutoSlowFrame ctor
            // (`ld r30, 0x0(r27)` / `std r30, 0x90(r31)` at 0x82732280), passes
            // the REGISTER to both ShaderCachedPath calls (`mr r4, r30`) and
            // the SLOT's address to both MILO_LOG MakeStrings
            // (`addi r5, r31, 0x90`). It is s64, not u64: MakeString is
            // instantiated as `AB_J` (const __int64 &), and the s64 -> u64
            // conversion into ShaderCachedPath's `_K` parameter is free, so one
            // signed local serves both without a second temp.
            // RESIDUAL (w7-al, 98.72 canonical): slots 0x88 and 0x90 are
            // swapped against the image in BOTH scopes that share them -- the
            // image homes shaderFlags at 0x90 and the MILO_NOTIFY's
            // optsStr.c_str() temp at 0x88, we do the reverse -- 7 rows. Moving
            // this declaration below the three char buffers is byte-for-byte
            // inert (measured: identical 29-row mismatch list), so the pair is
            // not being ordered by declaration.
            s64 shaderFlags = opts.flags;
            // Buffer sizes and declaration order are read off the image's frame:
            // it is 0x410 with the three buffers at 0x2d0 (source), 0x1d0 and
            // 0xd0, and __savegprlr_26's save area starting at 0x3f4 -- so
            // sourcePath is 0x100, not 0x140.  MSVC lays these out in REVERSE
            // declaration order, hence cachedPsPath is declared before
            // cachedVsPath to put the VS buffer at the lower address (0xd0).
            // RESIDUAL: the image puts cachedVsPath at 0xd0 and cachedPsPath at
            // 0x1d0; we get them the other way round (OFFSET_SWAP (0xd0,0x1d0)
            // x4).  Swapping the two DECLARATIONS is byte-for-byte inert -- at
            // equal sizes MSVC is not ordering these by declaration.
            char sourcePath[256];
            char cachedVsPath[256];
            char cachedPsPath[256];
            strcpy(sourcePath, ShaderSourcePath(ShaderTypeName(shaderType)));
            strcpy(cachedVsPath, ShaderCachedPath(sourcePath, shaderFlags, false));
            strcpy(cachedPsPath, ShaderCachedPath(sourcePath, shaderFlags, true));
            FileStat stat;
            unsigned int vsModTime = 0;
            if (FileGetStat(cachedVsPath, &stat) == 0) {
                vsModTime = stat.st_mtime;
            }
            unsigned int psModTime = vsModTime;
            if (FileGetStat(cachedPsPath, &stat) == 0) {
                if (stat.st_mtime < vsModTime) {
                    psModTime = stat.st_mtime;
                }
            } else {
                psModTime = 0;
            }
            if (gModTime > psModTime) {
                // w7-al: a REFERENCE-typed function-local static, not a pointer
                // with a hand-rolled null check. The image tests MSVC's own
                // one-bit init guard (`lwz lbl_830E1CDC; clrlwi. r9, r11, 31;
                // bne; ori r11, r11, 1; stw` at 0x82732?--) and, on the
                // freshly-initialised path, skips the reload of the slot because
                // DataVariable's return is already in r3. A pointer + `if (!p)`
                // compiles to a null test and no guard word.
                static DataNode &sCompileVerbose = DataVariable("shader_compile_print_opts");
                if (sCompileVerbose.Int(nullptr) != 0) {
                    String optsStr;
                    ShaderMakeOptionsString(shaderType, opts, optsStr);
                    MILO_LOG(
                        "Compiling shader: %s_%llx (%s) (compile options: %s)\n",
                        ShaderTypeName(shaderType),
                        shaderFlags,
                        PlatformSymbol(platform),
                        optsStr.c_str()
                    );
                } else {
                    MILO_LOG(
                        "Compiling shader: %s_%llx (%s)\n",
                        ShaderTypeName(shaderType),
                        shaderFlags,
                        PlatformSymbol(platform)
                    );
                }
                if (!MainThread() || !Compile(shaderType, opts, vsBuffer, psBuffer)) {
                    CopyErrorShader(shaderType, opts);
                    return false;
                }
                SaveShaderBuffer(cachedVsPath, *vsBuffer);
                SaveShaderBuffer(cachedPsPath, *psBuffer);
            } else {
                LoadShaderBuffer(cachedVsPath, vsBuffer);
                LoadShaderBuffer(cachedPsPath, psBuffer);
            }
            CreateVertexShader(*vsBuffer);
            CreatePixelShader(*psBuffer, shaderType);
            // BEHAVIOURAL FIX (w7-al): `delete`, not an explicit destructor
            // call. Both sites dispatch through vtable slot 0 -- the scalar
            // DELETING destructor ??_E -- and the image passes 1 in r4
            // (0x8273253C and 0x8273255C), the flag that makes it call
            // operator delete. We were passing 0, so every compiled shader
            // ran the destructor and leaked the RndShaderBuffer allocation.
            delete vsBuffer;
            delete psBuffer;
        }
    }
    return true;
}

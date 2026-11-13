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

    // Lane history (5 lines moved): docs/decomp/notes/system/rndobj/ShaderProgram.md
    // > Build options mask for error shader, preserving specific flags
    ShaderOptions newOpts(0);
    if (errorType == kErrorShader && (opts.flags & 0x1000)) {
        newOpts.flags = 0x1000;
    }
    newOpts.mDisplayError = TheShaderMgr.GetShaderErrorDisplay();
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
        // Lane history (5 lines moved): docs/decomp/notes/system/rndobj/ShaderProgram.md
        // > BUG FIX (w18-a): the error PROGRAM is the one that gets cached -- `mr r3,
        // > r30` (r30 = FindShader's result) before `bl Cache` at 0x827327D8.
        program.Cache(errorType, newOpts, nullptr, nullptr);
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
            // Lane history (7 lines moved): docs/decomp/notes/system/rndobj/ShaderProgram.md
            return true;
        } else {
            if (!TheShaderMgr.CacheShaders()) {
                CopyErrorShader(shaderType, opts);
                String optsStr;
                ShaderMakeOptionsString(shaderType, opts, optsStr);
                // Lane history (39 lines moved): docs/decomp/notes/system/rndobj/ShaderProgram.md
                MILO_NOTIFY(
                    "Missing shader %s_%llx\n(material: %s)\n(environment: %s)\n(compile options: %s)",
                    ShaderTypeName(shaderType),
                    opts.flags,
                    PathName(NgMat::Current()),
                    // Lane history (10 lines moved): docs/decomp/notes/system/rndobj/ShaderProgram.md
                    PathName(RndEnviron::Current()),
                    optsStr.c_str()
                );
                if (UsingCD()) {
                    // Lane history (8 lines moved): docs/decomp/notes/system/rndobj/ShaderProgram.md
                    Hmx::Object *envObj = RndEnviron::Current();
                    const char *shaderTypeName = ShaderTypeName(shaderType);
                    DataArray *cfg = SystemConfig("rnd", "title");
                    // Lane history (8 lines moved): docs/decomp/notes/system/rndobj/ShaderProgram.md
                    const char *dataRoot = cfg->Node(1).Str(cfg);
                    // w18-a: the image reads NgMat::sCurrent BEFORE the environ's
                    // PathName call (`lwz r30, sCurrent` ahead of the first
                    // `bl PathName`), so the material is fetched first.
                    NgMat *curMat = NgMat::Current();
                    const char *envPath = PathName(envObj);
                    const char *matPath2 = PathName(curMat);
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
            // Lane history (27 lines moved): docs/decomp/notes/system/rndobj/ShaderProgram.md
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
                // Lane history (7 lines moved): docs/decomp/notes/system/rndobj/ShaderProgram.md
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
            // Lane history (6 lines moved): docs/decomp/notes/system/rndobj/ShaderProgram.md
            // > BEHAVIOURAL FIX (w7-al): `delete`, not an explicit destructor call.
            delete vsBuffer;
            delete psBuffer;
        }
    }
    return true;
}

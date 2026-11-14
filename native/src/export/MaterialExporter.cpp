// MaterialExporter — Iterates BaseMaterial objects in an ObjectDir and writes JSON.

#include "export/MaterialExporter.h"

#include "obj/Dir.h"
#include "rndobj/BaseMaterial.h"
#include "rndobj/MatAnim.h"
#include "rndobj/PropAnim.h"
#include "rndobj/PropKeys.h"

#include <cstdio>
#include <string>
#include <sys/stat.h>

namespace MaterialExporter {

namespace {
struct PropAnimPeek : RndPropAnim {
    static const PropAnimPeek &Of(const RndPropAnim *p) {
        return *static_cast<const PropAnimPeek *>(p);
    }
    using RndPropAnim::mPropKeys;
};
} // namespace

static const char* BlendName(BaseMaterial::Blend b) {
    switch (b) {
    case BaseMaterial::kBlendDest:       return "dest";
    case BaseMaterial::kBlendSrc:        return "src";
    case BaseMaterial::kBlendAdd:        return "add";
    case BaseMaterial::kBlendSrcAlpha:   return "srcAlpha";
    case BaseMaterial::kBlendSrcAlphaAdd:return "srcAlphaAdd";
    case BaseMaterial::kBlendSubtract:   return "subtract";
    case BaseMaterial::kBlendMultiply:   return "multiply";
    case BaseMaterial::kPreMultAlpha:    return "preMultAlpha";
    case BaseMaterial::kScreen:          return "screen";
    case BaseMaterial::kLighten:         return "lighten";
    case BaseMaterial::kDarken:          return "darken";
    default:                             return "unknown";
    }
}

static const char* CullName(Cull c) {
    switch (c) {
    case kCullNone:      return "none";
    case kCullRegular:   return "front";
    case kCullBackwards: return "back";
    default:             return "unknown";
    }
}

static const char* ZModeName(ZMode z) {
    switch (z) {
    case kZModeDisable:      return "disable";
    case kZModeNormal:       return "normal";
    case kZModeTransparent:  return "transparent";
    case kZModeForce:        return "force";
    case kZModeDecal:        return "decal";
    default:                 return "unknown";
    }
}

static const char* TexWrapName(TexWrap w) {
    switch (w) {
    case kTexWrapClamp:   return "clamp";
    case kTexWrapRepeat:  return "repeat";
    case kTexBorderBlack: return "borderBlack";
    case kTexBorderWhite: return "borderWhite";
    case kTexWrapMirror:  return "mirror";
    default:              return "unknown";
    }
}

static const char* ShaderVarName(ShaderVariation v) {
    switch (v) {
    case kShaderVariationNone:            return "none";
    case kShaderVariationSkin:            return "skin";
    case kShaderVariationHair:            return "hair";
    case kShaderVariationWorldProjection: return "worldProjection";
    default:                              return "unknown";
    }
}

static void WriteColor(FILE* f, const char* name, const Hmx::Color& c, bool last = false) {
    fprintf(f, "    \"%s\": [%.4f, %.4f, %.4f, %.4f]%s\n",
            name, c.red, c.green, c.blue, c.alpha, last ? "" : ",");
}

static void WriteTexRef(FILE* f, const char* name, RndTex* tex, bool last = false) {
    if (tex && tex->Name() && tex->Name()[0]) {
        fprintf(f, "    \"%s\": \"%s\"%s\n", name, tex->Name(), last ? "" : ",");
    } else {
        fprintf(f, "    \"%s\": null%s\n", name, last ? "" : ",");
    }
}

int ExportAll(ObjectDir* dir, const char* outputDir, const Options& opts) {
    if (!dir || !outputDir) return 0;

    mkdir(outputDir, 0755);

    int count = 0;
    ObjDirItr<BaseMaterial> it(dir, true);
    while (it) {
        BaseMaterial* mat = it;
        const char* name = mat->Name();
        if (!name || !name[0]) {
            ++it;
            continue;
        }

        std::string path = std::string(outputDir) + "/" + name + ".json";
        FILE* f = fopen(path.c_str(), "w");
        if (!f) {
            fprintf(stderr, "  error writing %s\n", path.c_str());
            ++it;
            continue;
        }

        fprintf(f, "{\n");
        fprintf(f, "    \"name\": \"%s\",\n", name);

        // Base color
        WriteColor(f, "color", mat->GetColor());

        // Textures
        WriteTexRef(f, "diffuseTex", mat->GetDiffuseTex());
        WriteTexRef(f, "normalMap", mat->NormalMap());
        WriteTexRef(f, "emissiveMap", mat->GetEmissiveMap());
        WriteTexRef(f, "specularMap", mat->GetSpecularMap());
        WriteTexRef(f, "rimMap", mat->GetRimMap());
        WriteTexRef(f, "normDetailMap", mat->GetNormDetailMap());

        // Environment map
        RndCubeTex* envMap = mat->GetEnvironMap();
        if (envMap && envMap->Name() && envMap->Name()[0]) {
            fprintf(f, "    \"environMap\": \"%s\",\n", envMap->Name());
        } else {
            fprintf(f, "    \"environMap\": null,\n");
        }

        // Blend and render state
        fprintf(f, "    \"blend\": \"%s\",\n", BlendName(mat->GetBlend()));
        fprintf(f, "    \"zMode\": \"%s\",\n", ZModeName(mat->GetZMode()));
        fprintf(f, "    \"cull\": \"%s\",\n", CullName(mat->GetCull()));
        fprintf(f, "    \"texWrap\": \"%s\",\n", TexWrapName(mat->GetTexWrap()));
        fprintf(f, "    \"shaderVariation\": \"%s\",\n", ShaderVarName(mat->GetShaderVariation()));

        // Alpha
        fprintf(f, "    \"alphaCut\": %s,\n", mat->GetAlphaCut() ? "true" : "false");
        fprintf(f, "    \"alphaWrite\": %s,\n", mat->GetAlphaWrite() ? "true" : "false");
        fprintf(f, "    \"alphaThreshold\": %d,\n", mat->GetAlphaThreshold());

        // Lighting
        fprintf(f, "    \"useEnviron\": %s,\n", mat->UseEnviron() ? "true" : "false");
        fprintf(f, "    \"prelit\": %s,\n", mat->Prelit() ? "true" : "false");
        fprintf(f, "    \"pointLights\": %s,\n", mat->PointLights() ? "true" : "false");
        fprintf(f, "    \"fog\": %s,\n", mat->GetFog() ? "true" : "false");
        fprintf(f, "    \"intensify\": %s,\n", mat->GetIntensify() ? "true" : "false");

        // Specular
        WriteColor(f, "specularRGB", mat->GetSpecularRGB());
        WriteColor(f, "specular2RGB", mat->GetSpecular2RGB());
        fprintf(f, "    \"anisotropy\": %.4f,\n", mat->GetAnisotropy());

        // Rim
        WriteColor(f, "rimRGB", mat->GetRimRGB());
        fprintf(f, "    \"rimLightUnder\": %s,\n", mat->GetRimLightUnder() ? "true" : "false");

        // Normal detail
        fprintf(f, "    \"deNormal\": %.4f,\n", mat->GetDeNormal());
        fprintf(f, "    \"normDetailTiling\": %.4f,\n", mat->GetNormDetailTiling());
        fprintf(f, "    \"normDetailStrength\": %.4f,\n", mat->GetNormDetailStrength());

        // Emissive
        fprintf(f, "    \"emissiveMultiplier\": %.4f,\n", mat->GetEmissiveMultiplier());

        // Environment map settings
        fprintf(f, "    \"environMapFalloff\": %s,\n", mat->GetEnvironMapFalloff() ? "true" : "false");
        fprintf(f, "    \"environMapSpecMask\": %s,\n", mat->GetEnvironMapSpecMask() ? "true" : "false");

        // Texture coordinate generation (NgMat::SetupShader's MakeTex3 input)
        {
            const Transform& x = mat->TexXfm();
            fprintf(f, "    \"texGen\": %d,\n", (int)mat->GetTexGen());
            fprintf(f,
                    "    \"texXfm\": [%.6g, %.6g, %.6g, %.6g, %.6g, %.6g, %.6g, %.6g, %.6g, %.6g, %.6g, %.6g],\n",
                    x.m.x.x, x.m.x.y, x.m.x.z, x.m.y.x, x.m.y.y, x.m.y.z,
                    x.m.z.x, x.m.z.y, x.m.z.z, x.v.x, x.v.y, x.v.z);
            fprintf(f, "    \"screenAligned\": %s,\n", mat->GetScreenAligned() ? "true" : "false");
            fprintf(f, "    \"perPixelLit\": %s,\n", mat->GetPerPixelLit() ? "true" : "false");
            fprintf(f, "    \"recvProjLights\": %s,\n", mat->GetRecvProjLights() ? "true" : "false");
            fprintf(f, "    \"recvPointCubeTex\": %s,\n", mat->GetRecvPointCubeTex() ? "true" : "false");
            fprintf(f, "    \"fadeOut\": %s,\n", mat->FadeOut() ? "true" : "false");
            fprintf(f, "    \"colorAdjust\": %s,\n", mat->ColorAdjust() ? "true" : "false");
        }

        // Next pass
        BaseMaterial* next = mat->NextPass();
        if (next && next->Name() && next->Name()[0]) {
            fprintf(f, "    \"nextPass\": \"%s\"\n", next->Name());
        } else {
            fprintf(f, "    \"nextPass\": null\n");
        }

        fprintf(f, "}\n");
        fclose(f);

        count++;
        if (opts.verbose)
            printf("  exported: %s\n", path.c_str());

        ++it;
    }

    // Material animations: which materials have their tex xfm driven at runtime.
    std::string animDir = std::string(outputDir) + "/_matanim";
    bool madeAnimDir = false;
    ObjDirItr<RndMatAnim> ait(dir, true);
    int animSeen = 0, animWritten = 0;
    while (ait) {
        RndMatAnim* anim = ait;
        animSeen++;
        const char* name = anim->Name();
        if (!name || !name[0] || !anim->KeysOwner()) {
            ++ait;
            continue;
        }
        if (!madeAnimDir) {
            mkdir(animDir.c_str(), 0755);
            madeAnimDir = true;
        }
        std::string path = animDir + "/" + name + ".json";
        FILE* f = fopen(path.c_str(), "w");
        if (!f) {
            ++ait;
            continue;
        }
        Hmx::Object* target = anim->AnimTarget();
        fprintf(f, "{\n    \"name\": \"%s\",\n", name);
        fprintf(f, "    \"mat\": %s%s%s,\n", target ? "\"" : "",
                target ? target->Name() : "null", target ? "\"" : "");
        auto dumpV3 = [&](const char* key, Keys<Vector3, Vector3>& keys, bool last) {
            fprintf(f, "    \"%s\": [", key);
            for (int i = 0; i < (int)keys.size(); i++) {
                const Key<Vector3>& k = keys[i];
                fprintf(f, "%s[%.6g, %.6g, %.6g, %.6g]", i ? ", " : "", k.frame,
                        k.value.x, k.value.y, k.value.z);
            }
            fprintf(f, "]%s\n", last ? "" : ",");
        };
        dumpV3("trans", anim->TransKeys(), false);
        dumpV3("rot", anim->RotKeys(), false);
        dumpV3("scale", anim->ScaleKeys(), false);
        fprintf(f, "    \"texKeys\": %d,\n", (int)anim->GetTexKeys().size());
        fprintf(f, "    \"colorKeys\": %d,\n", (int)anim->ColorKeys().size());
        fprintf(f, "    \"alphaKeys\": %d\n}\n", (int)anim->AlphaKeys().size());
        fclose(f);
        animWritten++;
        ++ait;
    }
    if (opts.verbose || animSeen)
        printf("MatAnims: %d seen, %d written\n", animSeen, animWritten);

    // Prop animations whose target is a material (tex_xfm, color, alpha, ...):
    // one line per PropKeys in <outdir>/_propanim.jsonl.
    FILE* pf = nullptr;
    ObjDirItr<RndPropAnim> pit(dir, true);
    while (pit) {
        RndPropAnim* anim = pit;
        const PropAnimPeek& a = PropAnimPeek::Of(anim);
        for (std::list<PropKeys*>::const_iterator k = a.mPropKeys.begin();
             k != a.mPropKeys.end(); ++k) {
            PropKeys* keys = *k;
            if (!keys || !dynamic_cast<BaseMaterial*>(keys->Target())) continue;
            if (!pf) pf = fopen((std::string(outputDir) + "/_propanim.jsonl").c_str(), "w");
            if (!pf) break;
            String prop;
            if (keys->Prop()) keys->Prop()->Print(prop, kDataArray, true, 0);
            std::string ps = prop.c_str();
            for (char& c : ps)
                if (c == '"' || c == '\n' || c == '\\') c = ' ';
            fprintf(pf, "{\"anim\": \"%s\", \"mat\": \"%s\", \"prop\": \"%s\", \"type\": %d, "
                        "\"keys\": [",
                    anim->Name(), keys->Target()->Name(), ps.c_str(), (int)keys->KeysType());
            if (keys->KeysType() == PropKeys::kFloat) {
                Keys<float, float>* fk = keys->AsFloatKeys();
                for (size_t i = 0; fk && i < fk->size(); i++)
                    fprintf(pf, "%s[%g, %g]", i ? ", " : "", (*fk)[i].frame, (*fk)[i].value);
            } else if (keys->KeysType() == PropKeys::kVector3) {
                Keys<Vector3, Vector3>* vk = keys->AsVector3Keys();
                for (size_t i = 0; vk && i < vk->size(); i++)
                    fprintf(pf, "%s[%g, %g, %g, %g]", i ? ", " : "", (*vk)[i].frame,
                            (*vk)[i].value.x, (*vk)[i].value.y, (*vk)[i].value.z);
            } else {
                fprintf(pf, "%d", keys->NumKeys());
            }
            fprintf(pf, "]}\n");
        }
        ++pit;
    }
    if (pf) fclose(pf);

    return count;
}

} // namespace MaterialExporter

// MaterialExporter — Iterates BaseMaterial objects in an ObjectDir and writes JSON.

#include "export/MaterialExporter.h"

#include "obj/Dir.h"
#include "rndobj/BaseMaterial.h"

#include <cstdio>
#include <string>
#include <sys/stat.h>

namespace MaterialExporter {

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

    return count;
}

} // namespace MaterialExporter

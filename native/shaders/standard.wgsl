// DC3 Native — Standard Material Shader
// Supports: diffuse texture, normal mapping, specular/emissive/rim maps,
//           multi-light directional lighting, vertex color, alpha test,
//           fog, specular (Blinn-Phong), emissive, rim lighting, intensify

// === Bind Group 0: Per-scene (camera, environment) ===
struct SceneUniforms {
    viewProj: mat4x4f,
    view: mat4x4f,
    cameraPos: vec3f,
    _pad0: f32,
    // Fog
    fogColor: vec3f,
    fogStart: f32,
    fogEnd: f32,
    fogEnabled: f32,
    _pad1: vec2f,
    // Multi-light (up to 4 directional lights)
    lightDirs: array<vec4f, 4>,
    lightColors: array<vec4f, 4>,
    // Ambient
    ambientColor: vec4f,
    numLights: f32,
    _padN1: f32,
    _padN2: f32,
    _padN3: f32,
    // Point lights (up to 4)
    pointLightPos: array<vec4f, 4>,
    pointLightColors: array<vec4f, 4>,
    pointLightRanges: vec4f,
    numPointLights: f32,
    _padPL1: f32,
    _padPL2: f32,
    _padPL3: f32,
    // Shadow mapping
    lightViewProj: mat4x4f,
    shadowEnabled: f32,
    shadowBias: f32,
    shadowMapSize: f32,
    shadowStrength: f32,
};

@group(0) @binding(0) var<uniform> scene: SceneUniforms;
@group(0) @binding(1) var shadowMap: texture_depth_2d;
@group(0) @binding(2) var shadowSampler: sampler_comparison;

// === Bind Group 1: Per-material ===
struct MaterialUniforms {
    color: vec4f,
    alphaThreshold: f32,
    useTexture: f32,
    specularPower: f32,
    emissiveMultiplier: f32,
    specularColor: vec4f,
    rimColor: vec4f,        // .rgb = color, .a = power
    intensify: f32,
    shaderVariation: f32,   // 0=none, 1=skin, 2=hair
    rimLightUnder: f32,     // 1.0 = rim only on backlit edges
    deNormal: f32,          // normal map diminish (0=neutral, 1=no bumps, -1=exaggerate)
    specular2Color: vec4f,  // .rgb = color, .a = power (2nd specular lobe)
    anisotropy: f32,
    hasNormalMap: f32,       // 1.0 when normal map is bound
    materialFogEnabled: f32, // 1.0 if fog applies to this material
    prelit: f32,             // 1.0 if vertex color is pre-lit (skip lighting)
    environMapStrength: f32, // 1.0 when environ map is bound
    environMapFalloff: f32,  // 1.0 for Fresnel falloff at grazing angles
    environMapSpecMask: f32, // 1.0 to mask reflection by specular map alpha
    texGenMode: f32,         // 0=none, 1=xfm, 2=sphere, 3=projected, 4=xfmOrigin, 5=environ
    texXfmRow0: vec4f,       // UV transform row 0
    texXfmRow1: vec4f,       // UV transform row 1
    normDetailTiling: f32,   // UV tiling for detail normal map
    normDetailStrength: f32, // blend strength (0 = disabled)
    hasNormDetailMap: f32,   // 1.0 when detail map bound
    useAlphaAsRGB: f32,      // 1.0 to use texture alpha as grayscale RGB (font textures)
};

@group(1) @binding(0) var<uniform> material: MaterialUniforms;
@group(1) @binding(1) var diffuseTex: texture_2d<f32>;
@group(1) @binding(2) var diffuseSampler: sampler;
@group(1) @binding(3) var normalMapTex: texture_2d<f32>;
@group(1) @binding(4) var specularMapTex: texture_2d<f32>;
@group(1) @binding(5) var emissiveMapTex: texture_2d<f32>;
@group(1) @binding(6) var rimMapTex: texture_2d<f32>;
@group(1) @binding(7) var mapSampler: sampler;
@group(1) @binding(8) var environMapTex: texture_cube<f32>;
@group(1) @binding(9) var environSampler: sampler;
@group(1) @binding(10) var normDetailMapTex: texture_2d<f32>;

// === Bind Group 2: Per-object ===
struct ObjectUniforms {
    world: mat4x4f,
    worldInvTranspose: mat4x4f,
};

@group(2) @binding(0) var<uniform> object: ObjectUniforms;

// === Bind Group 3: Per-draw bone palette (skinned meshes) ===
@group(3) @binding(0) var<uniform> bones: array<mat4x4f, 40>;

// === Vertex/Fragment IO ===
struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) color: vec4f,
    @location(3) uv: vec2f,
    @location(4) tangent: vec4f,
};

struct SkinnedVertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) color: vec4f,
    @location(3) uv: vec2f,
    @location(4) boneWeights: vec4f,
    @location(5) boneIndices: vec4u,
    @location(6) tangent: vec4f,
};

struct VertexOutput {
    @builtin(position) clipPos: vec4f,
    @location(0) worldPos: vec3f,
    @location(1) worldNormal: vec3f,
    @location(2) color: vec4f,
    @location(3) uv: vec2f,
    @location(4) worldTangent: vec3f,
    @location(5) worldBitangent: vec3f,
};

// === TexGen UV computation ===
fn computeTexGenUV(baseUV: vec2f, worldPos: vec3f, N: vec3f, V: vec3f) -> vec2f {
    let mode = material.texGenMode;
    if (mode < 0.5) {
        return baseUV; // kTexGenNone
    }
    if (mode < 1.5) {
        // kTexGenXfm: transform UV about (0.5, 0.5) center
        let centered = baseUV - vec2f(0.5);
        return vec2f(
            dot(centered, material.texXfmRow0.xy) + material.texXfmRow0.z + 0.5,
            dot(centered, material.texXfmRow1.xy) + material.texXfmRow1.z + 0.5
        );
    }
    if (mode < 2.5) {
        // kTexGenSphere: sphere map from view-space normal
        let viewNormal = (scene.view * vec4f(N, 0.0)).xyz;
        return viewNormal.xy * 0.5 + 0.5;
    }
    if (mode < 3.5) {
        // kTexGenProjected: project from xfm direction in world coords
        return vec2f(
            dot(worldPos, material.texXfmRow0.xyz) + material.texXfmRow0.w,
            dot(worldPos, material.texXfmRow1.xyz) + material.texXfmRow1.w
        );
    }
    if (mode < 4.5) {
        // kTexGenXfmOrigin: transform UV about origin (not center)
        return vec2f(
            dot(baseUV, material.texXfmRow0.xy) + material.texXfmRow0.z,
            dot(baseUV, material.texXfmRow1.xy) + material.texXfmRow1.z
        );
    }
    // kTexGenEnviron: reflection-based UV (perspective-correct sphere map)
    let R = reflect(-V, N);
    let viewR = (scene.view * vec4f(R, 0.0)).xyz;
    return viewR.xy * 0.5 + 0.5;
}

// === Vertex Shader (static meshes) ===
@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    let worldPos = (object.world * vec4f(in.position, 1.0)).xyz;
    out.clipPos = scene.viewProj * vec4f(worldPos, 1.0);
    out.worldPos = worldPos;
    let N = normalize((object.worldInvTranspose * vec4f(in.normal, 0.0)).xyz);
    out.worldNormal = N;
    out.color = in.color;

    // TexGen UV computation
    let V_vs = normalize(scene.cameraPos - worldPos);
    out.uv = computeTexGenUV(in.uv, worldPos, N, V_vs);

    // Transform tangent to world space, compute bitangent
    let T = normalize((object.world * vec4f(in.tangent.xyz, 0.0)).xyz);
    out.worldTangent = T;
    out.worldBitangent = cross(N, T) * in.tangent.w;

    return out;
}

// === Vertex Shader (skinned meshes) ===
@vertex
fn vs_skinned(in: SkinnedVertexInput) -> VertexOutput {
    var out: VertexOutput;

    // Blend position, normal, and tangent across up to 4 bones
    var skinnedPos = vec4f(0.0);
    var skinnedNorm = vec4f(0.0);
    var skinnedTan = vec4f(0.0);
    let indices = in.boneIndices;

    // Normalize weights — UDEC4N (10-10-10-2) may not sum to 1.0
    let rawWeights = in.boneWeights;
    let totalWeight = rawWeights.x + rawWeights.y + rawWeights.z + rawWeights.w;
    var weights: vec4f;
    if (totalWeight > 0.0) {
        weights = rawWeights / totalWeight;
    } else {
        weights = vec4f(1.0, 0.0, 0.0, 0.0); // fallback: 100% to first bone
    }

    for (var i = 0u; i < 4u; i++) {
        var w: f32;
        var idx: u32;
        switch(i) {
            case 0u: { w = weights.x; idx = indices.x; }
            case 1u: { w = weights.y; idx = indices.y; }
            case 2u: { w = weights.z; idx = indices.z; }
            default: { w = weights.w; idx = indices.w; }
        }
        if (w > 0.0) {
            let m = bones[idx];
            skinnedPos += w * (m * vec4f(in.position, 1.0));
            skinnedNorm += w * (m * vec4f(in.normal, 0.0));
            skinnedTan += w * (m * vec4f(in.tangent.xyz, 0.0));
        }
    }

    // Apply object world transform
    let worldPos = (object.world * vec4f(skinnedPos.xyz, 1.0)).xyz;
    out.clipPos = scene.viewProj * vec4f(worldPos, 1.0);
    out.worldPos = worldPos;
    let N = normalize((object.worldInvTranspose * vec4f(skinnedNorm.xyz, 0.0)).xyz);
    out.worldNormal = N;
    out.color = in.color;

    // TexGen UV computation
    let V_sk = normalize(scene.cameraPos - worldPos);
    out.uv = computeTexGenUV(in.uv, worldPos, N, V_sk);

    let T = normalize((object.world * vec4f(skinnedTan.xyz, 0.0)).xyz);
    out.worldTangent = T;
    out.worldBitangent = cross(N, T) * in.tangent.w;

    return out;
}

// === Fragment Shader ===
@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Base color: material RGB tints vertex color.
    var matAlpha = material.color.a;
    var baseColor = vec4f(material.color.rgb * in.color.rgb, matAlpha * in.color.a);

    // Sample diffuse texture if available
    if (material.useTexture > 0.5) {
        let texColor = textureSample(diffuseTex, diffuseSampler, in.uv);
        // Font textures (DXT5): glyph shape is in alpha, RGB is garbage.
        // Use vertex color directly for text (skip material color tint).
        // The mTextColor is baked into vertex colors by the text mesh builder.
        if (material.useAlphaAsRGB > 0.5) {
            baseColor = vec4f(in.color.rgb, in.color.a * texColor.a);
        } else {
            baseColor = vec4f(baseColor.rgb * texColor.rgb, baseColor.a * texColor.a);
        }
    }

    // Apply intensify (doubles texture brightness)
    baseColor = vec4f(baseColor.rgb * material.intensify, baseColor.a);

    // Alpha test
    if (baseColor.a < material.alphaThreshold) {
        discard;
    }

    // Compute normal — apply normal map if present
    var N = normalize(in.worldNormal);
    if (material.hasNormalMap > 0.5) {
        let normalSample = textureSample(normalMapTex, mapSampler, in.uv);
        var tsNormal: vec3f;
        // Detect format: DXT5nm has data in alpha+green only (R+B near zero)
        // Standard RGB normal maps have data in all channels
        if (normalSample.r + normalSample.b < 0.1) {
            // DXT5nm format (Xbox 360): X in alpha, Y in green, reconstruct Z
            let nx = normalSample.a * 2.0 - 1.0;
            let ny = normalSample.g * 2.0 - 1.0;
            let nz = sqrt(max(1.0 - nx * nx - ny * ny, 0.0));
            tsNormal = vec3f(nx, ny, nz);
            // If all channels are zero, the texture is missing — use flat normal
            if (normalSample.a < 0.01 && normalSample.g < 0.01) {
                tsNormal = vec3f(0.0, 0.0, 1.0);
            }
        } else {
            // Standard RGB normal map: XYZ in RGB
            tsNormal = normalSample.rgb * 2.0 - 1.0;
        }
        // Apply deNormal: lerp toward (0,0,1) — 0=neutral, 1=flat, -1=exaggerate
        let diminish = clamp(material.deNormal, -3.0, 1.0);
        tsNormal = vec3f(
            tsNormal.x * (1.0 - diminish),
            tsNormal.y * (1.0 - diminish),
            mix(tsNormal.z, 1.0, max(diminish, 0.0))
        );
        tsNormal = normalize(tsNormal);
        // Blend detail normal map (UDN method)
        if (material.hasNormDetailMap > 0.5) {
            let detailUV = in.uv * material.normDetailTiling;
            let detailSample = textureSample(normDetailMapTex, mapSampler, detailUV);
            let detailNorm = detailSample.xyz * 2.0 - 1.0;
            tsNormal = normalize(vec3f(
                tsNormal.x + detailNorm.x * material.normDetailStrength,
                tsNormal.y + detailNorm.y * material.normDetailStrength,
                tsNormal.z
            ));
        }
        // Build TBN matrix and transform to world space
        let T = normalize(in.worldTangent);
        let B = normalize(in.worldBitangent);
        N = normalize(T * tsNormal.x + B * tsNormal.y + N * tsNormal.z);
    }

    let V = normalize(scene.cameraPos - in.worldPos);



    // Sample specular map (RGB = specular color mask, A = gloss/power mask)
    let specMapSample = textureSample(specularMapTex, mapSampler, in.uv);

    // Detect shader variation
    let isSkin = material.shaderVariation > 0.5 && material.shaderVariation < 1.5;
    let isHair = material.shaderVariation > 1.5 && material.shaderVariation < 2.5;

    // Shadow sampling — compute once for the primary directional light
    // Note: textureSampleCompare must be in uniform control flow (WGSL rule),
    // so we always sample and use the UV bounds check only to mask the result.
    var shadowFactor = 1.0;
    let shadowClipPos = scene.lightViewProj * vec4f(in.worldPos, 1.0);
    let shadowNdc = shadowClipPos.xyz / shadowClipPos.w;
    let shadowUV = clamp(shadowNdc.xy * vec2f(0.5, -0.5) + 0.5, vec2f(0.0), vec2f(1.0));
    let shadowDepth = clamp(shadowNdc.z - scene.shadowBias, 0.0, 1.0);
    let texel = 1.0 / max(scene.shadowMapSize, 1.0);
    // 3x3 PCF — unrolled because textureSampleCompare requires uniform control flow
    var shadowSum = 0.0;
    shadowSum += textureSampleCompare(shadowMap, shadowSampler, shadowUV + vec2f(-texel, -texel), shadowDepth);
    shadowSum += textureSampleCompare(shadowMap, shadowSampler, shadowUV + vec2f(0.0, -texel), shadowDepth);
    shadowSum += textureSampleCompare(shadowMap, shadowSampler, shadowUV + vec2f(texel, -texel), shadowDepth);
    shadowSum += textureSampleCompare(shadowMap, shadowSampler, shadowUV + vec2f(-texel, 0.0), shadowDepth);
    shadowSum += textureSampleCompare(shadowMap, shadowSampler, shadowUV, shadowDepth);
    shadowSum += textureSampleCompare(shadowMap, shadowSampler, shadowUV + vec2f(texel, 0.0), shadowDepth);
    shadowSum += textureSampleCompare(shadowMap, shadowSampler, shadowUV + vec2f(-texel, texel), shadowDepth);
    shadowSum += textureSampleCompare(shadowMap, shadowSampler, shadowUV + vec2f(0.0, texel), shadowDepth);
    shadowSum += textureSampleCompare(shadowMap, shadowSampler, shadowUV + vec2f(texel, texel), shadowDepth);
    shadowSum /= 9.0;
    // Only apply shadow when enabled and within shadow map bounds
    let rawUV = shadowNdc.xy * vec2f(0.5, -0.5) + 0.5;
    let inShadowBounds = scene.shadowEnabled > 0.5
        && rawUV.x >= 0.0 && rawUV.x <= 1.0
        && rawUV.y >= 0.0 && rawUV.y <= 1.0
        && shadowNdc.z >= 0.0 && shadowNdc.z <= 1.0;
    if (inShadowBounds) {
        shadowFactor = mix(scene.shadowStrength, 1.0, shadowSum);
    }

    // Accumulate diffuse and specular from all active lights
    var totalDiffuse = vec3f(0.0);
    var totalSpecular = vec3f(0.0);
    let lightCount = i32(scene.numLights);

    for (var i = 0; i < 4; i++) {
        if (i >= lightCount) { break; }

        let L = normalize(-scene.lightDirs[i].xyz);
        let lightColor = scene.lightColors[i].rgb;
        let rawNdotL = dot(N, L);

        if (isHair) {
            // --- Hair shader: Kajiya-Kay anisotropic specular ---
            // Wrap diffuse (same as skin — hair benefits from soft shadows)
            let wrapNdotL = rawNdotL * 0.5 + 0.5;
            totalDiffuse += lightColor * wrapNdotL;

            // Kajiya-Kay: specular highlight shifts along strand tangent
            let T = normalize(in.worldTangent);
            let H = normalize(L + V);
            let TdotH = dot(T, H);
            let sinTH = sqrt(max(1.0 - TdotH * TdotH, 0.0));
            let anisoExp = max(material.anisotropy, 1.0);
            let anisoSpec = pow(sinTH, anisoExp);
            totalSpecular += material.specularColor.rgb * specMapSample.rgb * lightColor * anisoSpec;
        } else if (isSkin) {
            // --- Skin shader: Half-Lambert + warm shadow tint + dual specular ---

            // Half-Lambert (wrap) diffuse — softens shadow falloff
            let wrapNdotL = rawNdotL * 0.5 + 0.5;
            let skinDiffuse = wrapNdotL * wrapNdotL;

            // Warm shadow tint at the terminator (cheap SSS approximation)
            let shadowZone = smoothstep(0.0, 0.5, rawNdotL);
            let warmTint = mix(vec3f(0.85, 0.45, 0.25), vec3f(1.0), shadowZone);
            totalDiffuse += lightColor * skinDiffuse * warmTint;

            // Dual specular lobes
            let H = normalize(L + V);
            let NdotH = max(dot(N, H), 0.0);

            // Lobe 1: primary specular from material, masked by specular map
            if (material.specularPower > 0.0) {
                let specPower = material.specularPower * specMapSample.a;
                let spec1 = pow(NdotH, max(specPower, 1.0));
                totalSpecular += material.specularColor.rgb * specMapSample.rgb * lightColor * spec1;
            }

            // Lobe 2: secondary (broad "oily skin" sheen)
            let spec2Power = material.specular2Color.a;
            if (spec2Power > 0.0) {
                let spec2 = pow(NdotH, spec2Power);
                totalSpecular += material.specular2Color.rgb * lightColor * spec2;
            }
        } else {
            // --- Standard Lambert + Blinn-Phong ---
            let NdotL = max(rawNdotL, 0.0);
            totalDiffuse += lightColor * NdotL;

            if (material.specularPower > 0.0) {
                let H = normalize(L + V);
                let specPower = material.specularPower * specMapSample.a;
                let spec = pow(max(dot(N, H), 0.0), max(specPower, 1.0));
                totalSpecular += material.specularColor.rgb * specMapSample.rgb * lightColor * spec;
            }
        }
    }

    // Point lights
    let pointLightCount = i32(scene.numPointLights);
    for (var pi = 0; pi < 4; pi++) {
        if (pi >= pointLightCount) { break; }

        let lightPos = scene.pointLightPos[pi].xyz;
        let toLight = lightPos - in.worldPos;
        let dist = length(toLight);
        let L = toLight / max(dist, 0.001);

        // Range-based attenuation (smooth falloff)
        var range: f32;
        switch(pi) {
            case 0: { range = scene.pointLightRanges.x; }
            case 1: { range = scene.pointLightRanges.y; }
            case 2: { range = scene.pointLightRanges.z; }
            default: { range = scene.pointLightRanges.w; }
        }
        let atten = saturate(1.0 - dist / max(range, 0.001));
        let atten2 = atten * atten; // quadratic falloff

        let lightColor = scene.pointLightColors[pi].rgb * atten2;
        let NdotL = max(dot(N, L), 0.0);
        totalDiffuse += lightColor * NdotL;

        if (material.specularPower > 0.0) {
            let H = normalize(L + V);
            let specPower = material.specularPower * specMapSample.a;
            let spec = pow(max(dot(N, H), 0.0), max(specPower, 1.0));
            totalSpecular += material.specularColor.rgb * specMapSample.rgb * lightColor * spec;
        }
    }

    let ambient = scene.ambientColor.rgb;
    var finalColor: vec3f;
    if (material.prelit > 0.5) {
        // Pre-lit: vertex color already contains lighting, skip diffuse/specular
        finalColor = baseColor.rgb;
    } else {
        finalColor = baseColor.rgb * (ambient + totalDiffuse * shadowFactor) + totalSpecular * shadowFactor;
    }

    // Emissive (self-illumination) — modulated by emissive map
    let emissiveSample = textureSample(emissiveMapTex, mapSampler, in.uv);
    finalColor += baseColor.rgb * material.emissiveMultiplier * emissiveSample.rgb;

    // Rim lighting — fresnel edge glow, modulated by rim map
    // Use original power but scale intensity — without full stage lighting the
    // pink rim color (designed for colored spot lights) overwhelms base colors
    let rimPower = max(material.rimColor.a, 0.5);
    if (rimPower > 0.0 && length(material.rimColor.rgb) > 0.0) {
        let rimMapSample = textureSample(rimMapTex, mapSampler, in.uv);
        let rimDot = 1.0 - max(dot(N, V), 0.0);
        let rimPowerMod = rimPower * rimMapSample.a;
        var rim = pow(rimDot, max(rimPowerMod, 0.5)) * material.rimColor.rgb * rimMapSample.rgb * 0.15;

        // Rim-under: only apply rim on edges facing away from the primary light
        if (material.rimLightUnder > 0.5 && scene.numLights > 0.0) {
            let L = normalize(-scene.lightDirs[0].xyz);
            let backlit = saturate(1.0 - dot(N, L));
            rim *= backlit;
        }

        finalColor += rim;
    }

    // Environment map reflection
    if (material.environMapStrength > 0.5) {
        let R = reflect(-V, N);
        let envSample = textureSample(environMapTex, environSampler, R).rgb;

        // Fresnel falloff: stronger at grazing angles
        var envFactor = 1.0;
        if (material.environMapFalloff > 0.5) {
            let NdotV = max(dot(N, V), 0.0);
            envFactor = 1.0 - NdotV;  // Schlick-like: strong at edges, weak head-on
            envFactor = envFactor * envFactor;  // square for sharper falloff
        }

        // Optionally mask by specular map alpha (gloss)
        var envMask = 1.0;
        if (material.environMapSpecMask > 0.5) {
            envMask = specMapSample.a;
        }

        finalColor += envSample * envFactor * envMask;
    }

    // Fog (gated by per-material flag + blend mode)
    if (scene.fogEnabled > 0.5 && material.materialFogEnabled > 0.5) {
        let dist = length(in.worldPos - scene.cameraPos);
        let fogFactor = clamp((scene.fogEnd - dist) / (scene.fogEnd - scene.fogStart), 0.0, 1.0);
        finalColor = mix(scene.fogColor, finalColor, fogFactor);
    }

    // Soft highlight compression — only affects values approaching 1.0+
    let knee = vec3f(0.9);
    finalColor = select(finalColor,
                        knee + (vec3f(1.0) - knee) * tanh((finalColor - knee) / (vec3f(1.0) - knee)),
                        finalColor > knee);

    // Attenuate RGB for near-transparent pixels to prevent alpha fringe
    if (baseColor.a < 0.1) {
        finalColor *= baseColor.a / 0.1;
    }

    return vec4f(finalColor, baseColor.a);
}

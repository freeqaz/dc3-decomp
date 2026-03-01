R"WGSL(
// DC3 Native — Standard Material Shader
// Supports: diffuse texture, multi-light directional lighting, vertex color,
//           alpha test discard, fog, specular (Blinn-Phong), emissive, rim lighting,
//           intensify

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
};

@group(0) @binding(0) var<uniform> scene: SceneUniforms;

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
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
};

@group(1) @binding(0) var<uniform> material: MaterialUniforms;
@group(1) @binding(1) var diffuseTex: texture_2d<f32>;
@group(1) @binding(2) var diffuseSampler: sampler;

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
};

struct SkinnedVertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) color: vec4f,
    @location(3) uv: vec2f,
    @location(4) boneWeights: vec4f,
    @location(5) boneIndices: vec4u,
};

struct VertexOutput {
    @builtin(position) clipPos: vec4f,
    @location(0) worldPos: vec3f,
    @location(1) worldNormal: vec3f,
    @location(2) color: vec4f,
    @location(3) uv: vec2f,
};

// === Vertex Shader (static meshes) ===
@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    let worldPos = (object.world * vec4f(in.position, 1.0)).xyz;
    out.clipPos = scene.viewProj * vec4f(worldPos, 1.0);
    out.worldPos = worldPos;
    out.worldNormal = normalize((object.worldInvTranspose * vec4f(in.normal, 0.0)).xyz);
    out.color = in.color;
    out.uv = in.uv;

    return out;
}

// === Vertex Shader (skinned meshes) ===
@vertex
fn vs_skinned(in: SkinnedVertexInput) -> VertexOutput {
    var out: VertexOutput;

    // Blend position and normal across up to 4 bones
    var skinnedPos = vec4f(0.0);
    var skinnedNorm = vec4f(0.0);
    let weights = in.boneWeights;
    let indices = in.boneIndices;

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
        }
    }

    // Apply object world transform
    let worldPos = (object.world * vec4f(skinnedPos.xyz, 1.0)).xyz;
    out.clipPos = scene.viewProj * vec4f(worldPos, 1.0);
    out.worldPos = worldPos;
    out.worldNormal = normalize((object.worldInvTranspose * vec4f(skinnedNorm.xyz, 0.0)).xyz);
    out.color = in.color;
    out.uv = in.uv;

    return out;
}

// === Fragment Shader ===
@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Base color from material * vertex color
    var baseColor = material.color * in.color;

    // Sample diffuse texture if available
    if (material.useTexture > 0.5) {
        let texColor = textureSample(diffuseTex, diffuseSampler, in.uv);
        baseColor = baseColor * texColor;
    }

    // Apply intensify (doubles texture brightness)
    baseColor = vec4f(baseColor.rgb * material.intensify, baseColor.a);

    // Alpha test
    if (baseColor.a < material.alphaThreshold) {
        discard;
    }

    // Lighting
    let N = normalize(in.worldNormal);
    let V = normalize(scene.cameraPos - in.worldPos);

    // Accumulate diffuse and specular from all active lights
    var totalDiffuse = vec3f(0.0);
    var totalSpecular = vec3f(0.0);
    let lightCount = i32(scene.numLights);

    for (var i = 0; i < 4; i++) {
        if (i >= lightCount) { break; }

        let L = normalize(-scene.lightDirs[i].xyz);
        let lightColor = scene.lightColors[i].rgb;

        // Half-Lambert diffuse
        let NdotL = dot(N, L) * 0.5 + 0.5;
        let NdotL2 = NdotL * NdotL;
        totalDiffuse += lightColor * NdotL2;

        // Blinn-Phong specular
        if (material.specularPower > 0.0) {
            let H = normalize(L + V);
            let spec = pow(max(dot(N, H), 0.0), material.specularPower);
            totalSpecular += material.specularColor.rgb * lightColor * spec;
        }
    }

    let ambient = scene.ambientColor.rgb;
    var finalColor = baseColor.rgb * (ambient + totalDiffuse) + totalSpecular;

    // Emissive (self-illumination)
    finalColor += baseColor.rgb * material.emissiveMultiplier;

    // Rim lighting
    let rimPower = material.rimColor.a;
    if (rimPower > 0.0) {
        let rimDot = 1.0 - max(dot(N, V), 0.0);
        let rim = pow(rimDot, rimPower) * material.rimColor.rgb;
        finalColor += rim;
    }

    // Fog
    if (scene.fogEnabled > 0.5) {
        let dist = length(in.worldPos - scene.cameraPos);
        let fogFactor = clamp((scene.fogEnd - dist) / (scene.fogEnd - scene.fogStart), 0.0, 1.0);
        finalColor = mix(scene.fogColor, finalColor, fogFactor);
    }

    return vec4f(finalColor, baseColor.a);
}
)WGSL"
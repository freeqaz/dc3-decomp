R"WGSL(
// DC3 Native — Standard Material Shader (Tier 1 MVP)
// Supports: diffuse texture, ambient+directional lighting, vertex color,
//           alpha test discard, fog

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
    // Main directional light
    lightDir: vec3f,
    _padL0: f32,
    lightColor: vec3f,
    _padL1: f32,
    // Ambient
    ambientColor: vec4f,
};

@group(0) @binding(0) var<uniform> scene: SceneUniforms;

// === Bind Group 1: Per-material ===
struct MaterialUniforms {
    color: vec4f,
    alphaThreshold: f32,
    useTexture: f32,
    _pad: vec2f,
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

// === Vertex/Fragment IO ===
struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) color: vec4f,
    @location(3) uv: vec2f,
};

struct VertexOutput {
    @builtin(position) clipPos: vec4f,
    @location(0) worldPos: vec3f,
    @location(1) worldNormal: vec3f,
    @location(2) color: vec4f,
    @location(3) uv: vec2f,
};

// === Vertex Shader ===
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

    // Alpha test
    if (baseColor.a < material.alphaThreshold) {
        discard;
    }

    // Directional + ambient lighting with half-Lambert wrap
    let N = normalize(in.worldNormal);
    let L = normalize(-scene.lightDir);
    let NdotL = dot(N, L) * 0.5 + 0.5; // half-Lambert: wraps light to dark side
    let NdotL2 = NdotL * NdotL;         // squared for softer falloff

    let diffuse = scene.lightColor * NdotL2;
    let ambient = scene.ambientColor.rgb;
    let lit = baseColor.rgb * (ambient + diffuse);

    // Fog
    var finalColor = lit;
    if (scene.fogEnabled > 0.5) {
        let dist = length(in.worldPos - scene.cameraPos);
        let fogFactor = clamp((scene.fogEnd - dist) / (scene.fogEnd - scene.fogStart), 0.0, 1.0);
        finalColor = mix(scene.fogColor, finalColor, fogFactor);
    }

    return vec4f(finalColor, baseColor.a);
}
)WGSL"
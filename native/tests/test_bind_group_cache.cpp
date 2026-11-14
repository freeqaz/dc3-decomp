// test_bind_group_cache.cpp -- the mesh path's cached bind groups and staged
// uniform rings (PLAN H-30 / H-31, lane np-drawperf).
//
// The mesh path used to make three bind groups per draw (material, object,
// bones) and issue one Queue::WriteBuffer per uniform block. Now the groups
// come from a BindGroupCache (engine gfx/BindGroupCache.h) keyed by every
// field of their descriptor, the uniform blocks are reached through dynamic
// offsets, and the rings are uploaded once per frame. What can go wrong:
//
//   * a key that drops a field hands one draw another draw's textures or
//     samplers (BindGroupCacheKey.*, MaterialGroupSeparatesEveryBoundResource);
//   * a cache that never lets go pins dead textures across songs
//     (UnusedEntriesAgeOut, ForgetDropsTheGroupsThatBindAView, BoundedByMaxEntries);
//   * a dynamic offset that does not reach the draw, or a ring that is not
//     flushed, shows every draw the first draw's uniforms -- or last frame's
//     (TwoDrawsSharingAGroupKeepTheirOwnUniforms).
//
// Sabotage tried (np-drawperf):
//   * BindGroupCache::MakeKey without the entry offset (w[2] = 0) ->
//     EveryDescriptorFieldSeparatesEntries fails on "offset" and
//     DescriptorKeyCarriesBindingOffsetSize on "offset";
//   * MakeKey without the texture view word -> EveryDescriptorFieldSeparatesEntries
//     fails on "textureView" and MaterialGroupSeparatesEveryBoundResource on
//     every view slot;
//   * Mesh_Wgpu binding group 1 at dynamic offset 0 for every draw ->
//     TwoDrawsSharingAGroupKeepTheirOwnUniforms reads the first quad's red on
//     the right half;
//   * WgpuRnd::EndDrawing without FlushUniformRings ->
//     TwoDrawsSharingAGroupKeepTheirOwnUniforms reads no colour at all.

#include "test_helpers.h"

#include "gfx/BindGroupCache.h"
#include "gfx/UniformStructs.h"
#include "platform/Rnd_Wgpu.h"

#include "math/Mtx.h"
#include "obj/Object.h"
#include "rndobj/Cam.h"
#include "rndobj/Mat.h"
#include "rndobj/Mesh.h"
#include "rndobj/Rnd.h"
#include "rndobj/Tex.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace {

using BgKey = BindGroupCache::Key;
using EntryRef = BindGroupCache::EntryRef;

const void *Fake(uintptr_t v) { return reinterpret_cast<const void *>(v); }

BgKey KeyOf(const void *layout, const std::vector<EntryRef> &e) {
    BgKey k;
    EXPECT_TRUE(BindGroupCache::MakeKey(layout, e.data(), (uint32_t)e.size(), k));
    return k;
}

// A material-shaped descriptor: a uniform buffer, a view, a sampler.
std::vector<EntryRef> BaseEntries() {
    std::vector<EntryRef> e(3);
    e[0].binding = 0;
    e[0].buffer = Fake(0x1000);
    e[0].offset = 0;
    e[0].size = 400;
    e[1].binding = 1;
    e[1].textureView = Fake(0x2000);
    e[2].binding = 2;
    e[2].sampler = Fake(0x3000);
    return e;
}

} // namespace

TEST(BindGroupCacheKey, EveryDescriptorFieldSeparatesEntries) {
    const void *layout = Fake(0x10);
    const BgKey base = KeyOf(layout, BaseEntries());
    EXPECT_EQ(base, KeyOf(layout, BaseEntries()));
    EXPECT_EQ(BindGroupCache::KeyHash{}(base), BindGroupCache::KeyHash{}(KeyOf(layout, BaseEntries())));

    struct Variant {
        const char *what;
        std::function<void(std::vector<EntryRef> &)> mutate;
    };
    const Variant variants[] = {
        {"binding", [](auto &e) { e[1].binding = 5; }},
        {"buffer", [](auto &e) { e[0].buffer = Fake(0x1100); }},
        {"offset", [](auto &e) { e[0].offset = 256; }},
        {"size", [](auto &e) { e[0].size = 512; }},
        {"sampler", [](auto &e) { e[2].sampler = Fake(0x3100); }},
        {"textureView", [](auto &e) { e[1].textureView = Fake(0x2100); }},
        // The same handle in another slot kind is another descriptor.
        {"view as sampler", [](auto &e) { e[1].sampler = e[1].textureView; e[1].textureView = nullptr; }},
        {"entry order", [](auto &e) { std::swap(e[1], e[2]); }},
        {"entry count", [](auto &e) { e.pop_back(); }},
    };
    for (const Variant &v : variants) {
        std::vector<EntryRef> e = BaseEntries();
        v.mutate(e);
        EXPECT_NE(base, KeyOf(layout, e)) << v.what << " does not separate bind groups";
    }
    EXPECT_NE(base, KeyOf(Fake(0x20), BaseEntries())) << "layout does not separate bind groups";

    // Too many entries to key: the caller must create uncached.
    std::vector<EntryRef> many(BindGroupCache::kMaxBindings + 1);
    BgKey k;
    EXPECT_FALSE(BindGroupCache::MakeKey(layout, many.data(), (uint32_t)many.size(), k));
}

namespace {

// Null-handle descriptors still differ by binding, offset and size; enough to
// drive the cache's bookkeeping without a GPU.
struct NullDesc {
    wgpu::BindGroupEntry entry{};
    wgpu::BindGroupDescriptor desc{};
    explicit NullDesc(uint64_t offset, uint32_t binding = 0, uint64_t size = 64) {
        entry.binding = binding;
        entry.offset = offset;
        entry.size = size;
        desc.entryCount = 1;
        desc.entries = &entry;
    }
};

struct CountingCreator {
    int calls = 0;
    wgpu::BindGroup operator()(const wgpu::BindGroupDescriptor &) {
        ++calls;
        return wgpu::BindGroup();
    }
};

} // namespace

TEST(BindGroupCacheKey, DescriptorKeyCarriesBindingOffsetSize) {
    BgKey a, b;
    NullDesc base(256);
    ASSERT_TRUE(BindGroupCache::MakeKey(base.desc, a));
    NullDesc same(256);
    ASSERT_TRUE(BindGroupCache::MakeKey(same.desc, b));
    EXPECT_EQ(a, b);
    NullDesc offset(512);
    ASSERT_TRUE(BindGroupCache::MakeKey(offset.desc, b));
    EXPECT_NE(a, b) << "offset";
    NullDesc binding(256, 3);
    ASSERT_TRUE(BindGroupCache::MakeKey(binding.desc, b));
    EXPECT_NE(a, b) << "binding";
    NullDesc size(256, 0, 128);
    ASSERT_TRUE(BindGroupCache::MakeKey(size.desc, b));
    EXPECT_NE(a, b) << "size";

    // A chained struct (an external texture, say) is not keyed.
    wgpu::ChainedStruct chain{};
    NullDesc chained(256);
    chained.entry.nextInChain = &chain;
    EXPECT_FALSE(BindGroupCache::MakeKey(chained.desc, b));
}

TEST(BindGroupCache, SecondLookupIsAHitAndAnotherDescriptorIsNot) {
    BindGroupCache cache;
    CountingCreator create;
    cache.BeginFrame(1);
    NullDesc a(0), b(256);
    cache.GetOrCreate(a.desc, std::ref(create));
    cache.GetOrCreate(a.desc, std::ref(create));
    EXPECT_EQ(create.calls, 1);
    EXPECT_EQ(cache.Hits(), 1u);
    cache.GetOrCreate(b.desc, std::ref(create));
    EXPECT_EQ(create.calls, 2);
    EXPECT_EQ(cache.Size(), 2u);

    // Unkeyable: created every time, never stored.
    wgpu::ChainedStruct chain{};
    NullDesc chained(512);
    chained.entry.nextInChain = &chain;
    cache.GetOrCreate(chained.desc, std::ref(create));
    cache.GetOrCreate(chained.desc, std::ref(create));
    EXPECT_EQ(create.calls, 4);
    EXPECT_EQ(cache.Size(), 2u);
    EXPECT_EQ(cache.Uncached(), 2u);
}

TEST(BindGroupCache, UnusedEntriesAgeOut) {
    BindGroupCache cache;
    CountingCreator create;
    NullDesc kept(0), dropped(256);
    cache.BeginFrame(1);
    cache.GetOrCreate(kept.desc, std::ref(create));
    cache.GetOrCreate(dropped.desc, std::ref(create));
    const uint64_t last = 1 + BindGroupCache::kMaxAgeFrames + 2 * BindGroupCache::kSweepInterval;
    for (uint64_t f = 2; f <= last; f++) {
        cache.BeginFrame(f);
        cache.GetOrCreate(kept.desc, std::ref(create));
    }
    EXPECT_EQ(cache.Size(), 1u) << "a group unused for " << last - 1 << " frames is still held";
    EXPECT_EQ(create.calls, 2) << "the group used every frame was recreated";
    // And the kept one goes once nothing uses it either.
    for (uint64_t f = last + 1; f <= 2 * last; f++) cache.BeginFrame(f);
    EXPECT_EQ(cache.Size(), 0u);
    EXPECT_GE(cache.Evictions(), 2u);
}

TEST(BindGroupCache, BoundedByMaxEntries) {
    BindGroupCache cache;
    CountingCreator create;
    cache.BeginFrame(1);
    for (uint64_t i = 0; i < BindGroupCache::kMaxEntries + 10; i++) {
        NullDesc d(i * 256);
        cache.GetOrCreate(d.desc, std::ref(create));
        ASSERT_LE(cache.Size(), BindGroupCache::kMaxEntries);
    }
    cache.Clear();
    EXPECT_EQ(cache.Size(), 0u);
}

// ---------------------------------------------------------------------------
// With a device: real handles, and the WgpuRnd mesh path.
// ---------------------------------------------------------------------------

namespace {

class BindGroupCacheGpuTest : public EngineTestFixture {
protected:
    static bool HaveGpu() { return gWgpuRnd && gWgpuRnd->Gpu().IsReady(); }

    static wgpu::TextureView MakeView(uint32_t w) {
        wgpu::TextureDescriptor d{};
        d.size = {w, 1, 1};
        d.format = wgpu::TextureFormat::RGBA8Unorm;
        d.usage = wgpu::TextureUsage::TextureBinding;
        return gWgpuRnd->Gpu().Device().CreateTexture(&d).CreateView();
    }
    static wgpu::Sampler MakeSampler(wgpu::FilterMode f) {
        wgpu::SamplerDescriptor d{};
        d.magFilter = f;
        return gWgpuRnd->Gpu().Device().CreateSampler(&d);
    }
};

} // namespace

TEST_F(BindGroupCacheGpuTest, ForgetDropsTheGroupsThatBindAView) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    wgpu::Device dev = gWgpuRnd->Gpu().Device();
    wgpu::BindGroupLayoutEntry le{};
    le.binding = 0;
    le.visibility = wgpu::ShaderStage::Fragment;
    le.texture.sampleType = wgpu::TextureSampleType::Float;
    le.texture.viewDimension = wgpu::TextureViewDimension::e2D;
    wgpu::BindGroupLayoutDescriptor ld{};
    ld.entryCount = 1;
    ld.entries = &le;
    wgpu::BindGroupLayout layout = dev.CreateBindGroupLayout(&ld);

    wgpu::TextureView v1 = MakeView(4), v2 = MakeView(8);
    auto lookup = [&](BindGroupCache &cache, const wgpu::TextureView &v) {
        wgpu::BindGroupEntry e{};
        e.binding = 0;
        e.textureView = v;
        wgpu::BindGroupDescriptor d{};
        d.layout = layout;
        d.entryCount = 1;
        d.entries = &e;
        return cache.GetOrCreate(dev, d);
    };
    BindGroupCache cache;
    cache.BeginFrame(1);
    wgpu::BindGroup g1 = lookup(cache, v1);
    wgpu::BindGroup g2 = lookup(cache, v2);
    ASSERT_TRUE(g1 && g2);
    EXPECT_EQ(lookup(cache, v1).Get(), g1.Get()) << "same view, same group";
    EXPECT_NE(g1.Get(), g2.Get());

    cache.Forget(v1.Get());
    // Until the next frame the entry still answers, through the same live view.
    EXPECT_EQ(lookup(cache, v1).Get(), g1.Get());
    cache.BeginFrame(2);
    EXPECT_EQ(cache.Size(), 1u) << "the forgotten view's group is still cached";
    EXPECT_EQ(lookup(cache, v2).Get(), g2.Get()) << "an unrelated group was dropped";
    const uint64_t creates = cache.Creates();
    lookup(cache, v1);
    EXPECT_EQ(cache.Creates(), creates + 1) << "the forgotten view's group was found again";
}

// Through WgpuRnd::MaterialBindGroup, as Mesh_Wgpu calls it: every view slot
// and both samplers separate groups; the same resources share one.
TEST_F(BindGroupCacheGpuTest, MaterialGroupSeparatesEveryBoundResource) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    WgpuRnd::MaterialTexViews views;
    views.diffuse = MakeView(2);
    views.normal = MakeView(3);
    views.specular = MakeView(4);
    views.emissive = MakeView(5);
    views.rim = MakeView(6);
    views.environCube = gWgpuRnd->BlackCubeTexView();
    views.normDetail = MakeView(7);
    views.refractNormal = MakeView(9);
    wgpu::Sampler s1 = MakeSampler(wgpu::FilterMode::Linear);
    wgpu::Sampler s2 = MakeSampler(wgpu::FilterMode::Nearest);

    wgpu::BindGroup base = gWgpuRnd->MaterialBindGroup(views, s1, s2);
    ASSERT_TRUE(base);
    EXPECT_EQ(gWgpuRnd->MaterialBindGroup(views, s1, s2).Get(), base.Get());

    struct Slot {
        const char *name;
        wgpu::TextureView WgpuRnd::MaterialTexViews::*member;
    };
    const Slot slots[] = {
        {"diffuse", &WgpuRnd::MaterialTexViews::diffuse},
        {"normal", &WgpuRnd::MaterialTexViews::normal},
        {"specular", &WgpuRnd::MaterialTexViews::specular},
        {"emissive", &WgpuRnd::MaterialTexViews::emissive},
        {"rim", &WgpuRnd::MaterialTexViews::rim},
        {"normDetail", &WgpuRnd::MaterialTexViews::normDetail},
        {"refractNormal", &WgpuRnd::MaterialTexViews::refractNormal},
    };
    wgpu::TextureView other = MakeView(11);
    for (const Slot &slot : slots) {
        WgpuRnd::MaterialTexViews v = views;
        v.*slot.member = other;
        EXPECT_NE(gWgpuRnd->MaterialBindGroup(v, s1, s2).Get(), base.Get())
            << slot.name << " view does not separate material groups";
    }
    {
        WgpuRnd::MaterialTexViews v = views;
        wgpu::TextureViewDescriptor cd{};
        cd.dimension = wgpu::TextureViewDimension::Cube;
        wgpu::TextureDescriptor td{};
        td.size = {1, 1, 6};
        td.format = wgpu::TextureFormat::RGBA8Unorm;
        td.usage = wgpu::TextureUsage::TextureBinding;
        v.environCube = gWgpuRnd->Gpu().Device().CreateTexture(&td).CreateView(&cd);
        EXPECT_NE(gWgpuRnd->MaterialBindGroup(v, s1, s2).Get(), base.Get())
            << "environCube view does not separate material groups";
    }
    EXPECT_NE(gWgpuRnd->MaterialBindGroup(views, s2, s2).Get(), base.Get())
        << "diffuse sampler does not separate material groups";
    EXPECT_NE(gWgpuRnd->MaterialBindGroup(views, s1, s1).Get(), base.Get())
        << "map sampler does not separate material groups";
}

#ifdef MILO_READBACK_GPU_RENDER_TARGET
namespace {

// A quad at y = 10 in front of an identity camera (Milo: +y forward, +z up),
// spanning x0..x1 and all of z, so it fills that side of the target.
RndMesh *MakeQuad(float x0, float x1, RndMat *mat) {
    RndMesh *mesh = Hmx::Object::New<RndMesh>();
    mesh->SetName(x0 < 0 ? "bgcache_left.mesh" : "bgcache_right.mesh", ObjectDir::Main());
    mesh->Verts().resize(4);
    const float xs[4] = {x0, x1, x1, x0};
    const float zs[4] = {-100.0f, -100.0f, 100.0f, 100.0f};
    for (int i = 0; i < 4; i++) {
        RndMesh::Vert &v = mesh->Verts()[i];
        v.pos.Set(xs[i], 10.0f, zs[i]);
        v.norm.Set(0.0f, -1.0f, 0.0f);
        v.color = Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f);
    }
    mesh->Faces().resize(2);
    mesh->Faces()[0].Set(0, 1, 2);
    mesh->Faces()[1].Set(0, 2, 3);
    mesh->SetMat(mat);
    mesh->Sync(0x3f);
    return mesh;
}

RndMat *MakeFlatMat(const Hmx::Color &c) {
    RndMat *mat = Hmx::Object::New<RndMat>();
    mat->SetColor(c.red, c.green, c.blue);
    mat->SetAlpha(1.0f);
    mat->SetBlend(BaseMaterial::kBlendSrc);
    mat->SetPreLit(true);
    mat->SetZMode(kZModeDisable);
    mat->SetCull(kCullNone);
    return mat;
}

struct Px {
    int r = -1, g = -1, b = -1;
};

} // namespace

// Two draws whose material groups bind the same resources share one cached
// group; only their dynamic offsets differ. Each must still show its own
// colour, the frame after must show the new colours (the staged ring was
// flushed again, not left at the old data), and that second frame must not
// create a single material, object or bone group.
TEST_F(BindGroupCacheGpuTest, TwoDrawsSharingAGroupKeepTheirOwnUniforms) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    RndTex *tex = Hmx::Object::New<RndTex>();
    tex->SetBitmap(64, 48, 32, RndTex::kRendered, false, nullptr);
    RndCam *cam = Hmx::Object::New<RndCam>();
    cam->SetFrustum(1.0f, 1000.0f, 1.0f, 0.75f);
    cam->SetTargetTex(tex);
    RndMat *left = MakeFlatMat(Hmx::Color(1.0f, 0.0f, 0.0f));
    RndMat *right = MakeFlatMat(Hmx::Color(0.0f, 1.0f, 0.0f));
    RndMesh *qLeft = MakeQuad(-1000.0f, 0.0f, left);
    RndMesh *qRight = MakeQuad(0.0f, 1000.0f, right);

    auto drawAndRead = [&](Px &l, Px &r) {
        TheRnd.BeginDrawing();
        RndCam *prev = RndCam::Current();
        cam->Select();
        qLeft->DrawShowing();
        qRight->DrawShowing();
        if (prev) prev->Select();
        TheRnd.EndDrawing();
        std::vector<uint8_t> px;
        int w = 0, h = 0;
        if (!ReadbackGpuRenderTarget(tex, px, w, h) || w != 64 || h != 48) return false;
        auto at = [&](int x, Px &out) {
            const uint8_t *c = &px[((h / 2) * w + x) * 4];
            out.r = c[0];
            out.g = c[1];
            out.b = c[2];
            if (out.r < out.b) std::swap(out.r, out.b);  // BGRA target
        };
        at(w / 4, l);
        at(3 * w / 4, r);
        return true;
    };

    Px l, r;
    // A first frame settles what persists between frames (the pre-process
    // buffer the material group binds as the refraction frame).
    ASSERT_TRUE(drawAndRead(l, r)) << "no GPU render target";
    ASSERT_TRUE(drawAndRead(l, r));
    // Red is in r (or swapped into r from b for BGRA); the green channel is
    // never swapped.
    EXPECT_GE(l.r, 250) << "left quad " << l.r << "," << l.g << "," << l.b;
    EXPECT_LE(l.g, 5) << "left quad " << l.r << "," << l.g << "," << l.b;
    EXPECT_GE(r.g, 250) << "right quad " << r.r << "," << r.g << "," << r.b;
    EXPECT_LE(r.r, 5) << "right quad " << r.r << "," << r.g << "," << r.b;

    // Swap the colours; same textures and samplers, so the same groups.
    left->SetColor(0.0f, 1.0f, 0.0f);
    right->SetColor(1.0f, 0.0f, 0.0f);
    const uint64_t creates = gWgpuRnd->BindGroups().Creates();
    ASSERT_TRUE(drawAndRead(l, r));
    EXPECT_EQ(gWgpuRnd->BindGroups().Creates(), creates)
        << "a frame drawing what the last one drew created bind groups";
    EXPECT_GE(l.g, 250) << "left quad kept last frame's colour: " << l.r << "," << l.g << "," << l.b;
    EXPECT_LE(l.r, 5) << "left quad " << l.r << "," << l.g << "," << l.b;
    EXPECT_GE(r.r, 250) << "right quad kept last frame's colour: " << r.r << "," << r.g << "," << r.b;
    EXPECT_LE(r.g, 5) << "right quad " << r.r << "," << r.g << "," << r.b;

    cam->SetTargetTex(nullptr);
    delete qRight;
    delete qLeft;
    delete right;
    delete left;
    delete cam;
    delete tex;
}
#endif // MILO_READBACK_GPU_RENDER_TARGET

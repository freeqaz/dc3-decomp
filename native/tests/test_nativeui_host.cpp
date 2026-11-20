// nativeui_host without a GPU and without game data: the command translation
// (through a recording sink), glyph layout from a hand-built bitmap font, the
// image cache against a fake loader, the input mapping and repeat timing, and
// the host's text capture and event routing with a two-widget test screen,
// and the host running the real song browser screen.
//
// The GPU and running-engine halves are test_nativeui_host_gpu.cpp.

#include <gtest/gtest.h>

#include "nativeui/Label.h"
#include "nativeui/Layout.h"
#include "nativeui/ListView.h"
#include "nativeui/Screen.h"
#include "nativeui/TextField.h"
#include "nativeui/Text.h"
#include "nativeui_host/BitmapGlyphSource.h"
#include "nativeui_host/CanvasRenderer.h"
#include "nativeui_host/FontSet.h"
#include "nativeui_host/ImageCache.h"
#include "nativeui_host/InputBridge.h"
#include "nativeui_host/NativeUiHost.h"
#include "platform/TextInput_Native.h"
#include "songbrowser/SongCatalog.h"
#include "songbrowser_ui/SongBrowserScreen.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

using namespace nativeui_host;

namespace {

    // Stand-ins for textures: the code under test only carries the pointers.
    char gTexels[64];
    RndTex *FakeTex(int n) { return reinterpret_cast<RndTex *>(&gTexels[n]); }

    struct RecordingSink : QuadSink {
        struct Op {
            char kind; // 'q' quad, '+' push, '-' pop
            Canvas2DQuad quad;
            Canvas2DRect clip;
        };
        std::vector<Op> ops;
        void Quad(const Canvas2DQuad &quad) override { ops.push_back({ 'q', quad, {} }); }
        void PushClip(const Canvas2DRect &rect) override {
            ops.push_back({ '+', Canvas2DQuad(), rect });
        }
        void PopClip() override { ops.push_back({ '-', Canvas2DQuad(), {} }); }
        std::vector<Canvas2DQuad> Quads() const {
            std::vector<Canvas2DQuad> quads;
            for (const Op &op : ops) {
                if (op.kind == 'q') quads.push_back(op.quad);
            }
            return quads;
        }
        std::string Kinds() const {
            std::string kinds;
            for (const Op &op : ops) kinds += op.kind;
            return kinds;
        }
    };

    BitmapGlyph Glyph(float u0, float width, float advance) {
        BitmapGlyph glyph;
        glyph.page = 0;
        glyph.u0 = u0;
        glyph.v0 = 0.25f;
        glyph.u1 = u0 + 0.05f;
        glyph.v1 = 0.5f;
        glyph.width = width;
        glyph.advance = advance;
        return glyph;
    }

    // A 10 x 20 texel cell, baseline three quarters down.  At size 40 one
    // texel is 2 logical units and a cell width is 20.
    BitmapFontData TestFont() {
        BitmapFontData data;
        data.cellWidth = 10.0f;
        data.cellHeight = 20.0f;
        data.capTop = 0.25f;
        data.baseline = 0.75f;
        data.descenderBottom = 0.95f;
        data.pages.push_back(FakeTex(1));
        data.glyphs[U'A'] = Glyph(0.10f, 0.6f, 0.7f);
        data.glyphs[U'B'] = Glyph(0.20f, 0.5f, 0.5f);
        data.glyphs[U'e'] = Glyph(0.30f, 0.4f, 0.45f);
        data.glyphs[U' '] = Glyph(0.40f, 0.0f, 0.3f);
        return data;
    }

    const nativeui::FontId kFont { 1 };

    struct FontRig {
        BitmapGlyphSource source;
        FontSet fonts;
        explicit FontRig(BitmapFontData data = TestFont()) : source(std::move(data)) {
            fonts.Set(kFont, &source);
        }
    };

    nativeui::TextRun TextAt(const std::string &text, float x, float y, float size, float ls = 0) {
        nativeui::TextRun run;
        run.origin = { x, y };
        run.font = kFont;
        run.size = size;
        run.color = { 0.2f, 0.4f, 0.6f, 0.8f };
        run.letterSpacing = ls;
        run.text = text;
        return run;
    }

} // namespace

// ---------------------------------------------------------------------------
// Command translation
// ---------------------------------------------------------------------------

TEST(NativeUiHostRenderer, FillIsOneQuadWithItsCornersColoursAndRadius) {
    FontRig rig;
    RecordingSink sink;
    CanvasRenderer renderer(sink, rig.fonts, nullptr);
    nativeui::RoundRectFill fill;
    fill.rect = { 10, 20, 300, 40 };
    fill.radius = 7;
    fill.colors = { { 1, 0, 0, 1 }, { 0, 1, 0, 1 }, { 0, 0, 1, 1 }, { 1, 1, 0, 0.5f } };
    renderer.Draw(fill);
    ASSERT_EQ(sink.Quads().size(), 1u);
    const Canvas2DQuad q = sink.Quads()[0];
    EXPECT_FLOAT_EQ(q.rect.x, 10);
    EXPECT_FLOAT_EQ(q.rect.y, 20);
    EXPECT_FLOAT_EQ(q.rect.w, 300);
    EXPECT_FLOAT_EQ(q.rect.h, 40);
    EXPECT_FLOAT_EQ(q.radius, 7);
    EXPECT_FLOAT_EQ(q.border, 0) << "a fill is not a ring";
    EXPECT_EQ(q.tex, nullptr);
    // nativeui lists corners clockwise (TL, TR, BR, BL); the canvas by row.
    EXPECT_FLOAT_EQ(q.tl.r, 1);
    EXPECT_FLOAT_EQ(q.tr.g, 1);
    EXPECT_FLOAT_EQ(q.br.b, 1);
    EXPECT_FLOAT_EQ(q.bl.r, 1);
    EXPECT_FLOAT_EQ(q.bl.g, 1);
    EXPECT_FLOAT_EQ(q.bl.a, 0.5f);
    EXPECT_FLOAT_EQ(q.br.r, 0);
}

// nativeui's softness 0 means "a crisp antialiased edge"; the canvas's 0 means
// "hard, aliased".  The canvas's antialiased edge is any negative softness.
TEST(NativeUiHostRenderer, ZeroSoftnessIsTheCanvasAntialiasedEdgeAndAGlowKeepsItsWidth) {
    FontRig rig;
    RecordingSink sink;
    CanvasRenderer renderer(sink, rig.fonts, nullptr);
    nativeui::RoundRectFill crisp;
    crisp.rect = { 0, 0, 10, 10 };
    renderer.Draw(crisp);
    nativeui::RoundRectFill glow = crisp;
    glow.softness = 18;
    renderer.Draw(glow);
    nativeui::RoundRectStroke ring;
    ring.rect = { 0, 0, 10, 10 };
    ring.thickness = 2;
    renderer.Draw(ring);
    ASSERT_EQ(sink.Quads().size(), 3u);
    EXPECT_LT(sink.Quads()[0].softness, 0.0f) << "softness 0 must not reach the canvas as 0";
    EXPECT_FLOAT_EQ(sink.Quads()[1].softness, 18);
    EXPECT_LT(sink.Quads()[2].softness, 0.0f);
}

TEST(NativeUiHostRenderer, StrokeIsARingInsideTheSameRectangle) {
    FontRig rig;
    RecordingSink sink;
    CanvasRenderer renderer(sink, rig.fonts, nullptr);
    nativeui::RoundRectStroke stroke;
    stroke.rect = { 5, 6, 100, 50 };
    stroke.radius = 9;
    stroke.thickness = 3;
    stroke.colors = nativeui::CornerColors::Solid({ 1, 1, 1, 1 });
    renderer.Draw(stroke);
    ASSERT_EQ(sink.Quads().size(), 1u);
    const Canvas2DQuad q = sink.Quads()[0];
    EXPECT_FLOAT_EQ(q.border, 3) << "the band lies inside the edge: border = thickness";
    EXPECT_FLOAT_EQ(q.rect.x, 5) << "the rectangle is not grown or shrunk for the band";
    EXPECT_FLOAT_EQ(q.rect.w, 100);
    EXPECT_FLOAT_EQ(q.rect.h, 50);
    EXPECT_FLOAT_EQ(q.radius, 9);

    // No thickness: nothing (a border of 0 would be a FILL on the canvas).
    stroke.thickness = 0;
    renderer.Draw(stroke);
    EXPECT_EQ(sink.Quads().size(), 1u);
}

TEST(NativeUiHostRenderer, ClipsReachTheCanvasUnchangedAndInOrder) {
    FontRig rig;
    RecordingSink sink;
    CanvasRenderer renderer(sink, rig.fonts, nullptr);
    nativeui::RoundRectFill fill;
    fill.rect = { 0, 0, 10, 10 };
    renderer.Draw(fill);
    renderer.PushClip({ 40, 50, 200, 100 });
    renderer.Draw(fill);
    renderer.PushClip({ 60, 70, 20, 30 });
    renderer.Draw(fill);
    renderer.PopClip();
    renderer.PopClip();
    renderer.Draw(fill);
    EXPECT_EQ(sink.Kinds(), "q+q+q--q");
    EXPECT_FLOAT_EQ(sink.ops[1].clip.x, 40);
    EXPECT_FLOAT_EQ(sink.ops[1].clip.y, 50);
    EXPECT_FLOAT_EQ(sink.ops[1].clip.w, 200);
    EXPECT_FLOAT_EQ(sink.ops[1].clip.h, 100);
    EXPECT_FLOAT_EQ(sink.ops[3].clip.x, 60);
    EXPECT_FLOAT_EQ(sink.ops[3].clip.h, 30);
}

namespace {
    struct OneImage : ImageSource {
        RndTex *TextureFor(nativeui::ImageHandle image) override {
            return image.id == 7 ? FakeTex(9) : nullptr;
        }
    };
}

TEST(NativeUiHostRenderer, ImageIsATexturedQuadAndAnUnresolvedHandleDrawsNothing) {
    FontRig rig;
    RecordingSink sink;
    OneImage images;
    CanvasRenderer renderer(sink, rig.fonts, &images);
    nativeui::ImageQuad image;
    image.rect = { 10, 10, 64, 64 };
    image.image = { 7 };
    image.uv = { 0.25f, 0.5f, 0.5f, 0.25f }; // x, y, width, height
    image.tint = { 1, 1, 1, 0.5f };
    image.radius = 4;
    renderer.Draw(image);
    ASSERT_EQ(sink.Quads().size(), 1u);
    const Canvas2DQuad q = sink.Quads()[0];
    EXPECT_EQ(q.tex, FakeTex(9));
    EXPECT_EQ(q.texMode, Canvas2DTexMode::kColor);
    EXPECT_FLOAT_EQ(q.u0, 0.25f);
    EXPECT_FLOAT_EQ(q.v0, 0.5f);
    EXPECT_FLOAT_EQ(q.u1, 0.75f);
    EXPECT_FLOAT_EQ(q.v1, 0.75f);
    EXPECT_FLOAT_EQ(q.tl.a, 0.5f);
    EXPECT_FLOAT_EQ(q.radius, 4);

    image.image = { 8 }; // nothing behind it yet
    renderer.Draw(image);
    EXPECT_EQ(sink.Quads().size(), 1u) << "an untextured quad would be a white square";
    EXPECT_EQ(renderer.GetCounts().imagesMissing, 1u);
}

// ---------------------------------------------------------------------------
// Glyph layout
// ---------------------------------------------------------------------------

TEST(NativeUiHostGlyphs, RunStartsAtTheLeftEndOfTheBaseline) {
    FontRig rig;
    RecordingSink sink;
    CanvasRenderer renderer(sink, rig.fonts, nullptr);
    renderer.Draw(TextAt("AB", 100, 200, 40));
    ASSERT_EQ(sink.Quads().size(), 2u);
    const Canvas2DQuad a = sink.Quads()[0];
    const Canvas2DQuad b = sink.Quads()[1];
    // Size 40 = the 20-texel cell: 2 units a texel, 20 a cell width.  The
    // baseline is 0.75 of the cell down, so the cell's top is 30 above y.
    EXPECT_FLOAT_EQ(a.rect.x, 100);
    EXPECT_FLOAT_EQ(a.rect.y, 170);
    EXPECT_FLOAT_EQ(a.rect.w, 12); // 0.6 cell widths
    EXPECT_FLOAT_EQ(a.rect.h, 40);
    EXPECT_FLOAT_EQ(b.rect.x, 114); // A advances 0.7 cell widths
    EXPECT_FLOAT_EQ(b.rect.w, 10);
    EXPECT_EQ(a.tex, FakeTex(1));
    EXPECT_EQ(a.texMode, Canvas2DTexMode::kCoverageA) << "a text face is a flat tint";
    EXPECT_FLOAT_EQ(a.u0, 0.10f);
    EXPECT_FLOAT_EQ(a.v0, 0.25f);
    EXPECT_FLOAT_EQ(a.u1, 0.15f);
    EXPECT_FLOAT_EQ(a.v1, 0.5f);
    EXPECT_FLOAT_EQ(b.u0, 0.20f);
    EXPECT_FLOAT_EQ(a.tl.b, 0.6f);
    EXPECT_FLOAT_EQ(a.br.a, 0.8f);
    EXPECT_FLOAT_EQ(a.softness, 0) << "a glyph's edge is in its texels, not the quad's";
    EXPECT_EQ(renderer.GetCounts().glyphs, 2u);
}

TEST(NativeUiHostGlyphs, AdvancesSumToWhatTheMeasurerSays) {
    FontRig rig;
    const std::string text = "AB eA";
    std::vector<GlyphQuad> quads;
    const float pen = rig.source.Layout(40, 0, text, quads);
    // 0.7 + 0.5 + 0.3 + 0.45 + 0.7 cell widths of 20.
    EXPECT_FLOAT_EQ(pen, 53);
    EXPECT_FLOAT_EQ(rig.source.Advance(40, text), pen);
    EXPECT_FLOAT_EQ(rig.fonts.AdvanceWidth(kFont, 40, text), pen);
    ASSERT_EQ(quads.size(), 4u) << "the space has no quad";
    EXPECT_FLOAT_EQ(quads[3].x, 39) << "the last glyph starts after the four before it";

    // The widget side: MeasureWidth adds tracking between glyphs.
    nativeui::TextStyle style;
    style.font = kFont;
    style.size = 40;
    style.tracking = 0.05f; // 2 units
    EXPECT_FLOAT_EQ(nativeui::MeasureWidth(rig.fonts, style, text), 53 + 4 * 2);
}

TEST(NativeUiHostGlyphs, LetterSpacingIsTheSameInTheMeasurerAndTheRenderer) {
    FontRig rig;
    RecordingSink sink;
    CanvasRenderer renderer(sink, rig.fonts, nullptr);
    renderer.Draw(TextAt("ABA", 0, 100, 40, 3));
    ASSERT_EQ(sink.Quads().size(), 3u);
    EXPECT_FLOAT_EQ(sink.Quads()[1].rect.x, 14 + 3);
    EXPECT_FLOAT_EQ(sink.Quads()[2].rect.x, 14 + 3 + 10 + 3);

    std::vector<GlyphQuad> quads;
    const float pen = rig.source.Layout(40, 3, "ABA", quads);
    nativeui::TextStyle style;
    style.font = kFont;
    style.size = 40;
    style.tracking = 3.0f / 40.0f;
    EXPECT_FLOAT_EQ(pen, nativeui::MeasureWidth(rig.fonts, style, "ABA"))
        << "none after the last glyph: the run is exactly as wide as it was measured";
    EXPECT_FLOAT_EQ(pen, 14 + 10 + 14 + 2 * 3);
}

TEST(NativeUiHostGlyphs, KerningMovesTheGlyphAndTheMeasure) {
    BitmapFontData data = TestFont();
    data.pairKerning[(std::uint32_t)U'A' | ((std::uint32_t)U'B' << 16)] = -0.1f;
    FontRig kerned(data);
    std::vector<GlyphQuad> quads;
    EXPECT_FLOAT_EQ(kerned.source.Layout(40, 0, "AB", quads), 22); // 24 - 0.1 cell
    ASSERT_EQ(quads.size(), 2u);
    EXPECT_FLOAT_EQ(quads[1].x, 12);
    EXPECT_FLOAT_EQ(kerned.source.Advance(40, "AB"), 22);
    EXPECT_FLOAT_EQ(kerned.source.Advance(40, "BA"), 24) << "the pair is ordered";

    // Base kerning comes before every glyph, the first included (RndText).
    data = TestFont();
    data.baseKerning = 0.05f; // 1 unit
    FontRig based(data);
    quads.clear();
    EXPECT_FLOAT_EQ(based.source.Layout(40, 0, "AB", quads), 26);
    EXPECT_FLOAT_EQ(quads[0].x, 1);
    EXPECT_FLOAT_EQ(quads[1].x, 16);
}

TEST(NativeUiHostGlyphs, MissingGlyphFallsBackToItsBaseLetterThenToABox) {
    FontRig rig;
    // e-acute is not in the font, e is.
    EXPECT_FALSE(rig.source.HasGlyph(U'é'));
    const BitmapGlyph *folded = rig.source.Resolve(U'é');
    ASSERT_NE(folded, nullptr);
    EXPECT_FLOAT_EQ(folded->u0, 0.30f);
    // E-acute: base letter E is not there either, its other case is.
    ASSERT_NE(rig.source.Resolve(U'É'), nullptr);
    EXPECT_FLOAT_EQ(rig.source.Resolve(U'É')->u0, 0.30f);
    // A typographic apostrophe and a snowman have nothing: the box.
    EXPECT_EQ(rig.source.Resolve(U'☃'), nullptr);

    RecordingSink sink;
    CanvasRenderer renderer(sink, rig.fonts, nullptr);
    renderer.Draw(TextAt("A\xE2\x98\x83" "B", 0, 100, 40)); // A, snowman, B
    ASSERT_EQ(sink.Quads().size(), 3u);
    const Canvas2DQuad box = sink.Quads()[1];
    EXPECT_EQ(box.tex, nullptr) << "the placeholder must not sample the atlas";
    EXPECT_GT(box.border, 0.0f) << "an outlined box, not a solid block";
    EXPECT_GT(box.rect.w, 0.0f);
    EXPECT_GE(box.rect.x, 14.0f);
    EXPECT_LE(box.rect.x + box.rect.w, 14.0f + 20.0f) << "inside its own advance";
    EXPECT_LE(box.rect.y + box.rect.h, 100.0f + 0.01f) << "sits on the baseline";
    // The box advances half the size; B follows it.
    EXPECT_FLOAT_EQ(sink.Quads()[2].rect.x, 14 + BitmapGlyphSource::kPlaceholderAdvance * 40);
    EXPECT_EQ(renderer.GetCounts().placeholders, 1u);
    EXPECT_FLOAT_EQ(rig.source.Advance(40, "A\xE2\x98\x83" "B"), 14 + 20 + 10);

    // Bytes that are not UTF-8 at all: no crash, and the measure is finite.
    const float garbage = rig.source.Advance(40, "\xFF\xFE" "A\xC3");
    EXPECT_GE(garbage, 14.0f);
    EXPECT_LT(garbage, 200.0f);
}

TEST(NativeUiHostGlyphs, BaselineSnapsToADevicePixelRowAndXDoesNot) {
    FontRig rig;
    RecordingSink sink;
    CanvasRenderer renderer(sink, rig.fonts, nullptr);
    renderer.SetDeviceScale(1.5f); // 1080 rows
    renderer.Draw(TextAt("A", 10.3f, 100.3f, 40));
    ASSERT_EQ(sink.Quads().size(), 1u);
    // 100.3 * 1.5 = 150.45 -> row 150 -> 100 logical; the cell top is 30 above.
    EXPECT_NEAR(sink.Quads()[0].rect.y, 70.0f, 1e-4f);
    EXPECT_FLOAT_EQ(sink.Quads()[0].rect.x, 10.3f);
}

TEST(NativeUiHostGlyphs, MetricsComeFromTheMeasuredCell) {
    FontRig rig;
    const nativeui::FontMetrics m = rig.fonts.Metrics(kFont, 40);
    EXPECT_FLOAT_EQ(m.ascent, 20); // cap top 0.25 .. baseline 0.75
    EXPECT_FLOAT_EQ(m.descent, 8); // baseline .. 0.95
    EXPECT_FLOAT_EQ(m.lineHeight, 40);
    // An id nobody registered measures as the default font, not as nothing.
    FontSet fonts;
    fonts.SetDefault(&rig.source);
    EXPECT_FLOAT_EQ(fonts.AdvanceWidth(nativeui::FontId { 99 }, 40, "A"), 14);
}

// ---------------------------------------------------------------------------
// Image cache
// ---------------------------------------------------------------------------

namespace {

    struct FakeLoader : ImageLoader {
        struct Job {
            std::string path;
            Status status = kPending;
            bool finished = false;
            bool cancelled = false;
        };
        std::map<int, Job> jobs;
        int next = 1;
        int texturesMade = 0;
        std::vector<RndTex *> destroyed;
        std::vector<std::string> begun;

        int Begin(const std::string &path) override {
            jobs[next].path = path;
            begun.push_back(path);
            return next++;
        }
        Status Poll(int job) override { return jobs[job].status; }
        RndTex *Finish(int job) override {
            jobs[job].finished = true;
            return FakeTex(++texturesMade);
        }
        void Cancel(int job) override { jobs[job].cancelled = true; }
        void Destroy(RndTex *texture) override { destroyed.push_back(texture); }

        int InFlight() const {
            int n = 0;
            for (const auto &job : jobs) {
                if (!job.second.finished && !job.second.cancelled
                    && job.second.status == kPending)
                    n++;
            }
            return n;
        }
        void Complete(const std::string &path, Status status = kDone) {
            for (auto &job : jobs) {
                if (job.second.path == path && !job.second.finished) job.second.status = status;
            }
        }
        void CompleteAll() {
            for (auto &job : jobs) {
                if (!job.second.finished) job.second.status = kDone;
            }
        }
    };

    ImageCache::Config SmallCache(int capacity) {
        ImageCache::Config config;
        config.capacity = capacity;
        config.maxConcurrent = 2;
        config.maxFinishPerPoll = 8;
        return config;
    }

} // namespace

TEST(NativeUiHostImages, ARequestGoesQueuedLoadingReadyAndAnnouncesItsArrivalOnce) {
    FakeLoader loader;
    ImageCache cache(loader, SmallCache(8));
    std::vector<std::string> arrived;
    cache.onArrived = [&](nativeui::ImageHandle, const std::string &path) {
        arrived.push_back(path);
    };
    const nativeui::ImageHandle a = cache.Request("songs/a/a_keep.png");
    EXPECT_FALSE(a.IsNull());
    EXPECT_EQ(cache.Request("songs/a/a_keep.png"), a) << "one handle per path, stable";
    EXPECT_EQ(cache.StateOf(a), ImageCache::kQueued);
    EXPECT_TRUE(loader.begun.empty()) << "Request does no I/O itself";
    EXPECT_EQ(cache.TextureFor(a), nullptr);

    cache.Poll();
    EXPECT_EQ(cache.StateOf(a), ImageCache::kLoading);
    EXPECT_EQ(loader.begun.size(), 1u);
    cache.Poll();
    EXPECT_EQ(cache.StateOf(a), ImageCache::kLoading) << "not done until the loader says so";
    EXPECT_TRUE(arrived.empty());

    loader.Complete("songs/a/a_keep.png");
    cache.Poll();
    EXPECT_EQ(cache.StateOf(a), ImageCache::kReady);
    EXPECT_NE(cache.TextureFor(a), nullptr);
    ASSERT_EQ(arrived.size(), 1u);
    EXPECT_EQ(arrived[0], "songs/a/a_keep.png");
    cache.Poll();
    cache.Poll();
    EXPECT_EQ(arrived.size(), 1u);
    EXPECT_EQ(cache.StateOf(nativeui::ImageHandle { 999 }), ImageCache::kUnknown);
    EXPECT_EQ(cache.TextureFor(nativeui::ImageHandle {}), nullptr);
}

TEST(NativeUiHostImages, AFailedLoadShowsTheFallbackAndCountsAsAFailure) {
    FakeLoader loader;
    ImageCache cache(loader, SmallCache(8));
    cache.SetFallback("ui/image/dc_logo_keep.png");
    const nativeui::ImageHandle bad = cache.Request("songs/gone/gone_keep.png");
    cache.Poll();
    loader.Complete("ui/image/dc_logo_keep.png");
    loader.Complete("songs/gone/gone_keep.png", ImageLoader::kFailed);
    cache.Poll();
    EXPECT_EQ(cache.StateOf(bad), ImageCache::kFailed);
    EXPECT_EQ(cache.GetStats().failures, 1);
    RndTex *shown = cache.TextureFor(bad);
    EXPECT_NE(shown, nullptr) << "a failed picture draws the fallback, not nothing";
    EXPECT_FALSE(cache.IsReady(bad));
    // It is not retried every frame.
    const std::size_t begun = loader.begun.size();
    cache.Poll();
    cache.Poll();
    EXPECT_EQ(loader.begun.size(), begun);
}

TEST(NativeUiHostImages, LoadsInFlightAndTexturesMadePerPollAreBounded) {
    FakeLoader loader;
    ImageCache::Config config;
    config.capacity = 32;
    config.maxConcurrent = 3;
    config.maxFinishPerPoll = 2;
    ImageCache cache(loader, config);
    std::vector<nativeui::ImageHandle> handles;
    for (int i = 0; i < 10; i++) {
        handles.push_back(cache.Request("art" + std::to_string(i)));
    }
    cache.Poll();
    EXPECT_EQ(loader.begun.size(), 3u) << "no more than maxConcurrent loads at once";
    EXPECT_EQ(loader.InFlight(), 3);
    loader.CompleteAll();
    cache.Poll();
    EXPECT_EQ(loader.texturesMade, 2) << "no more than maxFinishPerPoll textures a frame";
    EXPECT_LE(loader.InFlight(), 3);
    for (int i = 0; i < 20; i++) {
        loader.CompleteAll();
        cache.Poll();
        EXPECT_LE(loader.InFlight(), 3);
    }
    EXPECT_EQ(cache.GetStats().ready, 10);
    EXPECT_EQ(loader.begun[0], "art0") << "oldest request first";
}

TEST(NativeUiHostImages, TheCacheHoldsNoMoreThanItsCapacityAndDropsTheLeastRecentlyUsed) {
    FakeLoader loader;
    ImageCache cache(loader, SmallCache(3));
    std::vector<nativeui::ImageHandle> handles;
    for (int i = 0; i < 6; i++) {
        handles.push_back(cache.Request("art" + std::to_string(i)));
        // A frame: the newest picture is on screen, so is picture 0.
        for (int frame = 0; frame < 3; frame++) {
            loader.CompleteAll();
            cache.Poll();
            cache.TextureFor(handles[i]);
            cache.TextureFor(handles[0]);
            EXPECT_LE(cache.GetStats().ready, 3) << "after request " << i;
        }
    }
    EXPECT_EQ(cache.GetStats().ready, 3);
    EXPECT_TRUE(cache.IsReady(handles[0])) << "used every frame: never the oldest";
    EXPECT_TRUE(cache.IsReady(handles[5]));
    EXPECT_EQ(cache.StateOf(handles[1]), ImageCache::kEvicted);
    EXPECT_EQ(cache.StateOf(handles[2]), ImageCache::kEvicted);
    EXPECT_GE(cache.GetStats().evictions, 3);
    EXPECT_EQ(cache.TextureFor(handles[1]), nullptr);

    // An evicted picture asked for again loads again, under the same handle.
    const std::size_t begun = loader.begun.size();
    EXPECT_EQ(cache.Request("art1"), handles[1]);
    cache.Poll();
    EXPECT_EQ(loader.begun.size(), begun + 1);
}

// The canvas resolves a quad's texture at End(), after the frame's Poll() and
// TextureFor() calls: a texture handed out this frame must not be deleted
// before the next Poll().
TEST(NativeUiHostImages, ADroppedTextureLivesUntilTheNextPoll) {
    FakeLoader loader;
    ImageCache cache(loader, SmallCache(1));
    const nativeui::ImageHandle a = cache.Request("a");
    cache.Poll();
    loader.CompleteAll();
    cache.Poll();
    RndTex *aTex = cache.TextureFor(a); // drawn this frame
    ASSERT_NE(aTex, nullptr);
    const nativeui::ImageHandle b = cache.Request("b");
    cache.Poll(); // a was used since the last Poll: it stays
    loader.CompleteAll();
    EXPECT_TRUE(cache.IsReady(a));
    cache.TextureFor(a);
    cache.Poll(); // b arrives; over capacity; a was used this frame
    EXPECT_TRUE(loader.destroyed.empty()) << "a texture drawn this frame was deleted";
    cache.TextureFor(b);
    cache.Poll(); // a not used since: evicted, queued for deletion
    EXPECT_EQ(cache.StateOf(a), ImageCache::kEvicted);
    EXPECT_TRUE(loader.destroyed.empty())
        << "evicted and deleted in the same Poll: a quad submitted before this Poll "
           "would still name the texture when the canvas flushes";
    EXPECT_EQ(cache.GetStats().pendingDestroy, 1);
    cache.Poll();
    ASSERT_EQ(loader.destroyed.size(), 1u);
    EXPECT_EQ(cache.GetStats().pendingDestroy, 0);
    EXPECT_EQ(loader.destroyed[0], aTex);
    EXPECT_EQ(cache.StateOf(a), ImageCache::kEvicted);
    EXPECT_TRUE(cache.IsReady(b));

    cache.Clear();
    cache.Poll();
    EXPECT_EQ(loader.destroyed.size(), 2u) << "Clear gives every texture back";
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

TEST(NativeUiHostInput, PadButtonsMapToActions) {
    using nativeui::Action;
    EXPECT_EQ(MapPad(kPadDUp, kPadActionUp), Action::Up);
    EXPECT_EQ(MapPad(kPadDDown, kPadActionDown), Action::Down);
    EXPECT_EQ(MapPad(kPadDLeft, kPadActionLeft), Action::Left);
    EXPECT_EQ(MapPad(kPadDRight, kPadActionRight), Action::Right);
    EXPECT_EQ(MapPad(kPadLStickDown, kPadActionDown), Action::Down);
    EXPECT_EQ(MapPad(kPadX, kPadActionConfirm), Action::Accept); // A / Enter
    EXPECT_EQ(MapPad(kPadCircle, kPadActionCancel), Action::Back); // B / Esc
    EXPECT_EQ(MapPad(kPadL1, kPadActionNone), Action::PrevTab); // LB / Q
    EXPECT_EQ(MapPad(kPadR1, kPadActionNone), Action::NextTab); // RB / E
    EXPECT_EQ(MapPad(kPadL2, kPadActionNone), Action::PageUp); // LT / Z
    EXPECT_EQ(MapPad(kPadR2, kPadActionNone), Action::PageDown); // RT / C
    EXPECT_EQ(MapPad(kPadSelect, kPadActionNone), Action::NextTab); // Back / Tab
    // The button decides before the action: a game that maps the triggers to
    // its page actions still pages here.
    EXPECT_EQ(MapPad(kPadL2, kPadActionPageUp), Action::PageUp);
    // Buttons the UI has no meaning for are left to the game.
    EXPECT_FALSE(MapPad(kPadSquare, kPadActionNone).has_value());
    EXPECT_FALSE(MapPad(kPadTri, kPadActionNone).has_value());
    EXPECT_FALSE(MapPad(kPadStart, kPadActionNone).has_value());
    // An unknown button with a known action goes by the action.
    EXPECT_EQ(MapPad(999, kPadActionConfirm), Action::Accept);
    EXPECT_FALSE(MapPad(999, kPadActionNone).has_value());
}

TEST(NativeUiHostInput, TextEventsMapToTextAndEdits) {
    const auto typed = MapText({ kTextInputChar, U'q', 0 });
    ASSERT_TRUE(typed.has_value());
    ASSERT_TRUE(std::holds_alternative<nativeui::TextEvent>(*typed));
    EXPECT_EQ(std::get<nativeui::TextEvent>(*typed).codePoint, U'q');

    const auto back = MapText({ kTextInputBackspace, 0, 0 });
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(std::get<nativeui::EditEvent>(*back).edit, nativeui::Edit::Backspace);
    EXPECT_EQ(std::get<nativeui::EditEvent>(*MapText({ kTextInputDelete, 0, 0 })).edit,
              nativeui::Edit::Delete);
    EXPECT_EQ(std::get<nativeui::EditEvent>(*MapText({ kTextInputHome, 0, 0 })).edit,
              nativeui::Edit::CursorHome);
    EXPECT_EQ(std::get<nativeui::EditEvent>(*MapText({ kTextInputEnd, 0, 0 })).edit,
              nativeui::Edit::CursorEnd);
    // Left and Right also arrive as D-pad presses; the pad's copy moves the
    // caret, so the text copy is dropped (one key, one step).
    EXPECT_FALSE(MapText({ kTextInputLeft, 0, 0 }).has_value());
    EXPECT_FALSE(MapText({ kTextInputRight, 0, 0 }).has_value());
}

TEST(NativeUiHostInput, AHeldDirectionRepeatsAfterTheDelayThenAtTheInterval) {
    using nativeui::Action;
    DirectionRepeater repeater(RepeatConfig { 0.35, 0.08 });
    std::vector<Action> out;
    EXPECT_TRUE(repeater.Press(Action::Down, 10.0)) << "the press itself is the caller's";
    repeater.Tick(10.34, out);
    EXPECT_TRUE(out.empty()) << "nothing before the delay";
    repeater.Tick(10.36, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], Action::Down);
    repeater.Tick(10.40, out);
    EXPECT_EQ(out.size(), 1u) << "one interval has not passed";
    repeater.Tick(10.44, out);
    EXPECT_EQ(out.size(), 2u);
    repeater.Tick(10.52, out);
    EXPECT_EQ(out.size(), 3u);

    // A second "press" while held is the game's own repeat: not a new press.
    EXPECT_FALSE(repeater.Press(Action::Down, 10.53));

    repeater.Release(Action::Down);
    repeater.Tick(12.0, out);
    EXPECT_EQ(out.size(), 3u) << "released: no more";
    EXPECT_FALSE(repeater.Holding());

    // A stalled frame does not replay a second's worth of repeats.
    out.clear();
    repeater.Press(Action::Up, 20.0);
    repeater.Tick(25.0, out);
    EXPECT_LE(out.size(), 4u);
    EXPECT_GE(out.size(), 1u);
    out.clear();
    repeater.Tick(25.01, out);
    EXPECT_TRUE(out.empty()) << "the backlog is dropped, not owed";
}

// ---------------------------------------------------------------------------
// The host with a screen: a search field over a list
// ---------------------------------------------------------------------------

namespace {

    // Every printable ASCII character, each half a cell wide.
    BitmapFontData AsciiFont() {
        BitmapFontData data;
        data.cellWidth = 10.0f;
        data.cellHeight = 20.0f;
        data.pages.push_back(FakeTex(2));
        for (char32_t c = 0x20; c < 0x7F; c++) {
            data.glyphs[c] = Glyph(0.01f * (float)(c - 0x20), c == U' ' ? 0.0f : 0.5f, 0.55f);
        }
        return data;
    }

    // The smallest screen the host's rules can be seen on: a text field and
    // a list of forty rows; typing with the list focused goes to the field.
    class TestScreen {
    public:
        TestScreen(const nativeui::TextMeasurer &text, const nativeui::Clock &clock)
            : screen_({ kLogicalWidth, kLogicalHeight }, nativeui::Dc3Theme(), text, clock) {
            auto page = std::make_unique<nativeui::Column>(8.0f);
            search_ = &page->Add(std::make_unique<nativeui::TextField>("Search"));
            list_ = &page->Add(
                std::make_unique<nativeui::ListView>(
                    [](int) { return nativeui::ListRowInfo { 30.0f, true }; },
                    [](int index) -> std::unique_ptr<nativeui::Widget> {
                        return std::make_unique<nativeui::Label>(
                            "Song number " + std::to_string(index), &nativeui::Typography::body
                        );
                    }
                ),
                1.0f
            );
            screen_.SetRoot(std::move(page));
            list_->SetRowCount(40);
            list_->SelectFirst();
            screen_.SetFocus(list_);
            screen_.onUnhandledEvent = [this](const nativeui::Event &event) {
                if (std::holds_alternative<nativeui::TextEvent>(event)
                    || std::holds_alternative<nativeui::EditEvent>(event)) {
                    return search_->HandleEvent(event);
                }
                return false;
            };
        }
        nativeui::Screen &GetScreen() { return screen_; }
        nativeui::TextField &Search() { return *search_; }
        nativeui::ListView &SongList() { return *list_; }

    private:
        nativeui::Screen screen_;
        nativeui::TextField *search_ = nullptr;
        nativeui::ListView *list_ = nullptr;
    };

    class NativeUiHostTest : public ::testing::Test {
    protected:
        FontRig rig { AsciiFont() };
        NativeUiHost host { rig.fonts, nullptr };
        TestScreen browser { rig.fonts, host.Clock() };
        double now = 100.0;

        void SetUp() override {
            TextInputSetCapture(false);
            TextInputClear();
            rig.fonts.SetDefault(&rig.source);
            host.SetScreen(&browser.GetScreen());
            host.Enter(now);
        }
        void TearDown() override {
            host.Exit();
            TextInputSetCapture(false);
            TextInputClear();
        }
        void Frame(double seconds = 1.0 / 60.0) {
            now += seconds;
            host.Tick(now);
        }
        int Selected() { return browser.SongList().SelectedIndex(); }
    };

} // namespace

TEST_F(NativeUiHostTest, TextIsCapturedExactlyWhileATextFieldHasFocus) {
    Frame();
    EXPECT_FALSE(TextInputCapture()) << "the list has focus: keys keep their pad meaning";
    EXPECT_FALSE(host.CapturingText());

    browser.GetScreen().SetFocus(&browser.Search());
    Frame();
    EXPECT_TRUE(TextInputCapture()) << "the search field has focus";
    EXPECT_TRUE(host.CapturingText());

    browser.GetScreen().SetFocus(&browser.SongList());
    Frame();
    EXPECT_FALSE(TextInputCapture()) << "focus left the field";

    browser.GetScreen().SetFocus(&browser.Search());
    Frame();
    ASSERT_TRUE(TextInputCapture());
    host.Exit();
    EXPECT_FALSE(TextInputCapture()) << "a panel that exits gives the keyboard back";
}

TEST_F(NativeUiHostTest, CapturePolicyCanBeAlwaysOrNever) {
    host.SetTextCapture(TextCapture::Always);
    Frame();
    EXPECT_TRUE(TextInputCapture());
    host.SetTextCapture(TextCapture::Never);
    browser.GetScreen().SetFocus(&browser.Search());
    Frame();
    EXPECT_FALSE(TextInputCapture());
}

TEST_F(NativeUiHostTest, TypedTextAndEditKeysReachTheScreen) {
    browser.GetScreen().SetFocus(&browser.Search());
    Frame();
    TextInputInjectUtf8("cr");
    Frame();
    EXPECT_EQ(browser.Search().Text(), "cr");
    TextInputInjectKey(kTextInputBackspace);
    Frame();
    EXPECT_EQ(browser.Search().Text(), "c");
    EXPECT_EQ(TextInputPending(), 0) << "the queue is drained every Tick";
}

TEST_F(NativeUiHostTest, ButtonsMoveTheSelectionAndUnmappedButtonsAreNotConsumed) {
    Frame();
    const int first = Selected();
    EXPECT_TRUE(host.OnButtonDown(kPadDDown, kPadActionDown));
    host.OnButtonUp(kPadDDown, kPadActionDown);
    EXPECT_EQ(Selected(), first + 1);
    EXPECT_FALSE(host.OnButtonDown(kPadSquare, kPadActionNone)) << "left to the game";
    EXPECT_EQ(Selected(), first + 1);

    host.Exit();
    EXPECT_FALSE(host.OnButtonDown(kPadDDown, kPadActionDown)) << "not shown: not ours";
    EXPECT_EQ(Selected(), first + 1);
}

TEST_F(NativeUiHostTest, AHeldDirectionRepeatsOnTheHostClockAndGameRepeatsAreSwallowed) {
    Frame();
    const int first = Selected();
    EXPECT_TRUE(host.OnButtonDown(kPadDDown, kPadActionDown));
    EXPECT_EQ(Selected(), first + 1);
    // The game's own auto-repeat of the same held button: consumed, no move.
    EXPECT_TRUE(host.OnButtonDown(kPadDDown, kPadActionDown));
    EXPECT_EQ(Selected(), first + 1);

    Frame(0.2);
    EXPECT_EQ(Selected(), first + 1) << "0.2 s held: before the 0.35 s delay";
    Frame(0.2);
    EXPECT_EQ(Selected(), first + 2) << "0.4 s held: the first repeat";
    Frame(0.08);
    EXPECT_EQ(Selected(), first + 3) << "then one every 0.08 s";
    host.OnButtonUp(kPadDDown, kPadActionDown);
    Frame(0.2);
    Frame(0.2);
    EXPECT_EQ(Selected(), first + 3) << "released";
}

TEST_F(NativeUiHostTest, TheHostClockNeverRunsBackwardsOrJumps) {
    const double start = host.Clock().NowSeconds();
    host.Tick(now + 0.1);
    EXPECT_NEAR(host.Clock().NowSeconds() - start, 0.1, 1e-9);
    host.Tick(now - 50.0); // the game's UI clock was reset
    EXPECT_NEAR(host.Clock().NowSeconds() - start, 0.1, 1e-9);
    host.Tick(now + 1000.0); // a long stall
    EXPECT_NEAR(host.Clock().NowSeconds() - start, 0.35, 1e-9) << "one step is at most 0.25 s";
}

TEST_F(NativeUiHostTest, DrawToReplaysTheFrameAndCountsIt) {
    // A frame is Tick() then Draw(): the screen's animations advance when
    // the frame is built, by the host clock's step since the last build.
    RecordingSink sink;
    for (int i = 0; i < 120; i++) {
        Frame();
        sink.ops.clear();
        host.DrawTo(sink);
    }
    const NativeUiHost::FrameStats &stats = host.Stats();
    EXPECT_TRUE(stats.drew);
    EXPECT_GT(stats.commands, 20u);
    EXPECT_EQ(stats.quads, sink.Quads().size());
    EXPECT_GT(stats.glyphs, 100u);
    EXPECT_EQ(stats.placeholderGlyphs, 0u) << "the test screen is ASCII";
    int depth = 0;
    for (const RecordingSink::Op &op : sink.ops) {
        depth += op.kind == '+' ? 1 : op.kind == '-' ? -1 : 0;
        ASSERT_GE(depth, 0);
    }
    EXPECT_EQ(depth, 0) << "every clip pushed is popped";

    // With no screen there is nothing to draw and nothing is consumed.
    host.SetScreen(nullptr);
    RecordingSink empty;
    host.DrawTo(empty);
    EXPECT_FALSE(host.Stats().drew);
    EXPECT_TRUE(empty.ops.empty());
    EXPECT_FALSE(host.OnButtonDown(kPadDDown, kPadActionDown));
}

// ---------------------------------------------------------------------------
// The host with the real song browser (a HostedScreen that is not a
// nativeui::Screen)
// ---------------------------------------------------------------------------

namespace {

    songbrowser::SongCatalog TwelveSongs() {
        songbrowser::SongCatalog catalog;
        for (int i = 0; i < 12; i++) {
            songbrowser::SongEntry song;
            song.id = i + 1;
            song.shortName = "song" + std::to_string(i);
            song.title = std::string(1, (char)('A' + i)) + " Title " + std::to_string(i);
            song.artist = "Artist " + std::to_string(11 - i);
            song.difficultyTier = i % 7;
            song.origin = "ham3";
            catalog.Add(song);
        }
        return catalog;
    }

}

TEST(NativeUiHostHostedScreen, TheSongBrowserRunsUnderTheHost) {
    TextInputSetCapture(false);
    TextInputClear();
    FontRig rig { AsciiFont() };
    rig.fonts.SetDefault(&rig.source);
    NativeUiHost host { rig.fonts, nullptr };
    const songbrowser::SongCatalog catalog = TwelveSongs();
    std::vector<std::string> picks;
    int backs = 0;
    songbrowser_ui::Delegate delegate;
    delegate.onPick = [&](const std::string &song) { picks.push_back(song); };
    delegate.onBack = [&] { backs++; };
    songbrowser_ui::SongBrowserScreen browser(
        catalog, delegate, songbrowser_ui::Environment { rig.fonts, host.Clock() }
    );
    HostedScreenOf<songbrowser_ui::SongBrowserScreen> hosted(browser);
    host.SetScreen(&hosted);
    ASSERT_EQ(host.GetScreen(), &hosted);
    double now = 5.0;
    host.Enter(now);
    host.Tick(now += 1.0 / 60.0);
    EXPECT_TRUE(TextInputCapture()) << "the browser wants the keyboard wherever its focus is";

    // Pad presses reach it: Down moves the list, Accept picks that song.
    const std::string first = browser.SaveState().selectedSong;
    ASSERT_FALSE(first.empty());
    EXPECT_TRUE(host.OnButtonDown(kPadDDown, kPadActionDown));
    host.OnButtonUp(kPadDDown, kPadActionDown);
    const std::string second = browser.SaveState().selectedSong;
    EXPECT_NE(second, first);
    EXPECT_TRUE(host.OnButtonDown(kPadX, kPadActionConfirm));
    ASSERT_EQ(picks.size(), 1u);
    EXPECT_EQ(picks[0], second);

    // Typed text reaches it through the engine's queue, and its frame is
    // replayed through the host's renderer.
    TextInputInjectUtf8("title 7");
    host.Tick(now += 1.0 / 60.0);
    EXPECT_EQ(browser.SaveState().query, "title 7");
    RecordingSink sink;
    for (int i = 0; i < 90; i++) {
        host.Tick(now += 1.0 / 60.0);
        sink.ops.clear();
        host.DrawTo(sink);
    }
    EXPECT_TRUE(host.Stats().drew);
    EXPECT_GT(host.Stats().glyphs, 40u);
    EXPECT_EQ(browser.DetailSong(), "song7");

    // Back clears the query first, then asks to leave.
    EXPECT_TRUE(host.OnButtonDown(kPadCircle, kPadActionCancel));
    EXPECT_EQ(browser.SaveState().query, "");
    EXPECT_EQ(backs, 0);
    EXPECT_TRUE(host.OnButtonDown(kPadCircle, kPadActionCancel));
    EXPECT_EQ(backs, 1);

    host.Exit();
    EXPECT_FALSE(TextInputCapture()) << "leaving gives the keyboard back";
    host.SetScreen(nullptr);
    EXPECT_FALSE(host.OnButtonDown(kPadDDown, kPadActionDown));
    TextInputClear();
}

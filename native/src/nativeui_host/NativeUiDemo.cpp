#include "nativeui_host/NativeUiDemo.h"

#include "dc3_render_hook.h"
#include "meta_ham/HamSongMetadata.h"
#include "meta_ham/HamSongMgr.h"
#include "nativeui/SampleBrowser.h"
#include "nativeui/Utf8.h"
#include "nativeui_host/GameBindings.h"
#include "nativeui_host/GameFonts.h"
#include "nativeui_host/GameImages.h"
#include "nativeui_host/NativeUiHost.h"
#include "obj/Object.h"
#include "os/JoypadClient.h"
#include "os/JoypadMsgs.h"
#include "ui/UI.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>
#include <vector>

// src/App.cpp: called once a frame from the native main loop, outside any
// render pass. Null unless something installs it.
extern void (*gDc3NativeFrameHook)();

namespace nativeui_host {

    namespace {

        class Demo;
        Demo *sDemo = nullptr;

        // Receives pad messages the way the game's UI does: through a
        // JoypadClient of its own (left stick as D-pad; no repeat mask -- the
        // host times its own repeats).
        class DemoPadSink : public Hmx::Object {
        public:
            DemoPadSink(NativeUiHost &host) : host_(host) {}
            virtual DataNode Handle(DataArray *, bool);
            DataNode OnMsg(const ButtonDownMsg &msg) {
                OnButtonDown(host_, msg);
                return DataNode(0);
            }
            DataNode OnMsg(const ButtonUpMsg &msg) {
                OnButtonUp(host_, msg);
                return DataNode(0);
            }

        private:
            NativeUiHost &host_;
        };

        BEGIN_HANDLERS(DemoPadSink)
            HANDLE_MESSAGE(ButtonDownMsg)
            HANDLE_MESSAGE(ButtonUpMsg)
        END_HANDLERS

        std::string JsonString(const std::string &text) {
            std::string out = "\"";
            for (unsigned char c : text) {
                if (c == '"' || c == '\\') {
                    out += '\\';
                    out += (char)c;
                } else if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
            }
            return out + "\"";
        }

        class Demo : public ImageSource {
        public:
            Demo() : images_(loader_, ImageCache::Config { 24, 4, 2 }), host_(fonts_.Fonts(), &images_) {}

            ~Demo() {
                host_.Exit();
                delete padClient_;
                delete padSink_;
            }

            // The sample's art handles are bare numbers (100 + song index).
            // Each stands for one real song's album art, loaded through the
            // cache; the game's logo shows until it arrives.
            RndTex *TextureFor(nativeui::ImageHandle image) override {
                if (image.id < 100 || artPaths_.empty()) {
                    return nullptr;
                }
                const std::string &path = artPaths_[(image.id - 100) % artPaths_.size()];
                const nativeui::ImageHandle art = images_.Request(path);
                if (RndTex *tex = images_.TextureFor(art)) {
                    return tex;
                }
                return images_.TextureFor(fallback_);
            }

            void FramePoll() {
                if (!ready_) {
                    if (failed_ || !TheUI) {
                        return;
                    }
                    if (!fonts_.Load()) {
                        failed_ = true;
                        printf("NativeUI demo: the menu font did not load; the demo is off\n");
                        fflush(stdout);
                        return;
                    }
                    for (const GameFontInfo &info : fonts_.Table()) {
                        printf(
                            "NativeUI demo: font %s (%s) cell %gx%g, %d page(s) %dx%d, %d glyphs, "
                            "%d kerning pairs, cap top %.3f baseline %.3f descender %.3f%s\n",
                            info.resource.c_str(), info.fontName.c_str(), info.cellWidth,
                            info.cellHeight, info.pages, info.pageWidth, info.pageHeight,
                            info.glyphs, info.kerningPairs, info.capTop, info.baseline,
                            info.descenderBottom,
                            info.measuredFromAtlas ? "" : " (ink NOT measured: defaults)"
                        );
                    }
                    browser_.reset(new nativeui::SampleBrowser(
                        { kLogicalWidth, kLogicalHeight }, fonts_.Fonts(), host_.Clock()
                    ));
                    fallback_ = images_.Request(kFallbackArtPath);
                    images_.SetFallback(kFallbackArtPath);
                    host_.SetImageSource(this);
                    host_.SetScreen(&browser_->GetScreen());
                    host_.Enter(GameUiSeconds());
                    padSink_ = new DemoPadSink(host_);
                    padClient_ = new JoypadClient(padSink_);
                    padClient_->SetVirtualDpad(true);
                    ready_ = true;
                    printf("NativeUI demo: ready\n");
                    fflush(stdout);
                }
                if (artPaths_.empty()) {
                    CollectSongs();
                }
                host_.Tick(GameUiSeconds());
            }

            void DrawOverlay() {
                if (!ready_) {
                    return;
                }
                SetOutputSize(host_);
                host_.Draw();
                const NativeUiHost::FrameStats &stats = host_.Stats();
                if (stats.drew) {
                    frames_++;
                    sumBuild_ += stats.buildMicros;
                    sumReplay_ += stats.replayMicros;
                    if (frames_ == 1 || frames_ % 600 == 0) {
                        printf(
                            "NativeUI demo: frame %u: %u commands, %u quads in %u draws, "
                            "build %u us, replay %u us (mean %.0f / %.0f us)\n",
                            frames_, stats.commands, stats.quads, stats.drawCalls,
                            stats.buildMicros, stats.replayMicros,
                            (double)sumBuild_ / frames_, (double)sumReplay_ / frames_
                        );
                        fflush(stdout);
                    }
                }
            }

            std::string StateJson() {
                if (!ready_) {
                    return failed_ ? "{\"active\":false,\"error\":\"menu font not loaded\"}"
                                   : "{\"active\":false}";
                }
                nativeui::SampleBrowser &b = *browser_;
                const nativeui::Widget *focused = b.GetScreen().Focused();
                const char *focusName = "other";
                if (focused == &b.Search()) {
                    focusName = "search";
                } else if (focused == &b.SongList()) {
                    focusName = "list";
                } else if (focused == &b.Recent()) {
                    focusName = "recent";
                } else if (focused == &b.Tabs()) {
                    focusName = "tabs";
                } else if (!focused) {
                    focusName = "none";
                }
                const nativeui::SampleSong *detail = b.DetailSong();
                const NativeUiHost::FrameStats &stats = host_.Stats();
                const ImageCache::Stats imageStats = images_.GetStats();
                const nativeui::Rect &search = b.Search().Bounds();
                const nativeui::Rect &list = b.SongList().Bounds();
                const nativeui::Rect bar = b.SongList().HighlightRect();
                char buf[1024];
                std::string out = "{\"active\":true";
                out += ",\"search\":" + JsonString(b.Search().Text());
                snprintf(buf, sizeof(buf), ",\"cursor\":%d", (int)b.Search().Cursor());
                out += buf;
                out += std::string(",\"focus\":\"") + focusName + "\"";
                snprintf(
                    buf, sizeof(buf),
                    ",\"selected_row\":%d,\"rows\":%d,\"tab\":%d,\"capture\":%s,\"at_rest\":%s",
                    b.SongList().SelectedIndex(), (int)b.Entries().size(), b.Tabs().SelectedIndex(),
                    host_.CapturingText() ? "true" : "false",
                    b.GetScreen().AtRest() ? "true" : "false"
                );
                out += buf;
                out += ",\"detail_title\":" + JsonString(detail ? detail->title : "");
                snprintf(
                    buf, sizeof(buf),
                    ",\"search_rect\":[%g,%g,%g,%g],\"list_rect\":[%g,%g,%g,%g]"
                    ",\"highlight_rect\":[%g,%g,%g,%g]",
                    search.x, search.y, search.width, search.height, list.x, list.y,
                    list.width, list.height, bar.x, bar.y, bar.width, bar.height
                );
                out += buf;
                snprintf(
                    buf, sizeof(buf),
                    ",\"stats\":{\"frames\":%u,\"commands\":%u,\"quads\":%u,\"draw_calls\":%u,"
                    "\"glyphs\":%u,\"placeholder_glyphs\":%u,\"images_missing\":%u,"
                    "\"build_us\":%u,\"replay_us\":%u,\"mean_build_us\":%.1f,"
                    "\"mean_replay_us\":%.1f}",
                    frames_, stats.commands, stats.quads, stats.drawCalls, stats.glyphs,
                    stats.placeholderGlyphs, stats.imagesMissing, stats.buildMicros,
                    stats.replayMicros, frames_ ? (double)sumBuild_ / frames_ : 0.0,
                    frames_ ? (double)sumReplay_ / frames_ : 0.0
                );
                out += buf;
                snprintf(
                    buf, sizeof(buf),
                    ",\"images\":{\"ready\":%d,\"loading\":%d,\"queued\":%d,\"evictions\":%d,"
                    "\"failures\":%d}",
                    imageStats.ready, imageStats.loading, imageStats.queued,
                    imageStats.evictions, imageStats.failures
                );
                out += buf;
                snprintf(buf, sizeof(buf), ",\"songs\":%d", songCount_);
                out += buf;
                out += ",\"missing_glyphs\":" + JsonString(missingGlyphs_);
                out += ",\"missing_glyphs_upper\":" + JsonString(missingGlyphsUpper_);
                out += "}";
                return out;
            }

        private:
            // The real song list: album-art paths for the sample's rows, and
            // which characters of the titles and artists the menu font lacks.
            void CollectSongs() {
                std::vector<int> songs;
                TheHamSongMgr.GetRankedSongs(songs);
                if (songs.empty()) {
                    return;
                }
                const GameFont *menu = fonts_.Find(nativeui::kDc3MenuFont);
                std::set<char32_t> missing, missingUpper;
                for (int id : songs) {
                    const HamSongMetadata *data = TheHamSongMgr.Data(id);
                    if (!data) {
                        continue;
                    }
                    const Symbol shortName = TheHamSongMgr.GetShortNameFromSongID(id, false);
                    const std::string art = AlbumArtPath(shortName.Str());
                    if (!art.empty()) {
                        artPaths_.push_back(art);
                    }
                    if (!menu) {
                        continue;
                    }
                    for (const char *text : { data->Title(), data->Artist() }) {
                        if (!text) {
                            continue;
                        }
                        const std::string asWritten(text);
                        const std::string upper = nativeui::utf8::ToUpper(asWritten);
                        for (int pass = 0; pass < 2; pass++) {
                            const std::string &s = pass == 0 ? asWritten : upper;
                            for (std::size_t at = 0; at < s.size();) {
                                const nativeui::utf8::Decoded d = nativeui::utf8::Decode(s, at);
                                if (d.length == 0) {
                                    break;
                                }
                                at += d.length;
                                if (!menu->Source().HasGlyph(d.codePoint)) {
                                    (pass == 0 ? missing : missingUpper).insert(d.codePoint);
                                }
                            }
                        }
                    }
                }
                songCount_ = (int)songs.size();
                for (char32_t c : missing) {
                    nativeui::utf8::Append(missingGlyphs_, c);
                }
                for (char32_t c : missingUpper) {
                    nativeui::utf8::Append(missingGlyphsUpper_, c);
                }
                printf(
                    "NativeUI demo: %d songs, %d with album art paths; menu font lacks "
                    "[%s] as written, [%s] in upper case\n",
                    songCount_, (int)artPaths_.size(), missingGlyphs_.c_str(),
                    missingGlyphsUpper_.c_str()
                );
                fflush(stdout);
            }

            GameFonts fonts_;
            GameImageLoader loader_;
            ImageCache images_;
            NativeUiHost host_;
            std::unique_ptr<nativeui::SampleBrowser> browser_;
            DemoPadSink *padSink_ = nullptr;
            JoypadClient *padClient_ = nullptr;
            nativeui::ImageHandle fallback_;
            std::vector<std::string> artPaths_;
            std::string missingGlyphs_, missingGlyphsUpper_;
            int songCount_ = 0;
            bool ready_ = false;
            bool failed_ = false;
            unsigned frames_ = 0;
            unsigned long long sumBuild_ = 0, sumReplay_ = 0;
        };

        void DemoFramePoll() {
            if (sDemo) {
                sDemo->FramePoll();
            }
        }

        void DemoDrawOverlay() {
            if (sDemo) {
                sDemo->DrawOverlay();
            }
        }

    }

    void DemoInstall() {
        const char *v = getenv("DC3_NATIVEUI_DEMO");
        if (!v || v[0] != '1' || sDemo) {
            return;
        }
        sDemo = new Demo();
        gDc3NativeFrameHook = DemoFramePoll;
        SetGameOverlayDraw(DemoDrawOverlay);
        printf("NativeUI demo: installed (DC3_NATIVEUI_DEMO=1)\n");
        fflush(stdout);
    }

    bool DemoActive() { return sDemo != nullptr; }

    std::string DemoStateJson() {
        return sDemo ? sDemo->StateJson() : std::string("{\"active\":false}");
    }

}

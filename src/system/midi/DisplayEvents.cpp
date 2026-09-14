#include "midi\DisplayEvents.h"
#include "math\Utl.h"
#include "rndobj\Rnd.h"

// Reloaded from memory after every call in the shipped code, so it is a
// mutable file-scope float, not a literal. Nothing ever stores to it.
static float sLeftEdge = 5.0f;

float DisplayEvents(DataEventList *events, float f1, float f2) {
    float f9 = f1 + 2.0f;
    int min = Min(events->CurIndex(), events->Size() - 1);
    MaxEq(min, 0);
    while (min > 0 && events->Event(min - 1).end > f2)
        min--;
    while (min < events->Size() && events->Event(min).end < f2)
        min++;
    float f10 = -1.0f;
    float fsum = f2 + (float)TheRnd.Width() / 200.0f;
    while (min < events->Size() && events->Event(min).start < fsum) {
        DataEvent curEvent(events->Event(min));
        float start14c = (curEvent.start - f2) * 200.0f + sLeftEdge;
        float start150 = (curEvent.end - f2) * 200.0f + sLeftEdge;
        String str108;
        str108 << curEvent.Msg();
        MaxEq(start14c, sLeftEdge);
        MinEq(start150, (float)TheRnd.Width() + sLeftEdge);
        if (min < events->Size() - 1) {
            MinEq(start150, ((events->Event(min + 1).start - f2) * 200.0f + sLeftEdge) - 1.0f);
        }
        // NEGATIVE RESULT (w7-bg): floor at 98.96% canonical.  Frame 0x190 vs
        // the image's 0x180 -- callee-saved GPR (8) and FPR (14) counts already
        // agree, so the whole 0x10 is one extra 16-byte temp.  The image gets
        // by with THREE Rect/Color-sized slot groups because it OVERLAYS the
        // loop's first group with the tail's: its 0x80..0x8c carries two stores
        // each, one inside the loop (rows 132..146) and one after it
        // (rows 194..206).  We allocate a fourth group at 0xc0..0xcc.
        // Refuted: dropping both named Rect locals and passing
        // `Hmx::Rect(...)` straight into DrawRect -> 98.05%, worse.  Same MSVC
        // slot-overlay class as CampaignPerformer::OnMovePassed; not reachable
        // from the source spelling.
        auto eventRect = Hmx::Rect(start14c, f1 + 2.0f, Max(1.0f, start150 - start14c), 12.0f);
        TheRnd.DrawRect(
            eventRect,
            Hmx::Color(0, 0, 1),
            0,
            0,
            0
        );
        if (f10 == -1.0f) {
            f10 =
                Max(f10,
                    TheRnd
                        .DrawString(
                            MakeString("%.2f: %s", curEvent.start, str108.c_str()),
                            Vector2(start14c, f9 + 14.0f),
                            Hmx::Color(1, 1, 1),
                            true
                        )
                        .y);
        }
        min++;
    }
    auto cursorRect = Hmx::Rect(sLeftEdge, f9 - 2.0f, 1.0f, 14.0f);
    TheRnd.DrawRect(cursorRect, Hmx::Color(1, 0, 0), 0, 0, 0);
    return f9 + f10;
}

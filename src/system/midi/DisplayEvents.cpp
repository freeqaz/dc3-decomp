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
    Hmx::Rect rect;
    for (; min < events->Size() && events->Event(min).start < fsum; min++) {
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
        // ONE Rect for the whole function (w7-bn, 98.96 -> 100.0): the image's
        // 0x80..0x8c is stored both inside the loop and after it because it
        // is the same function-scope local, not two sibling-scope temps being
        // overlaid.  `min++` lives in the for-increment so it lands after the
        // String/DataEvent dtors (82552C6C), and the tail assigns the four
        // fields in x, w, y, h order -- that order is what schedules the
        // image's `stfs f31, 0x88` ahead of `stfs f13, 0x84` at 82552C8C/94.
        // (Named `auto eventRect`/`cursorRect` in sibling scopes: 98.96;
        //  block-scoping the tail: inert; `rect = Hmx::Rect(...)`: 94.2.)
        rect.Set(start14c, f1 + 2.0f, Max(1.0f, start150 - start14c), 12.0f);
        TheRnd.DrawRect(
            rect,
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
    }
    rect.x = sLeftEdge;
    rect.w = 1.0f; // stored before y: see the note above
    rect.y = f9 - 2.0f;
    rect.h = 14.0f;
    TheRnd.DrawRect(rect, Hmx::Color(1, 0, 0), 0, 0, 0);
    return f9 + f10;
}

// Frame pacing instrument for the web build (PLAN W-14, H-34).
//
//   import { installPacing, collectPacing, analyzePacing, formatPacing } from './lib/pacing.mjs';
//   await installPacing(page);            // before page.goto
//   ... drive the game ...
//   const raw = await collectPacing(page);
//   const rep = analyzePacing(raw);       // per-phase distribution + stall attribution
//   console.log(formatPacing(rep));
//
// What it records, all on the page's performance.now() clock:
//   game    one stamp per game frame: a setter on window.dc3FrameCount, which
//           main_web.cpp writes at the end of every BOOT_RUNNING frame. The
//           interval between two stamps is what the player sees: the canvas
//           only changes when a game frame completes (a frame that JSPI-suspends
//           on a fetch keeps the old picture on screen however fast rAF runs).
//   raf     an independent requestAnimationFrame loop: the browser's own frame
//           clock (the display rate; a gap here is a blocked main thread or a
//           compositor/GPU stall, not a slow game frame)
//   lt      PerformanceObserver 'longtask' (main thread busy > 50 ms)
//   loaf    'long-animation-frame' entries with their script attribution
//   net     every fetch() / XMLHttpRequest the page makes: start, end, URL, bytes
//           (the engine's blocking asset loads are JSPI-suspended fetch()es,
//           bundles are emscripten_fetch XHRs)
//   ft      the engine's per-frame cost counters when the URL carries
//           env=DC3_FRAME_TRACE=1 (main_web.cpp FrameTraceRecord): fetch,
//           texture/mesh upload, vertex unpack, pipeline creation, DTA parse
//   heap    wasm memory growth events (byteLength changes)
//   screen  'will enter' console lines, time-stamped in the page
//   audio   the worklet's underrun counter, sampled every game frame

export function pacingInitScript() {
    const P = (window.__dc3Pacing = { game: [], raf: [], lt: [], loaf: [], net: [], ft: [], heap: [],
        screen: [], underrun: [], start: performance.now() });
    let frame = 0;
    let lastBytes = 0;
    let lastUnder = -1;
    Object.defineProperty(window, 'dc3FrameCount', {
        configurable: true,
        get() { return frame; },
        set(v) {
            frame = v;
            const now = performance.now();
            P.game.push(now);
            const mem = window.__dc3Mem;
            if (mem) {
                const b = mem.buffer.byteLength;
                if (b !== lastBytes) { P.heap.push([now, b]); lastBytes = b; }
            }
            const a = window._dc3Audio;
            const u = a && a.underruns ? a.underruns.underrunEvents : -1;
            if (u !== lastUnder) { P.underrun.push([now, u]); lastUnder = u; }
        },
    });
    window.dc3FrameTraceSink = (o) => { P.ft.push(o); };
    const raf = (t) => { P.raf.push(t); requestAnimationFrame(raf); };
    requestAnimationFrame(raf);
    try {
        new PerformanceObserver((l) => { for (const e of l.getEntries()) P.lt.push([e.startTime, e.duration]); })
            .observe({ type: 'longtask', buffered: true });
    } catch (e) { /* not supported */ }
    try {
        new PerformanceObserver((l) => {
            for (const e of l.getEntries()) {
                P.loaf.push({ s: e.startTime, d: e.duration, block: e.blockingDuration,
                    scripts: (e.scripts || []).map((s) => ({ s: s.startTime, d: s.duration, inv: s.invoker,
                        type: s.invokerType, fn: s.sourceFunctionName, src: (s.sourceURL || '').split('/').pop() })) });
            }
        }).observe({ type: 'long-animation-frame', buffered: true });
    } catch (e) { /* not supported */ }
    // Network: fetch + XHR, wrapped before any page script runs.
    const of = window.fetch;
    window.fetch = function (input, init) {
        const url = typeof input === 'string' ? input : (input && input.url) || String(input);
        const rec = { s: performance.now(), e: 0, url, kind: 'fetch', bytes: 0 };
        P.net.push(rec);
        return of.call(this, input, init).then((r) => {
            rec.status = r.status;
            rec.bytes = +r.headers.get('content-length') || 0;
            // Ends at the headers unless the caller reads the body, which the
            // engine's asset loads do (arrayBuffer): then it ends there.
            rec.e = rec.h = performance.now();
            const ab = r.arrayBuffer.bind(r);
            r.arrayBuffer = () => ab().then((buf) => { rec.e = performance.now(); rec.bytes = buf.byteLength; return buf; });
            return r;
        }, (err) => { rec.e = performance.now(); rec.err = String(err); throw err; });
    };
    const xo = XMLHttpRequest.prototype.open, xs = XMLHttpRequest.prototype.send;
    XMLHttpRequest.prototype.open = function (m, url) { this.__dc3 = { url: String(url), kind: 'xhr', bytes: 0 }; return xo.apply(this, arguments); };
    XMLHttpRequest.prototype.send = function () {
        const rec = this.__dc3;
        if (rec) {
            rec.s = performance.now();
            P.net.push(rec);
            this.addEventListener('loadend', () => {
                rec.e = performance.now(); rec.status = this.status;
                try { rec.bytes = this.response && this.response.byteLength ? this.response.byteLength : 0; } catch (e) {}
            });
        }
        return xs.apply(this, arguments);
    };
    // Screen changes, in the page's clock.
    const ol = console.log;
    console.log = function (...a) {
        const t = a.length && typeof a[0] === 'string' ? a[0] : '';
        if (t.indexOf("will enter '") >= 0) {
            const m = t.match(/will enter '([^']+)'/);
            if (m) P.screen.push([performance.now(), m[1]]);
        }
        return ol.apply(this, a);
    };
    // The wasm memory, for heap growth (also what pose-gameplay.mjs grabs).
    const grab = (res) => {
        try { const ex = (res.instance || res).exports || {}; for (const k in ex) if (ex[k] instanceof WebAssembly.Memory && !window.__dc3Mem) window.__dc3Mem = ex[k]; } catch (e) {}
        return res;
    };
    const is = WebAssembly.instantiateStreaming;
    if (is) WebAssembly.instantiateStreaming = (...a) => is.apply(WebAssembly, a).then(grab);
    const ii = WebAssembly.instantiate;
    WebAssembly.instantiate = (...a) => ii.apply(WebAssembly, a).then(grab);
}

export async function installPacing(page) {
    await page.addInitScript(pacingInitScript);
}

export async function collectPacing(page) {
    return page.evaluate(() => {
        const P = window.__dc3Pacing;
        if (!P) return null;
        return { ...P, net: P.net.map((r) => ({ ...r })), now: performance.now(),
            lastFrame: window.dc3FrameCount || 0 };
    });
}

// Screen name -> phase.
export function phaseOf(screen) {
    if (!screen) return 'boot';
    if (screen === 'game_screen' || /pause/.test(screen)) return 'game';
    if (/loading/.test(screen)) return 'loading';
    if (/song_select|multiuser|choose_song|seldiff/.test(screen)) return 'song_select';
    if (/results|endgame|complete/.test(screen)) return 'results';
    return 'menu';
}

const quant = (sorted, p) => (sorted.length ? sorted[Math.min(sorted.length - 1, Math.floor(p * sorted.length))] : null);
const r1 = (x) => (x === null || x === undefined ? null : +x.toFixed(1));

function distribution(intervals) {
    const s = [...intervals].sort((a, b) => a - b);
    const sum = s.reduce((a, b) => a + b, 0);
    return {
        frames: s.length, seconds: r1(sum / 1000), fps: s.length ? r1((1000 * s.length) / sum) : null,
        p50: r1(quant(s, 0.5)), p95: r1(quant(s, 0.95)), p99: r1(quant(s, 0.99)), max: r1(s[s.length - 1] || 0),
        over25: s.filter((x) => x > 25).length, over50: s.filter((x) => x > 50).length,
        over100: s.filter((x) => x > 100).length, over250: s.filter((x) => x > 250).length,
        over1000: s.filter((x) => x > 1000).length,
        // time lost to frames longer than 2 display periods (what a player sees as a hitch)
        stallMs: Math.round(s.filter((x) => x > 33.4).reduce((a, b) => a + b - 16.7, 0)),
    };
}

// Attribute one stall (a game-frame interval [a, b]) to what overlapped it.
function attribute(raw, a, b, ftByEnd) {
    const ov = (s, e) => Math.max(0, Math.min(e, b) - Math.max(s, a));
    const net = raw.net.filter((r) => r.s < b && (r.e || raw.now) > a)
        .map((r) => ({ url: r.url.replace(/^.*\/api\//, ''), kind: r.kind, ms: r1((r.e || raw.now) - r.s), ovMs: r1(ov(r.s, r.e || raw.now)), bytes: r.bytes }))
        .sort((x, y) => y.ovMs - x.ovMs);
    const lt = raw.lt.filter(([s, d]) => s < b && s + d > a).map(([s, d]) => r1(d));
    const loaf = raw.loaf.filter((l) => l.s < b && l.s + l.d > a);
    const scripts = loaf.flatMap((l) => l.scripts).sort((x, y) => y.d - x.d).slice(0, 3)
        .map((s) => `${s.type}:${s.inv || ''}${s.fn ? ' ' + s.fn : ''}${s.src ? ' @' + s.src : ''} ${r1(s.d)}ms`);
    const ft = ftByEnd(b);
    const heap = raw.heap.filter(([t]) => t > a && t <= b).map(([, by]) => +(by / 1048576).toFixed(0));
    const under = raw.underrun.filter(([t]) => t > a && t <= b).map(([, u]) => u);
    // the browser frames around and inside the interval: the last before a,
    // every one inside, the first after b
    let i0 = 0;
    while (i0 + 1 < raw.raf.length && raw.raf[i0 + 1] <= a) i0++;
    const rafIn = [];
    for (let i = i0; i < raw.raf.length; i++) { rafIn.push(raw.raf[i]); if (raw.raf[i] >= b) break; }
    let rafGap = 0;
    for (let i = 1; i < rafIn.length; i++) rafGap = Math.max(rafGap, rafIn[i] - rafIn[i - 1]);
    // Cause, by the largest share of the interval:
    const dur = b - a;
    const cands = [];
    if (ft) {
        cands.push(['asset fetch (blocking)', ft.fetchMs]);
        cands.push(['texture upload', ft.texMs]);
        cands.push(['mesh upload', ft.meshMs + ft.unpackMs]);
        cands.push(['pipeline create', ft.pipeMs]);
        cands.push(['dta parse', ft.dtaMs]);
        cands.push(['stream read', ft.streamMs]);
    } else {
        const fetchOv = net.filter((n) => n.kind === 'fetch').reduce((m, n) => Math.max(m, n.ovMs), 0);
        cands.push(['asset fetch (blocking)', fetchOv]);
    }
    const ltMs = lt.reduce((x, y) => x + y, 0);
    cands.sort((x, y) => y[1] - x[1]);
    let cause = cands[0] && cands[0][1] > 0.4 * dur ? cands[0][0] : null;
    if (!cause && ft && ft.t1 - ft.t0 > 0.6 * dur) cause = 'game frame compute (main thread)';
    if (!cause && ltMs > 0.6 * dur) cause = 'main-thread task (not a game frame)';
    if (!cause && rafGap > 0.6 * dur) cause = 'browser frame gap (main thread / compositor)';
    if (!cause) cause = 'unattributed';
    return { net: net.slice(0, 3), lt, scripts, heapMB: heap, underruns: under, rafGapMs: r1(rafGap),
        ft: ft ? { frameMs: r1(ft.t1 - ft.t0), fetchMs: r1(ft.fetchMs), fetchN: ft.fetchN, texMs: r1(ft.texMs), texN: ft.texN,
            meshMs: r1(ft.meshMs), unpackMs: r1(ft.unpackMs), pipeMs: r1(ft.pipeMs), pipeN: ft.pipeN, dtaMs: r1(ft.dtaMs), streamMs: r1(ft.streamMs) } : null,
        cause };
}

export function analyzePacing(raw, { stallMs = 50, top = 25, from = 0 } = {}) {
    if (!raw || raw.game.length < 2) return { error: 'no game frames recorded' };
    const screens = raw.screen;
    const screenAt = (t) => { let s = ''; for (const [st, n] of screens) { if (st <= t) s = n; else break; } return s; };
    // engine trace, keyed by frame end time (t1 is stamped just before the
    // dc3FrameCount write of the same frame)
    const ft = raw.ft || [];
    const ftByEnd = (t) => {
        let lo = 0, hi = ft.length - 1, best = null;
        while (lo <= hi) { const m = (lo + hi) >> 1; if (ft[m].t1 <= t + 0.5) { best = ft[m]; lo = m + 1; } else hi = m - 1; }
        return best && t - best.t1 < 5 ? best : null;
    };
    const per = {};
    const stalls = [];
    const allIv = [];
    for (let i = 1; i < raw.game.length; i++) {
        const a = raw.game[i - 1], b = raw.game[i];
        if (b < from) continue;
        const d = b - a;
        const sc = screenAt(b);
        const ph = phaseOf(sc);
        (per[ph] || (per[ph] = [])).push(d);
        allIv.push(d);
        if (d > stallMs) stalls.push({ at: r1((b - raw.start) / 1000), ms: r1(d), screen: sc, phase: ph, a, b });
    }
    const phases = {};
    for (const [k, v] of Object.entries(per)) phases[k] = distribution(v);
    phases.all = distribution(allIv);
    const rafIv = [];
    for (let i = 1; i < raw.raf.length; i++) if (raw.raf[i] >= from) rafIv.push(raw.raf[i] - raw.raf[i - 1]);
    const rafDist = distribution(rafIv);
    for (const s of stalls) Object.assign(s, attribute(raw, s.a, s.b, ftByEnd));
    const causes = {};
    for (const s of stalls) {
        const c = causes[s.cause] || (causes[s.cause] = { n: 0, ms: 0, phases: {} });
        c.n++; c.ms += s.ms; c.phases[s.phase] = (c.phases[s.phase] || 0) + 1;
    }
    for (const c of Object.values(causes)) c.ms = Math.round(c.ms);
    // engine counters summed per phase (when traced)
    const ftPhase = {};
    for (const f of ft) {
        const ph = phaseOf(screenAt(f.t1));
        const o = ftPhase[ph] || (ftPhase[ph] = { frames: 0, frameMs: 0, fetchMs: 0, fetchN: 0, fetchMB: 0, texMs: 0, texN: 0, meshMs: 0, unpackMs: 0, pipeMs: 0, pipeN: 0, dtaMs: 0, streamMs: 0 });
        o.frames++; o.frameMs += f.t1 - f.t0; o.fetchMs += f.fetchMs; o.fetchN += f.fetchN; o.fetchMB += f.fetchBytes / 1048576;
        o.texMs += f.texMs; o.texN += f.texN; o.meshMs += f.meshMs; o.unpackMs += f.unpackMs; o.pipeMs += f.pipeMs; o.pipeN += f.pipeN;
        o.dtaMs += f.dtaMs; o.streamMs += f.streamMs;
    }
    for (const o of Object.values(ftPhase)) for (const k of Object.keys(o)) if (k !== 'frames' && k !== 'fetchN' && k !== 'texN' && k !== 'pipeN') o[k] = Math.round(o[k]);
    const underruns = raw.underrun.length ? raw.underrun[raw.underrun.length - 1][1] : null;
    const worst = [...stalls].sort((x, y) => y.ms - x.ms).slice(0, top).map(({ a, b, ...rest }) => rest);
    return { phases, raf: rafDist, stallThresholdMs: stallMs, stalls: stalls.length, causes, worst,
        engine: ft.length ? ftPhase : null, heapGrowths: raw.heap.length, underruns, longtasks: raw.lt.length,
        netRequests: raw.net.length };
}

export function formatPacing(rep) {
    if (rep.error) return `PACING ${rep.error}`;
    const lines = [];
    const order = ['boot', 'menu', 'song_select', 'loading', 'game', 'results', 'all'];
    lines.push('PACING game-frame interval (ms) by phase:');
    lines.push('  phase        frames   secs   fps    p50   p95    p99     max  >25  >50 >100 >250 >1s  stall-ms');
    for (const k of order) {
        const d = rep.phases[k];
        if (!d) continue;
        lines.push(`  ${k.padEnd(11)} ${String(d.frames).padStart(7)} ${String(d.seconds).padStart(6)} ${String(d.fps).padStart(5)} ${String(d.p50).padStart(6)} ${String(d.p95).padStart(5)} ${String(d.p99).padStart(6)} ${String(d.max).padStart(7)} ${String(d.over25).padStart(4)} ${String(d.over50).padStart(4)} ${String(d.over100).padStart(4)} ${String(d.over250).padStart(4)} ${String(d.over1000).padStart(3)} ${String(d.stallMs).padStart(9)}`);
    }
    const r = rep.raf;
    lines.push(`  rAF (browser) frames ${r.frames} fps ${r.fps} p50 ${r.p50} p95 ${r.p95} p99 ${r.p99} max ${r.max} >50 ${r.over50}`);
    lines.push(`PACING stalls > ${rep.stallThresholdMs} ms: ${rep.stalls}; by cause:`);
    for (const [c, v] of Object.entries(rep.causes).sort((x, y) => y[1].ms - x[1].ms))
        lines.push(`  ${c.padEnd(40)} n=${v.n} total ${v.ms} ms  ${JSON.stringify(v.phases)}`);
    if (rep.engine) {
        lines.push('PACING engine counters per phase (ms summed over the phase):');
        for (const [k, o] of Object.entries(rep.engine)) lines.push(`  ${k.padEnd(11)} ${JSON.stringify(o)}`);
    }
    lines.push(`PACING heap growths ${rep.heapGrowths}, audio underrun events ${rep.underruns}, longtasks ${rep.longtasks}, requests ${rep.netRequests}`);
    lines.push('PACING worst stalls:');
    for (const s of rep.worst.slice(0, 12)) {
        lines.push(`  ${String(s.at).padStart(7)}s ${String(s.ms).padStart(7)} ms ${s.screen} -> ${s.cause}` +
            (s.ft ? ` ft=${JSON.stringify(s.ft)}` : '') +
            (s.net.length ? ` net=${s.net.map((n) => `${n.url}(${n.kind} ${n.ovMs}/${n.ms}ms ${n.bytes}B)`).join(',')}` : '') +
            (s.lt.length ? ` lt=${s.lt.join(',')}` : '') + (s.scripts.length ? ` loaf=[${s.scripts.join('; ')}]` : '') +
            (s.heapMB.length ? ` heap->${s.heapMB.join(',')}MB` : '') + ` rafGap=${s.rafGapMs}`);
    }
    return lines.join('\n');
}

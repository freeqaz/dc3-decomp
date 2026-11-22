// scripts/web/phone_pointer.mjs <port> <outdir> [landscape|portrait] [extra query]
// (serve with: python3 native/web/server.py --port <port>; a debug web build)
// np-pointer web check: Pixel 7 (emulated), touch only, landscape or portrait (argv[4]).
// tap the title -> tap main-menu rows -> song select -> flick the song list -> tap a song.
// Every row position is READ from the engine (window.dc3PointerState(): the rows it
// drew, mapped to page px through dc3PictureBox and the present rectangle) -- nothing
// about the canvas's place or shape is assumed here.
import { createRequire } from 'module';
const require = createRequire(import.meta.url); // playwright: the same install scripts/web/phone.mjs uses
const { chromium, devices } = require('playwright');
const port = +process.argv[2], out = process.argv[3], orient = process.argv[4] || 'landscape', extra = process.argv[5] || '';
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const browser = await chromium.launch({ headless: !process.env.DISPLAY,
  args: ['--no-sandbox', '--enable-unsafe-webgpu', '--use-angle=vulkan', '--ozone-platform=x11',
    '--enable-features=Vulkan,VulkanFromANGLE,WebAssemblyJSPromiseIntegration', '--mute-audio',
    '--autoplay-policy=no-user-gesture-required'] });
const pixel = devices['Pixel 7'];
const vp = orient === 'portrait' ? pixel.viewport : { width: pixel.viewport.height, height: pixel.viewport.width };
const ctx = await browser.newContext({ ...pixel, viewport: vp });
const page = await ctx.newPage();
const st = { screen: '', screens: [], ptr: [] };
page.on('console', (m) => { const t = m.text();
  const s = /will enter '([^']+)'/.exec(t); if (s) { st.screen = s[1]; st.screens.push(s[1]); }
  if (t.includes('[dc3-pointer]')) { st.ptr.push(t); console.log('   ', t); } });
let fails = 0; const check = (c, m) => { console.log(`${c ? 'ok  ' : 'FAIL'} ${m}`); if (!c) fails++; };
const waitFor = async (fn, ms) => { const t = Date.now(); while (Date.now() - t < ms) { if (await fn()) return true; await sleep(250); } return false; };
const t0 = Date.now();
await page.goto(`http://127.0.0.1:${port}/?debug=true&dev=0&pose=off${orient === "portrait" ? "&portrait=1" : ""}${extra}`, { waitUntil: 'domcontentloaded' });
// boot: the title, or an attract screen that waits for a press (the taps below go through it)
check(await waitFor(() => st.screen === 'title_screen' || (st.screen === 'attract_screen' && st.screens.length && Date.now() - t0 > 60000), 180000),
  `booted to ${st.screen} (${st.screens.join(' > ')})`);
await sleep(2000);
const S = () => page.evaluate(() => window.dc3PointerState && window.dc3PointerState());
const s0 = await S();
console.log('portrait layout', JSON.stringify(await page.evaluate(() => window.dc3Portrait && { mode: window.dc3Portrait.mode, active: window.dc3Portrait.active })));
console.log('viewport', JSON.stringify(vp), 'canvas', JSON.stringify(s0 && s0.canvas), 'picture', JSON.stringify(s0 && s0.picture), 'present', JSON.stringify(s0 && s0.present));
// the list to tap: right_hand.hnl if drawn with rows, else the first list with rows
const rowOf = (s, name, row) => { if (!s) return null;
  const ls = s.lists.filter((l) => l.rows.length && (!name || l.name === name));
  const l = ls[0] || s.lists.find((l) => l.rows.length); if (!l) return null;
  const r = row == null ? l.rows[0] : l.rows.find((r) => r.row === row) || l.rows[0];
  return { list: l.name, row: r.row, page: r.page, frame: [Math.round(r.x), Math.round(r.y)] }; };
const shot = (n) => page.screenshot({ path: `${out}/${orient}_${n}.png` });
await shot('01_title');
// the title: its row if it draws one, else the picture's centre (A on a screen with no rows)
{ const t = rowOf(await S(), 'right_hand.hnl', 0), pic = s0.picture;
  const at = t ? t.page : [pic[0] + pic[2] / 2, pic[1] + pic[3] / 2];
  await page.touchscreen.tap(at[0], at[1]);
  check(await waitFor(() => st.screen && st.screen !== 'title_screen', 20000),
    `a tap on the title (${t ? `${t.list} row ${t.row}` : 'centre'}, page ${at.map(Math.round)}) moved on -> ${st.screen}`); }
await sleep(4000); await shot('02_menu');
for (let i = 0; i < 5 && !st.screen.includes('song_select'); i++) {
  const s = st.screen; let t = null;
  await waitFor(async () => (t = rowOf(await S(), 'right_hand.hnl', 0)), 8000);
  if (!t) { console.log('   no rows on', s); break; }
  await page.touchscreen.tap(t.page[0], t.page[1]);
  const moved = await waitFor(() => st.screen !== s, 15000);
  check(moved, `tap ${t.list} row ${t.row} (frame ${t.frame}, page ${t.page.map(Math.round)}) on ${s} -> ${st.screen}`);
  await sleep(4000);
}
check(st.screen.includes('song_select'), `reached song select by taps (${st.screens.join(' > ')})`);
await sleep(4000); await shot('03_song_select');
let ss = await S();
const songList = (ss.lists.find((l) => l.rows.length && l.scrollable) || ss.lists.find((l) => l.rows.length));
console.log('song list', songList && songList.name, 'first_showing', songList && songList.first_showing, 'selected', songList && songList.selected,
  'rows', songList && songList.rows.map((r) => `${r.row}@${r.page.map(Math.round)}`).join(' '));
// a finger flick up the song list: CDP touch events (pointerType touch)
const cdp = await ctx.newCDPSession(page);
const top = songList.rows[0].page, bot = songList.rows[songList.rows.length - 1].page;
const a = { x: bot[0], y: bot[1] }, b = { x: top[0], y: top[1] };
const before = ss.steps_done, selBefore = ss.tap_selects;
await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: a.x, y: a.y }] });
for (let i = 1; i <= 8; i++) { await sleep(16); const y = a.y + (b.y - a.y) * i / 8;
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: a.x, y }] }); }
await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
await sleep(5000);
ss = await S(); const sl2 = ss.lists.find((l) => l.name === songList.name);
check(ss.steps_done > before, `a flick from page y ${Math.round(a.y)} to ${Math.round(b.y)} scrolled the song list: ${ss.steps_done - before} rows, first_showing ${songList.first_showing} -> ${sl2 && sl2.first_showing}`);
check(ss.tap_selects === selBefore && st.screen.includes('song_select'), `the flick selected nothing (still ${st.screen})`);
await shot('04_after_flick');
// tap a song: the classic list's rows can be section headers (a tap opens one);
// so tap the third drawn row, then the row after the one tapped, up to 3 taps
const name0 = st.screen; let want = sl2.rows[Math.min(2, sl2.rows.length - 1)].row, picked = false;
for (let k = 0; k < 3 && !picked; k++) {
  const cur = (await S()).lists.find((l) => l.name === songList.name);
  const pick = cur.rows.find((r) => r.row === want) || cur.rows[Math.min(2, cur.rows.length - 1)];
  await page.touchscreen.tap(pick.page[0], pick.page[1]);
  await sleep(800); await shot(`05_tap_song${k}`);
  picked = await waitFor(() => st.screen !== name0, 8000);
  console.log(`   tap row ${pick.row} (page ${pick.page.map(Math.round)}): ${st.ptr.slice(-1)[0] || ''} -> ${st.screen}`);
  want = pick.row + 1;
}
check(picked, `taps on the song list picked a song -> ${st.screen}`);
await sleep(3000); await shot('06_after_pick');
console.log(fails ? `${fails} FAILED` : 'all ok');
await browser.close(); process.exit(fails ? 1 : 0);

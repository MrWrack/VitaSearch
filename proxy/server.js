import express from 'express';
import { chromium } from 'playwright';
import crypto from 'crypto';
import fs from 'fs';
import http from 'http';
import https from 'https';
import path from 'path';
import dns from 'dns/promises';
import net from 'net';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PORT = Number(process.env.PORT || 8080);
const HTTPS_PORT = Number(process.env.HTTPS_PORT || 8443);
const TLS_CERT_FILE = String(process.env.TLS_CERT_FILE || '');
const TLS_KEY_FILE = String(process.env.TLS_KEY_FILE || '');
const TLS_CA_FILE = String(process.env.TLS_CA_FILE || '');
const HTTPS_ENABLED = Boolean(TLS_CERT_FILE && TLS_KEY_FILE);
const REDIRECT_HTTP_TO_HTTPS = /^(1|true|yes)$/i.test(String(process.env.REDIRECT_HTTP_TO_HTTPS || (HTTPS_ENABLED ? '1' : '0')));
const HOST = process.env.HOST || '0.0.0.0';
const WIDTH = 960;
const HEIGHT = 544;
const SPOTIFY_CLIENT_ID = process.env.SPOTIFY_CLIENT_ID || '';
const SPOTIFY_REDIRECT_URI = process.env.SPOTIFY_REDIRECT_URI || `${HTTPS_ENABLED ? 'https' : 'http'}://127.0.0.1:${HTTPS_ENABLED ? HTTPS_PORT : PORT}/spotify/callback`;
const TOKEN_FILE = process.env.SPOTIFY_TOKEN_FILE || path.join(__dirname, 'spotify-token.json');
const API_KEY = String(process.env.VITASEARCH_API_KEY || '');
const ALLOW_PRIVATE_TARGETS = /^(1|true|yes)$/i.test(String(process.env.ALLOW_PRIVATE_TARGETS || '0'));
const MAX_SESSIONS = Math.max(1, Math.min(12, Number(process.env.MAX_SESSIONS || 6)));
const sessions = new Map();
const MAX_TABS_PER_SESSION = 6;

const oauthStates = new Map();
const searchHistory = [];
const SEARCH_HISTORY_MAX = 100;
let spotifyToken = loadToken();

const app = express();
process.on('unhandledRejection', (reason) => {
  console.error('[VitaSearch] unhandled rejection:', reason);
});
process.on('uncaughtException', (error) => {
  console.error('[VitaSearch] uncaught exception:', error);
});
app.disable('x-powered-by');
app.use(express.json({ limit: '128kb' }));
app.use(express.urlencoded({ extended: false, limit: '64kb' }));
app.use((_req,res,next)=>{
  res.set('X-Content-Type-Options','nosniff');
  res.set('Referrer-Policy','no-referrer');
  res.set('Permissions-Policy','camera=(), microphone=(), geolocation=()');
  res.set('Cache-Control','no-store');
  next();
});

if (!API_KEY && !['127.0.0.1','::1','localhost'].includes(HOST)) {
  throw new Error('VITASEARCH_API_KEY is required when proxy is exposed on the network. Use a random value of at least 24 characters.');
}
if (API_KEY && API_KEY.length < 16) throw new Error('VITASEARCH_API_KEY must be at least 16 characters.');

const browser = await chromium.launch({ headless: true });

function loadToken() {
  try { return JSON.parse(fs.readFileSync(TOKEN_FILE, 'utf8')); }
  catch { return null; }
}
function saveToken(token) {
  spotifyToken = token;
  fs.writeFileSync(TOKEN_FILE, JSON.stringify(token, null, 2), { mode: 0o600 });
}
function b64url(buf) {
  return Buffer.from(buf).toString('base64').replace(/=/g, '').replace(/\+/g, '-').replace(/\//g, '_');
}
function makeVerifier() { return b64url(crypto.randomBytes(64)); }
function challenge(verifier) { return b64url(crypto.createHash('sha256').update(verifier).digest()); }
function htmlEscape(s='') { return String(s).replace(/[&<>\"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c])); }

function safeEqual(a,b){
  const aa=Buffer.from(String(a||'')), bb=Buffer.from(String(b||''));
  return aa.length===bb.length && aa.length>0 && crypto.timingSafeEqual(aa,bb);
}
function requestKey(req){ return String(req.get('X-VitaSearch-Key') || req.query?.key || ''); }
function requireKey(req,res,next){
  if (!API_KEY || safeEqual(requestKey(req), API_KEY)) return next();
  console.warn(`[auth] rejected ${req.method} ${req.path}: missing/wrong X-VitaSearch-Key`);
  return res.status(401).json({ok:false,error:'Unauthorized'});
}

function isPrivateIp(ip){
  if (!ip) return true;
  if (ip.startsWith('::ffff:')) ip=ip.slice(7);
  if (net.isIP(ip)===4){
    const p=ip.split('.').map(Number), a=p[0], b=p[1];
    return a===10 || a===127 || a===0 || (a===169&&b===254) || (a===172&&b>=16&&b<=31) || (a===192&&b===168) || a>=224;
  }
  if (net.isIP(ip)===6){
    const x=ip.toLowerCase();
    return x==='::1' || x==='::' || x.startsWith('fc') || x.startsWith('fd') || x.startsWith('fe8') || x.startsWith('fe9') || x.startsWith('fea') || x.startsWith('feb');
  }
  return true;
}
async function assertSafeUrl(raw){
  const u=new URL(raw);
  if (!['http:','https:'].includes(u.protocol)) throw new Error('Only HTTP/HTTPS targets are allowed');
  const selfSpotify=([String(PORT),String(HTTPS_PORT)].includes(u.port) || (!u.port && (PORT===80 || HTTPS_PORT===443))) && ['127.0.0.1','localhost','::1'].includes(u.hostname.toLowerCase()) && u.pathname.startsWith('/spotify');
  if (selfSpotify || ALLOW_PRIVATE_TARGETS) return u;
  const h=u.hostname.toLowerCase();
  if (h==='localhost' || h.endsWith('.local')) throw new Error('Private/local targets are blocked');
  const addrs=await dns.lookup(h,{all:true,verbatim:true});
  if (!addrs.length || addrs.some(x=>isPrivateIp(x.address))) throw new Error('Private/local targets are blocked');
  return u;
}
const rateBuckets=new Map();
function rateLimit(req,res,next){
  const key=req.ip || req.socket.remoteAddress || 'unknown', now=Date.now();
  let b=rateBuckets.get(key); if(!b || now-b.start>60000) b={start:now,count:0};
  b.count++; rateBuckets.set(key,b);
  if(b.count>600) return res.status(429).json({ok:false,error:'Too many requests'});
  next();
}
app.use(rateLimit);

async function createSession(options={}) {
  if (sessions.size >= MAX_SESSIONS && !options.id) throw new Error(`Session limit reached (${MAX_SESSIONS})`);
  const id = options.id || crypto.randomUUID();
  const javascriptEnabled = options.javascriptEnabled !== false;
  const context = await browser.newContext({
    viewport: { width: WIDTH, height: HEIGHT },
    deviceScaleFactor: 1,
    javaScriptEnabled: javascriptEnabled,
    userAgent: 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/140 Safari/537.36 VitaSearchProxy/0.8'
  });
  await context.route('**/*', async route => {
    try { await assertSafeUrl(route.request().url()); await route.continue(); }
    catch { await route.abort('blockedbyclient'); }
  });
  const page = await context.newPage();
  page.setDefaultNavigationTimeout(30000);
  await page.setContent(`<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=960,height=544"><style>body{margin:0;background:#f7f8fa;color:#1b1f23;font:20px Arial,sans-serif;display:flex;align-items:center;justify-content:center;height:544px}.box{text-align:center;width:760px}.logo{font-size:52px;font-weight:800;color:#202124}.g{color:#20c96b}.hint{margin-top:22px;color:#60656b}.bar{margin:28px auto 0;background:white;border:1px solid #d9dde3;border-radius:28px;padding:16px 24px;width:650px;box-shadow:0 2px 8px #0001}</style></head><body><div class="box"><div class="logo">Vita<span class="g">Search</span></div><div class="bar">Tap the address bar above or press □ to search Google</div><div class="hint">Chromium proxy connected · browser session ready</div></div></body></html>`);
  sessions.set(id, { context, page, lastUsed: Date.now(), javascriptEnabled });
  return { id, context, page, javascriptEnabled };
}


function ensureTabState(session) {
  if (!session.pages) {
    const p = session.page;
    session.pages = p ? [p] : [];
    session.activeTab = 0;
  }
  if (typeof session.activeTab !== 'number') session.activeTab = 0;
  if (session.activeTab < 0) session.activeTab = 0;
  if (session.pages.length && session.activeTab >= session.pages.length) session.activeTab = session.pages.length - 1;
  session.page = session.pages[session.activeTab] || session.page;
  return session;
}

function activePage(session) {
  ensureTabState(session);
  return session.pages[session.activeTab] || session.page;
}

async function listTabs(session) {
  ensureTabState(session);
  const tabs = [];
  for (let i = 0; i < session.pages.length; i++) {
    const p = session.pages[i];
    let title = '';
    let url = '';
    try { title = await p.title(); } catch (_) {}
    try { url = p.url(); } catch (_) {}
    tabs.push({ index: i, title: title || 'New Tab', url: url || '', active: i === session.activeTab });
  }
  return tabs;
}

async function recreateSession(id, options={}) {
  const old = sessions.get(id);
  let javascriptEnabled = options.javascriptEnabled;
  if (javascriptEnabled === undefined) javascriptEnabled = old?.javascriptEnabled ?? true;

  let oldUrls = ['https://www.google.com/'];
  let activeTab = 0;
  if (old) {
    ensureTabState(old);
    activeTab = old.activeTab || 0;
    oldUrls = old.pages.length ? old.pages.map(p => {
      try { return p.url() || 'https://www.google.com/'; } catch (_) { return 'https://www.google.com/'; }
    }) : oldUrls;
    try { await old.context.close(); } catch (_) {}
  }

  const context = await browser.newContext({
    viewport: { width: 960, height: 544 },
    javaScriptEnabled
  });

  const pages = [];
  for (const url of oldUrls.slice(0, MAX_TABS_PER_SESSION)) {
    const p = await context.newPage();
    try { await p.goto(url, { waitUntil: 'domcontentloaded', timeout: 30000 }); } catch (_) {}
    pages.push(p);
  }
  if (!pages.length) pages.push(await context.newPage());

  if (activeTab >= pages.length) activeTab = pages.length - 1;
  const session = { context, pages, activeTab, page: pages[activeTab], lastUsed: Date.now(), javascriptEnabled };
  sessions.set(id, session);
  return session;
}

async function session(req) {
  const id = String(req.query.session || req.body?.session || '');
  let s = sessions.get(id);
  if (!s) s = await createSession();
  s.lastUsed = Date.now();
  return { id: [...sessions.entries()].find(([, v]) => v === s)?.[0], ...s };
}

function normalizeTarget(input, engine='google', useSelected=true) {
  const q = String(input || '').trim();
  if (!q) return 'https://www.google.com/';
  if (/^https?:\/\//i.test(q)) return q;
  if (/^[\w.-]+\.[a-z]{2,}(\/.*)?$/i.test(q)) return `https://${q}`;
  const selected = useSelected ? String(engine || 'google').toLowerCase() : 'google';
  if (selected === 'bing') return `https://www.bing.com/search?q=${encodeURIComponent(q)}`;
  if (selected === 'duckduckgo' || selected === 'ddg') return `https://duckduckgo.com/?q=${encodeURIComponent(q)}`;
  return `https://www.google.com/search?q=${encodeURIComponent(q)}`;
}

async function refreshSpotifyToken() {
  if (!spotifyToken?.refresh_token) throw new Error('Spotify is not connected');
  const body = new URLSearchParams({
    grant_type: 'refresh_token', refresh_token: spotifyToken.refresh_token, client_id: SPOTIFY_CLIENT_ID
  });
  const r = await fetch('https://accounts.spotify.com/api/token', {
    method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body
  });
  const data = await r.json();
  if (!r.ok) throw new Error(data.error_description || data.error || 'Token refresh failed');
  saveToken({ ...spotifyToken, ...data, refresh_token: data.refresh_token || spotifyToken.refresh_token,
    expires_at: Date.now() + (data.expires_in || 3600) * 1000 });
  return spotifyToken.access_token;
}

async function spotifyAccessToken() {
  if (!spotifyToken) throw new Error('Spotify is not connected');
  if (!spotifyToken.expires_at || Date.now() > spotifyToken.expires_at - 60000) return refreshSpotifyToken();
  return spotifyToken.access_token;
}

async function spotifyApi(endpoint, options={}) {
  let token = await spotifyAccessToken();
  const request = async () => fetch(`https://api.spotify.com/v1${endpoint}`, {
    ...options,
    headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json', ...(options.headers || {}) }
  });
  let r = await request();
  if (r.status === 401 && spotifyToken?.refresh_token) { token = await refreshSpotifyToken(); r = await request(); }
  if (r.status === 204) return null;
  const text = await r.text();
  let data = null; try { data = text ? JSON.parse(text) : null; } catch { data = { raw: text }; }
  if (!r.ok) throw new Error(data?.error?.message || data?.error_description || `Spotify HTTP ${r.status}`);
  return data;
}

app.get('/health', async (req, res) => {
  const started = Date.now();
  let internet = false;
  let internetError = '';
  try {
    const r = await fetch('https://www.google.com/generate_204', { redirect: 'manual' });
    internet = r.status === 204 || (r.status >= 200 && r.status < 400);
  } catch (e) {
    internetError = String(e?.message || e || 'internet_check_failed').slice(0, 120);
  }

  res.json({
    ok: true,
    proxy: true,
    internet,
    https: !!req.secure,
    protocol: req.protocol,
    latency_ms: Date.now() - started,
    active_sessions: sessions.size,
    max_sessions: MAX_SESSIONS,
    internet_error: internet ? '' : internetError
  });
});

app.post('/session', requireKey, async (req, res) => {
  console.log('[session] create request accepted');
  try {
    const s = await createSession();
    return res.json({ ok: true, session: s.id, javascriptEnabled: s.javascriptEnabled !== false });
  } catch (e) {
    console.error('session_create_failed', e);
    return res.status(500).json({ ok: false, error: 'session_create_failed', detail: String(e?.message || e || '') });
  }
});

app.get('/tabs', requireKey, async (req, res) => {
  try {
    const id = String(req.query.session || '');
    const session = sessions.get(id);
    if (!session) return res.status(404).json({ error: 'session_not_found' });
    session.lastUsed = Date.now();
    return res.json({ active: session.activeTab || 0, tabs: await listTabs(session) });
  } catch (_) {
    return res.status(500).json({ error: 'tabs_failed' });
  }
});

app.post('/tabs/new', requireKey, async (req, res) => {
  try {
    const id = String(req.body?.session || '');
    const session = sessions.get(id);
    if (!session) return res.status(404).json({ error: 'session_not_found' });
    ensureTabState(session);
    if (session.pages.length >= MAX_TABS_PER_SESSION) return res.status(409).json({ error: 'tab_limit', limit: MAX_TABS_PER_SESSION });
    const page = await session.context.newPage();
    session.pages.push(page);
    session.activeTab = session.pages.length - 1;
    session.page = page;
    session.lastUsed = Date.now();
    try { await page.goto('https://www.google.com/', { waitUntil: 'domcontentloaded', timeout: 30000 }); } catch (_) {}
    return res.json({ ok: true, active: session.activeTab, tabs: await listTabs(session) });
  } catch (_) {
    return res.status(500).json({ error: 'tab_new_failed' });
  }
});

app.post('/tabs/select', requireKey, async (req, res) => {
  try {
    const id = String(req.body?.session || '');
    const index = Number(req.body?.index);
    const session = sessions.get(id);
    if (!session) return res.status(404).json({ error: 'session_not_found' });
    ensureTabState(session);
    if (!Number.isInteger(index) || index < 0 || index >= session.pages.length) return res.status(400).json({ error: 'invalid_tab' });
    session.activeTab = index;
    session.page = session.pages[index];
    session.lastUsed = Date.now();
    return res.json({ ok: true, active: index, tabs: await listTabs(session) });
  } catch (_) {
    return res.status(500).json({ error: 'tab_select_failed' });
  }
});

app.post('/tabs/close', requireKey, async (req, res) => {
  try {
    const id = String(req.body?.session || '');
    const index = Number(req.body?.index);
    const session = sessions.get(id);
    if (!session) return res.status(404).json({ error: 'session_not_found' });
    ensureTabState(session);
    if (!Number.isInteger(index) || index < 0 || index >= session.pages.length) return res.status(400).json({ error: 'invalid_tab' });

    if (session.pages.length === 1) {
      try { await session.pages[0].goto('https://www.google.com/', { waitUntil: 'domcontentloaded', timeout: 30000 }); } catch (_) {}
      session.activeTab = 0;
      session.page = session.pages[0];
      return res.json({ ok: true, active: 0, tabs: await listTabs(session) });
    }

    const [closed] = session.pages.splice(index, 1);
    try { await closed.close(); } catch (_) {}
    if (session.activeTab > index) session.activeTab--;
    else if (session.activeTab === index) session.activeTab = Math.min(index, session.pages.length - 1);
    session.page = session.pages[session.activeTab];
    session.lastUsed = Date.now();
    return res.json({ ok: true, active: session.activeTab, tabs: await listTabs(session) });
  } catch (_) {
    return res.status(500).json({ error: 'tab_close_failed' });
  }
});

app.post('/open', requireKey, async (req, res) => {
  try {
    const s = await session(req);
    const raw = String(req.body?.url || '').trim();
    const isSearch = raw && !/^https?:\/\//i.test(raw) && !/^[\w.-]+\.[a-z]{2,}(\/.*)?$/i.test(raw);
    if (isSearch) { searchHistory.unshift({ q: raw.slice(0, 256), at: Date.now() }); if (searchHistory.length > SEARCH_HISTORY_MAX) searchHistory.length = SEARCH_HISTORY_MAX; }
    const url = normalizeTarget(raw, req.body?.search_engine || 'google', req.body?.use_selected_search !== false); await assertSafeUrl(url);
    await s.page.goto(url, { waitUntil: 'domcontentloaded' });
    res.json({ ok: true, session: s.id, url: s.page.url(), title: await s.page.title(), javascriptEnabled: s.javascriptEnabled });
  } catch (e) { res.status(500).json({ ok: false, error: String(e) }); }
});
app.post('/click', requireKey, async (req, res) => {
  try { const s = await session(req); const x=Math.max(0,Math.min(WIDTH-1,Number(req.body?.x||0))); const y=Math.max(0,Math.min(HEIGHT-1,Number(req.body?.y||0))); await s.page.mouse.click(x,y); await s.page.waitForTimeout(250); res.json({ok:true,session:s.id,url:s.page.url(),title:await s.page.title()}); }
  catch(e){res.status(500).json({ok:false,error:String(e)});}
});
app.post('/scroll', requireKey, async(req,res)=>{try{const s=await session(req);await s.page.mouse.wheel(Number(req.body?.dx||0),Number(req.body?.dy||0));await s.page.waitForTimeout(12);res.json({ok:true,session:s.id});}catch(e){res.status(500).json({ok:false,error:String(e)});}});

// RC44: scrolling and the refreshed 960x544 frame in one LAN round trip.
app.post('/scroll-frame', requireKey, async(req,res)=>{
  try{
    const s=await session(req);
    const dy=Math.max(-180,Math.min(180,Number(req.body?.dy||0)));
    await s.page.mouse.wheel(0,dy);
    await s.page.waitForTimeout(8);
    const png=await s.page.screenshot({type:'png'});
    res.set('X-VitaSearch-Session',s.id);
    res.type('png').send(png);
  }catch(e){res.status(500).json({ok:false,error:String(e)});}
});
app.post('/key', requireKey, async(req,res)=>{try{const s=await session(req);const key=String(req.body?.key||'');if(key)await s.page.keyboard.press(key);res.json({ok:true,session:s.id});}catch(e){res.status(500).json({ok:false,error:String(e)});}});
app.post('/text', requireKey, async(req,res)=>{try{const s=await session(req);await s.page.keyboard.type(String(req.body?.text||''),{delay:10});res.json({ok:true,session:s.id});}catch(e){res.status(500).json({ok:false,error:String(e)});}});
for (const [route,method] of [['/back','goBack'],['/forward','goForward']]) app.post(route,requireKey,async(req,res)=>{try{const s=await session(req);await s.page[method]({waitUntil:'domcontentloaded'}).catch(()=>null);res.json({ok:true,session:s.id,url:s.page.url()});}catch(e){res.status(500).json({ok:false,error:String(e)});}});
app.post('/reload',requireKey,async(req,res)=>{try{const s=await session(req);await s.page.reload({waitUntil:'domcontentloaded'});res.json({ok:true,session:s.id,url:s.page.url()});}catch(e){res.status(500).json({ok:false,error:String(e)});}});
app.get('/frame',requireKey,async(req,res)=>{try{const s=await session(req);const png=await s.page.screenshot({type:'png'});res.set('X-VitaSearch-Session',s.id);res.type('png').send(png);}catch(e){res.status(500).json({ok:false,error:String(e)});}});


// ---- Browser settings ----
app.get('/settings', requireKey, async(req,res)=>{
  try { const s=await session(req); res.json({ok:true,session:s.id,javascriptEnabled:s.javascriptEnabled!==false}); }
  catch(e){res.status(500).json({ok:false,error:String(e)});}
});
app.post('/settings/javascript', requireKey, async(req,res)=>{
  try {
    const id=String(req.body?.session||'');
    if(!id || !sessions.has(id)) return res.status(404).json({ok:false,error:'Session not found'});
    const enabled=!!req.body?.enabled;
    const old=sessions.get(id);
    const target=old?.page?.url?.() || 'https://www.google.com/';
    const ns=await recreateSession(id,{javascriptEnabled:enabled,target});
    res.json({ok:true,session:id,javascriptEnabled:ns.javascriptEnabled,url:ns.page.url()});
  } catch(e){res.status(500).json({ok:false,error:String(e)});}
});


// ---- Privacy controls ----
app.post('/privacy/clear', requireKey, async(req,res)=>{
  try {
    const what=String(req.body?.what||'all');
    const targets=req.body?.session ? [[String(req.body.session),sessions.get(String(req.body.session))]] : [...sessions.entries()];
    let cleared=0;
    for(const [id,s] of targets){
      if(!s) continue;
      if(what==='cookies'||what==='site-data'||what==='all') await s.context.clearCookies();
      if(what==='site-data'||what==='all') await s.page.evaluate(()=>{try{localStorage.clear()}catch{} try{sessionStorage.clear()}catch{}}).catch(()=>{});
      if(what==='cache'||what==='all') { const cdp=await s.context.newCDPSession(s.page).catch(()=>null); if(cdp){await cdp.send('Network.clearBrowserCache').catch(()=>{}); await cdp.detach().catch(()=>{});} }
      if(what==='history'||what==='all') await recreateSession(id,{javascriptEnabled:s.javascriptEnabled,target:'https://www.google.com/'});
      cleared++;
    }
    if(what==='search'||what==='all') searchHistory.length=0;
    res.json({ok:true,what,cleared,searchHistoryCount:searchHistory.length});
  } catch(e){res.status(500).json({ok:false,error:String(e)});}
});
app.post('/privacy/clear-spotify-token', requireKey, async(_req,res)=>{
  spotifyToken=null;
  try{fs.rmSync(TOKEN_FILE,{force:true});}catch{}
  res.json({ok:true});
});

// ---- Spotify PKCE + Connect controller ----
function createSpotifyAuthorizationUrl() {
  if (!SPOTIFY_CLIENT_ID) throw new Error('SPOTIFY_CLIENT_ID is not configured');
  const state = crypto.randomUUID();
  const verifier = makeVerifier();
  oauthStates.set(state, { verifier, created: Date.now() });
  const scopes = ['user-read-playback-state','user-modify-playback-state','user-read-currently-playing','user-read-private'];
  const u = new URL('https://accounts.spotify.com/authorize');
  Object.entries({response_type:'code',client_id:SPOTIFY_CLIENT_ID,redirect_uri:SPOTIFY_REDIRECT_URI,scope:scopes.join(' '),state,code_challenge_method:'S256',code_challenge:challenge(verifier)}).forEach(([k,v])=>u.searchParams.set(k,v));
  return u.toString();
}

app.get('/spotify/login', requireKey, (req,res) => {
  try { res.redirect(createSpotifyAuthorizationUrl()); }
  catch (e) { res.status(500).send('Set SPOTIFY_CLIENT_ID on the proxy first.'); }
});

/* RC41: the Vita native Connect button asks the proxy to navigate the existing
   Chromium session directly. This avoids trying to open the LAN proxy URL as
   an external/private target, which the SSRF guard correctly blocks. */
app.post('/spotify/session-login', requireKey, async (req,res) => {
  try {
    const s = await session(req);
    const authUrl = createSpotifyAuthorizationUrl();
    await s.page.goto(authUrl, { waitUntil:'domcontentloaded', timeout:30000 });
    console.log('[spotify] login page opened in session', s.id);
    res.json({ok:true,session:s.id,url:s.page.url()});
  } catch (e) {
    console.warn('[spotify] session-login failed:', String(e?.message || e));
    res.status(400).json({ok:false,error:String(e?.message || e || 'spotify_login_failed')});
  }
});

app.get('/spotify/callback', async(req,res)=>{
  try {
    const code=String(req.query.code||''), state=String(req.query.state||''); const pending=oauthStates.get(state);
    if(!code||!pending) throw new Error('Invalid Spotify callback/state'); oauthStates.delete(state);
    const body=new URLSearchParams({grant_type:'authorization_code',code,redirect_uri:SPOTIFY_REDIRECT_URI,client_id:SPOTIFY_CLIENT_ID,code_verifier:pending.verifier});
    const r=await fetch('https://accounts.spotify.com/api/token',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
    const data=await r.json(); if(!r.ok) throw new Error(data.error_description||data.error||'Token exchange failed');
    saveToken({...data,expires_at:Date.now()+(data.expires_in||3600)*1000});
    res.redirect('/spotify?key='+encodeURIComponent(API_KEY));
  } catch(e){res.status(500).send(`<pre>${htmlEscape(String(e))}</pre>`);}
});

app.get('/spotify/api/state', requireKey, async(_req,res)=>{try{const p=await spotifyApi('/me/player');res.json({ok:true,player:p});}catch(e){res.status(401).json({ok:false,error:String(e)});}});
app.get('/spotify/api/devices', requireKey, async(_req,res)=>{try{res.json({ok:true,...await spotifyApi('/me/player/devices')});}catch(e){res.status(401).json({ok:false,error:String(e)});}});
app.get('/spotify/api/search', requireKey, async(req,res)=>{try{const q=encodeURIComponent(String(req.query.q||''));const d=await spotifyApi(`/search?q=${q}&type=track&limit=8`);res.json({ok:true,items:d?.tracks?.items||[]});}catch(e){res.status(400).json({ok:false,error:String(e)});}});
app.post('/spotify/api/play', requireKey, async(req,res)=>{try{const body=req.body?.uri?JSON.stringify({uris:[String(req.body.uri)]}):undefined;await spotifyApi('/me/player/play',{method:'PUT',body});res.json({ok:true});}catch(e){res.status(400).json({ok:false,error:String(e)});}});
app.post('/spotify/api/pause', requireKey, async(_req,res)=>{try{await spotifyApi('/me/player/pause',{method:'PUT'});res.json({ok:true});}catch(e){res.status(400).json({ok:false,error:String(e)});}});
app.post('/spotify/api/next', requireKey, async(_req,res)=>{try{await spotifyApi('/me/player/next',{method:'POST'});res.json({ok:true});}catch(e){res.status(400).json({ok:false,error:String(e)});}});
app.post('/spotify/api/previous', requireKey, async(_req,res)=>{try{await spotifyApi('/me/player/previous',{method:'POST'});res.json({ok:true});}catch(e){res.status(400).json({ok:false,error:String(e)});}});
app.post('/spotify/api/transfer', requireKey, async(req,res)=>{try{const deviceId=String(req.body?.device_id||'');if(!deviceId)throw new Error('Missing device_id');await spotifyApi('/me/player',{method:'PUT',body:JSON.stringify({device_ids:[deviceId],play:req.body?.play!==false})});res.json({ok:true});}catch(e){res.status(400).json({ok:false,error:String(e)});}});
app.post('/spotify/api/volume', requireKey, async(req,res)=>{try{const volume=Math.max(0,Math.min(100,Number(req.body?.volume)));if(!Number.isFinite(volume))throw new Error('Invalid volume');await spotifyApi(`/me/player/volume?volume_percent=${Math.round(volume)}`,{method:'PUT'});res.json({ok:true,volume:Math.round(volume)});}catch(e){res.status(400).json({ok:false,error:String(e)});}});
app.post('/spotify/api/seek', requireKey, async(req,res)=>{try{const position=Math.max(0,Math.round(Number(req.body?.position_ms)||0));await spotifyApi(`/me/player/seek?position_ms=${position}`,{method:'PUT'});res.json({ok:true,position_ms:position});}catch(e){res.status(400).json({ok:false,error:String(e)});}});
app.post('/spotify/api/queue', requireKey, async(req,res)=>{try{const uri=String(req.body?.uri||'');if(!uri)throw new Error('Missing uri');await spotifyApi(`/me/player/queue?uri=${encodeURIComponent(uri)}`,{method:'POST'});res.json({ok:true});}catch(e){res.status(400).json({ok:false,error:String(e)});}});
app.get('/spotify/api/queue', requireKey, async(_req,res)=>{try{res.json({ok:true,...await spotifyApi('/me/player/queue')});}catch(e){res.status(400).json({ok:false,error:String(e)});}});
app.post('/spotify/api/shuffle', requireKey, async(req,res)=>{try{const state=!!req.body?.state;await spotifyApi(`/me/player/shuffle?state=${state}`,{method:'PUT'});res.json({ok:true,state});}catch(e){res.status(400).json({ok:false,error:String(e)});}});
app.post('/spotify/api/repeat', requireKey, async(req,res)=>{try{const state=['off','track','context'].includes(String(req.body?.state))?String(req.body.state):'off';await spotifyApi(`/me/player/repeat?state=${state}`,{method:'PUT'});res.json({ok:true,state});}catch(e){res.status(400).json({ok:false,error:String(e)});}});


// ---- Compact native-Vita Spotify endpoints ----

// VitaSearch v0.9: compact OAuth/callback health for the native Vita UI.
// This never returns access/refresh tokens.
app.get('/spotify/native/status', requireKey, async (req, res) => {
  try {
    let token = null;
    try { token = await spotifyAccessToken(); } catch (_) { token = null; }
    if (!token) {
      return res.json({
        connected: false,
        callback: spotifyToken ? 'token_expired' : 'not_connected',
        token: 'inactive',
        device: null
      });
    }

    let device = null;
    try {
      const body = await spotifyApi('/me/player/devices');
      {
        const active = (body.devices || []).find(d => d.is_active) || (body.devices || [])[0];
        if (active) device = { name: active.name || 'Spotify device', type: active.type || '' };
      }
    } catch (_) {}

    return res.json({
      connected: true,
      callback: 'ok',
      token: 'active',
      device
    });
  } catch (error) {
    return res.status(500).json({
      connected: false,
      callback: 'error',
      token: 'inactive',
      device: null,
      error: 'spotify_status_failed'
    });
  }
});

app.get('/spotify/native/state', requireKey, async (_req,res) => {
  if (!spotifyToken) return res.json({ok:true,connected:false,playing:false,title:'Spotify not connected',artist:'',device:'',progress_ms:0,duration_ms:0,volume:50,cover_url:''});
  try {
    const p = await spotifyApi('/me/player');
    const item = p?.item;
    res.json({
      ok:true, connected:true, playing:!!p?.is_playing,
      title:item?.name || 'No active playback',
      artist:(item?.artists || []).map(a=>a.name).join(', '),
      device:p?.device?.name || '', progress_ms:p?.progress_ms || 0,
      duration_ms:item?.duration_ms || 0, volume:p?.device?.volume_percent ?? 50,
      cover_url:item?.album?.images?.[1]?.url || item?.album?.images?.[0]?.url || ''
    });
  } catch(e) { res.status(400).json({ok:false,connected:true,error:String(e)}); }
});

app.get('/spotify/native/search', requireKey, async(req,res) => {
  try {
    const q=encodeURIComponent(String(req.query.q||''));
    const d=await spotifyApi(`/search?q=${q}&type=track&limit=8`);
    const items=(d?.tracks?.items||[]).map(x=>({
      name:x.name||'', artist:(x.artists||[]).map(a=>a.name).join(', '), uri:x.uri||'',
      cover_url:x.album?.images?.[2]?.url||x.album?.images?.[1]?.url||x.album?.images?.[0]?.url||''
    }));
    res.json({ok:true,items});
  } catch(e){res.status(400).json({ok:false,error:String(e)});}
});

app.get('/spotify/native/image', requireKey, async(req,res) => {
  try {
    const raw=String(req.query.url||''); const u=new URL(raw);
    const host=u.hostname.toLowerCase();
    if(u.protocol!=='https:' || !(host==='i.scdn.co' || host.endsWith('.scdn.co'))) throw new Error('Image host not allowed');
    const r=await fetch(u,{redirect:'follow'}); if(!r.ok) throw new Error(`Image HTTP ${r.status}`);
    const type=r.headers.get('content-type')||'image/jpeg'; const bytes=Buffer.from(await r.arrayBuffer());
    res.set('Cache-Control','public, max-age=3600'); res.type(type).send(bytes);
  } catch(e){res.status(400).json({ok:false,error:String(e)});}
});

app.get('/spotify', requireKey, (_req,res)=>res.type('html').send(`<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=960,height=544"><style>
*{box-sizing:border-box}body{margin:0;background:#0b0d10;color:#eef3f0;font:18px Arial,sans-serif}header{height:58px;background:#11151a;padding:9px 16px;display:flex;align-items:center;gap:12px}.brand{color:#39e676;font-weight:800;font-size:25px}.grow{flex:1}button,input,select{font:inherit;border:0;border-radius:8px;padding:8px 11px}button{background:#252c34;color:#fff;cursor:pointer}button.primary{background:#39e676;color:#07140b;font-weight:700}main{padding:12px}.bar{display:flex;gap:8px}.bar input{flex:1;background:#1b2026;color:#fff}.now{margin-top:10px;background:#151a20;border-radius:12px;padding:12px;display:flex;gap:14px;min-height:116px}.now img{width:92px;height:92px;border-radius:8px;object-fit:cover}.title{font-size:22px;font-weight:700}.muted{color:#aeb8b2}.controls{display:flex;gap:7px;margin-top:7px;flex-wrap:wrap}.sliders{display:flex;gap:10px;align-items:center;margin-top:7px;font-size:13px}.sliders input{padding:0}.device{margin-top:9px;display:flex;gap:8px;align-items:center}.device select{max-width:310px;background:#1b2026;color:#fff}.results{margin-top:10px;display:grid;grid-template-columns:1fr 1fr;gap:7px}.track{background:#171c22;border-radius:9px;padding:8px;display:flex;gap:9px;align-items:center;min-height:68px}.track img{width:52px;height:52px;border-radius:5px;object-fit:cover}.track .t{font-size:16px;font-weight:700}.track .a{font-size:13px;color:#aeb8b2}.track .actions{margin-left:auto;display:flex;gap:5px}.track .actions button{font-size:12px;padding:6px}#msg{font-size:13px;color:#ffcf66;margin-left:8px}</style></head><body>
<header><span class="brand">VitaSearch Spotify</span><span class="grow"></span><button onclick="location='/spotify/login?key='+encodeURIComponent(VS_KEY)">Connect Spotify</button></header>
<main><div class="bar"><input id="q" placeholder="Search Spotify tracks"><button class="primary" onclick="searchTracks()">Search</button><span id="msg"></span></div>
<div class="now"><img id="cover" alt=""><div class="grow"><div class="muted">NOW PLAYING</div><div id="title" class="title">Not connected / no active player</div><div id="artist" class="muted"></div><div class="controls"><button onclick="cmd('previous')">◀ Prev</button><button class="primary" onclick="cmd('play')">▶ Play</button><button onclick="cmd('pause')">Ⅱ Pause</button><button onclick="cmd('next')">Next ▶</button><button onclick="toggleShuffle()" id="shuffle">Shuffle</button><button onclick="cycleRepeat()" id="repeat">Repeat: off</button></div><div class="sliders"><span>Seek</span><input id="seek" type="range" min="0" max="1000" value="0" oninput="seeking=true" onchange="sendSeek()"><span id="time">0:00 / 0:00</span><span>Vol</span><input id="volume" type="range" min="0" max="100" value="50" onchange="setVolume()"></div><div class="device"><span>Device</span><select id="devices"></select><button onclick="transfer()">Use device</button></div></div></div>
<div id="results" class="results"></div></main><script>
const msg=t=>document.getElementById('msg').textContent=t||'';let duration=0,seeking=false,shuffleState=false,repeatState='off';const VS_KEY=${JSON.stringify(API_KEY)};
async function api(url,opt={}){opt.headers={...(opt.headers||{}),'X-VitaSearch-Key':VS_KEY};const r=await fetch(url,opt);const d=await r.json();if(!r.ok||d.ok===false)throw new Error(d.error||'Request failed');return d}
function fmt(ms){ms=Math.max(0,Number(ms)||0);const s=Math.floor(ms/1000);return Math.floor(s/60)+':'+String(s%60).padStart(2,'0')}
async function state(){try{const d=await api('/spotify/api/state');const p=d.player;if(!p||!p.item){msg('Open Spotify on a phone/PC to create an active device.');await loadDevices();return}duration=p.item.duration_ms||0;title.textContent=p.item.name;artist.textContent=(p.item.artists||[]).map(x=>x.name).join(', ');cover.src=p.item.album?.images?.[1]?.url||p.item.album?.images?.[0]?.url||'';if(!seeking&&duration){seek.value=Math.round((p.progress_ms||0)*1000/duration)}time.textContent=fmt(p.progress_ms)+' / '+fmt(duration);if(p.device?.volume_percent!=null)volume.value=p.device.volume_percent;shuffleState=!!p.shuffle_state;repeatState=p.repeat_state||'off';shuffle.textContent=shuffleState?'Shuffle ✓':'Shuffle';repeat.textContent='Repeat: '+repeatState;msg((p.is_playing?'Playing':'Paused')+(p.device?.name?' · '+p.device.name:''))}catch(e){msg('Connect Spotify first')}}
async function cmd(c){try{await api('/spotify/api/'+c,{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'});setTimeout(state,300)}catch(e){msg(e.message)}}
async function playUri(uri){try{await api('/spotify/api/play',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({uri})});setTimeout(state,450)}catch(e){msg(e.message)}}
async function addQueue(uri){try{await api('/spotify/api/queue',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({uri})});msg('Added to queue')}catch(e){msg(e.message)}}
async function loadDevices(){try{const d=await api('/spotify/api/devices');devices.innerHTML=(d.devices||[]).map(x=>'<option value="'+esc(x.id)+'" '+(x.is_active?'selected':'')+'>'+esc(x.name)+(x.type?' · '+esc(x.type):'')+'</option>').join('')}catch(e){}}
async function transfer(){try{if(!devices.value)return;await api('/spotify/api/transfer',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({device_id:devices.value,play:true})});setTimeout(state,500)}catch(e){msg(e.message)}}
async function setVolume(){try{await api('/spotify/api/volume',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({volume:Number(volume.value)})})}catch(e){msg(e.message)}}
async function sendSeek(){seeking=false;try{const pos=Math.round(duration*Number(seek.value)/1000);await api('/spotify/api/seek',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({position_ms:pos})});setTimeout(state,300)}catch(e){msg(e.message)}}
async function toggleShuffle(){shuffleState=!shuffleState;try{await api('/spotify/api/shuffle',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({state:shuffleState})});state()}catch(e){msg(e.message)}}
async function cycleRepeat(){repeatState=repeatState==='off'?'context':repeatState==='context'?'track':'off';try{await api('/spotify/api/repeat',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({state:repeatState})});state()}catch(e){msg(e.message)}}
async function searchTracks(){const text=q.value.trim();if(!text)return;try{const d=await api('/spotify/api/search?q='+encodeURIComponent(text));results.innerHTML=d.items.map(x=>{const im=x.album?.images?.[2]?.url||x.album?.images?.[0]?.url||'';const uri=JSON.stringify(x.uri).replace(/"/g,'&quot;');return '<div class="track"><img src="'+esc(im)+'"><div><div class="t">'+esc(x.name)+'</div><div class="a">'+esc((x.artists||[]).map(a=>a.name).join(', '))+'</div></div><div class="actions"><button class="primary" onclick="playUri('+uri+')">Play</button><button onclick="addQueue('+uri+')">+ Queue</button></div></div>'}).join('')}catch(e){msg(e.message)}}
function esc(s){return String(s||'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}q.addEventListener('keydown',e=>{if(e.key==='Enter')searchTracks()});loadDevices();state();setInterval(()=>{state();loadDevices()},5000);
</script></body></html>`));

setInterval(async()=>{const cutoff=Date.now()-30*60*1000;for(const[id,s]of sessions){if(s.lastUsed<cutoff){sessions.delete(id);await s.context.close().catch(()=>{});}}for(const[id,v]of oauthStates){if(v.created<Date.now()-10*60*1000)oauthStates.delete(id);}},60000).unref();
process.on('SIGINT',async()=>{await browser.close();process.exit(0);});
const httpServer = http.createServer((req, res) => {
  if (HTTPS_ENABLED && REDIRECT_HTTP_TO_HTTPS) {
    const hostHeader = String(req.headers.host || HOST).replace(/:\d+$/, '');
    res.writeHead(308, { Location: `https://${hostHeader}:${HTTPS_PORT}${req.url || '/'}` });
    return res.end();
  }
  app(req, res);
});
httpServer.listen(PORT, HOST, () => console.log(`VitaSearch Proxy HTTP listening on http://${HOST}:${PORT}${HTTPS_ENABLED && REDIRECT_HTTP_TO_HTTPS ? ' (redirecting to HTTPS)' : ''}`));

if (HTTPS_ENABLED) {
  const tlsOptions = {
    cert: fs.readFileSync(TLS_CERT_FILE),
    key: fs.readFileSync(TLS_KEY_FILE),
    minVersion: 'TLSv1.2'
  };
  if (TLS_CA_FILE) tlsOptions.ca = fs.readFileSync(TLS_CA_FILE);
  https.createServer(tlsOptions, app).listen(HTTPS_PORT, HOST, () =>
    console.log(`VitaSearch Proxy HTTPS listening on https://${HOST}:${HTTPS_PORT}`));
}


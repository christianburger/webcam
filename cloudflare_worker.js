// =============================================================================
//  cloudflare_worker.js  –  ESP32-CAM relay backend
//
//  Deploy:
//    1. npm install -g wrangler
//    2. wrangler login
//    3. wrangler kv:namespace create CAM_STORE
//    4. Copy the KV ID into wrangler.toml
//    5. wrangler secret put DEVICE_SECRET    (long random string, matches POLLER_DEVICE_KEY)
//    6. wrangler deploy
//
//  Routes:
//    POST /device/checkin    ← ESP32 only (authenticated with X-Device-Key header)
//    GET  /browser/frame     ← Browser polls for latest frame
//    POST /browser/command   ← Browser sends a command
//    GET  /                  ← Browser monitoring UI (full single-page app)
//    GET  /health            ← Uptime check
// =============================================================================

const CORS = {
    "Access-Control-Allow-Origin":  "*",
    "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type, X-Device-Key",
};

// KV keys
const KEY_FRAME   = "frame:latest";       // {blob, ts, status}
const KEY_QUEUE   = "cmd:queue";          // [{id, type, value, ts}]
const KEY_RESULTS = "result:latest";      // [{id, type, value, ok, ts}]

// =============================================================================
//  Main fetch handler
// =============================================================================

export default {
    async fetch(request, env) {
        const url    = new URL(request.url);
        const path   = url.pathname;
        const method = request.method;

        // CORS preflight
        if (method === "OPTIONS") {
            return new Response(null, { status: 204, headers: CORS });
        }

        // ── Routes ────────────────────────────────────────────────────────────
        if (path === "/" || path === "/index.html") {
            return serveUI();
        }
        if (path === "/health" && method === "GET") {
            return json({ ok: true, ts: Date.now() });
        }
        if (path === "/device/checkin" && method === "POST") {
            return handleCheckin(request, env);
        }
        if (path === "/browser/frame" && method === "GET") {
            return handleGetFrame(env);
        }
        if (path === "/browser/command" && method === "POST") {
            return handleCommand(request, env);
        }

        return json({ ok: false, error: "Not found" }, 404);
    },
};

// =============================================================================
//  Device checkin  (POST /device/checkin)
// =============================================================================

async function handleCheckin(request, env) {
    // ── Authenticate ──────────────────────────────────────────────────────────
    const deviceKey = request.headers.get("X-Device-Key") || "";
    const expected  = env.DEVICE_SECRET || "";
    if (!expected || deviceKey !== expected) {
        return json({ ok: false, error: "Unauthorized" }, 401);
    }

    // ── Parse body ────────────────────────────────────────────────────────────
    let body;
    try {
        body = await request.json();
    } catch {
        return json({ ok: false, error: "Invalid JSON" }, 400);
    }

    if (!body.frame) {
        return json({ ok: false, error: "Missing frame" }, 400);
    }

    // ── Store latest frame in KV (TTL: 60 s so stale frames auto-expire) ─────
    const frameData = {
        blob:   body.frame,        // base64(IV + AES-CBC ciphertext)
        ts:     Date.now(),
        status: body.status || {},
    };
    await env.CAM_STORE.put(KEY_FRAME, JSON.stringify(frameData), {
        expirationTtl: 60,
    });

    // ── Drain command queue ───────────────────────────────────────────────────
    let commands = [];
    const raw = await env.CAM_STORE.get(KEY_QUEUE);
    if (raw) {
        try {
            commands = JSON.parse(raw);
            // Clear queue so commands are delivered exactly once
            await env.CAM_STORE.delete(KEY_QUEUE);
        } catch {
            commands = [];
        }
    }

    return json({ ok: true, commands });
}

// =============================================================================
//  Get latest frame  (GET /browser/frame)
// =============================================================================

async function handleGetFrame(env) {
    const raw = await env.CAM_STORE.get(KEY_FRAME);
    if (!raw) {
        return json({ ok: false, error: "No frame available" }, 404);
    }
    try {
        const data = JSON.parse(raw);
        return json({
            ok:     true,
            blob:   data.blob,
            ts:     data.ts,
            status: data.status || {},
            age_ms: Date.now() - (data.ts || 0),
        });
    } catch {
        return json({ ok: false, error: "Corrupt frame data" }, 500);
    }
}

// =============================================================================
//  Send command  (POST /browser/command)
// =============================================================================

async function handleCommand(request, env) {
    let body;
    try {
        body = await request.json();
    } catch {
        return json({ ok: false, error: "Invalid JSON" }, 400);
    }

    const validTypes = ["pan", "tilt", "led", "switch", "center"];
    if (!validTypes.includes(body.type)) {
        return json({ ok: false, error: `Unknown command type: ${body.type}` }, 400);
    }

    // Append to queue (read → push → write)
    let queue = [];
    const raw = await env.CAM_STORE.get(KEY_QUEUE);
    if (raw) {
        try { queue = JSON.parse(raw); } catch { queue = []; }
    }

    const cmd = {
        id:    crypto.randomUUID(),
        type:  body.type,
        value: body.value ?? 0,
        ts:    Date.now(),
    };
    queue.push(cmd);

    // Keep at most 10 pending commands to avoid stale build-up
    if (queue.length > 10) queue = queue.slice(-10);

    await env.CAM_STORE.put(KEY_QUEUE, JSON.stringify(queue), {
        expirationTtl: 120,
    });

    return json({ ok: true, queued: cmd });
}

// =============================================================================
//  Browser UI  (GET /)
//
//  Single-page app with:
//    - AES key entry (stored in sessionStorage — never sent to server)
//    - Frame polling every 3 s
//    - WebCrypto AES-128-CBC decryption (matches ESP32 side exactly)
//    - Device status display
//    - Pan/tilt D-pad + sliders
//    - LED + relay toggles
//    - Age indicator (how old is the latest frame)
// =============================================================================

function serveUI() {
    const html = `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32\u00b7CAM Monitor</title>
<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg:#0d1117;--s1:#161b22;--s2:#21262d;--s3:#2d333b;
  --accent:#58a6ff;--accent-dim:#1f6feb;
  --green:#3fb950;--red:#f85149;--amber:#d29922;--purple:#bc8cff;
  --text:#c9d1d9;--muted:#8b949e;--border:#30363d;--r:10px;
}
body{background:var(--bg);color:var(--text);font-family:'Segoe UI',system-ui,sans-serif;min-height:100vh;display:flex;flex-direction:column}
header{background:var(--s1);border-bottom:1px solid var(--border);padding:11px 20px;display:flex;align-items:center;gap:14px;flex-wrap:wrap}
.logo{font-size:1rem;font-weight:700;color:#fff;letter-spacing:.4px}.logo span{color:var(--accent)}
.chip{background:var(--s2);border:1px solid var(--border);border-radius:6px;padding:4px 10px;font-size:.72rem;color:var(--muted);display:flex;align-items:center;gap:4px}
.chip b{color:var(--text)}
.hdr-right{margin-left:auto;display:flex;gap:8px;flex-wrap:wrap}
main{flex:1;display:grid;grid-template-columns:1fr 310px;gap:14px;padding:14px}
@media(max-width:780px){main{grid-template-columns:1fr}}
.card{background:var(--s1);border:1px solid var(--border);border-radius:var(--r);overflow:hidden}
.card-hdr{padding:10px 14px;border-bottom:1px solid var(--border);display:flex;align-items:center;gap:8px;font-size:.7rem;font-weight:700;text-transform:uppercase;letter-spacing:.9px;color:var(--muted)}
.dot{width:7px;height:7px;border-radius:50%;background:var(--s3)}
.dot-green{background:var(--green);box-shadow:0 0 5px var(--green)}
.dot-amber{background:var(--amber);box-shadow:0 0 5px var(--amber)}
.dot-red{background:var(--red)}
.card-body{padding:14px}

/* key gate */
.gate{position:fixed;inset:0;background:rgba(13,17,23,.96);display:flex;align-items:center;justify-content:center;z-index:100}
.gate-box{background:var(--s1);border:1px solid var(--border);border-radius:14px;padding:28px;width:340px;text-align:center}
.gate-box h2{font-size:1rem;font-weight:700;margin-bottom:8px}
.gate-box p{font-size:.82rem;color:var(--muted);margin-bottom:18px;line-height:1.5}
.inp{width:100%;background:var(--s2);border:1px solid var(--border);border-radius:8px;padding:10px 12px;font-size:.88rem;color:var(--text);outline:none;transition:border .15s}
.inp:focus{border-color:var(--accent)}
.btn{padding:10px 18px;border:none;border-radius:8px;font-size:.82rem;font-weight:600;cursor:pointer;transition:background .15s}
.btn-primary{background:var(--accent-dim);color:#fff;width:100%;margin-top:12px}
.btn-primary:hover{background:var(--accent)}
.btn-ghost{background:transparent;color:var(--muted);border:1px solid var(--border)}
.btn-ghost:hover{color:var(--accent);border-color:var(--accent)}

/* frame */
.frame-wrap{position:relative;background:#000;aspect-ratio:4/3;width:100%;display:flex;align-items:center;justify-content:center;overflow:hidden}
#frame-img{width:100%;height:100%;object-fit:contain}
.frame-overlay{position:absolute;bottom:8px;right:10px;font-size:.68rem;color:rgba(255,255,255,.5);background:rgba(0,0,0,.4);padding:2px 8px;border-radius:10px}
.frame-ph{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:8px;color:var(--muted);font-size:.82rem}
.frame-ph .ico{font-size:2.2rem;opacity:.3}
.action-row{display:flex;gap:8px;padding:10px 12px;border-top:1px solid var(--border)}
.btn-sm{flex:1;padding:8px;border:none;border-radius:8px;font-size:.78rem;font-weight:600;cursor:pointer;display:flex;align-items:center;justify-content:center;gap:4px}
.btn-sm.primary{background:var(--accent-dim);color:#fff}.btn-sm.primary:hover{background:var(--accent)}
.btn-sm.ghost{background:transparent;color:var(--muted);border:1px solid var(--border)}.btn-sm.ghost:hover{border-color:var(--accent);color:var(--accent)}

/* ctrl */
.ctrl-panel{display:flex;flex-direction:column;gap:12px}
.dpad-wrap{padding:14px 14px 6px;display:flex;flex-direction:column;align-items:center;gap:10px}
.dpad{display:grid;grid-template-columns:repeat(3,48px);grid-template-rows:repeat(3,48px);gap:5px}
.dp{background:var(--s2);border:1px solid var(--border);border-radius:8px;cursor:pointer;display:flex;align-items:center;justify-content:center;font-size:1.05rem;user-select:none;transition:background .1s,transform .08s}
.dp:hover{background:var(--s3);border-color:var(--accent)}.dp:active{transform:scale(.9);background:var(--accent-dim)}
.dp-center{background:var(--s3);border-radius:50%;cursor:pointer;font-size:.85rem;color:var(--muted)}
.dp-center:hover{border-color:var(--accent);color:var(--accent)}.dp-empty{pointer-events:none}
.angle-row{display:flex;gap:10px;padding:0 14px 12px}
.angle-box{flex:1;background:var(--s2);border:1px solid var(--border);border-radius:8px;padding:8px;text-align:center}
.angle-val{font-size:1.4rem;font-weight:700;color:var(--accent);line-height:1}
.angle-lbl{font-size:.65rem;color:var(--muted);text-transform:uppercase;letter-spacing:.5px;margin-top:3px}
.sl-row{display:flex;align-items:center;gap:10px;margin-bottom:10px}.sl-row:last-child{margin-bottom:0}
.sl-lbl{font-size:.72rem;color:var(--muted);width:28px;text-align:right;flex-shrink:0}
input[type=range]{flex:1;accent-color:var(--accent);cursor:pointer}
.sl-val{font-size:.75rem;font-weight:700;color:var(--accent);width:32px;text-align:center;flex-shrink:0}
.tog-row{display:flex;align-items:center;gap:12px;padding:10px 0;border-bottom:1px solid var(--border)}
.tog-row:last-child{border-bottom:none;padding-bottom:0}.tog-row:first-child{padding-top:0}
.tog-ico{font-size:1.1rem;width:24px;text-align:center;flex-shrink:0}
.tog-info{flex:1}.tog-name{font-size:.85rem;font-weight:600}
.tog-status{font-size:.7rem;color:var(--muted);margin-top:1px}.tog-status.on{color:var(--green)}
.sw{position:relative;display:inline-block;width:40px;height:22px;flex-shrink:0}
.sw input{opacity:0;width:0;height:0;position:absolute}
.sw-track{position:absolute;inset:0;background:var(--s3);border-radius:22px;cursor:pointer;transition:background .2s;border:1px solid var(--border)}
.sw-track::before{content:'';position:absolute;height:16px;width:16px;left:2px;top:2px;background:var(--muted);border-radius:50%;transition:transform .2s,background .2s}
.sw input:checked~.sw-track{background:var(--accent-dim);border-color:var(--accent)}
.sw input:checked~.sw-track::before{transform:translateX(18px);background:#fff}
.status-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;font-size:.76rem}
.stat-box{background:var(--s2);border:1px solid var(--border);border-radius:8px;padding:8px}
.stat-val{font-size:1rem;font-weight:700;color:var(--accent);line-height:1}
.stat-lbl{font-size:.65rem;color:var(--muted);margin-top:2px;text-transform:uppercase;letter-spacing:.4px}
.age-bar{height:3px;background:var(--s3);border-radius:2px;margin-top:10px;overflow:hidden}
.age-fill{height:100%;background:var(--green);transition:width .5s,background .5s}
.toast{position:fixed;bottom:18px;right:18px;z-index:200;background:var(--s2);border:1px solid var(--border);border-radius:8px;padding:9px 15px;font-size:.8rem;transform:translateY(60px);opacity:0;transition:transform .22s,opacity .22s;pointer-events:none}
.toast.show{transform:translateY(0);opacity:1}.toast.ok{border-color:var(--green);color:var(--green)}
.toast.err{border-color:var(--red);color:var(--red)}.toast.info{border-color:var(--accent);color:var(--accent)}
.spin{display:inline-block;width:12px;height:12px;border:2px solid rgba(255,255,255,.2);border-top-color:#fff;border-radius:50%;animation:spin .6s linear infinite}
@keyframes spin{to{transform:rotate(360deg)}}
</style>
</head>
<body>

<!-- AES key gate -->
<div class="gate" id="gate">
  <div class="gate-box">
    <h2>&#128274; Enter AES Key</h2>
    <p>The 16-character key set in <code>POLLER_AES_KEY</code>.<br>
       It stays in your browser — the server never sees it.</p>
    <input class="inp" id="key-inp" type="password" maxlength="16" placeholder="Exactly 16 characters"
           onkeydown="if(event.key==='Enter')startMonitor()">
    <div id="key-err" style="font-size:.75rem;color:var(--red);margin-top:6px;min-height:16px"></div>
    <button class="btn btn-primary" onclick="startMonitor()">Start monitoring</button>
  </div>
</div>

<header>
  <div class="logo">ESP32&#xb7;<span>CAM</span> <span style="font-size:.7rem;color:var(--muted);font-weight:400">Monitor</span></div>
  <div class="hdr-right">
    <div class="chip">&#129504; Heap <b id="h-heap">--</b> KB</div>
    <div class="chip">&#8987; Up <b id="h-up">--</b></div>
    <div class="chip">&#128246; <b id="h-age">--</b></div>
    <button class="btn btn-ghost" style="font-size:.75rem;padding:4px 10px" onclick="lockSession()">&#128275; Change key</button>
  </div>
</header>

<main>

<!-- LEFT: camera feed -->
<div class="card">
  <div class="card-hdr"><span class="dot dot-green" id="feed-dot"></span>Live Feed</div>
  <div class="frame-wrap">
    <img id="frame-img" src="" alt="" style="display:none">
    <div class="frame-ph" id="frame-ph"><div class="ico">&#128247;</div><div id="ph-msg">Waiting for key...</div></div>
    <div class="frame-overlay" id="frame-age"></div>
  </div>
  <div class="age-bar"><div class="age-fill" id="age-fill" style="width:0%"></div></div>
  <div class="action-row">
    <button class="btn-sm primary" onclick="forceFrame()">&#128247; Refresh</button>
    <button class="btn-sm ghost"   onclick="togglePoll()" id="poll-btn">&#9208; Pause</button>
    <button class="btn-sm ghost"   onclick="rotate()" id="rot-btn">0&deg; &#8635;</button>
  </div>
</div>

<!-- RIGHT: controls -->
<div class="ctrl-panel">

  <!-- pan/tilt -->
  <div class="card">
    <div class="card-hdr"><span class="dot"></span>Pan &middot; Tilt</div>
    <div class="dpad-wrap">
      <div class="dpad">
        <div class="dp dp-empty"></div>
        <div class="dp" onclick="move('tilt',-10)">&#9650;</div>
        <div class="dp dp-empty"></div>
        <div class="dp" onclick="move('pan',-10)">&#9664;</div>
        <div class="dp dp-center" onclick="center()">&#10011;</div>
        <div class="dp" onclick="move('pan',10)">&#9654;</div>
        <div class="dp dp-empty"></div>
        <div class="dp" onclick="move('tilt',10)">&#9660;</div>
        <div class="dp dp-empty"></div>
      </div>
    </div>
    <div class="angle-row">
      <div class="angle-box"><div class="angle-val" id="pan-val">90&deg;</div><div class="angle-lbl">Pan</div></div>
      <div class="angle-box"><div class="angle-val" id="tilt-val">90&deg;</div><div class="angle-lbl">Tilt</div></div>
    </div>
    <div class="card-body" style="padding-top:0">
      <div class="sl-row">
        <span class="sl-lbl">Pan</span>
        <input type="range" id="pan-sl" min="0" max="180" value="90" oninput="onSlider('pan',+this.value)">
        <span class="sl-val" id="pan-sv">90&deg;</span>
      </div>
      <div class="sl-row">
        <span class="sl-lbl">Tilt</span>
        <input type="range" id="tilt-sl" min="0" max="180" value="90" oninput="onSlider('tilt',+this.value)">
        <span class="sl-val" id="tilt-sv">90&deg;</span>
      </div>
    </div>
  </div>

  <!-- peripherals -->
  <div class="card">
    <div class="card-hdr"><span class="dot"></span>Peripherals</div>
    <div class="card-body">
      <div class="tog-row">
        <span class="tog-ico">&#128161;</span>
        <div class="tog-info"><div class="tog-name">Flash LED</div><div class="tog-status" id="led-st">Unknown</div></div>
        <label class="sw"><input type="checkbox" id="led-tog" onchange="sendCmd('led',this.checked?1:0)"><span class="sw-track"></span></label>
      </div>
      <div class="tog-row">
        <span class="tog-ico">&#128268;</span>
        <div class="tog-info"><div class="tog-name">Relay Switch</div><div class="tog-status" id="sw-st">Unknown</div></div>
        <label class="sw"><input type="checkbox" id="sw-tog" onchange="sendCmd('switch',this.checked?1:0)"><span class="sw-track"></span></label>
      </div>
    </div>
  </div>

  <!-- device status -->
  <div class="card">
    <div class="card-hdr"><span class="dot dot-amber"></span>Device Status</div>
    <div class="card-body">
      <div class="status-grid">
        <div class="stat-box"><div class="stat-val" id="s-heap">--</div><div class="stat-lbl">Free heap</div></div>
        <div class="stat-box"><div class="stat-val" id="s-up">--</div><div class="stat-lbl">Uptime</div></div>
        <div class="stat-box"><div class="stat-val" id="s-pan">--</div><div class="stat-lbl">Pan angle</div></div>
        <div class="stat-box"><div class="stat-val" id="s-tilt">--</div><div class="stat-lbl">Tilt angle</div></div>
      </div>
    </div>
  </div>

</div>
</main>
<div class="toast" id="toast"></div>

<script>
// ── State ────────────────────────────────────────────────────────────────────
var aesKey = null;
var panA = 90, tiltA = 90;
var polling = false, pollTimer = null, slTimer = null, toastTimer = null;
var rotDeg = 0;
var lastFrameTs = 0;

// ── Session management ───────────────────────────────────────────────────────
function startMonitor() {
  var k = id('key-inp').value;
  if (k.length !== 16) {
    id('key-err').textContent = 'Key must be exactly 16 characters (' + k.length + '/16)';
    return;
  }
  id('key-err').textContent = '';
  aesKey = k;
  sessionStorage.setItem('aes_key_len', '16'); // just a flag, not the key
  id('gate').style.display = 'none';
  id('ph-msg').textContent = 'Connecting...';
  startPolling();
}

function lockSession() {
  stopPolling();
  aesKey = null;
  id('key-inp').value = '';
  id('gate').style.display = 'flex';
  id('frame-img').style.display = 'none';
  id('frame-ph').style.display = 'flex';
  id('ph-msg').textContent = 'Waiting for key...';
}

// ── AES-128-CBC decrypt (WebCrypto) ──────────────────────────────────────────
async function decryptBlob(b64blob) {
  // 1. Import key  (UTF-8 bytes of the 16-char string)
  var keyBytes = new TextEncoder().encode(aesKey);
  var cryptoKey = await crypto.subtle.importKey(
    'raw', keyBytes, { name: 'AES-CBC' }, false, ['decrypt']
  );
  // 2. Decode base64
  var bin = atob(b64blob);
  var bytes = new Uint8Array(bin.length);
  for (var i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  // 3. Split IV (first 16 bytes) and ciphertext
  var iv         = bytes.slice(0, 16);
  var ciphertext = bytes.slice(16);
  // 4. Decrypt
  var plain = await crypto.subtle.decrypt({ name: 'AES-CBC', iv: iv }, cryptoKey, ciphertext);
  return new Uint8Array(plain);
}

// ── Frame display ─────────────────────────────────────────────────────────────
async function fetchAndShowFrame() {
  var r;
  try {
    r = await fetch('/browser/frame');
  } catch (e) {
    id('ph-msg').textContent = 'Network error';
    return;
  }
  if (!r.ok) {
    if (r.status === 404) { id('ph-msg').textContent = 'No frame yet — device connecting...'; }
    return;
  }
  var data = await r.json();
  if (!data.ok || !data.blob) return;

  // Decrypt
  var jpegBytes;
  try {
    jpegBytes = await decryptBlob(data.blob);
  } catch (e) {
    toast('Decrypt failed — wrong key?', 'err');
    return;
  }

  // Display
  var blob  = new Blob([jpegBytes], { type: 'image/jpeg' });
  var oldUrl = id('frame-img').src;
  var newUrl = URL.createObjectURL(blob);
  id('frame-img').onload = function() { URL.revokeObjectURL(oldUrl); };
  id('frame-img').src    = newUrl;
  id('frame-img').style.display = 'block';
  id('frame-ph').style.display  = 'none';
  applyRotation();
  lastFrameTs = data.ts || Date.now();

  // Age indicator
  updateAge(data.age_ms || 0);

  // Device status from frame payload
  var st = data.status || {};
  if (st.heap  != null) { set('s-heap',  Math.round(st.heap/1024)+'KB'); set('h-heap', Math.round(st.heap/1024)); }
  if (st.uptime!= null) { set('s-up',    fmt_uptime(st.uptime));        set('h-up',    fmt_uptime(st.uptime)); }
  if (st.pan   != null) { panA = st.pan;  tiltA = st.tilt||tiltA; syncUI(); }
  if (st.led   != null) { id('led-tog').checked = !!st.led;  updStatus('led-st',  !!st.led); }
  if (st.sw    != null) { id('sw-tog').checked  = !!st.sw;   updStatus('sw-st',   !!st.sw);  }
  set('s-pan',  (st.pan  ?? '--') + '\u00b0');
  set('s-tilt', (st.tilt ?? '--') + '\u00b0');
}

function updateAge(ms) {
  var s = Math.round(ms / 1000);
  set('h-age', s < 60 ? s+'s ago' : Math.round(s/60)+'m ago');
  set('frame-age', s+'s');
  var pct = Math.max(0, 100 - (s / 30) * 100);
  id('age-fill').style.width    = pct + '%';
  id('age-fill').style.background = s < 8 ? '#3fb950' : s < 20 ? '#d29922' : '#f85149';
  id('feed-dot').className = 'dot ' + (s < 10 ? 'dot-green' : 'dot-red');
}

function fmt_uptime(s) {
  if (s < 60)   return s + 's';
  if (s < 3600) return Math.floor(s/60) + 'm';
  return Math.floor(s/3600) + 'h ' + Math.floor((s%3600)/60) + 'm';
}

// ── Polling ───────────────────────────────────────────────────────────────────
function startPolling() {
  polling = true;
  fetchAndShowFrame();
  pollTimer = setInterval(fetchAndShowFrame, 3000);
  set('poll-btn', '\u23f9 Pause');
}

function stopPolling() {
  polling = false;
  clearInterval(pollTimer);
  set('poll-btn', '\u25b6 Resume');
}

function togglePoll() { polling ? stopPolling() : startPolling(); }
function forceFrame()  { fetchAndShowFrame(); }

// ── Commands ──────────────────────────────────────────────────────────────────
async function sendCmd(type, value) {
  try {
    var r = await fetch('/browser/command', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ type: type, value: value }),
    });
    var d = await r.json();
    if (!d.ok) throw new Error(d.error);
    toast(cap(type) + ' \u2192 ' + value, 'ok');
  } catch (e) {
    toast('Command failed: ' + e.message, 'err');
  }
}

function move(ax, d) {
  if (ax === 'pan')  panA  = clamp(panA  + d, 0, 180);
  else               tiltA = clamp(tiltA + d, 0, 180);
  syncUI();
  sendCmd(ax, ax === 'pan' ? panA : tiltA);
}

function center() {
  panA = 90; tiltA = 90; syncUI();
  sendCmd('pan', 90);
  sendCmd('tilt', 90);
  toast('Centred', 'ok');
}

function onSlider(ax, v) {
  if (ax === 'pan') panA = v; else tiltA = v;
  syncUI();
  clearTimeout(slTimer);
  slTimer = setTimeout(function() { sendCmd(ax, v); }, 120);
}

function syncUI() {
  set('pan-val',  panA  + '\u00b0'); set('tilt-val', tiltA + '\u00b0');
  id('pan-sl').value  = panA;        id('tilt-sl').value  = tiltA;
  set('pan-sv',   panA  + '\u00b0'); set('tilt-sv', tiltA + '\u00b0');
}

function updStatus(el, on) {
  id(el).textContent  = on ? 'On' : 'Off';
  id(el).className    = 'tog-status' + (on ? ' on' : '');
}

// ── Keyboard shortcuts ────────────────────────────────────────────────────────
document.addEventListener('keydown', function(e) {
  if (id('gate').style.display !== 'none') return;
  if (e.target.tagName === 'INPUT') return;
  if (e.key === 'ArrowLeft')  move('pan',  -10);
  else if (e.key === 'ArrowRight') move('pan',  10);
  else if (e.key === 'ArrowUp')    move('tilt', -10);
  else if (e.key === 'ArrowDown')  move('tilt',  10);
  else if (e.key === 'c' || e.key === 'C') center();
  else if (e.key === 'p' || e.key === 'P') togglePoll();
});

// ── Image rotation ────────────────────────────────────────────────────────────
function applyRotation() {
  var img = id('frame-img');
  var sideways = (rotDeg === 90 || rotDeg === 270);
  // When rotated 90/270° a landscape image overflows its portrait container,
  // so we scale it down to fit. The frame-wrap is 4:3 (w > h), so for sideways
  // rotation scale by h/w = 0.75.
  var scale = sideways ? 0.75 : 1;
  img.style.transform = 'rotate(' + rotDeg + 'deg) scale(' + scale + ')';
  img.style.transition = 'transform .25s ease';
  set('rot-btn', rotDeg + '\u00b0 \u21bb');
}

function rotate() {
  rotDeg = (rotDeg + 90) % 360;
  applyRotation();
}


function id(s)      { return document.getElementById(s); }
function set(s,v)   { var e=id(s); if(e) e.textContent=v; }
function clamp(v,a,b) { return Math.max(a, Math.min(b, v)); }
function cap(s)     { return s.charAt(0).toUpperCase() + s.slice(1); }
function toast(msg, type) {
  var el = id('toast');
  el.textContent = msg;
  el.className   = 'toast show ' + (type || '');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(function() { el.className = 'toast'; }, 2600);
}

// Live age counter (ticks every second when a frame is displayed)
setInterval(function() {
  if (!lastFrameTs) return;
  updateAge(Date.now() - lastFrameTs);
}, 1000);
</script>
</body>
</html>`;

    return new Response(html, {
        headers: { "Content-Type": "text/html; charset=utf-8", ...CORS },
    });
}

// =============================================================================
//  Helper
// =============================================================================

function json(data, status = 200) {
    return new Response(JSON.stringify(data), {
        status,
        headers: { "Content-Type": "application/json", ...CORS },
    });
}

#include "controller.h"
#include "peripherals.h"
#include "cam_log.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_timer.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "img_converters.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// frame_queue is produced by camera_task (main.c) and consumed here.
extern QueueHandle_t frame_queue;

#define SW_JPEG_QUALITY 80
#define STREAM_BOUNDARY "ESP32CAMBOUNDARY"

// ═════════════════════════════════════════════════════════════════════════════
//  Embedded HTML  (original, unchanged)
// ═════════════════════════════════════════════════════════════════════════════

static const char ROOT_HTML[] =
"<!DOCTYPE html>\n"
"<html lang='en'>\n"
"<head>\n"
"<meta charset='UTF-8'>\n"
"<meta name='viewport' content='width=device-width,initial-scale=1'>\n"
"<title>ESP32\xc2\xb7" "CAM</title>\n"
"<style>\n"
"*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}\n"
":root{\n"
"  --bg:#0d1117;--s1:#161b22;--s2:#21262d;--s3:#2d333b;\n"
"  --accent:#58a6ff;--accent-dim:#1f6feb;\n"
"  --green:#3fb950;--red:#f85149;--amber:#d29922;\n"
"  --text:#c9d1d9;--muted:#8b949e;--border:#30363d;--r:10px;\n"
"}\n"
"body{background:var(--bg);color:var(--text);font-family:'Segoe UI',system-ui,sans-serif;min-height:100vh;display:flex;flex-direction:column}\n"
"header{\n"
"  background:var(--s1);border-bottom:1px solid var(--border);\n"
"  padding:11px 20px;display:flex;align-items:center;gap:14px;flex-wrap:wrap;\n"
"}\n"
".logo{font-size:1.05rem;font-weight:700;letter-spacing:.4px;color:#fff;white-space:nowrap}\n"
".logo span{color:var(--accent)}\n"
".badge{\n"
"  font-size:.68rem;padding:3px 9px;border-radius:20px;font-weight:700;\n"
"  letter-spacing:.6px;border:1px solid currentColor;\n"
"}\n"
".badge-live{color:var(--green);animation:pulse 2s infinite}\n"
"@keyframes pulse{0%,100%{opacity:1}50%{opacity:.55}}\n"
".badge-off{color:var(--muted)}\n"
".header-stats{margin-left:auto;display:flex;gap:8px;flex-wrap:wrap}\n"
".chip{\n"
"  background:var(--s2);border:1px solid var(--border);border-radius:6px;\n"
"  padding:4px 10px;font-size:.72rem;color:var(--muted);\n"
"  display:flex;align-items:center;gap:4px;\n"
"}\n"
".chip b{color:var(--text)}\n"
"main{\n"
"  flex:1;display:grid;\n"
"  grid-template-columns:1fr 330px;\n"
"  gap:14px;padding:14px;\n"
"}\n"
"@media(max-width:820px){main{grid-template-columns:1fr}}\n"
".card{\n"
"  background:var(--s1);border:1px solid var(--border);\n"
"  border-radius:var(--r);overflow:hidden;\n"
"}\n"
".card-hdr{\n"
"  padding:10px 14px;border-bottom:1px solid var(--border);\n"
"  display:flex;align-items:center;gap:8px;\n"
"  font-size:.7rem;font-weight:700;text-transform:uppercase;\n"
"  letter-spacing:.9px;color:var(--muted);\n"
"}\n"
".dot{width:7px;height:7px;border-radius:50%;background:var(--s3)}\n"
".dot-green{background:var(--green);box-shadow:0 0 5px var(--green)}\n"
".dot-amber{background:var(--amber);box-shadow:0 0 5px var(--amber)}\n"
".card-body{padding:14px}\n"
".stream-wrap{\n"
"  position:relative;background:#000;\n"
"  aspect-ratio:4/3;width:100%;\n"
"  display:flex;align-items:center;justify-content:center;\n"
"}\n"
"#stream-img{width:100%;height:100%;object-fit:cover;display:block}\n"
".stream-placeholder{\n"
"  position:absolute;inset:0;display:none;\n"
"  flex-direction:column;align-items:center;justify-content:center;\n"
"  gap:10px;color:var(--muted);font-size:.85rem;\n"
"}\n"
".stream-placeholder .ico{font-size:2.4rem;opacity:.4}\n"
".action-row{\n"
"  display:flex;gap:8px;padding:10px 12px;\n"
"  border-top:1px solid var(--border);\n"
"}\n"
".btn{\n"
"  flex:1;padding:9px 10px;border:none;border-radius:8px;\n"
"  font-size:.8rem;font-weight:600;cursor:pointer;\n"
"  display:flex;align-items:center;justify-content:center;gap:5px;\n"
"  transition:background .15s,transform .1s;\n"
"}\n"
".btn:active{transform:scale(.96)}\n"
".btn-primary{background:var(--accent-dim);color:#fff}\n"
".btn-primary:hover{background:var(--accent)}\n"
".btn-ghost{\n"
"  background:transparent;color:var(--muted);\n"
"  border:1px solid var(--border);\n"
"}\n"
".btn-ghost:hover{border-color:var(--accent);color:var(--accent)}\n"
".ctrl-panel{display:flex;flex-direction:column;gap:12px}\n"
".dpad-wrap{padding:14px 14px 6px;display:flex;flex-direction:column;align-items:center;gap:10px}\n"
".dpad{\n"
"  display:grid;\n"
"  grid-template-columns:repeat(3,50px);\n"
"  grid-template-rows:repeat(3,50px);\n"
"  gap:5px;\n"
"}\n"
".dp{\n"
"  background:var(--s2);border:1px solid var(--border);\n"
"  border-radius:8px;cursor:pointer;\n"
"  display:flex;align-items:center;justify-content:center;\n"
"  font-size:1.1rem;user-select:none;\n"
"  transition:background .12s,border-color .12s,transform .1s;\n"
"}\n"
".dp:hover{background:var(--s3);border-color:var(--accent)}\n"
".dp:active{background:var(--accent-dim);transform:scale(.91)}\n"
".dp-center{\n"
"  background:var(--s3);border-radius:50%;\n"
"  cursor:pointer;font-size:.9rem;color:var(--muted);\n"
"}\n"
".dp-center:hover{border-color:var(--accent);color:var(--accent)}\n"
".dp-empty{pointer-events:none}\n"
".angle-row{display:flex;gap:10px;justify-content:center;padding:0 14px 12px}\n"
".angle-box{\n"
"  flex:1;background:var(--s2);border:1px solid var(--border);\n"
"  border-radius:8px;padding:8px;text-align:center;\n"
"}\n"
".angle-val{font-size:1.5rem;font-weight:700;color:var(--accent);line-height:1}\n"
".angle-lbl{font-size:.65rem;color:var(--muted);text-transform:uppercase;letter-spacing:.5px;margin-top:3px}\n"
".sl-row{display:flex;align-items:center;gap:10px;margin-bottom:10px}\n"
".sl-row:last-child{margin-bottom:0}\n"
".sl-lbl{font-size:.72rem;color:var(--muted);width:28px;text-align:right;flex-shrink:0}\n"
"input[type=range]{\n"
"  flex:1;accent-color:var(--accent);\n"
"  height:4px;cursor:pointer;\n"
"}\n"
".sl-val{font-size:.75rem;font-weight:700;color:var(--accent);width:32px;text-align:center;flex-shrink:0}\n"
".tog-row{\n"
"  display:flex;align-items:center;gap:12px;\n"
"  padding:10px 0;border-bottom:1px solid var(--border);\n"
"}\n"
".tog-row:last-child{border-bottom:none;padding-bottom:0}\n"
".tog-row:first-child{padding-top:0}\n"
".tog-ico{font-size:1.15rem;width:26px;text-align:center;flex-shrink:0}\n"
".tog-info{flex:1}\n"
".tog-name{font-size:.85rem;font-weight:600}\n"
".tog-status{font-size:.7rem;color:var(--muted);margin-top:1px}\n"
".tog-status.on{color:var(--green)}\n"
".sw{position:relative;display:inline-block;width:40px;height:22px;flex-shrink:0}\n"
".sw input{opacity:0;width:0;height:0;position:absolute}\n"
".sw-track{\n"
"  position:absolute;inset:0;background:var(--s3);\n"
"  border-radius:22px;cursor:pointer;\n"
"  transition:background .2s;border:1px solid var(--border);\n"
"}\n"
".sw-track::before{\n"
"  content:'';position:absolute;height:16px;width:16px;\n"
"  left:2px;top:2px;background:var(--muted);\n"
"  border-radius:50%;transition:transform .2s,background .2s;\n"
"}\n"
".sw input:checked~.sw-track{background:var(--accent-dim);border-color:var(--accent)}\n"
".sw input:checked~.sw-track::before{transform:translateX(18px);background:#fff}\n"
".sys-info{font-size:.75rem;color:var(--muted);line-height:1.9}\n"
".sys-info b{color:var(--text)}\n"
".sys-row{display:flex;justify-content:space-between;border-bottom:1px solid var(--border);padding:4px 0}\n"
".sys-row:last-child{border-bottom:none}\n"
".toast{\n"
"  position:fixed;bottom:18px;right:18px;z-index:200;\n"
"  background:var(--s2);border:1px solid var(--border);\n"
"  border-radius:8px;padding:9px 15px;font-size:.8rem;\n"
"  transform:translateY(60px);opacity:0;\n"
"  transition:transform .22s,opacity .22s;pointer-events:none;\n"
"}\n"
".toast.show{transform:translateY(0);opacity:1}\n"
".toast.ok{border-color:var(--green);color:var(--green)}\n"
".toast.err{border-color:var(--red);color:var(--red)}\n"
".toast.info{border-color:var(--accent);color:var(--accent)}\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<header>\n"
"  <div class='logo'>ESP32\xc2\xb7<span>CAM</span></div>\n"
"  <span class='badge badge-live' id='stream-badge'>\xe2\x97\x8f LIVE</span>\n"
"  <div class='header-stats'>\n"
"    <div class='chip'>\xf0\x9f\xa7\xa0 Heap <b id='h-heap'>--</b> KB</div>\n"
"    <div class='chip'>\xe2\x9a\xa1 <b id='h-cpu'>--</b> MHz</div>\n"
"    <div class='chip'>\xf0\x9f\x93\x8b <b id='h-tasks'>--</b> tasks</div>\n"
"  </div>\n"
"</header>\n"
"<main>\n"
"<div class='card'>\n"
"  <div class='card-hdr'><span class='dot dot-green'></span>Camera Feed</div>\n"
"  <div class='stream-wrap'>\n"
"    <img id='stream-img' src='' alt='stream'\n"
"         onerror='onStreamErr()'\n"
"         onload='onStreamLoad()'>\n"
"    <div class='stream-placeholder' id='stream-ph'>\n"
"      <div class='ico'>\xf0\x9f\x93\xb7</div><div>Stream offline</div>\n"
"    </div>\n"
"  </div>\n"
"  <div class='action-row'>\n"
"    <button class='btn btn-primary' onclick='capturePhoto()'>\xf0\x9f\x93\xb8 Capture</button>\n"
"    <button class='btn btn-ghost' id='stream-btn' onclick='toggleStream()'>\xe2\x8f\xb9 Pause</button>\n"
"    <button class='btn btn-ghost' onclick='loadHwInfo()'>\xe2\x84\xb9 Info</button>\n"
"    <button class='btn btn-ghost' id='rot-btn' onclick='rotate()'>0\xc2\xb0 \xe2\x86\xbb</button>\n"
"  </div>\n"
"</div>\n"
"<div class='ctrl-panel'>\n"
"  <div class='card'>\n"
"    <div class='card-hdr'><span class='dot'></span>Pan \xc2\xb7 Tilt</div>\n"
"    <div class='dpad-wrap'>\n"
"      <div class='dpad'>\n"
"        <div class='dp dp-empty'></div>\n"
"        <div class='dp' onclick='move(\"tilt\",-10)' title='Tilt up'>\xe2\x96\xb2</div>\n"
"        <div class='dp dp-empty'></div>\n"
"        <div class='dp' onclick='move(\"pan\",-10)'  title='Pan left'>\xe2\x97\x84</div>\n"
"        <div class='dp dp-center' onclick='center()' title='Centre'>\xe2\x9c\x9b</div>\n"
"        <div class='dp' onclick='move(\"pan\",10)'   title='Pan right'>\xe2\x96\xba</div>\n"
"        <div class='dp dp-empty'></div>\n"
"        <div class='dp' onclick='move(\"tilt\",10)'  title='Tilt down'>\xe2\x96\xbc</div>\n"
"        <div class='dp dp-empty'></div>\n"
"      </div>\n"
"    </div>\n"
"    <div class='angle-row'>\n"
"      <div class='angle-box'><div class='angle-val' id='pan-val'>90\xc2\xb0</div><div class='angle-lbl'>Pan</div></div>\n"
"      <div class='angle-box'><div class='angle-val' id='tilt-val'>90\xc2\xb0</div><div class='angle-lbl'>Tilt</div></div>\n"
"    </div>\n"
"    <div class='card-body' style='padding-top:0'>\n"
"      <div class='sl-row'>\n"
"        <span class='sl-lbl'>Pan</span>\n"
"        <input type='range' id='pan-sl' min='0' max='180' value='90' oninput='onSlider(\"pan\",+this.value)'>\n"
"        <span class='sl-val' id='pan-sl-val'>90\xc2\xb0</span>\n"
"      </div>\n"
"      <div class='sl-row'>\n"
"        <span class='sl-lbl'>Tilt</span>\n"
"        <input type='range' id='tilt-sl' min='0' max='180' value='90' oninput='onSlider(\"tilt\",+this.value)'>\n"
"        <span class='sl-val' id='tilt-sl-val'>90\xc2\xb0</span>\n"
"      </div>\n"
"    </div>\n"
"  </div>\n"
"  <div class='card'>\n"
"    <div class='card-hdr'><span class='dot'></span>Peripherals</div>\n"
"    <div class='card-body'>\n"
"      <div class='tog-row'>\n"
"        <span class='tog-ico'>\xf0\x9f\x92\xa1</span>\n"
"        <div class='tog-info'>\n"
"          <div class='tog-name'>Flash LED</div>\n"
"          <div class='tog-status' id='led-st'>Off</div>\n"
"        </div>\n"
"        <label class='sw'>\n"
"          <input type='checkbox' id='led-tog' onchange='setPeriph(\"led\",this.checked)'>\n"
"          <span class='sw-track'></span>\n"
"        </label>\n"
"      </div>\n"
"      <div class='tog-row'>\n"
"        <span class='tog-ico'>\xf0\x9f\x94\x8c</span>\n"
"        <div class='tog-info'>\n"
"          <div class='tog-name'>Relay Switch</div>\n"
"          <div class='tog-status' id='sw-st'>Off</div>\n"
"        </div>\n"
"        <label class='sw'>\n"
"          <input type='checkbox' id='sw-tog' onchange='setPeriph(\"switch\",this.checked)'>\n"
"          <span class='sw-track'></span>\n"
"        </label>\n"
"      </div>\n"
"    </div>\n"
"  </div>\n"
"  <div class='card'>\n"
"    <div class='card-hdr'><span class='dot dot-amber'></span>System</div>\n"
"    <div class='card-body'>\n"
"      <div id='sys-info' class='sys-info'>Loading\xe2\x80\xa6</div>\n"
"    </div>\n"
"  </div>\n"
"</div>\n"
"</main>\n"
"<div class='toast' id='toast'></div>\n"
"<script>\n"
"var panA=90,tiltA=90,paused=false,slTmr=null,toastTmr=null,rotDeg=0;\n"
"function move(ax,d){\n"
"  if(ax==='pan'){panA=clamp(panA+d,0,180);}else{tiltA=clamp(tiltA+d,0,180);}\n"
"  syncUI();sendServo(ax,ax==='pan'?panA:tiltA);\n"
"}\n"
"function onSlider(ax,v){\n"
"  if(ax==='pan')panA=v;else tiltA=v;\n"
"  syncUI();\n"
"  clearTimeout(slTmr);slTmr=setTimeout(function(){sendServo(ax,v);},80);\n"
"}\n"
"function center(){\n"
"  panA=90;tiltA=90;syncUI();\n"
"  sendServo('pan',90);sendServo('tilt',90);\n"
"  toast('Centred','ok');\n"
"}\n"
"function syncUI(){\n"
"  set('pan-val',panA+'\xc2\xb0');\n"
"  set('tilt-val',tiltA+'\xc2\xb0');\n"
"  id('pan-sl').value=panA;\n"
"  id('tilt-sl').value=tiltA;\n"
"  set('pan-sl-val',panA+'\xc2\xb0');\n"
"  set('tilt-sl-val',tiltA+'\xc2\xb0');\n"
"}\n"
"function sendServo(ax,v){\n"
"  get('/control/'+ax+'?angle='+v).catch(function(){toast('Servo error','err');});\n"
"}\n"
"function setPeriph(name,on){\n"
"  get('/control/'+name+'?state='+(on?1:0)).then(function(r){\n"
"    if(!r.ok)throw r;\n"
"    var st=id(name==='led'?'led-st':'sw-st');\n"
"    st.textContent=on?'On':'Off';\n"
"    st.className='tog-status'+(on?' on':'');\n"
"    toast(cap(name)+' '+(on?'on':'off'),'ok');\n"
"  }).catch(function(){toast('Error: '+name,'err');});\n"
"}\n"
"function startStream(){\n"
"  id('stream-img').src='/stream?t='+Date.now();\n"
"}\n"
"function capturePhoto(){\n"
"  var wasStreaming=!paused;\n"
"  if(wasStreaming){id('stream-img').src='';paused=true;set('stream-btn','\xe2\x96\xb6 Resume');}\n"
"  toast('Capturing\xe2\x80\xa6','info');\n"
"  setTimeout(function(){\n"
"    fetch('/capture')\n"
"      .then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);return r.blob();})\n"
"      .then(function(blob){\n"
"        var url=URL.createObjectURL(blob);\n"
"        var a=document.createElement('a');\n"
"        a.href=url;a.download='capture_'+Date.now()+'.jpg';a.click();\n"
"        setTimeout(function(){URL.revokeObjectURL(url);},5000);\n"
"        toast('Photo saved!','ok');\n"
"        if(wasStreaming){startStream();paused=false;set('stream-btn','\xe2\x8f\xb9 Pause');}\n"
"      })\n"
"      .catch(function(e){\n"
"        toast('Capture failed: '+e.message,'err');\n"
"        if(wasStreaming){startStream();paused=false;set('stream-btn','\xe2\x8f\xb9 Pause');}\n"
"      });\n"
"  },350);\n"
"}\n"
"function toggleStream(){\n"
"  if(paused){\n"
"    startStream();\n"
"    set('stream-btn','\xe2\x8f\xb9 Pause');paused=false;\n"
"  }else{\n"
"    id('stream-img').src='';\n"
"    set('stream-btn','\xe2\x96\xb6 Resume');paused=true;\n"
"  }\n"
"}\n"
"function onStreamErr(){id('stream-ph').style.display='flex';id('stream-img').style.display='none';}\n"
"function onStreamLoad(){id('stream-ph').style.display='none';id('stream-img').style.display='block';applyRotation();}\n"
"function pollStatus(){\n"
"  fetch('/status').then(function(r){return r.json();}).then(function(d){\n"
"    set('h-heap',Math.round(d.heap/1024));\n"
"    set('h-cpu',d.cpu);\n"
"    set('h-tasks',d.tasks);\n"
"  }).catch(function(){});\n"
"}\n"
"function pollPeriph(){\n"
"  fetch('/periph/state').then(function(r){return r.json();}).then(function(d){\n"
"    id('led-tog').checked=!!d.led;\n"
"    id('sw-tog').checked=!!d.sw;\n"
"    var lst=id('led-st'),sst=id('sw-st');\n"
"    lst.textContent=d.led?'On':'Off'; lst.className='tog-status'+(d.led?' on':'');\n"
"    sst.textContent=d.sw?'On':'Off';  sst.className='tog-status'+(d.sw?' on':'');\n"
"    panA=d.pan||90;tiltA=d.tilt||90;syncUI();\n"
"  }).catch(function(){});\n"
"}\n"
"function loadHwInfo(){\n"
"  fetch('/hardware').then(function(r){return r.json();}).then(function(d){\n"
"    var f=d.features||{};\n"
"    var rows=[\n"
"      ['Model',d.model||'ESP32'],\n"
"      ['Cores',d.cores],\n"
"      ['Revision',d.revision],\n"
"      ['PSRAM',Math.round(d.psram_size/1024)+' KB'],\n"
"      ['WiFi',f.wifi?'\xe2\x9c\x85':'\xe2\x9d\x8c'],\n"
"      ['BT',f.bt?'\xe2\x9c\x85':'\xe2\x9d\x8c'],\n"
"      ['BLE',f.ble?'\xe2\x9c\x85':'\xe2\x9d\x8c']\n"
"    ];\n"
"    id('sys-info').innerHTML=rows.map(function(r){\n"
"      return \"<div class='sys-row'><span>\"+r[0]+\"</span><b>\"+r[1]+\"</b></div>\";\n"
"    }).join('');\n"
"  }).catch(function(){toast('Hardware info unavailable','err');});\n"
"}\n"
"document.addEventListener('keydown',function(e){\n"
"  if(e.target.tagName==='INPUT')return;\n"
"  if(e.key==='ArrowLeft') move('pan',-10);\n"
"  else if(e.key==='ArrowRight')move('pan',10);\n"
"  else if(e.key==='ArrowUp')  move('tilt',-10);\n"
"  else if(e.key==='ArrowDown')move('tilt',10);\n"
"  else if(e.key==='c'||e.key==='C')center();\n"
"  else if(e.key==='p'||e.key==='P')capturePhoto();\n"
"});\n"
"function applyRotation(){\n"
"  var img=id('stream-img');\n"
"  var sideways=(rotDeg===90||rotDeg===270);\n"
"  var scale=sideways?0.75:1;\n"
"  img.style.transform='rotate('+rotDeg+'deg) scale('+scale+')';\n"
"  img.style.transition='transform .25s ease';\n"
"  set('rot-btn',rotDeg+'\xc2\xb0 \xe2\x86\xbb');\n"
"}\n"
"function rotate(){rotDeg=(rotDeg+90)%360;applyRotation();}\n"
"function id(s){return document.getElementById(s);}\n"
"function set(s,v){var el=id(s);if(el)el.textContent=v;}\n"
"function get(u){return fetch(u);}\n"
"function clamp(v,a,b){return Math.max(a,Math.min(b,v));}\n"
"function cap(s){return s.charAt(0).toUpperCase()+s.slice(1);}\n"
"function toast(msg,type){\n"
"  var el=id('toast');\n"
"  el.textContent=msg;el.className='toast show '+(type||'');\n"
"  clearTimeout(toastTmr);\n"
"  toastTmr=setTimeout(function(){el.className='toast';},2600);\n"
"}\n"
"startStream();\n"
"pollStatus();\n"
"pollPeriph();\n"
"loadHwInfo();\n"
"setInterval(pollStatus,3000);\n"
"setInterval(pollPeriph,6000);\n"
"</script>\n"
"</body>\n"
"</html>\n";

// ═════════════════════════════════════════════════════════════════════════════
//  Shared helpers (unchanged)
// ═════════════════════════════════════════════════════════════════════════════

static esp_err_t query_int(httpd_req_t *req, const char *key, int *out) {
    char qs[64];
    if (httpd_req_get_url_query_str(req, qs, sizeof(qs)) != ESP_OK) return ESP_FAIL;
    char val[16];
    if (httpd_query_key_value(qs, key, val, sizeof(val)) != ESP_OK) return ESP_FAIL;
    *out = atoi(val);
    return ESP_OK;
}

static void send_ok(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "{\"ok\":true}");
}

static void send_err(httpd_req_t *req, const char *msg) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_status(req, "400 Bad Request");
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", msg);
    httpd_resp_sendstr(req, buf);
}

static void log_req_headers(httpd_req_t *req, const char *tag) {
    size_t approx = 0;
    const char *keys[] = { "Host", "User-Agent", "CF-Ray", "CF-Connecting-IP", "X-Forwarded-For", "CF-IPCountry" };
    char v[160];
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        size_t n = httpd_req_get_hdr_value_len(req, keys[i]);
        if (n > 0) approx += n + 4 + strlen(keys[i]); // value + ": " + key
        if (n > 0 && httpd_req_get_hdr_value_str(req, keys[i], v, sizeof(v)) == ESP_OK) {
            ESP_LOGI(tag, "hdr %s=%s", keys[i], v);
        }
    }
    ESP_LOGI(tag, "req uri=%s method=%d approx_hdr_bytes=%u", req->uri, (int)req->method, (unsigned)approx);
}

static bool get_jpeg(camera_fb_t *pic, uint8_t **out_buf, size_t *out_len, bool *converted) {
    *converted = false;
    if (pic->format == PIXFORMAT_JPEG) {
        *out_buf = pic->buf;
        *out_len = pic->len;
        return true;
    }
    if (!frame2jpg(pic, SW_JPEG_QUALITY, out_buf, out_len)) {
        ESP_LOGE(TG_CTL_CAPT, "SW JPEG encode failed (fmt=%d)", pic->format);
        return false;
    }
    *converted = true;
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Camera handlers (with added logging)
// ═════════════════════════════════════════════════════════════════════════════

esp_err_t root_handler(httpd_req_t *req) {
    log_req_headers(req, TG_NET_HTTP);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, ROOT_HTML, (ssize_t)strlen(ROOT_HTML));
    return ESP_OK;
}

esp_err_t status_handler(httpd_req_t *req) {
    log_req_headers(req, TG_NET_HTTP);
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"heap\":%lu,\"tasks\":%d,\"cpu\":%d}",
             esp_get_free_heap_size(),
             uxTaskGetNumberOfTasks(),
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, buf, (ssize_t)strlen(buf));
    return ESP_OK;
}

esp_err_t hardware_info_handler(httpd_req_t *req) {
    log_req_headers(req, TG_NET_HTTP);
    char buf[512];
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    snprintf(buf, sizeof(buf),
        "{"
        "\"model\":\"ESP32\","
        "\"cores\":%d,"
        "\"revision\":%d,"
        "\"psram_size\":%d,"
        "\"features\":{"
          "\"wifi\":%s,"
          "\"bt\":%s,"
          "\"ble\":%s,"
          "\"embedded_psram\":%s"
        "}"
        "}",
        ci.cores, ci.revision,
        esp_psram_get_size(),
        (ci.features & CHIP_FEATURE_WIFI_BGN) ? "true" : "false",
        (ci.features & CHIP_FEATURE_BT)        ? "true" : "false",
        (ci.features & CHIP_FEATURE_BLE)       ? "true" : "false",
        (ci.features & CHIP_FEATURE_EMB_PSRAM) ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, buf, (ssize_t)strlen(buf));
    return ESP_OK;
}

esp_err_t capture_handler(httpd_req_t *req) {
    log_req_headers(req, TG_CTL_CAPT);
    ESP_LOGI(TG_CTL_CAPT, "capture request received");
    // Drain stale queued frames
    camera_fb_t *stale;
    int drained = 0;
    while (xQueueReceive(frame_queue, &stale, 0) == pdTRUE && stale) {
        esp_camera_fb_return(stale);
        drained++;
    }
    if (drained) ESP_LOGD(TG_CTL_CAPT, "drained %d stale frames", drained);

    ESP_LOGD(TG_CTL_CAPT, "calling esp_camera_fb_get()");
    int64_t t0 = esp_timer_get_time();
    camera_fb_t *pic = esp_camera_fb_get();
    int64_t t1 = esp_timer_get_time();
    if (!pic) {
        ESP_LOGE(TG_CTL_CAPT, "fb_get NULL after %lld µs", (long long)(t1-t0));
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    ESP_LOGI(TG_CTL_CAPT, "got frame %zu B in %lld µs", pic->len, (long long)(t1-t0));

    uint8_t *jpg = NULL;
    size_t   len = 0;
    bool converted = false;
    if (!get_jpeg(pic, &jpg, &len, &converted)) {
        ESP_LOGE(TG_CTL_CAPT, "JPEG conversion failed");
        esp_camera_fb_return(pic);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t res = httpd_resp_send(req, (const char *)jpg, (ssize_t)len);
    if (converted) free(jpg);
    esp_camera_fb_return(pic);
    ESP_LOGI(TG_CTL_CAPT, "capture response sent, result=%d", res);
    return res;
}

esp_err_t stream_handler(httpd_req_t *req) {
    log_req_headers(req, TG_CTL_STRM);
    ESP_LOGI(TG_CTL_STRM, "stream client connected, task=%s", pcTaskGetName(NULL));
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" STREAM_BOUNDARY);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    uint32_t frame_idx = 0;
    while (1) {
        camera_fb_t *pic;
        int64_t t0 = esp_timer_get_time();
        ESP_LOGD(TG_CTL_STRM, "waiting for frame from queue...");
        if (xQueueReceive(frame_queue, &pic, pdMS_TO_TICKS(2000)) != pdTRUE || !pic) {
            int64_t t1 = esp_timer_get_time();
            ESP_LOGW(TG_CTL_STRM, "frame receive timeout after %lld µs", (long long)(t1-t0));
            break;
        }
        int64_t t1 = esp_timer_get_time();
        ESP_LOGD(TG_CTL_STRM, "got frame %zu B from queue in %lld µs", pic->len, (long long)(t1-t0));

        uint8_t *jpg = NULL;
        size_t   len = 0;
        bool converted = false;
        if (!get_jpeg(pic, &jpg, &len, &converted)) {
            ESP_LOGE(TG_CTL_STRM, "JPEG conversion failed");
            esp_camera_fb_return(pic);
            break;
        }
        char hdr[160];
        int hdr_len = snprintf(hdr, sizeof(hdr),
            "--" STREAM_BOUNDARY "\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %zu\r\n\r\n", len);

        esp_err_t res = httpd_resp_send_chunk(req, hdr, hdr_len);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)jpg, (ssize_t)len);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, "\r\n", 2);

        if (converted) free(jpg);
        esp_camera_fb_return(pic);

        if (res != ESP_OK) {
            ESP_LOGI(TG_CTL_STRM, "client disconnected (chunk send err %d)", res);
            break;
        }
        frame_idx++;
        if (frame_idx % 30 == 0) {
            ESP_LOGI(TG_CTL_STRM, "sent %lu frames", (unsigned long)frame_idx);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_OK;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Control handlers (unchanged)
// ═════════════════════════════════════════════════════════════════════════════

esp_err_t pan_handler(httpd_req_t *req) {
    int angle = 90;
    if (query_int(req, "angle", &angle) != ESP_OK) { send_err(req, "missing angle"); return ESP_FAIL; }
    if (servo_set_pan(angle) != ESP_OK)             { send_err(req, "servo error");  return ESP_FAIL; }
    ESP_LOGI(TG_CTL_CMD, "pan=%d°", servo_get_pan());
    send_ok(req);
    return ESP_OK;
}

esp_err_t tilt_handler(httpd_req_t *req) {
    int angle = 90;
    if (query_int(req, "angle", &angle) != ESP_OK) { send_err(req, "missing angle"); return ESP_FAIL; }
    if (servo_set_tilt(angle) != ESP_OK)            { send_err(req, "servo error");  return ESP_FAIL; }
    ESP_LOGI(TG_CTL_CMD, "tilt=%d°", servo_get_tilt());
    send_ok(req);
    return ESP_OK;
}

esp_err_t led_handler(httpd_req_t *req) {
    int state = 0;
    if (query_int(req, "state", &state) != ESP_OK) { send_err(req, "missing state"); return ESP_FAIL; }
    if (led_set(state != 0) != ESP_OK)             { send_err(req, "gpio error");   return ESP_FAIL; }
    ESP_LOGI(TG_CTL_CMD, "led=%s", led_get() ? "on" : "off");
    send_ok(req);
    return ESP_OK;
}

esp_err_t switch_handler(httpd_req_t *req) {
    int state = 0;
    if (query_int(req, "state", &state) != ESP_OK) { send_err(req, "missing state"); return ESP_FAIL; }
    if (switch_set(state != 0) != ESP_OK)          { send_err(req, "gpio error");   return ESP_FAIL; }
    ESP_LOGI(TG_CTL_CMD, "switch=%s", switch_get() ? "on" : "off");
    send_ok(req);
    return ESP_OK;
}

esp_err_t periph_state_handler(httpd_req_t *req) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"led\":%d,\"sw\":%d,\"pan\":%d,\"tilt\":%d}",
             led_get() ? 1 : 0, switch_get() ? 1 : 0,
             servo_get_pan(), servo_get_tilt());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, buf, (ssize_t)strlen(buf));
    return ESP_OK;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Handler registration
// ═════════════════════════════════════════════════════════════════════════════

void controller_register_handlers(httpd_handle_t server) {
    const httpd_uri_t routes[] = {
        { .uri = "/",              .method = HTTP_GET, .handler = root_handler          },
        { .uri = "/capture",       .method = HTTP_GET, .handler = capture_handler       },
        { .uri = "/stream",        .method = HTTP_GET, .handler = stream_handler        },
        { .uri = "/status",        .method = HTTP_GET, .handler = status_handler        },
        { .uri = "/hardware",      .method = HTTP_GET, .handler = hardware_info_handler },
        { .uri = "/control/pan",   .method = HTTP_GET, .handler = pan_handler           },
        { .uri = "/control/tilt",  .method = HTTP_GET, .handler = tilt_handler          },
        { .uri = "/control/led",   .method = HTTP_GET, .handler = led_handler           },
        { .uri = "/control/switch",.method = HTTP_GET, .handler = switch_handler        },
        { .uri = "/periph/state",  .method = HTTP_GET, .handler = periph_state_handler  },
    };
    const int n = (int)(sizeof(routes) / sizeof(routes[0]));
    for (int i = 0; i < n; i++) {
        esp_err_t r = httpd_register_uri_handler(server, &routes[i]);
        if (r != ESP_OK)
            ESP_LOGE(TG_NET_HTTP, "register %s failed: %s", routes[i].uri, esp_err_to_name(r));
    }
    ESP_LOGI(TG_NET_HTTP, "%d routes registered", n);
}

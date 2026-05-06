/*
  ESP32 — Joystick central + 3 botones NEMA (click: subir / bajar / parar)
  - AP: ESP32-ROVER / 12345678
  - UART2: TX2=17 → MEGA RX1(19), RX2=16 ← MEGA TX1(18)
  - Hacia MEGA:
      DRV:FWD/BACK/STOP , STR:LEFT/RIGHT/STOP
      N1:FWD/BACK/STOP  , N2:FWD/BACK/STOP
*/

#include <WiFi.h>
#include <WebServer.h>
#include <math.h>

// ------------ Wi-Fi ------------
const char* AP_SSID = "ESP32-ROVER";
const char* AP_PASS = "12345678";
IPAddress apIP(192,168,4,1), apGW(192,168,4,1), apMask(255,255,255,0);

// ------------ UART2 ------------
const int RX2_PIN = 16;   // <= MEGA TX1 (18)
const int TX2_PIN = 17;   // => MEGA RX1 (19)
const uint32_t BAUD = 115200;

WebServer server(80);

// ------------ Control ------------
static const uint8_t SPD_MIN = 60, SPD_MAX = 255;
static const float   DEADZONE = 0.15f;
static const uint32_t GAP_MS = 40;

// ====== TIPOS Y ESTADO (antes de usarlos) ======
enum Dir2 { NEG=-1, ZERO=0, POS=1 };

struct AxisState {
  Dir2    dir = ZERO;
  uint8_t spd = 0;
};

AxisState drivePrev, steerPrev;

// ====== HELPERS ======
static inline uint8_t clamp255(int v){ if(v<0)v=0; if(v>255)v=255; return (uint8_t)v; }
static inline uint8_t mapSpeed(float a){ int v=(int)roundf(SPD_MIN + a*(SPD_MAX-SPD_MIN)); return clamp255(v); }

// ====== HTML ======
const char PAGE_INDEX[] PROGMEM = R"HTML(
<!doctype html><html lang="es"><head>
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Rover</title>
<style>
  :root{font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial}
  body{margin:0;min-height:100vh;background:#0b0b0b;color:#fff}
  .grid{display:grid;gap:16px;padding:16px}
  .card{background:#151515;border-radius:16px;padding:16px;box-shadow:0 8px 24px rgba(0,0,0,.35)}
  .h{margin:0 0 10px;font-size:18px}
  .hint{opacity:.75}
  #pad{touch-action:none;background:#141414;border-radius:24px;box-shadow:0 10px 30px rgba(0,0,0,.45);width:320px;height:320px}
  .row{display:flex;gap:10px}
  .btn{flex:1;height:56px;border-radius:14px;border:0;background:#262626;color:#fff;font-size:18px}
  .btn:active{filter:brightness(1.15)}
  .slider{width:100%}
</style></head><body>
<div class="grid">
  <div class="card">
    <h2 class="h">Joystick</h2>
    <canvas id="pad" width="320" height="320"></canvas>
    <div class="hint">Arrastrá el joystick. Soltar = STOP (drive+steer).</div>
  </div>

  <div class="card">
    <h2 class="h">NEMA (2× DRV8825)</h2>
    <div>Velocidad: <span id="spdNemaVal">200</span>/255</div>
    <input id="spdNema" class="slider" type="range" min="60" max="255" value="200"/>
    <div style="height:10px"></div>
    <div class="row">
      <button id="btnDown" class="btn">NEMA ↓ Bajar</button>
      <button id="btnUp"   class="btn">NEMA ↑ Subir</button>
      <button id="btnStop" class="btn">⏹ Parar</button>
    </div>
    <div id="statusNema" class="hint"></div>
  </div>
</div>

<script>
// --------- Joystick ---------
const pad=document.getElementById('pad'), ctx=pad.getContext('2d');
const R=Math.min(pad.width,pad.height)/2-16;
let C={x:pad.width/2,y:pad.height/2}, K={x:C.x,y:C.y}, down=false, last=0;
function draw(){
  ctx.clearRect(0,0,pad.width,pad.height);
  ctx.beginPath(); ctx.arc(C.x,C.y,R,0,Math.PI*2); ctx.fillStyle='#1a1a1a'; ctx.fill();
  ctx.strokeStyle='#2a2a2a'; ctx.lineWidth=2;
  ctx.beginPath(); ctx.moveTo(C.x-R,C.y); ctx.lineTo(C.x+R,C.y); ctx.stroke();
  ctx.beginPath(); ctx.moveTo(C.x,C.y-R); ctx.lineTo(C.x,C.y+R); ctx.stroke();
  ctx.beginPath(); ctx.arc(K.x,K.y,28,0,Math.PI*2); ctx.fillStyle='#2e7dd7'; ctx.fill();
  ctx.beginPath(); ctx.arc(K.x,K.y,28,0,Math.PI*2); ctx.strokeStyle='rgba(255,255,255,.85)'; ctx.lineWidth=3; ctx.stroke();
}
draw();
function norm(x,y){
  const r=pad.getBoundingClientRect(); x-=r.left; y-=r.top;
  let dx=x-C.x, dy=y-C.y; const L=Math.hypot(dx,dy); if(L>R){ dx*=R/L; dy*=R/L; }
  K.x=C.x+dx; K.y=C.y+dy; draw();
  let nx=dx/R, ny=-dy/R; const mag=Math.hypot(nx,ny), DZ=%DEADZONE%;
  if(mag<DZ) return {x:0,y:0};
  const s=(mag-DZ)/(1-DZ); return {x:nx*s/mag, y:ny*s/mag};
}
async function send(nx,ny){
  const now=performance.now(); if(now-last<60) return; last=now;
  try{ await fetch(`/joy?x=${nx.toFixed(3)}&y=${ny.toFixed(3)}`,{method:'POST'});}catch(e){}
}
function center(){ K.x=C.x; K.y=C.y; draw(); fetch('/stop',{method:'POST'}).catch(()=>{}); }
pad.addEventListener('pointerdown',e=>{down=true; pad.setPointerCapture(e.pointerId); const v=norm(e.clientX,e.clientY); send(v.x,v.y);});
pad.addEventListener('pointermove',e=>{if(!down)return; const v=norm(e.clientX,e.clientY); send(v.x,v.y);});
function end(e){ if(!down)return; down=false; pad.releasePointerCapture(e.pointerId); center(); }
pad.addEventListener('pointerup',end); pad.addEventListener('pointercancel',end); pad.addEventListener('pointerleave',end);

// --------- NEMA: click para subir/bajar/parar ---------
const sp=document.getElementById('spdNema');
const spVal=document.getElementById('spdNemaVal');
const st=document.getElementById('statusNema');
sp.addEventListener('input',()=>spVal.textContent=sp.value);

document.getElementById('btnDown').addEventListener('click',()=>{
  console.log(sp.value);
  fetch('/nemaDown?spd='+sp.value,{method:'POST'}).then(r=>r.text()).then(t=>st.textContent=t).catch(()=>{});
});
document.getElementById('btnUp').addEventListener('click',()=>{
  fetch('/nemaUp?spd='+sp.value,{method:'POST'}).then(r=>r.text()).then(t=>st.textContent=t).catch(()=>{});
});
document.getElementById('btnStop').addEventListener('click',()=>{
  fetch('/nemaStop',{method:'POST'}).then(r=>r.text()).then(t=>st.textContent=t).catch(()=>{});
});
</script>
</body></html>
)HTML";

// ====== SERIAL TX helpers ======
void sendDrive(Dir2 d, uint8_t s){
  if (d==ZERO){ if(drivePrev.dir!=ZERO){ Serial2.print("DRV:STOP\n"); drivePrev={ZERO,0}; delay(GAP_MS);} return; }
  if (drivePrev.dir==d && drivePrev.spd==s) return;
  if (d==POS) Serial2.printf("DRV:FWD:%u\n",s); else Serial2.printf("DRV:BACK:%u\n",s);
  drivePrev={d,s}; delay(GAP_MS);
}
void sendSteer(Dir2 d, uint8_t s){
  if (d==ZERO){ if(steerPrev.dir!=ZERO){ Serial2.print("STR:STOP\n"); steerPrev={ZERO,0}; delay(GAP_MS);} return; }
  if (steerPrev.dir==d && steerPrev.spd==s) return;
  if (d==NEG) Serial2.printf("STR:LEFT:%u\n",s); else Serial2.printf("STR:RIGHT:%u\n",s);
  steerPrev={d,s}; delay(GAP_MS);
}

// ====== HTTP ======
void hIndex(){ server.send_P(200,"text/html; charset=utf-8",PAGE_INDEX); }

void hJoy(){
  if (!server.hasArg("x") || !server.hasArg("y")){ server.send(400,"text/plain","ERR:args"); return; }
  float x=server.arg("x").toFloat(), y=server.arg("y").toFloat();
  x=constrain(x,-1.0f,1.0f); y=constrain(y,-1.0f,1.0f);
  Dir2 sx=ZERO, sy=ZERO; uint8_t spx=0, spy=0;
  if (fabsf(x)>1e-3f){ sx=(x<0)?NEG:POS; spx=mapSpeed(fabsf(x)); }
  if (fabsf(y)>1e-3f){ sy=(y>0)?POS:NEG; spy=mapSpeed(fabsf(y)); }
  sendSteer(sx,spx); sendDrive(sy,spy);
  server.send(200,"text/plain","OK");
}
void hStop(){
  Serial2.print("DRV:STOP\n"); delay(GAP_MS);
  Serial2.print("STR:STOP\n");
  drivePrev={ZERO,0}; steerPrev={ZERO,0};
  server.send(200,"text/plain","STOP");
}

// NEMA: click para mover ambos
uint8_t qSpd(){ if(server.hasArg("spd")){ int v=server.arg("spd").toInt(); return clamp255(v);} return 200; }
void hNemaUp(){   uint8_t s=qSpd(); Serial2.printf("N1:FWD:%u\n",s);  delay(GAP_MS); Serial2.printf("N2:FWD:%u\n",s);  server.send(200,"text/plain",String("NEMA ↑ ")+s); }
void hNemaDown(){ uint8_t s=qSpd(); Serial2.printf("N1:BACK:%u\n",s); delay(GAP_MS); Serial2.printf("N2:BACK:%u\n",s); server.send(200,"text/plain",String("NEMA ↓ ")+s); }
void hNemaStop(){                  Serial2.print ("N1:STOP\n");      delay(GAP_MS); Serial2.print ("N2:STOP\n");      server.send(200,"text/plain","NEMA ⏹ STOP"); }

// ====== setup / loop ======
void setup(){
  Serial.begin(115200);
  Serial2.begin(BAUD, SERIAL_8N1, RX2_PIN, TX2_PIN);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apGW, apMask);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.println("AP: http://192.168.4.1");

  String page(PAGE_INDEX); page.replace("%DEADZONE%", String(DEADZONE,2));
  server.on("/", HTTP_GET, [page](){ server.send(200,"text/html; charset=utf-8", page); });

  server.on("/joy",     HTTP_POST, hJoy);
  server.on("/stop",    HTTP_POST, hStop);

  server.on("/nemaUp",   HTTP_POST, hNemaUp);
  server.on("/nemaDown", HTTP_POST, hNemaDown);
  server.on("/nemaStop", HTTP_POST, hNemaStop);

  server.begin();
}

void loop(){
  server.handleClient();
  while (Serial2.available()) Serial.write(Serial2.read());
}

#pragma once
#include <Arduino.h>
// ─────────────────────────────────────────────────────────────────────────────
//  Web-Seiten als PROGMEM-Raw-Strings.
//
//  BEWUSST in einen Header ausgelagert: Der Arduino/PlatformIO-Prototyp-Generator
//  scannt nur .ino-Dateien und stolpert sonst über das JavaScript ("async
//  function ...") in den Raw-Strings ("does not name a type"-Fehler). In einem
//  .h-Header passiert das nicht.
// ─────────────────────────────────────────────────────────────────────────────

const char CSS[] PROGMEM = R"CSS(
*{box-sizing:border-box}
body{font-family:system-ui,sans-serif;margin:0;background:#0f172a;color:#e2e8f0;font-size:16px}
nav{display:flex;background:#1e293b;position:sticky;top:0;z-index:9}
nav a{flex:1;text-align:center;padding:13px 4px;color:#93c5fd;text-decoration:none;font-size:.9rem}
nav a.active{background:#334155;color:#fff;font-weight:600}
.card{background:#1e293b;margin:12px;padding:14px 16px;border-radius:12px}
.card h2{font-size:.95rem;margin:0 0 10px;color:#93c5fd}
.big{font-size:2rem;font-weight:700}
table{width:100%;border-collapse:collapse;font-size:.92rem}
td{padding:7px 4px;border-bottom:1px solid #334155}
td.u{color:#94a3b8;text-align:right;white-space:nowrap}
.row{display:flex;justify-content:space-between;align-items:center;margin:9px 0;gap:10px}
.s{color:#94a3b8;font-size:.82rem}
button{background:#2563eb;color:#fff;border:0;border-radius:9px;padding:12px 16px;font-size:.95rem;margin:4px 4px 4px 0;cursor:pointer;min-height:44px}
button.alt{background:#334155}
select,input{background:#0f172a;color:#e2e8f0;border:1px solid #475569;border-radius:8px;padding:11px;font-size:1rem;min-height:44px}
input[type=number]{width:90px}
input:not([type=number]):not([type=file]){width:170px}
.pill{padding:4px 10px;border-radius:999px;font-size:.8rem;font-weight:600}
.on{background:#14532d;color:#bbf7d0}.off{background:#7f1d1d;color:#fecaca}
a.btnlink{display:block;text-align:center;background:#334155;color:#fff;text-decoration:none;padding:14px;border-radius:10px;margin:10px 0 2px;font-weight:600}
.foot{text-align:center;color:#64748b;font-size:.78rem;margin:16px 12px 24px}
)CSS";

const char MAIN_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html lang=de><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Zähler</title><link rel=stylesheet href=/style.css><link rel=icon href="data:,"></head><body>
<nav><a href=/ class=active>Start</a><a href=/strom>Strom</a><a href=/waerme>Wärme</a><a href=/update>Einstellungen</a></nav>
<div class=card><h2>Verbindung</h2>
 <div class=row><span>WLAN</span><b id=rssi>–</b></div>
 <div class=row><span>MQTT</span><span id=mqtt class=pill>–</span></div>
 <div class=row><span>Uptime</span><b id=up>–</b></div></div>
<div class=card><h2>⚡ Strom</h2>
 <div class=big><span id=leist>–</span> W</div>
 <div class=row><span>Bezug</span><b id=bezug>–</b></div>
 <div class=row><span>Einspeisung</span><b id=einsp>–</b></div>
 <div class=row><span>Status</span><span id=ss class=pill>–</span></div>
 <a class=btnlink href=/strom>Alle Stromwerte &rarr;</a></div>
<div class=card><h2>🔥 Wärme</h2>
 <div class=big><span id=h68>–</span> MWh</div>
 <div class=row><span>Status</span><span id=hs class=pill>–</span></div>
 <div class=row><span>nächste Lesung</span><b id=next>–</b></div>
 <a class=btnlink href=/waerme>Alle Wärmewerte &rarr;</a></div>
<div class=foot>Firmware v<span id=fwv>–</span> · Build <span id=fwb>–</span></div>
<script>
function pill(el,on,t){el.textContent=t;el.className='pill '+(on?'on':'off');}
function fmtNext(s){var h=Math.floor(s/3600),m=Math.floor(s/60)%60;return h+'h '+m+'m';}
async function tick(){try{const d=await(await fetch('/api')).json();
 rssi.textContent=d.rssi+' dBm';
 if(d.fw_ver!=null)fwv.textContent=d.fw_ver;
 if(d.fw_build)fwb.textContent=d.fw_build;
 if(!d.mqtt_en)pill(mqtt,false,'aus');else pill(mqtt,d.mqtt,d.mqtt?'verbunden':'getrennt');
 up.textContent=Math.floor(d.uptime_s/3600)+'h '+Math.floor(d.uptime_s/60)%60+'m';
 leist.textContent=d.strom.leistung_w??'–';
 bezug.textContent=(d.strom.bezug_kwh??'–')+' kWh';
 einsp.textContent=(d.strom.einspeisung_kwh??'–')+' kWh';
 pill(ss,d.strom.enabled&&d.strom.status=='ok',d.strom.enabled?d.strom.status:'aus');
 let v='–';for(const x of d.heat.codes)if(x.code=='6.8')v=x.value;
 h68.textContent=v;
 pill(hs,d.heat.enabled&&d.heat.status.indexOf('ok')==0,d.heat.enabled?d.heat.status:'aus');
 next.textContent=!d.heat.enabled?'aus':(d.heat.time_ok?d.heat.next_at+' (in '+fmtNext(d.heat.next_read_s)+')':fmtNext(d.heat.next_read_s));
}catch(e){}}
tick();setInterval(tick,3000);
</script></body></html>)HTML";

const char STROM_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html lang=de><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Strom</title><link rel=stylesheet href=/style.css><link rel=icon href="data:,"></head><body>
<nav><a href=/>Start</a><a href=/strom class=active>Strom</a><a href=/waerme>Wärme</a><a href=/update>Einstellungen</a></nav>
<div class=card><h2>⚡ Stromzähler</h2>
 <div class=row><span>Status</span><span id=ss class=pill>–</span></div></div>
<div class=card><h2>Alle Werte</h2><table id=tbl></table>
 <div class=s style=margin-top:8px>Hauptwerte: Bezug <b id=bz>–</b> · Einspeisung <b id=es>–</b> · Leistung <b id=lw>–</b></div></div>
<script>
function pill(el,on,t){el.textContent=t;el.className='pill '+(on?'on':'off');}
async function tick(){try{const d=await(await fetch('/api')).json();const s=d.strom;
 pill(ss,s.enabled&&s.status=='ok',s.enabled?s.status:'aus');
 let h='';for(const x of s.codes)h+='<tr><td>'+x.code+'</td><td><b>'+x.value+'</b></td><td class=u>'+(x.unit||'')+'</td></tr>';
 tbl.innerHTML=h||'<tr><td>– (keine Daten)</td></tr>';
 bz.textContent=(s.bezug_kwh??'–')+' kWh';es.textContent=(s.einspeisung_kwh??'–')+' kWh';lw.textContent=(s.leistung_w??'–')+' W';
}catch(e){}}
tick();setInterval(tick,3000);
</script></body></html>)HTML";

const char WAERME_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html lang=de><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Wärme</title><link rel=stylesheet href=/style.css><link rel=icon href="data:,"></head><body>
<nav><a href=/>Start</a><a href=/strom>Strom</a><a href=/waerme class=active>Wärme</a><a href=/update>Einstellungen</a></nav>
<div class=card><h2>🔥 Wärmezähler</h2>
 <button class=alt onclick="cmd('/read')">Jetzt lesen</button>
 <div class=row><span>nächste Lesung</span><b id=next>–</b></div></div>
<div class="card s">
 <div class=row><span>Status</span><b id=st>–</b></div>
 <div class=row><span>Identifikation</span><b id=ident>–</b></div>
 <div class=row><span>Anfrage</span><b id=req>–</b></div>
 <div class=row><span>Lesungen ok/total</span><b id=cnt>–</b></div>
 <div class=row><span>letztes Telegramm</span><b id=len>–</b></div></div>
<div class=card><h2>Alle Werte</h2><table id=tbl></table></div>
<script>
function fmtNext(s){var h=Math.floor(s/3600),m=Math.floor(s/60)%60;return h+'h '+m+'m';}
function cmd(u){fetch(u).then(()=>setTimeout(tick,500));}
async function tick(){try{const d=await(await fetch('/api')).json();const w=d.heat;
 next.textContent=!w.enabled?'aus':(w.time_ok?w.next_at+' (in '+fmtNext(w.next_read_s)+')':fmtNext(w.next_read_s));
 let h='';for(const x of w.codes)h+='<tr><td>'+x.code+'</td><td><b>'+(x.raw||x.value)+'</b></td><td class=u>'+(x.unit||'')+'</td></tr>';
 tbl.innerHTML=h||'<tr><td>– (keine Daten)</td></tr>';
 st.textContent=w.status;ident.textContent=w.ident||'–';req.textContent=w.request;
 cnt.textContent=w.ok+' / '+w.reads;len.textContent=w.last_len+' B';
}catch(e){}}
tick();setInterval(tick,3000);
</script></body></html>)HTML";

const char UPDATE_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html lang=de><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Einstellungen</title><link rel=stylesheet href=/style.css><link rel=icon href="data:,"></head><body>
<nav><a href=/>Start</a><a href=/strom>Strom</a><a href=/waerme>Wärme</a><a href=/update class=active>Einstellungen</a></nav>
<div class=card><h2>⚡ Strom</h2>
 <div class=row><span>Auslesen</span><button id=sen onclick=sToggle()>–</button></div>
 <div class=row><span>Lesekopf-GPIO</span><select id=sgpio></select></div>
 <div class=row><span>Sendeintervall (MQTT)</span><span><input type=number id=ssi min=2 max=300> s</span></div>
 <button onclick=sSave()>Speichern</button><div class=s id=smsg></div></div>
<div class=card><h2>🔥 Wärme</h2>
 <div class=row><span>Auslesen</span><button id=hen onclick=hToggle()>–</button></div>
 <div class=row><span>Startuhrzeit</span><input type=time id=hstart></div>
 <div class=row><span>Intervall</span><select id=hih>
   <option value=1>1 h</option><option value=2>2 h</option><option value=3>3 h</option>
   <option value=4>4 h</option><option value=6>6 h</option><option value=8>8 h</option>
   <option value=12>12 h</option><option value=24>24 h</option></select></div>
 <div class=s>Nur Teiler von 24 h &rarr; feste Uhrzeiten jeden Tag, ohne Lücke über Mitternacht.</div>
 <div class=row><span>TX-GPIO (&rarr; Lesekopf Rx)</span><select id=htx></select></div>
 <div class=row><span>RX-GPIO (&larr; Lesekopf Tx)</span><select id=hrx></select></div>
 <button onclick=hSave()>Speichern</button><div class=s id=hmsg></div></div>
<div class=card><h2>MQTT</h2>
 <div class=row><span>MQTT aktiv</span><button id=men onclick=mToggle()>–</button></div>
 <div class=row><span>Status</span><span id=mqtt class=pill>–</span></div>
 <div class=row><span>Haupttopic</span><input id=mroot placeholder=ESP32smartmeter></div>
 <div class=row><span>Host/IP</span><input id=mhost></div>
 <div class=row><span>Port</span><input type=number id=mport min=1 max=65535></div>
 <div class=row><span>User</span><input id=muser placeholder="leer = anonym"></div>
 <div class=row><span>Passwort</span><input type=password id=mpw placeholder="leer = unverändert"></div>
 <button onclick=saveMqtt()>Speichern</button><div class=s id=mmsg></div></div>
<div class=card><h2>📶 WLAN</h2>
 <div class=row><span>Verbunden mit</span><b id=wssid>–</b></div>
 <button class=alt onclick=wreset()>WLAN vergessen &amp; neu einrichten</button>
 <div class=s>Öffnet nach dem Neustart das offene Setup-WLAN „Zaehler-Setup".</div></div>
<div class=card><h2>Firmware-Update</h2>
<p class=s>Arduino-IDE: <b>Sketch &rarr; Kompilierte Binärdatei exportieren</b>, dann die <code>.ino.bin</code> hier wählen. (PlatformIO: <code>firmware.bin</code>)</p>
<input type=file id=file accept='.bin'><br><button onclick='go()'>Flashen</button>
<pre id=p style='font-size:1.3rem'></pre></div>
<div class=foot>Firmware v<span id=fwv>–</span> · Build <span id=fwb>–</span></div>
<script>
const INPINS=[16,17,18,19,21,22,23,25,26,27,32,33,34,35,36,39];
const OUTPINS=[16,17,18,19,21,22,23,25,26,27,32,33];
function opt(sel,a){sel.innerHTML='';for(const p of a){var o=document.createElement('option');o.value=p;o.textContent='GPIO'+p;sel.appendChild(o);}}
opt(sgpio,INPINS);opt(htx,OUTPINS);opt(hrx,INPINS);
function pill(el,on,t){el.textContent=t;el.className='pill '+(on?'on':'off');}
let sEnabled=true,hEnabled=true;
function sToggle(){fetch('/setstrom?en='+(sEnabled?0:1)).then(()=>setTimeout(tick,200));}
function sSave(){fetch('/setstrom?rx='+sgpio.value+'&s='+ssi.value).then(()=>{smsg.textContent='gespeichert';});}
function hToggle(){fetch('/setheat?en='+(hEnabled?0:1)).then(()=>setTimeout(tick,200));}
let mEnabled=false;
function mToggle(){fetch('/setmqtt?en='+(mEnabled?0:1)).then(()=>setTimeout(tick,200));}
function hSave(){fetch('/setheat?h='+hih.value+'&start='+encodeURIComponent(hstart.value||'00:00')+'&tx='+htx.value+'&rx='+hrx.value).then(()=>{hmsg.textContent='gespeichert';});}
function saveMqtt(){var q='/setmqtt?root='+encodeURIComponent(mroot.value)+
 '&host='+encodeURIComponent(mhost.value)+
 '&port='+mport.value+'&user='+encodeURIComponent(muser.value)+
 '&pw='+encodeURIComponent(mpw.value);
 fetch(q).then(()=>{mmsg.textContent=mEnabled?'gespeichert – verbinde neu…':'gespeichert';mpw.value='';});}
function wreset(){if(!confirm('WLAN-Daten löschen und neu einrichten? Der Zähler startet neu und öffnet das Setup-WLAN „Zaehler-Setup".'))return;
 fetch('/wifireset').then(()=>{alert('Neustart… bitte mit dem WLAN „Zaehler-Setup" verbinden.');});}
let inited=false;   // Eingabefelder NUR einmal befüllen, sonst überschreibt der Poll die Eingabe
async function tick(){try{const d=await(await fetch('/api')).json();const s=d.strom,w=d.heat;
 // Live-Status (keine Eingabefelder) — darf jeder Poll aktualisieren:
 if(d.fw_ver!=null)fwv.textContent=d.fw_ver;if(d.fw_build)fwb.textContent=d.fw_build;
 wssid.textContent=d.wifi_ssid||'–';
 sEnabled=s.enabled;sen.textContent=s.enabled?'AN':'AUS';sen.className=s.enabled?'':'alt';
 hEnabled=w.enabled;hen.textContent=w.enabled?'AN':'AUS';hen.className=w.enabled?'':'alt';
 mEnabled=d.mqtt_en;men.textContent=d.mqtt_en?'AN':'AUS';men.className=d.mqtt_en?'':'alt';
 if(!d.mqtt_en)pill(mqtt,false,'aus');else pill(mqtt,d.mqtt,d.mqtt?'verbunden':'getrennt');
 if(d.mqtt&&mmsg.textContent.indexOf('verbinde')>=0)mmsg.textContent='';
 mpw.placeholder=d.mqtt_haspw?'•••• gesetzt (leer=unverändert)':'leer = unverändert';
 // Eingabefelder nur beim ERSTEN erfolgreichen Poll füllen (danach gehört das Feld dem User):
 if(!inited){inited=true;
  sgpio.value=s.gpio;ssi.value=s.send_s;
  hih.value=w.interval_h;hstart.value=w.start_hhmm;htx.value=w.tx;hrx.value=w.rx;
  mroot.value=d.mqtt_root||'';mhost.value=d.mqtt_host||'';mport.value=d.mqtt_port||1883;muser.value=d.mqtt_user||'';
 }
}catch(e){}}
tick();setInterval(tick,3000);
function go(){var f=document.getElementById('file').files[0];if(!f)return;
var x=new XMLHttpRequest(),d=new FormData();d.append('f',f);
x.upload.onprogress=function(e){p.textContent=Math.round(e.loaded/e.total*100)+'%';};
x.onloadend=function(){p.textContent=x.responseText||'fertig';};
x.open('POST','/update');x.send(d);}</script></body></html>)HTML";

// Setup-Portal: wird im apMode unter "/" (und über den Captive-Redirect) ausgeliefert.
const char PORTAL_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html lang=de><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>WLAN einrichten</title><link rel=stylesheet href=/style.css><link rel=icon href="data:,"></head><body>
<div class=card><h2>📶 WLAN einrichten</h2>
 <p class=s>Wähle dein WLAN, gib das Passwort ein und speichere. Der Zähler startet
 danach neu und verbindet sich mit deinem Netz.</p>
 <div class=row><span>Netzwerk</span><button class=alt id=scanb onclick=scan()>Suchen</button></div>
 <select id=sel onchange="ssid.value=this.value"></select>
 <div class=row><span>SSID</span><input id=ssid placeholder="WLAN-Name"></div>
 <div class=row><span>Passwort</span><input type=password id=pass placeholder="WLAN-Passwort"></div>
 <button onclick=save()>Speichern &amp; Neustart</button>
 <div class=s id=msg></div></div>
<div class=foot>ESP32 Zähler · Ersteinrichtung</div>
<script>
let timer=null;
function scan(){scanb.textContent='sucht…';fetch('/scan').then(poll);}
function poll(){clearTimeout(timer);timer=setTimeout(async()=>{
 try{const d=await(await fetch('/scan.json')).json();
  if(d.scanning){poll();return;}
  scanb.textContent='Suchen';
  sel.innerHTML='<option value="">– Netzwerk wählen –</option>';
  for(const n of d.nets){var o=document.createElement('option');o.value=n.ssid;
   o.textContent=n.ssid+(n.enc?' 🔒':'')+'  '+n.rssi+'dBm';sel.appendChild(o);}
 }catch(e){scanb.textContent='Suchen';}
},1200);}
function save(){var s=ssid.value.trim();if(!s){msg.textContent='Bitte eine SSID angeben.';return;}
 fetch('/wifisave?ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(pass.value))
  .then(()=>{msg.textContent='Gespeichert – Neustart. Verbinde dich wieder mit deinem Heim-WLAN.';});}
scan();
</script></body></html>)HTML";

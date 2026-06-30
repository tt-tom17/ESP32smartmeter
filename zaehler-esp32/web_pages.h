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
)CSS";

const char MAIN_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html lang=de><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Zähler</title><link rel=stylesheet href=/style.css></head><body>
<nav><a href=/ class=active>Start</a><a href=/strom>Strom</a><a href=/waerme>Wärme</a><a href=/update>Update</a></nav>
<div class=card><h2>Verbindung</h2>
 <div class=row><span>WLAN</span><b id=rssi>–</b></div>
 <div class=row><span>Uptime</span><b id=up>–</b></div></div>
<div class=card><h2>MQTT (ioBroker)</h2>
 <div class=row><span>Status</span><span id=mqtt class=pill>–</span></div>
 <div class=row><span>Host/IP</span><input id=mhost></div>
 <div class=row><span>Port</span><input type=number id=mport min=1 max=65535></div>
 <div class=row><span>User</span><input id=muser placeholder="leer = anonym"></div>
 <div class=row><span>Passwort</span><input type=password id=mpw placeholder="leer = unverändert"></div>
 <button onclick=saveMqtt()>Speichern</button><div class=s id=mmsg></div></div>
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
<script>
function pill(el,on,t){el.textContent=t;el.className='pill '+(on?'on':'off');}
function fmtNext(s){var h=Math.floor(s/3600),m=Math.floor(s/60)%60;return h+'h '+m+'m';}
function saveMqtt(){var q='/setmqtt?host='+encodeURIComponent(mhost.value)+
 '&port='+mport.value+'&user='+encodeURIComponent(muser.value)+
 '&pw='+encodeURIComponent(mpw.value);
 fetch(q).then(()=>{mmsg.textContent='gespeichert – verbinde neu…';mpw.value='';});}
async function tick(){try{const d=await(await fetch('/api')).json();
 rssi.textContent=d.rssi+' dBm';
 pill(mqtt,d.mqtt,d.mqtt?'verbunden':'getrennt');
 var ae=document.activeElement;
 if(ae!==mhost)mhost.value=d.mqtt_host||'';
 if(ae!==mport)mport.value=d.mqtt_port||1883;
 if(ae!==muser)muser.value=d.mqtt_user||'';
 if(ae!==mpw)mpw.placeholder=d.mqtt_haspw?'•••• gesetzt (leer=unverändert)':'leer = unverändert';
 up.textContent=Math.floor(d.uptime_s/3600)+'h '+Math.floor(d.uptime_s/60)%60+'m';
 leist.textContent=d.strom.leistung_w??'–';
 bezug.textContent=(d.strom.bezug_kwh??'–')+' kWh';
 einsp.textContent=(d.strom.einspeisung_kwh??'–')+' kWh';
 pill(ss,d.strom.enabled&&d.strom.status=='ok',d.strom.enabled?d.strom.status:'aus');
 let v='–';for(const x of d.heat.codes)if(x.code=='6.8')v=x.value;
 h68.textContent=v;
 pill(hs,d.heat.enabled&&d.heat.status.indexOf('ok')==0,d.heat.enabled?d.heat.status:'aus');
 next.textContent=d.heat.enabled?fmtNext(d.heat.next_read_s):'aus';
}catch(e){}}
tick();setInterval(tick,3000);
</script></body></html>)HTML";

const char STROM_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html lang=de><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Strom</title><link rel=stylesheet href=/style.css></head><body>
<nav><a href=/>Start</a><a href=/strom class=active>Strom</a><a href=/waerme>Wärme</a><a href=/update>Update</a></nav>
<div class=card><h2>⚡ Stromzähler</h2>
 <div class=row><span>Auslesen</span><button id=en onclick=tEn()>–</button></div>
 <div class=row><span>Lesekopf-GPIO</span><select id=gpio></select></div>
 <div class=row><span>Sendeintervall (MQTT)</span><span><input type=number id=si min=2 max=300> s</span></div>
 <button onclick=save()>Speichern</button>
 <div class=s id=msg></div>
 <div class=row><span>Status</span><span id=ss class=pill>–</span></div></div>
<div class=card><h2>Alle Werte</h2><table id=tbl></table>
 <div class=s style=margin-top:8px>Hauptwerte: Bezug <b id=bz>–</b> · Einspeisung <b id=es>–</b> · Leistung <b id=lw>–</b></div></div>
<script>
const INPINS=[16,17,18,19,21,22,23,25,26,27,32,33,34,35,36,39];
let curEn=true;
function opt(sel,a){sel.innerHTML='';for(const p of a){var o=document.createElement('option');o.value=p;o.textContent='GPIO'+p;sel.appendChild(o);}}
opt(gpio,INPINS);
function pill(el,on,t){el.textContent=t;el.className='pill '+(on?'on':'off');}
function tEn(){fetch('/setstrom?en='+(curEn?0:1)).then(()=>setTimeout(tick,200));}
function save(){fetch('/setstrom?rx='+gpio.value+'&s='+si.value).then(()=>{msg.textContent='gespeichert';});}
async function tick(){try{const d=await(await fetch('/api')).json();const s=d.strom;
 curEn=s.enabled;en.textContent=s.enabled?'AN':'AUS';en.className=s.enabled?'':'alt';
 const ae=document.activeElement;
 if(ae!==gpio)gpio.value=s.gpio;
 if(ae!==si)si.value=s.send_s;
 pill(ss,s.enabled&&s.status=='ok',s.enabled?s.status:'aus');
 let h='';for(const x of s.codes)h+='<tr><td>'+x.code+'</td><td><b>'+x.value+'</b></td><td class=u>'+(x.unit||'')+'</td></tr>';
 tbl.innerHTML=h||'<tr><td>– (keine Daten)</td></tr>';
 bz.textContent=(s.bezug_kwh??'–')+' kWh';es.textContent=(s.einspeisung_kwh??'–')+' kWh';lw.textContent=(s.leistung_w??'–')+' W';
}catch(e){}}
tick();setInterval(tick,3000);
</script></body></html>)HTML";

const char WAERME_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html lang=de><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Wärme</title><link rel=stylesheet href=/style.css></head><body>
<nav><a href=/>Start</a><a href=/strom>Strom</a><a href=/waerme class=active>Wärme</a><a href=/update>Update</a></nav>
<div class=card><h2>🔥 Wärmezähler</h2>
 <div class=row><span>Auslesen</span><button id=en onclick=tEn()>–</button></div>
 <div class=row><span>Leseintervall</span><span><input type=number id=ih min=1 max=24> h</span></div>
 <div class=row><span>TX-GPIO (&rarr; Lesekopf Rx)</span><select id=tx></select></div>
 <div class=row><span>RX-GPIO (&larr; Lesekopf Tx)</span><select id=rx></select></div>
 <button onclick=save()>Speichern</button><div class=s id=msg></div></div>
<div class=card>
 <button class=alt onclick="cmd('/read')">Jetzt lesen</button>
 <button class=alt onclick="cmd('/toggle')">Anfrage /?! &harr; /#!</button>
 <div class=row><span>nächste Lesung</span><b id=next>–</b></div></div>
<div class=card><h2>Alle Werte</h2><table id=tbl></table></div>
<div class="card s">
 <div class=row><span>Status</span><b id=st>–</b></div>
 <div class=row><span>Identifikation</span><b id=ident>–</b></div>
 <div class=row><span>Anfrage</span><b id=req>–</b></div>
 <div class=row><span>Lesungen ok/total</span><b id=cnt>–</b></div>
 <div class=row><span>letztes Telegramm</span><b id=len>–</b></div></div>
<script>
const INPINS=[16,17,18,19,21,22,23,25,26,27,32,33,34,35,36,39];
const OUTPINS=[16,17,18,19,21,22,23,25,26,27,32,33];
let curEn=true;
function opt(sel,a){sel.innerHTML='';for(const p of a){var o=document.createElement('option');o.value=p;o.textContent='GPIO'+p;sel.appendChild(o);}}
opt(tx,OUTPINS);opt(rx,INPINS);
function fmtNext(s){var h=Math.floor(s/3600),m=Math.floor(s/60)%60;return h+'h '+m+'m';}
function tEn(){fetch('/setheat?en='+(curEn?0:1)).then(()=>setTimeout(tick,200));}
function save(){fetch('/setheat?h='+ih.value+'&tx='+tx.value+'&rx='+rx.value).then(()=>{msg.textContent='gespeichert';});}
function cmd(u){fetch(u).then(()=>setTimeout(tick,500));}
async function tick(){try{const d=await(await fetch('/api')).json();const w=d.heat;
 curEn=w.enabled;en.textContent=w.enabled?'AN':'AUS';en.className=w.enabled?'':'alt';
 const ae=document.activeElement;
 if(ae!==ih)ih.value=w.interval_h;
 if(ae!==tx)tx.value=w.tx;
 if(ae!==rx)rx.value=w.rx;
 next.textContent=w.enabled?fmtNext(w.next_read_s):'aus';
 let h='';for(const x of w.codes)h+='<tr><td>'+x.code+'</td><td><b>'+(x.raw||x.value)+'</b></td><td class=u>'+(x.unit||'')+'</td></tr>';
 tbl.innerHTML=h||'<tr><td>– (keine Daten)</td></tr>';
 st.textContent=w.status;ident.textContent=w.ident||'–';req.textContent=w.request;
 cnt.textContent=w.ok+' / '+w.reads;len.textContent=w.last_len+' B';
}catch(e){}}
tick();setInterval(tick,3000);
</script></body></html>)HTML";

const char UPDATE_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html lang=de><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Update</title><link rel=stylesheet href=/style.css></head><body>
<nav><a href=/>Start</a><a href=/strom>Strom</a><a href=/waerme>Wärme</a><a href=/update class=active>Update</a></nav>
<div class=card><h2>Firmware-Update</h2>
<p class=s>Arduino-IDE: <b>Sketch &rarr; Kompilierte Binärdatei exportieren</b>, dann die <code>.ino.bin</code> hier wählen. (PlatformIO: <code>firmware.bin</code>)</p>
<input type=file id=file accept='.bin'><br><button onclick='go()'>Flashen</button>
<pre id=p style='font-size:1.3rem'></pre></div>
<script>function go(){var f=document.getElementById('file').files[0];if(!f)return;
var x=new XMLHttpRequest(),d=new FormData();d.append('f',f);
x.upload.onprogress=function(e){p.textContent=Math.round(e.loaded/e.total*100)+'%';};
x.onloadend=function(){p.textContent=x.responseText||'fertig';};
x.open('POST','/update');x.send(d);}</script></body></html>)HTML";

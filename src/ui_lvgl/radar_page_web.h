// 26/10/2026 : page web de réglage du radar LD2410S, reprise TELLE QUELLE de la production (radar_webserver.h) ;
// servie par ui_radar_esp.cpp sur le port 80 (/ et /radar).
#pragma once
#include <pgmspace.h>

static const char RADAR_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Réglage radar LD2410S</title>
<style>
:root{--bg:#0f1115;--card:#1a1d24;--line:#2a2f3a;--fg:#e6e9ef;--mut:#8b93a3;--acc:#38bdf8;--ok:#22c55e;--warn:#f59e0b;--bad:#ef4444}
*{box-sizing:border-box}body{margin:0;font:15px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--fg);padding:16px}
h1{font-size:19px;margin:0 0 4px}.sub{color:var(--mut);font-size:13px;margin:0 0 16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin-bottom:16px}
.card.disabled{opacity:.45}
.row{display:flex;flex-wrap:wrap;gap:12px}.row>div{flex:1;min-width:130px}
label{display:block;font-size:12px;color:var(--mut);margin-bottom:4px}
input[type=number]{width:100%;background:#0f1115;border:1px solid var(--line);color:var(--fg);border-radius:8px;padding:8px;font-size:15px}
select{width:100%;background:#0f1115;border:1px solid var(--line);color:var(--fg);border-radius:8px;padding:8px;font-size:15px}
table{width:100%;border-collapse:collapse;font-size:13px}th,td{padding:5px;text-align:center;border-bottom:1px solid var(--line)}
th{color:var(--mut);font-weight:600}td input{width:60px}
button{background:var(--acc);color:#04242f;border:0;border-radius:8px;padding:10px 14px;font-size:14px;font-weight:600;cursor:pointer}
button.sec{background:#2a2f3a;color:var(--fg)}
.btns{display:flex;flex-wrap:wrap;gap:8px;margin-top:12px}
.stat{display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid var(--line)}.stat b{font-variant-numeric:tabular-nums}
.pill{display:inline-block;padding:2px 10px;border-radius:99px;font-size:12px;font-weight:600}
.on{background:rgba(34,197,94,.15);color:var(--ok)}.off{background:rgba(139,147,163,.15);color:var(--mut)}
.mode{background:rgba(56,189,248,.15);color:var(--acc)}
.bar{height:9px;background:#0f1115;border-radius:6px;overflow:hidden}.bar>i{display:block;height:100%;background:var(--acc)}
.eng td{padding:3px 5px}
.note{font-size:12px;color:var(--warn);margin-top:8px}
.toast{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:var(--card);border:1px solid var(--line);padding:10px 16px;border-radius:8px;opacity:0;transition:.3s}
.toast.show{opacity:1}small{color:var(--mut)}
.tabs{display:flex;gap:8px;margin-bottom:16px;border-bottom:2px solid var(--line);padding-bottom:0}
.tab-btn{background:var(--card);color:var(--fg);border:2px solid var(--line);border-bottom:0;border-radius:10px 10px 0 0;padding:10px 18px;font-size:14px;font-weight:600;cursor:pointer;margin-bottom:-2px}
.tab-btn:hover{background:#242833}
.tab-btn.active{background:var(--acc);color:#04242f;border-color:var(--acc)}
.tabpage{display:none}
.tabpage.active{display:block}
pre.logbox{max-height:45vh;overflow:auto;font-size:12px;background:#0f1115;border:1px solid var(--line);border-radius:8px;padding:10px;white-space:pre-wrap;word-break:break-all}
</style></head><body>
<h1>Dashboard Guition 4848S040</h1>

<div class="tabs">
  <button class="tab-btn active" id="tabbtn-radar" onclick="showTab('radar')">Radar</button>
  <button class="tab-btn" id="tabbtn-journal" onclick="showTab('journal')">Journal</button>
</div>

<div id="tabpage-radar" class="tabpage active">

<p class="sub">Radar LD2410S — chaque porte ≈ 0,7 m (0 à 15)</p>

<div class="card">
  <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px">
    <b>État en direct</b>
    <span><span id="mode" class="pill mode">—</span> <span id="presence" class="pill off">—</span></span>
  </div>
  <div class="stat"><span>Distance détectée</span><b id="dd">—</b></div>
</div>

<div class="card" id="engCard">
  <b>Énergie par porte (mode PC uniquement)</b>
  <table class="eng"><thead><tr><th>Porte</th><th>Distance</th><th>Énergie</th></tr></thead>
  <tbody id="engBody"></tbody></table>
  <div class="note" id="engNote" style="display:none">
    Non disponible : le capteur est en mode "Minimal" (basse consommation). Bascule
    RADAR_COMM_MODE sur RADAR_COMM_MODE_PC dans radar.h pour l'activer.
  </div>
</div>

<div class="card">
  <b>Portée &amp; délai</b>
  <div class="row" style="margin-top:8px">
    <div><label>Porte max (1-16)</label><input id="mg" type="number" min="1" max="16"></div>
    <div><label>Porte min (0-16)</label><input id="ng" type="number" min="0" max="16"></div>
    <div><label>Délai d'absence (10-120s)</label><input id="delay" type="number" min="10" max="120"></div>
  </div>
  <div class="row" style="margin-top:8px">
    <div><label>Fréq. statut (0.5-8 Hz)</label><input id="sf" type="number" min="0.5" max="8" step="0.5"></div>
    <div><label>Fréq. distance (0.5-8 Hz)</label><input id="df" type="number" min="0.5" max="8" step="0.5"></div>
    <div><label>Vitesse de réponse</label>
      <select id="speed"><option value="5">Normale</option><option value="10">Rapide</option></select>
    </div>
  </div>
</div>

<div class="card">
  <b>Auto-réglage des seuils</b>
  <p class="note">Assurez-vous qu'il n'y a personne dans la pièce pendant toute la durée du scan : le capteur calcule les seuils à partir du bruit ambiant, une présence pendant la mesure fausserait le résultat.</p>
  <div class="row" style="margin-top:8px">
    <div><label>Durée du scan (10-600s)</label><input id="acScan" type="number" min="10" max="600" value="120"></div>
    <div><label>Marge ajoutée au déclenchement (0-30 dB)</label><input id="acMarge" type="number" min="0" max="30" value="0" onchange="fetch('/radar/marge?db='+this.value)"></div>
  </div>
  <div class="btns">
    <button id="acBtn" onclick="startAutoCal()">Démarrer l'auto-réglage</button>
  </div>
  <div style="margin-top:10px">
    <div class="bar"><i id="acBar" style="width:0%"></i></div>
    <small id="acPct">0%</small>
  </div>
  <pre id="acLog" style="margin-top:10px;max-height:140px;overflow:auto;font-size:11px;background:#0f1115;border:1px solid var(--line);border-radius:8px;padding:8px;white-space:pre-wrap"></pre>
</div>

<div class="card">
  <b>Sensibilité par porte</b>
  <p class="sub" style="margin:4px 0 8px">Seuils absolus de 10 à 95 dB pour les 16 portes (firmware 1.1.1). Plus bas = plus sensible.</p>
  <table><thead><tr><th>Porte</th><th>Distance</th><th>Type</th><th>Déclenchement</th><th>Maintien</th></tr></thead>
  <tbody id="gates"></tbody></table>
  <div class="btns">
    <button onclick="load()" class="sec">Lire la config actuelle</button>
    <button onclick="apply()">Appliquer</button>
    <button onclick="usine()" class="sec">Valeurs d'usine</button>
  </div>
</div>

</div>

<div id="tabpage-journal" class="tabpage">
  <p class="sub">Journaux de démarrage — LittleFS</p>
  <div class="card">
    <div class="btns" style="margin-top:0">
      <button onclick="loadJournal()" class="sec">Rafraîchir</button>
    </div>
  </div>
  <div class="card">
    <div style="display:flex;justify-content:space-between;align-items:center">
      <b>Journal complet (une ligne par redémarrage)</b>
      <button class="sec" style="background:var(--bad);color:#fff" onclick="clearJournal('complet')">Effacer</button>
    </div>
    <pre id="journalComplet" class="logbox" style="margin-top:8px">—</pre>
  </div>
  <div class="card">
    <div style="display:flex;justify-content:space-between;align-items:center">
      <b>Journal résumé (par jour)</b>
      <button class="sec" style="background:var(--bad);color:#fff" onclick="clearJournal('resume')">Effacer</button>
    </div>
    <pre id="journalResume" class="logbox" style="margin-top:8px">—</pre>
  </div>
</div>

<div id="toast" class="toast"></div>
<script>
const $=id=>document.getElementById(id);
function toast(t){const e=$('toast');e.textContent=t;e.classList.add('show');setTimeout(()=>e.classList.remove('show'),1800)}
function gm(g){return (g*0.7).toFixed(2)+'-'+((g+1)*0.7).toFixed(2)+' m'}
for(let g=0;g<16;g++){
  $('gates').insertAdjacentHTML('beforeend',
   '<tr><td>'+g+'</td><td><small>'+gm(g)+'</small></td><td><small>Abs.</small></td>'+
   '<td><input id="t'+g+'" type="number" min="10" max="95"></td>'+
   '<td><input id="h'+g+'" type="number" min="10" max="95"></td></tr>');
  $('engBody').insertAdjacentHTML('beforeend',
   '<tr><td>'+g+'</td><td><small>'+gm(g)+'</small></td>'+
   '<td><div class="bar"><i id="e'+g+'" style="width:0%"></i></div></td></tr>');
}
async function load(){
  const r=await fetch('/radar/config');const c=await r.json();
  if(!c.ok){toast('Lecture echouee');return}
  $('mg').value=c.maxGate;$('ng').value=c.minGate;$('delay').value=c.delay;
  $('sf').value=c.statusFreq;$('df').value=c.distFreq;$('speed').value=c.speed;
  for(let g=0;g<16;g++){$('t'+g).value=c.trig[g];$('h'+g).value=c.hold[g];}
  toast('Config chargee');
}
async function apply(){
  const p=new URLSearchParams();
  p.set('mg',$('mg').value);p.set('ng',$('ng').value);p.set('delay',$('delay').value);
  p.set('sf',$('sf').value);p.set('df',$('df').value);p.set('speed',$('speed').value);
  for(let g=0;g<16;g++){p.set('t'+g,$('t'+g).value);p.set('h'+g,$('h'+g).value);}
  toast('Application...');
  const r=await fetch('/radar/apply',{method:'POST',body:p});
  toast((await r.json()).ok?'Reglages appliques':'Echec');
}
async function usine(){
  if(!confirm("Ecrire les seuils de declenchement d'usine (maintien, portee, delai et frequences gardes) ?")) return;
  const j=await (await fetch('/radar/usine',{method:'POST'})).json();
  toast(j.identique?"Valeurs d'usine ecrites et relues":(j.ok?'Ecrites, relecture differente':'Echec'));
  load();
}
// 30/10 : chaque trame est poussée par l'écran (flux d'événements, port 81) : un rafraîchissement par trame, sans demande
let derniereTrame=-1;
function afficher(c){
    if(c.n===derniereTrame)return;
    derniereTrame=c.n;
    const p=$('presence');p.textContent=c.presence?'Presence':'Rien';p.className='pill '+(c.presence?'on':'off');
    $('dd').textContent=c.distance+' cm';
    $('mode').textContent=c.pcMode?'Mode PC':'Mode Minimal';
    $('engCard').classList.toggle('disabled', !c.pcMode);
    $('engNote').style.display = c.pcMode?'none':'block';
    if(c.pcMode && c.gateEnergy){
      for(let g=0;g<16;g++){ $('e'+g).style.width=c.gateEnergy[g]+'%'; }
    }
}
function brancherFlux(){
  const f=new EventSource('http://'+location.hostname+':81/');
  f.onmessage=e=>{try{afficher(JSON.parse(e.data))}catch(x){}};
}
let autoCalTimer=null;
function acLog(t){
  const el=$('acLog');
  const time=new Date().toLocaleTimeString();
  el.textContent += '['+time+'] '+t+'\n';
  el.scrollTop = el.scrollHeight;
}
async function startAutoCal(){
  if(!confirm("Assurez-vous qu'il n'y a personne dans la piece pendant tout le scan. Demarrer l'auto-reglage ?")) return;
  const p=new URLSearchParams();p.set('scan',$('acScan').value);p.set('marge',$('acMarge').value);
  acLog('Demande de demarrage envoyee (scan='+$('acScan').value+'s)');
  const r=await fetch('/radar/autocal/start',{method:'POST',body:p});
  const j=await r.json();
  acLog('Reponse demarrage : '+JSON.stringify(j));
  if(!j.ok){toast('Echec du demarrage');return}
  $('acBtn').disabled=true;
  toast('Auto-reglage demarre');
  if(autoCalTimer)clearInterval(autoCalTimer);
  autoCalTimer=setInterval(pollAutoCal,1000);
  pollAutoCal();
}
let lastAcProgress=-1;
async function pollAutoCal(){
  const j=await (await fetch('/radar/autocal/status')).json();
  $('acBar').style.width=j.progress+'%';
  $('acPct').textContent=j.progress.toFixed(0)+'%';
  if(j.progress!==lastAcProgress){
    acLog('Progression : '+j.progress.toFixed(1)+'% (running='+j.running+')');
    lastAcProgress=j.progress;
  }
  if(!j.running){
    if(autoCalTimer){clearInterval(autoCalTimer);autoCalTimer=null;}
    $('acBtn').disabled=false;
    toast('Auto-reglage termine');
    acLog('Termine. Seuils relus : declenchement='+JSON.stringify(j.trig)+' maintien='+JSON.stringify(j.hold));
    if(j.trig){
      for(let g=0;g<16;g++){$('t'+g).value=j.trig[g];$('h'+g).value=j.hold[g];}
    }
  }
}
function showTab(name){
  document.querySelectorAll('.tabpage').forEach(e=>e.classList.remove('active'));
  document.querySelectorAll('.tab-btn').forEach(e=>e.classList.remove('active'));
  $('tabpage-'+name).classList.add('active');
  $('tabbtn-'+name).classList.add('active');
  if(name==='journal') loadJournal();
}
function scrollToEnd(el){ el.scrollTop = el.scrollHeight; }
async function loadJournal(){
  $('journalComplet').textContent='Chargement...';
  $('journalResume').textContent='Chargement...';
  try{
    const j=await (await fetch('/radar/journal')).json();
    $('journalComplet').textContent=j.complet||'(vide)';
    $('journalResume').textContent=j.resume||'(vide)';
  }catch(e){
    $('journalComplet').textContent='Erreur de chargement';
    $('journalResume').textContent='Erreur de chargement';
  }
  scrollToEnd($('journalComplet'));
  scrollToEnd($('journalResume'));
}
async function clearJournal(which){
  const label = which==='complet' ? 'le journal complet' : 'le journal resume';
  if(!confirm('Effacer definitivement '+label+' ? Cette action est irreversible.')) return;
  const p=new URLSearchParams();p.set('which',which);
  const r=await fetch('/radar/journal/clear',{method:'POST',body:p});
  const j=await r.json();
  toast(j.ok?'Journal efface':'Echec');
  loadJournal();
}
load();brancherFlux();fetch('/radar/status').then(r=>r.json()).then(afficher);
fetch('/radar/marge').then(r=>r.json()).then(j=>{$('acMarge').value=j.marge});
</script></body></html>
)HTML";

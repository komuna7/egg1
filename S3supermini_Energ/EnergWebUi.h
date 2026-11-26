#pragma once
#include <Arduino.h>

// Iestatījumu lapa. Karte (Leaflet + OpenStreetMap) ielādējas no interneta telefona pārlūkā;
// ja tā neielādējas, punktu var ievadīt ar koordinātām.
const char ENERG_WEB_UI[] PROGMEM = R"HTML(<!doctype html>
<html lang="lv"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#1d2b24"><title>Energ ekrāns</title>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" crossorigin="">
<style>
:root{--ground:#e8ece6;--panel:#fbfcfa;--ink:#17231d;--soft:#5b6b62;--rule:#cdd6cf;--band:#1d2b24;--band-ink:#f1f6f2;--accent:#f2b705;--go:#1f7a4d;--stop:#b5412f;--field:#fff;color-scheme:light}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){--ground:#101813;--panel:#18231d;--ink:#e7efe9;--soft:#9fb1a6;--rule:#2c3c33;--band:#0b120e;--go:#5cc28f;--stop:#ff8e7c;--field:#121c16;color-scheme:dark}}
*{box-sizing:border-box}body{margin:0;background:var(--ground);color:var(--ink);font:16px/1.5 system-ui,"Segoe UI",Roboto,sans-serif;padding-bottom:calc(90px + env(safe-area-inset-bottom))}
header{background:var(--band);color:var(--band-ink);padding:18px 16px 14px;border-bottom:5px solid var(--accent)}header h1{margin:0;font-size:26px;line-height:1.1}header p{margin:4px 0 0;opacity:.8;font-size:14px}
main{max-width:720px;margin:0 auto;padding:12px 16px}section{background:var(--panel);border:1px solid var(--rule);border-radius:8px;padding:14px;margin:0 0 14px}h2{margin:0 0 4px;font-size:19px}p.hint{margin:0 0 12px;color:var(--soft);font-size:14px}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}.card{border:1px solid var(--rule);border-radius:6px;padding:10px}.card b{display:block;font-size:13px;color:var(--soft);font-weight:600}.card .v{font-size:26px;font-weight:700;font-variant-numeric:tabular-nums}.card small{display:block;color:var(--soft);font-size:13px;line-height:1.35}.card.cur{border-color:var(--accent);box-shadow:inset 0 0 0 1px var(--accent)}
canvas#spark{width:100%;height:46px;display:block;margin-top:6px}
.row{display:grid;grid-template-columns:auto 1fr auto;align-items:center;gap:10px;padding:10px 0;border-top:1px solid var(--rule)}.row:first-of-type{border-top:0}.row .name{font-weight:600}.row .extra{grid-column:2/4;display:flex;flex-wrap:wrap;gap:12px;font-size:14px;color:var(--soft)}
input[type=checkbox]{width:22px;height:22px;accent-color:var(--go)}label{font-weight:600}label.f{display:block;margin:0 0 12px}input:not([type=checkbox]):not([type=range]),select{display:block;width:100%;height:44px;margin-top:4px;padding:0 10px;border:1px solid var(--rule);border-radius:6px;background:var(--field);color:var(--ink);font:inherit}
.sec{display:flex;align-items:center;gap:6px;font-weight:400}.sec input{width:74px!important;display:inline-block!important;margin:0!important;text-align:right}
.inline{display:flex;align-items:center;gap:8px;font-weight:400}.grid2{display:grid;grid-template-columns:1fr 1fr;gap:10px}input[type=range]{width:100%;accent-color:var(--go)}
#map{height:340px;border-radius:6px;border:1px solid var(--rule);margin:0 0 10px;background:var(--ground)}#mapNote{color:var(--soft);font-size:14px}
button{font:inherit;cursor:pointer}.btn{min-height:44px;padding:0 14px;border:1px solid var(--rule);border-radius:6px;background:var(--field);color:var(--ink);font-weight:600}.danger{border-color:var(--stop);color:var(--stop)}
.bar{position:fixed;left:0;right:0;bottom:0;background:var(--band);color:var(--band-ink);padding:10px 16px calc(10px + env(safe-area-inset-bottom));display:flex;gap:10px;justify-content:center;transform:translateY(110%);transition:transform .2s}.bar.on{transform:none}.bar .main{min-height:46px;padding:0 22px;border:0;border-radius:6px;background:var(--accent);color:#17231d;font-weight:700}.bar .btn{background:transparent;color:var(--band-ink);border-color:#ffffff44}
.toast{position:fixed;left:50%;top:14px;transform:translateX(-50%);background:var(--band);color:var(--band-ink);padding:8px 14px;border-radius:6px;opacity:0;transition:opacity .2s;pointer-events:none;z-index:2000}.toast.on{opacity:1}
dl{display:grid;grid-template-columns:auto 1fr;gap:2px 12px;margin:0 0 12px;font-size:14px}dt{color:var(--soft)}dd{margin:0;word-break:break-all}
:focus-visible{outline:3px solid var(--accent);outline-offset:2px}
</style></head><body>
<header><h1>Energ</h1><p id="sub">Savienojas ar ierīci…</p></header>
<main>
<section><h2>Tagad</h2><p class="hint">Ko ekrāns rāda šobrīd. Izcelta ir sadaļa, kas redzama ekrānā.</p><div class="cards" id="cards"></div><canvas id="spark" aria-label="Elektrības cena nākamās 24 stundas"></canvas></section>

<section><h2>Ekrāna sadaļas</h2><p class="hint">Ieslēgtās sadaļas ekrānā mainās pēc kārtas. Pieskāriens sensoram pāriet uz nākamo uzreiz.</p><div id="rows"></div></section>

<section><h2>Mana vieta</h2><p class="hint">Pieskaries kartei, lai noliktu punktu. Lētāko degvielu meklē tikai stacijās, līdz kurām nokļūsti norādītajā laikā.</p>
<div id="map"></div><p id="mapNote"></p>
<div class="grid2"><label class="f">Platums<input id="lat" inputmode="decimal"></label><label class="f">Garums<input id="lon" inputmode="decimal"></label></div>
<label class="f">Maksimālais laiks ceļā: <span id="maxMinText"></span><input id="maxMin" type="range" min="1" max="90" step="1"></label>
<div class="grid2"><label class="f">Vidējais ātrums, km/h<input id="speed" type="number" min="5" max="130"></label><label class="f inline" style="margin-top:28px"><input id="roundTrip" type="checkbox">Turp un atpakaļ</label></div>
<p class="hint" id="reach"></p></section>

<section><h2>Dati</h2>
<dl id="dataInfo"></dl>
<label class="f">Degvielas staciju saraksta adrese (JSON)<input id="fuelUrl" type="url" autocomplete="off"></label>
<button class="btn" id="refresh">Atjaunot datus tagad</button></section>

<section><h2>Wi-Fi tīkli</h2><p class="hint">Pēc saglabāšanas ierīce pārstartējas. Paroli atstāj tukšu, ja tā nemainās.</p>
<div class="grid2"><label class="f">Tīkls 1<input id="ssid1" maxlength="32"></label><label class="f">Parole 1<input id="pass1" type="password" maxlength="63"></label></div>
<button class="btn" data-slot="1">Saglabāt tīklu 1</button>
<div class="grid2" style="margin-top:12px"><label class="f">Tīkls 2<input id="ssid2" maxlength="32"></label><label class="f">Parole 2<input id="pass2" type="password" maxlength="63"></label></div>
<button class="btn" data-slot="2">Saglabāt tīklu 2</button>
<p><button class="btn" id="setup">Atvērt iestatīšanu no jauna</button></p></section>

<section><h2>Rūpnīcas atiestatīšana</h2><p class="hint">Izdzēš Wi-Fi paroles un visus iestatījumus. Ierīce startēsies kā jauna.</p><button class="btn danger" id="reset">Atiestatīt</button></section>
</main>
<div class="bar" id="bar"><button class="btn" id="undo">Atcelt</button><button class="main" id="save">Saglabāt izmaiņas</button></div>
<div class="toast" id="toast" role="status"></div>
<script>
const $=id=>document.getElementById(id);let S=null,saved=null,map=null,home=null,circle=null,layer=null,first=true;
const FUEL={lpg:'Gāze (LPG)','95':'Benzīns 95','98':'Benzīns 98',dd:'Dīzeļdegviela'};
const copy=o=>JSON.parse(JSON.stringify(o));
async function api(path,body){const o=body?{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}:{};const r=await fetch(path,o),t=await r.text();let d=null;try{d=t?JSON.parse(t):null}catch(e){}if(!r.ok)throw new Error(d&&d.error||'Kļūda '+r.status);return d}
function toast(t){$('toast').textContent=t;$('toast').classList.add('on');clearTimeout(toast.t);toast.t=setTimeout(()=>$('toast').classList.remove('on'),2600)}
function pick(s){return{sections:s.sections.map(x=>({id:x.id,on:x.on,sec:x.sec})),place:{lat:s.place.lat,lon:s.place.lon,maxMin:s.place.maxMin,speed:s.place.speed,roundTrip:s.place.roundTrip},petrol:s.petrol,vat:s.vat,fuelUrl:s.fuelUrl}}
function dirty(){$('bar').classList.toggle('on',JSON.stringify(pick(S))!==JSON.stringify(pick(saved)));drawReach()}
const fmt=(v,d)=>v==null?'–':Number(v).toFixed(d).replace('.',',');
const when=t=>t?new Date(t*1000).toLocaleString('lv-LV',{day:'numeric',month:'numeric',hour:'2-digit',minute:'2-digit'}):'nav';
function reachKm(p){return p.maxMin/(p.roundTrip?2:1)/60*p.speed/(saved?saved.place.roadFactor:1.3)}
function card(title,value,lines,cur){const c=document.createElement('div');c.className='card'+(cur?' cur':'');const b=document.createElement('b');b.textContent=title;const v=document.createElement('div');v.className='v';v.textContent=value;c.append(b,v);for(const l of lines){const s=document.createElement('small');s.textContent=l;c.append(s)}return c}
function fuelCard(key,title,cur){const p=saved.fuel.picks[key]||{},b=p.best,n=p.nearest;if(b)return card(title,fmt(b.price,3)+' €',[b.brand+', '+b.address,Math.round(b.minutes)+' min, '+fmt(b.km,1)+' km'],cur);if(n)return card(title,fmt(n.price,3)+' €',['Robežās nav. Tuvākā:',n.brand+', '+n.address,Math.round(n.minutes)+' min'],cur);return card(title,'–',[saved.fuel.stations?'Nav cenu':'Nav datu'],cur)}
function renderCards(){const c=$('cards');c.textContent='';const e=saved.elec,cur=saved.current;c.append(card('Elektrība LV',e.now==null?'–':fmt(e.now,2)+' c',[saved.vat?'c/kWh ar PVN':'c/kWh bez PVN',e.times?'no '+new Date(e.slotStart*1000).toLocaleTimeString('lv-LV',{hour:'2-digit',minute:'2-digit'}):'nav datu'],cur==='elec'));c.append(fuelCard('lpg','Gāze (LPG)',cur==='lpg'),fuelCard(saved.petrol,FUEL[saved.petrol],cur==='petrol'),fuelCard('dd','Dīzeļdegviela',cur==='diesel'));drawSpark()}
function drawSpark(){const cv=$('spark'),e=saved.elec,dpr=devicePixelRatio||1,w=cv.clientWidth,h=46;cv.width=w*dpr;cv.height=h*dpr;const g=cv.getContext('2d');g.scale(dpr,dpr);g.clearRect(0,0,w,h);if(!e.points||!e.points.length)return;const lo=Math.min(0,...e.points),hi=Math.max(...e.points,lo+1),n=e.points.length,cs=getComputedStyle(document.documentElement);const min=e.points.indexOf(Math.min(...e.points));for(let i=0;i<n;i++){const x=i*w/n,bw=Math.max(1,w/n-1),bh=2+(e.points[i]-lo)/(hi-lo)*(h-12);g.fillStyle=i===0?cs.getPropertyValue('--accent'):i===min?cs.getPropertyValue('--go'):cs.getPropertyValue('--soft');g.fillRect(x,h-bh,bw,bh)}g.fillStyle=cs.getPropertyValue('--soft');g.font='11px system-ui';g.fillText('tagad',0,10);g.textAlign='right';g.fillText('+24 h',w,10)}
function renderRows(){const box=$('rows');box.textContent='';for(const s of S.sections){const row=document.createElement('div');row.className='row';const on=document.createElement('input');on.type='checkbox';on.checked=s.on;on.setAttribute('aria-label','Rādīt: '+s.name);on.onchange=()=>{s.on=on.checked;dirty()};const name=document.createElement('span');name.className='name';name.textContent=s.name;const sec=document.createElement('label');sec.className='sec';const n=document.createElement('input');n.type='number';n.min=2;n.max=600;n.value=s.sec;n.oninput=()=>{s.sec=Math.max(2,Math.min(600,parseInt(n.value)||2));dirty()};sec.append(n,' s');row.append(on,name,sec);const extra=document.createElement('div');extra.className='extra';if(s.id==='elec'){const l=document.createElement('label');l.className='inline';const c=document.createElement('input');c.type='checkbox';c.checked=S.vat;c.onchange=()=>{S.vat=c.checked;dirty()};l.append(c,'Cena ar PVN 21%');extra.append(l,'Nord Pool biržas cena, bez tirgotāja maksas un sadales.')}else if(s.id==='petrol'){const l=document.createElement('label');l.className='inline';const sel=document.createElement('select');sel.style.width='auto';for(const k of['95','98']){const o=document.createElement('option');o.value=k;o.textContent='Benzīns '+k;sel.append(o)}sel.value=S.petrol;sel.onchange=()=>{S.petrol=sel.value;dirty();loadStations()};l.append(sel);extra.append(l)}if(extra.childNodes.length)row.append(extra);box.append(row)}}
function renderPlace(){const p=S.place;$('lat').value=p.lat.toFixed(5);$('lon').value=p.lon.toFixed(5);$('maxMin').value=Math.min(90,p.maxMin);$('maxMinText').textContent=p.maxMin+' min';$('speed').value=p.speed;$('roundTrip').checked=p.roundTrip;drawReach()}
function drawReach(){if(!S)return;const p=S.place,km=reachKm(p);$('reach').textContent='Ar '+p.speed+' km/h '+(p.roundTrip?'turp un atpakaļ ':'')+p.maxMin+' minūtēs aizbrauc apmēram '+fmt(km*(saved?saved.place.roadFactor:1.3),1)+' km pa ceļu. Taisnē tas ir līdz '+fmt(km,1)+' km.';if(map){home.setLatLng([p.lat,p.lon]);circle.setLatLng([p.lat,p.lon]).setRadius(km*1000)}}
function setPoint(lat,lon){S.place.lat=Math.round(lat*1e5)/1e5;S.place.lon=Math.round(lon*1e5)/1e5;$('lat').value=S.place.lat.toFixed(5);$('lon').value=S.place.lon.toFixed(5);dirty()}
for(const k of['lat','lon'])$(k).onchange=()=>{const v=parseFloat($(k).value.replace(',','.'));if(isFinite(v)){S.place[k]=v;dirty();if(map)map.panTo([S.place.lat,S.place.lon])}};
$('maxMin').oninput=()=>{S.place.maxMin=parseInt($('maxMin').value);$('maxMinText').textContent=S.place.maxMin+' min';dirty()};
$('speed').oninput=()=>{const v=parseInt($('speed').value);if(v>=5&&v<=130){S.place.speed=v;dirty()}};
$('roundTrip').onchange=()=>{S.place.roundTrip=$('roundTrip').checked;dirty()};
$('fuelUrl').oninput=()=>{S.fuelUrl=$('fuelUrl').value.trim();dirty()};
function initMap(){if(!window.L){$('mapNote').textContent='Karte neielādējās (vajag internetu telefonā). Ievadi koordinātas, piem., no Google Maps.';$('map').hidden=true;return}const p=S.place;map=L.map('map').setView([p.lat,p.lon],11);L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:18,attribution:'&copy; OpenStreetMap'}).addTo(map);circle=L.circle([p.lat,p.lon],{radius:reachKm(p)*1000,color:'#1f7a4d',weight:2,fillOpacity:.08}).addTo(map);home=L.marker([p.lat,p.lon],{draggable:true}).addTo(map);home.on('dragend',()=>{const l=home.getLatLng();setPoint(l.lat,l.lng)});map.on('click',e=>setPoint(e.latlng.lat,e.latlng.lng));layer=L.layerGroup().addTo(map);loadStations()}
async function loadStations(){if(!map)return;try{const list=await api('/api/stations');layer.clearLayers();const keys=['lpg',S.petrol,'dd'];for(const s of list){const lines=keys.filter(k=>s.p[k]!=null).map(k=>FUEL[k]+': '+fmt(s.p[k],3)+' €');const m=L.circleMarker([s.lat,s.lon],{radius:6,color:'#b5412f',weight:2,fillOpacity:.6});const box=document.createElement('div');const t=document.createElement('b');t.textContent=s.b;box.append(t,document.createElement('br'),s.a,document.createElement('br'),'~'+s.min+' min');for(const l of lines){box.append(document.createElement('br'),l)}m.bindPopup(box);layer.addLayer(m)}$('mapNote').textContent=list.length?'Sarkanie punkti: stacijas ar zināmām cenām ap saglabāto punktu.':'Ap saglabāto punktu nav staciju ar cenām.'}catch(e){$('mapNote').textContent=e.message}}
function renderData(){const d=$('dataInfo');d.textContent='';const add=(k,v)=>{const a=document.createElement('dt'),b=document.createElement('dd');a.textContent=k;b.textContent=v;d.append(a,b)};const e=saved.elec,f=saved.fuel;add('Elektrība',e.updated?'atjaunota '+when(e.updated):'nav datu'+(e.status?' (HTTP '+e.status+')':''));add('Degviela',f.fetched?f.stations+' stacijas, ielādētas '+when(f.fetched):'nav datu'+(f.status?' (HTTP '+f.status+')':''));if(f.feedUpdated)add('Saraksts veidots',new Date(f.feedUpdated).toLocaleString('lv-LV'));add('Ierīce',saved.device+', '+saved.wifi.ip+' ('+saved.wifi.connected+')')}
function render(){renderRows();renderPlace();$('fuelUrl').value=S.fuelUrl;$('ssid1').value=saved.wifi.ssid1;$('ssid2').value=saved.wifi.ssid2;dirty()}
async function refresh(full){const st=await api('/api/state');saved=st;$('sub').textContent=st.device+' · '+(st.state==='ready'?'darbojas':st.state==='error'?'kļūda: '+st.detail:'startē…');if(full||!S){S=copy(st);render();if(first){first=false;initMap()}}else{S.elec=st.elec;S.fuel=st.fuel}renderCards();renderData()}
$('undo').onclick=()=>{S=copy(saved);render();if(map)map.panTo([S.place.lat,S.place.lon])};
$('save').onclick=async()=>{$('save').disabled=true;try{await api('/api/settings',pick(S));toast('Saglabāts');await refresh(true);loadStations()}catch(e){toast(e.message)}$('save').disabled=false};
$('refresh').onclick=async()=>{try{await api('/api/refresh',{});toast('Ielādē… pēc brīža atjaunojas');setTimeout(()=>refresh().then(loadStations).catch(()=>{}),8000)}catch(e){toast(e.message)}};
for(const b of document.querySelectorAll('[data-slot]'))b.onclick=async()=>{const n=b.dataset.slot;try{await api('/api/wifi',{slot:+n,ssid:$('ssid'+n).value.trim(),password:$('pass'+n).value});toast('Saglabāts, ierīce pārstartējas')}catch(e){toast(e.message)}};
$('setup').onclick=async()=>{if(!confirm('Ierīce pārstartēsies un atvērs savu Wi-Fi tīklu iestatīšanai. Turpināt?'))return;try{await api('/api/setup/start',{});toast('Pārstartē…')}catch(e){toast(e.message)}};
$('reset').onclick=async()=>{if(prompt('Ieraksti RESET, lai izdzēstu visus iestatījumus.')!=='RESET')return;try{await api('/api/factory-reset',{confirm:'RESET'});toast('Atiestata…')}catch(e){toast(e.message)}};
let booted=false;function boot(){if(booted)return;booted=true;refresh(true).catch(e=>{$('sub').textContent='Neizdevās sazināties ar ierīci: '+e.message})}
const ls=document.createElement('script');ls.src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js';ls.crossOrigin='';ls.onload=boot;ls.onerror=boot;document.head.append(ls);setTimeout(boot,6000);
setInterval(()=>{if(saved&&!document.hidden)refresh().catch(()=>{})},30000);addEventListener('resize',()=>saved&&drawSpark());
</script></body></html>)HTML";

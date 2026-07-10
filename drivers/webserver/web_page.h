#ifndef _WEB_PAGE_H
#define _WEB_PAGE_H

static const char WEB_PAGE[] = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no,viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="default">
<meta name="apple-mobile-web-app-title" content="Korn">
<meta name="theme-color" content="#ffffff">
<title>Korn Dispenser</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{--bg:#fff;--ink:#111;--ink2:#666;--hair:#ddd;--red:#E30613}
body{font-family:"Helvetica Neue",Helvetica,-apple-system,Arial,sans-serif;
 background:var(--bg);color:var(--ink);max-width:520px;margin:0 auto;
 padding:max(16px,env(safe-area-inset-top)) 16px max(24px,env(safe-area-inset-bottom));
 -webkit-tap-highlight-color:transparent}
.num{font-variant-numeric:tabular-nums;font-feature-settings:"tnum"}
.lbl{font-size:11px;text-transform:uppercase;letter-spacing:.08em;color:var(--ink2)}
.masthead{padding-bottom:10px;border-bottom:2px solid var(--ink);margin-bottom:6px}
.masthead h1{font-size:22px;font-weight:700;letter-spacing:-.01em}
.statusline{font-size:12px;color:var(--ink2);margin-top:4px;letter-spacing:.03em}
.statusline .on{color:var(--ink)}
.statusline .ap{color:var(--red);font-weight:600}
section{padding:18px 0;border-bottom:1px solid var(--hair)}
section h2{font-size:11px;font-weight:600;text-transform:uppercase;
 letter-spacing:.08em;color:var(--ink2);margin-bottom:12px}
.scales{display:grid;grid-template-columns:1fr 1fr 1fr;gap:1px;background:var(--hair);
 border:1px solid var(--hair)}
.scell{background:var(--bg);padding:10px 8px;text-align:center;cursor:pointer;
 border-top:2px solid transparent;touch-action:manipulation;
 -webkit-user-select:none;user-select:none}
.scell.active{border-top-color:var(--red)}
.scell .sw{font-size:20px;font-weight:700;margin:4px 0 2px}
.scell .sc{font-size:10px;letter-spacing:.05em;color:var(--ink2)}
.scell .sc.no{color:var(--red)}
.bignum{font-size:60px;font-weight:700;letter-spacing:-.02em;text-align:center;
 padding:6px 0 2px;line-height:1}
.bignum small{font-size:22px;font-weight:400;color:var(--ink2)}
.bignum.over{color:var(--red)}
.pbar{height:2px;background:var(--hair);margin:12px 0}
.pfill{height:100%;background:var(--ink);width:0;transition:width .3s}
.pfill.over{background:var(--red)}
.substatus{font-size:12px;color:var(--ink2);text-align:center;margin-bottom:10px}
.field{margin:10px 0}
.field label{display:block;font-size:11px;text-transform:uppercase;
 letter-spacing:.08em;color:var(--ink2);margin-bottom:4px}
input[type=number]{width:100%;padding:8px 2px;border:0;border-bottom:1px solid var(--ink);
 background:transparent;color:var(--ink);font-size:20px;font-family:inherit;
 font-variant-numeric:tabular-nums;text-align:right;border-radius:0;
 -moz-appearance:textfield}
input:focus{outline:none;border-bottom:2px solid var(--ink)}
input::-webkit-outer-spin-button,input::-webkit-inner-spin-button{-webkit-appearance:none}
.row{display:flex;gap:8px;margin-top:14px}
.btn{flex:1;padding:13px 8px;font-size:12px;font-weight:600;font-family:inherit;
 text-transform:uppercase;letter-spacing:.08em;cursor:pointer;border-radius:0;
 background:var(--bg);color:var(--ink);border:1px solid var(--ink);
 -webkit-appearance:none;text-align:center;text-decoration:none}
.btn:active{background:var(--ink);color:var(--bg)}
.btn-pri{background:var(--ink);color:var(--bg)}
.btn-pri:active{background:var(--bg);color:var(--ink)}
.btn-stop{background:var(--red);color:#fff;border-color:var(--red)}
canvas{width:100%;height:180px;display:block;margin-top:4px}
.toggles{display:flex;gap:6px;align-items:center;margin-top:8px}
.toggles .lbl{margin-right:2px}
.tg{padding:5px 12px;font-size:11px;font-weight:600;font-family:inherit;
 letter-spacing:.08em;border:1px solid var(--hair);background:var(--bg);
 color:var(--ink2);cursor:pointer;border-radius:0;-webkit-appearance:none}
.tg.on{border-color:var(--ink);color:var(--ink)}
.runmeta{font-size:11px;color:var(--ink2);letter-spacing:.03em;margin-top:10px;
 font-variant-numeric:tabular-nums}
.sliderrow{display:flex;align-items:center;gap:12px;margin:14px 0}
.sliderrow .lbl{min-width:64px}
.sliderrow input[type=range]{flex:1;accent-color:var(--red)}
.sliderrow .val{min-width:44px;text-align:right;font-size:14px;
 font-variant-numeric:tabular-nums}
.pidgrid{display:flex;gap:12px;align-items:flex-end}
.pidgrid .field{flex:1;margin:0}
.saved{color:var(--ink2);font-size:11px;letter-spacing:.08em;text-transform:uppercase;
 margin-left:8px;opacity:0;transition:opacity .3s}
.saved.show{opacity:1}
table{width:100%;border-collapse:collapse;margin-top:4px;font-size:12px;
 font-variant-numeric:tabular-nums}
th{text-align:left;font-size:10px;text-transform:uppercase;letter-spacing:.08em;
 color:var(--ink2);font-weight:600;border-bottom:1px solid var(--ink);padding:6px 4px}
td{padding:6px 4px;border-bottom:1px solid var(--hair)}
td.bad{color:var(--red)}
.empty{font-size:12px;color:var(--ink2);margin-top:8px}
</style>
</head>
<body>

<header class="masthead">
<h1>KORN DISPENSER</h1>
<div class="statusline num" id="statusText">CONNECTING&hellip;</div>
</header>

<section>
<h2>Scales</h2>
<div class="scales" id="scaleDash"></div>
</section>

<section>
<h2>Dispense</h2>
<div class="bignum num" id="weight">&ndash;&ndash;<small> g</small></div>
<div class="pbar"><div class="pfill" id="progress"></div></div>
<div class="substatus num" id="dispStatus"></div>
<div class="field"><label for="targetInput">Target &middot; grams</label>
<input type="number" id="targetInput" value="100" min="1" max="9999" onchange="setTarget()"></div>
<div class="row">
<button class="btn" onclick="doTare()">Tare</button>
<button class="btn btn-pri" id="btnStart" onclick="startDisp()">Start</button>
<button class="btn btn-stop" id="btnStop" onclick="stopDisp()" style="display:none">Stop</button>
</div>
</section>

<section>
<h2>Run Chart</h2>
<canvas id="graph" height="180"></canvas>
<div class="toggles" id="pidToggles">
<span class="lbl">Show</span>
<button class="tg on" id="tgBAG" onclick="tgl('bag')">Bag</button>
<button class="tg" id="tgP" onclick="tgl('p')">P</button>
<button class="tg" id="tgI" onclick="tgl('i')">I</button>
<button class="tg" id="tgD" onclick="tgl('d')">D</button>
</div>
<div class="runmeta" id="runMeta"></div>
<div class="row" id="dlRow" style="display:none">
<a class="btn" href="/api/log.csv" download>Download CSV</a>
</div>
</section>

<section>
<h2>PID</h2>
<div class="pidgrid">
<div class="field"><label for="pidKp">Kp</label><input type="number" id="pidKp" step="0.1" min="0"></div>
<div class="field"><label for="pidKi">Ki</label><input type="number" id="pidKi" step="0.01" min="0"></div>
<div class="field"><label for="pidKd">Kd</label><input type="number" id="pidKd" step="0.1" min="0"></div>
</div>
<div class="row">
<button class="btn btn-pri" onclick="applyPID()">Apply</button>
</div>
<div style="text-align:center;margin-top:8px"><span class="saved" id="pidSaved">Saved</span></div>
</section>

<section>
<h2>Test</h2>
<div class="sliderrow"><span class="lbl">Servo</span>
<input type="range" min="0" max="180" value="0" id="servoSlider" oninput="sendServo(this.value)">
<span class="val num" id="servoVal">0&deg;</span></div>
<div class="sliderrow"><span class="lbl">Vibrator</span>
<input type="range" min="0" max="60" value="0" id="vibSlider" oninput="sendVib(this.value)">
<span class="val num" id="vibVal">0%</span></div>
<div class="row">
<button class="btn" onclick="testStop()">Stop All</button>
</div>
</section>

<section>
<h2>Calibrate</h2>
<div class="row" style="margin-top:0">
<button class="btn" onclick="doTare()">1 &middot; Tare</button>
</div>
<div class="field"><label for="calWeight">Known weight &middot; grams</label>
<input type="number" id="calWeight" value="1000" min="1" max="9999"></div>
<div class="row">
<button class="btn btn-pri" onclick="doCal()">2 &middot; Calibrate</button>
</div>
<div class="empty" id="calStatus" style="text-align:center"></div>
</section>

<section style="border-bottom:0">
<h2>History</h2>
<table id="histTable">
<thead><tr><th>Time</th><th>Scale</th><th>Target</th><th>Actual</th><th>Acc</th></tr></thead>
<tbody id="histBody"></tbody>
</table>
<div class="empty" id="histEmpty">No dispenses recorded yet.</div>
<div class="row">
<button class="btn" onclick="clearHistory()">Clear History</button>
</div>
</section>

<script>
const $=id=>document.getElementById(id);
const INK='#111',INK2='#666',HAIR='#ddd',RED='#E30613';

// --- Live graph state (750 ms poll samples) ---
const G={buf:[],gross:[],servo:[],vib:[],max:60};
let graphCanvas,graphCtx;
let lastTgt=100;

// --- Post-run data (full 10 Hz from /api/log.csv) ---
let R=null;                 // {t,sp,disp,w,gross,servo,p,i,d,vib,meta}
let lastRunLoaded=0;
let loadingRun=false;
const show={bag:true,p:false,i:false,d:false};

// --- Scale-select latch: keep the tapped scale highlighted until the server
// confirms it (or 4 s pass), so polls with the stale value can't flicker it back
let pendingScale=-1,pendingUntil=0;

// --- History ---
let history=JSON.parse(localStorage.getItem('kd_history')||'[]');
let prevDone=false,prevDisp=false;
let pidLoaded=false;
let busy=false;

function api(method,url,body){
 busy=true;
 let c=new AbortController();
 setTimeout(()=>c.abort(),3000);
 return fetch(url,{method,signal:c.signal,headers:body?{'Content-Type':'application/json'}:{},
  body:body?JSON.stringify(body):undefined}).then(r=>{busy=false;return r;}).catch(()=>{busy=false;});
}

function selScale(i){
 pendingScale=i;pendingUntil=Date.now()+4000;
 for(let j=0;j<3;j++){let el=$('sc'+j);if(el)el.classList.toggle('active',j===i);}
 $('statusText').textContent='SWITCHING TO SCALE '+(i+1)+'…';
 api('POST','/api/select-scale',{scale:i});
}
function setTarget(){
 let v=parseInt($('targetInput').value)||100;
 if(v<1)v=1;if(v>9999)v=9999;
 $('targetInput').value=v;
 api('POST','/api/target',{target:v});
}
function doTare(){api('POST','/api/tare');}
function startDisp(){setTarget();api('POST','/api/dispense',{action:'start'});}
function stopDisp(){api('POST','/api/dispense',{action:'stop'});}
function sendServo(v){
 $('servoVal').textContent=v+'°';
 api('POST','/api/test/servo',{angle:parseInt(v)});
}
function sendVib(v){
 $('vibVal').textContent=v+'%';
 api('POST','/api/test/vibrator',{intensity:parseInt(v)/100});
}
function testStop(){
 $('servoSlider').value=0;$('servoVal').textContent='0°';
 $('vibSlider').value=0;$('vibVal').textContent='0%';
 api('POST','/api/test/stop');
}
function doCal(){
 let w=parseInt($('calWeight').value)||1000;
 api('POST','/api/calibrate',{weight:w});
 $('calStatus').textContent='Calibrating…';
 setTimeout(()=>$('calStatus').textContent='Done.',2000);
}
function applyPID(){
 let kp=parseFloat($('pidKp').value)||0;
 let ki=parseFloat($('pidKi').value)||0;
 let kd=parseFloat($('pidKd').value)||0;
 api('POST','/api/pid',{kp:kp,ki:ki,kd:kd}).then(()=>{
  $('pidSaved').classList.add('show');
  setTimeout(()=>$('pidSaved').classList.remove('show'),2000);
 });
}
function tgl(k){
 show[k]=!show[k];
 $('tg'+k.toUpperCase()).classList.toggle('on',show[k]);
 if(R&&!prevDisp)drawRun();else drawLive(lastTgt);
}

// --- Scale dashboard ---
let dashBuilt=false;
function buildDash(d){
 if(!dashBuilt){
  let html='';
  for(let i=0;i<3;i++){
   html+='<div class="scell" id="sc'+i+'"><div class="lbl">Scale '+(i+1)+'</div>'+
    '<div class="sw num" id="scw'+i+'">&ndash;</div>'+
    '<div class="sc" id="scc'+i+'"></div></div>';
  }
  $('scaleDash').innerHTML=html;
  for(let j=0;j<3;j++){
   (function(idx){
    let el=$('sc'+idx);
    el.onclick=function(){selScale(idx);};
    el.ontouchend=function(e){e.preventDefault();selScale(idx);};
   })(j);
  }
  dashBuilt=true;
 }
 // Latch: trust the tapped scale until the server confirms it or 4 s pass
 if(pendingScale>=0&&(d.selected_scale===pendingScale||Date.now()>pendingUntil))
  pendingScale=-1;
 let act=pendingScale>=0?pendingScale:d.selected_scale;
 for(let i=0;i<3;i++){
  $('sc'+i).classList.toggle('active',i===act);
  let g=d.gross?d.gross[i]:d.weights[i];   // cards show the bag's absolute weight
  $('scw'+i).textContent=g.toFixed(0)+' g';
  let ce=$('scc'+i),cal=d.scale_calibrated[i];
  ce.textContent=cal?'CAL':'NOT CAL';
  ce.className='sc'+(cal?'':' no');
 }
}

// --- Canvas setup ---
function initGraph(){
 graphCanvas=$('graph');
 graphCtx=graphCanvas.getContext('2d');
 let dpr=window.devicePixelRatio||1;
 let rect=graphCanvas.getBoundingClientRect();
 graphCanvas.width=rect.width*dpr;
 graphCanvas.height=rect.height*dpr;
 graphCtx.scale(dpr,dpr);
}
function cdims(){
 let r=graphCanvas.getBoundingClientRect();
 return [r.width,r.height];
}
function axisLabel(ctx,txt,x,y,align){
 ctx.fillStyle=INK2;
 ctx.font='10px "Helvetica Neue",Helvetica,sans-serif';
 ctx.textAlign=align;
 ctx.fillText(txt,x,y);
}

// --- Live graph (while dispensing / idle) ---
function drawLive(target){
 if(!graphCtx)return;
 let[w,h]=cdims(),ctx=graphCtx;
 ctx.clearRect(0,0,w,h);
 if(G.buf.length<2)return;
 let minY=Infinity,maxY=-Infinity;
 for(let v of G.buf){if(v<minY)minY=v;if(v>maxY)maxY=v;}
 if(target>maxY)maxY=target;
 if(target<minY)minY=target;
 let pad=(maxY-minY)*0.15||10;
 minY-=pad;maxY+=pad;
 let yR=maxY-minY||1;
 let toX=i=>(i/(G.max-1))*w;
 let toY=v=>h-(v-minY)/yR*h;
 let toYR=v=>h-(v/180)*h;
 let s0=G.max-G.buf.length;
 // setpoint: red dashed hairline
 ctx.strokeStyle=RED;ctx.lineWidth=1;ctx.setLineDash([5,4]);
 ctx.beginPath();let ty=toY(target);ctx.moveTo(0,ty);ctx.lineTo(w,ty);ctx.stroke();
 ctx.setLineDash([]);
 // bag (absolute) weight: own normalized band so a heavy bag doesn't squash
 // the dispensed curve; min 50 g span so idle noise stays flat
 if(show.bag&&G.gross.length>=2){
  let bMin=Infinity,bMax=-Infinity;
  for(let v of G.gross){if(v<bMin)bMin=v;if(v>bMax)bMax=v;}
  let span=Math.max(bMax-bMin,50),mid=(bMax+bMin)/2;
  bMin=mid-span/2;bMax=mid+span/2;
  let toYB=v=>h*0.92-(v-bMin)/(bMax-bMin)*h*0.84;
  ctx.strokeStyle='#bbb';ctx.lineWidth=1;ctx.beginPath();
  for(let i=0;i<G.gross.length;i++){
   let x=toX(s0+i),y=toYB(G.gross[i]);
   i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);
  }
  ctx.stroke();
  let bl=G.gross[G.gross.length-1];
  ctx.fillStyle='#bbb';
  ctx.font='10px "Helvetica Neue",Helvetica,sans-serif';
  ctx.textAlign='right';
  ctx.fillText('BAG '+bl.toFixed(0)+' G',w-4,toYB(bl)-4);
 }
 // servo: 1px gray
 if(G.servo.length>=2){
  ctx.strokeStyle=INK2;ctx.lineWidth=1;ctx.beginPath();
  for(let i=0;i<G.servo.length;i++){
   let x=toX(s0+i),y=toYR(G.servo[i]);
   i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);
  }
  ctx.stroke();
 }
 // vib: 1px light gray (0-100 mapped onto right axis)
 if(G.vib.length>=2){
  ctx.strokeStyle=HAIR;ctx.lineWidth=1;ctx.beginPath();
  for(let i=0;i<G.vib.length;i++){
   let x=toX(s0+i),y=toYR(G.vib[i]*1.8);
   i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);
  }
  ctx.stroke();
 }
 // weight: 1.5px ink (red when over target)
 let last=G.buf[G.buf.length-1];
 ctx.strokeStyle=last>target+5?RED:INK;
 ctx.lineWidth=1.5;ctx.beginPath();
 for(let i=0;i<G.buf.length;i++){
  let x=toX(s0+i),y=toY(G.buf[i]);
  i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);
 }
 ctx.stroke();
 axisLabel(ctx,maxY.toFixed(0)+' G',4,12,'left');
 axisLabel(ctx,minY.toFixed(0)+' G',4,h-4,'left');
 axisLabel(ctx,'LIVE',w-4,12,'right');
}

// --- Post-run chart (full 20 Hz data) ---
function drawRun(){
 if(!graphCtx||!R)return;
 let[w,h]=cdims(),ctx=graphCtx;
 ctx.clearRect(0,0,w,h);
 let n=R.t.length;
 if(n<2)return;
 let tMax=R.t[n-1]||1;
 // Left axis: grams (dispensed scale; the bag trace has its own band below)
 let minY=0,maxY=R.meta.target||1;
 for(let v of R.disp){if(v>maxY)maxY=v;}
 maxY*=1.1;
 // Right axis: servo + enabled terms
 let rMin=0,rMax=180;
 let seriesR=[R.servo];
 if(show.p)seriesR.push(R.p);
 if(show.i)seriesR.push(R.i);
 if(show.d)seriesR.push(R.d);
 for(let arr of seriesR)for(let v of arr){if(v<rMin)rMin=v;if(v>rMax)rMax=v;}
 let rR=rMax-rMin||1;
 let toX=t=>(t/tMax)*w;
 let yR0=maxY-minY||1;
 let toY=v=>h-(v-minY)/yR0*h;
 let toYR=v=>h-(v-rMin)/rR*h;
 function line(xs,ys,style,width,dash,mapper){
  ctx.strokeStyle=style;ctx.lineWidth=width;ctx.setLineDash(dash||[]);
  ctx.beginPath();
  for(let i=0;i<n;i++){
   let x=toX(xs[i]),y=mapper(ys[i]);
   i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);
  }
  ctx.stroke();ctx.setLineDash([]);
 }
 // setpoint: red dashed
 ctx.strokeStyle=RED;ctx.lineWidth=1;ctx.setLineDash([5,4]);
 ctx.beginPath();let sy=toY(R.meta.target);ctx.moveTo(0,sy);ctx.lineTo(w,sy);ctx.stroke();
 ctx.setLineDash([]);
 // bag (absolute) weight: own normalized band (heavy bag must not squash the
 // dispensed curve); min 50 g span
 if(show.bag&&R.gross&&R.gross.length===n){
  let bMin=Infinity,bMax=-Infinity;
  for(let v of R.gross){if(v<bMin)bMin=v;if(v>bMax)bMax=v;}
  let span=Math.max(bMax-bMin,50),mid=(bMax+bMin)/2;
  bMin=mid-span/2;bMax=mid+span/2;
  let toYB=v=>h*0.92-(v-bMin)/(bMax-bMin)*h*0.84;
  line(R.t,R.gross,'#bbb',1,null,toYB);
  ctx.fillStyle='#bbb';
  ctx.font='10px "Helvetica Neue",Helvetica,sans-serif';
  ctx.textAlign='left';
  ctx.fillText('BAG '+R.gross[0].toFixed(0)+'→'+R.gross[n-1].toFixed(0)+' G',4,toYB(R.gross[0])-4);
 }
 // vib: light-gray steps (0-1 -> right axis 0-100 zone)
 line(R.t,R.vib.map(v=>rMin+v*100),HAIR,1,null,toYR);
 // PID terms (gray, distinguishable dashes)
 if(show.p)line(R.t,R.p,INK2,1,[2,2],toYR);
 if(show.i)line(R.t,R.i,INK2,1,[6,3],toYR);
 if(show.d)line(R.t,R.d,'#aaa',1,[1,3],toYR);
 // servo: 1px gray solid
 line(R.t,R.servo,INK2,1,null,toYR);
 // dispensed: 1.5px ink (the star of the show)
 line(R.t,R.disp,INK,1.5,null,toY);
 axisLabel(ctx,maxY.toFixed(0)+' G',4,12,'left');
 axisLabel(ctx,'0',4,h-4,'left');
 axisLabel(ctx,(tMax/1000).toFixed(1)+' S',w-4,h-4,'right');
 axisLabel(ctx,'RUN '+R.meta.run_id,w-4,12,'right');
}

// --- CSV run loading ---
function loadRun(id){
 if(loadingRun)return;
 loadingRun=true;
 fetch('/api/log.csv').then(r=>{
  if(!r.ok)throw 0;
  return r.text();
 }).then(txt=>{
  let lines=txt.split('\n');
  let meta={};
  let data={t:[],sp:[],disp:[],w:[],gross:[],servo:[],p:[],i:[],d:[],vib:[]};
  for(let ln of lines){
   if(!ln)continue;
   if(ln[0]==='#'){
    for(let kv of ln.slice(1).split(',')){
     let m=kv.trim().split('=');
     if(m.length===2)meta[m[0]]=parseFloat(m[1])||m[1];
    }
    continue;
   }
   if(ln[0]==='t')continue; // header row
   let c=ln.split(',');
   if(c.length<10)continue;
   data.t.push(+c[0]);data.sp.push(+c[1]);data.disp.push(+c[2]);
   data.w.push(+c[3]);data.gross.push(+c[4]);data.servo.push(+c[5]);
   data.p.push(+c[6]);data.i.push(+c[7]);data.d.push(+c[8]);data.vib.push(+c[9]);
  }
  // Validate against metadata (stream may truncate if a new run started)
  if(meta.samples&&data.t.length<meta.samples)throw 0;
  R=data;
  R.meta={run_id:meta.run_id||id,scale:meta.scale||1,target:meta.target_g||0,
   kp:meta.kp,ki:meta.ki,kd:meta.kd,final:meta.final_g,samples:data.t.length};
  lastRunLoaded=id;
  loadingRun=false;
  $('dlRow').style.display='flex';
  $('runMeta').textContent='RUN '+R.meta.run_id+' · SCALE '+R.meta.scale+
   ' · '+R.meta.samples+' SAMPLES · KP '+R.meta.kp+' KI '+R.meta.ki+
   ' KD '+R.meta.kd+' · FINAL '+R.meta.final+' G';
  drawRun();
 }).catch(()=>{loadingRun=false;});
}

// --- History ---
function renderHistory(){
 let body=$('histBody');
 if(history.length===0){
  body.innerHTML='';
  $('histEmpty').style.display='block';
  return;
 }
 $('histEmpty').style.display='none';
 let html='';
 for(let i=history.length-1;i>=0;i--){
  let e=history[i];
  let acc=(e.actual/e.target*100).toFixed(1);
  let bad=Math.abs(e.actual-e.target)>e.target*0.05;
  html+='<tr><td>'+e.time+'</td><td>'+e.scale+'</td><td>'+e.target+' g</td><td>'+
   e.actual.toFixed(1)+' g</td><td'+(bad?' class="bad"':'')+'>'+acc+'%</td></tr>';
 }
 body.innerHTML=html;
}
function clearHistory(){
 history=[];
 localStorage.setItem('kd_history','[]');
 renderHistory();
}

// --- Poll ---
function poll(){
 let c=new AbortController();
 setTimeout(()=>c.abort(),3000);
 return fetch('/api/status',{signal:c.signal}).then(r=>r.json()).then(d=>{
  let w=d.weights[d.selected_scale];
  let tgt=d.target_grams;
  let disp=d.dispensed_grams;
  let displayW=d.dispensing?disp:w;
  let over=(d.dispensing?disp:w)>tgt+5;

  $('weight').innerHTML=displayW.toFixed(0)+'<small> g</small>';
  $('weight').className='bignum num'+(over?' over':'');

  let pct=tgt>0?Math.min(100,Math.max(0,(d.dispensing?disp:0)/tgt*100)):0;
  $('progress').style.width=pct+'%';
  $('progress').className='pfill'+(over&&d.dispensing?' over':'');

  $('btnStart').style.display=d.dispensing?'none':'block';
  $('btnStop').style.display=d.dispensing?'block':'none';

  if(document.activeElement!==$('targetInput'))
   $('targetInput').value=tgt;

  // Masthead status
  let cal=d.scale_calibrated[d.selected_scale];
  let st=d.dispensing?'DISPENSING':(d.dispense_done?'COMPLETE':'READY');
  let net;
  if(d.mode==='ap'){
   net='<span class="ap">AP MODE · 192.168.4.1</span>';
  }else{
   let rssi=d.rssi||0;
   let bars=rssi>-50?'▂▄▆█':rssi>-65?'▂▄▆':rssi>-75?'▂▄':'▂';
   net=bars+' '+rssi+' dBm';
  }
  $('statusText').innerHTML='<span class="on">SCALE '+(d.selected_scale+1)+'</span> · '+st+
   (cal?'':' · <span class="ap">NOT CALIBRATED</span>')+' · '+net;
  let bag=d.gross?d.gross[d.selected_scale]:0;
  $('dispStatus').textContent=(d.dispensing?
   disp.toFixed(1)+' of '+tgt+' g':'Target '+tgt+' g')+' · Bag '+bag.toFixed(0)+' g';

  buildDash(d);

  // Live graph while dispensing (or before any run is loaded)
  lastTgt=tgt;
  G.buf.push(displayW);
  if(G.buf.length>G.max)G.buf.shift();
  G.gross.push(bag);
  if(G.gross.length>G.max)G.gross.shift();
  G.servo.push(d.servo||0);
  if(G.servo.length>G.max)G.servo.shift();
  G.vib.push((d.vib||0)*100);
  if(G.vib.length>G.max)G.vib.shift();
  if(d.dispensing||!R)drawLive(tgt);

  // Post-run: when a run just finished, fetch the full 20 Hz log
  if(d.run&&d.run.id>0&&!d.run.active&&!d.dispensing&&d.run.id!==lastRunLoaded){
   loadRun(d.run.id);
  }

  // PID field sync
  if(d.pid){
   let ae=document.activeElement;
   let pidInputs=[$('pidKp'),$('pidKi'),$('pidKd')];
   if(!pidLoaded||pidInputs.indexOf(ae)===-1){
    $('pidKp').value=d.pid.kp;
    $('pidKi').value=d.pid.ki;
    $('pidKd').value=d.pid.kd;
    pidLoaded=true;
   }
  }

  // History entry on dispense completion
  if(d.dispense_done&&!prevDone&&prevDisp){
   history.push({time:new Date().toLocaleTimeString(),scale:d.selected_scale+1,
    target:tgt,actual:disp});
   localStorage.setItem('kd_history',JSON.stringify(history));
   renderHistory();
  }
  prevDone=d.dispense_done;
  prevDisp=d.dispensing||d.dispense_done;

 }).catch(()=>{
  $('statusText').textContent='CONNECTION LOST…';
 });
}

initGraph();
window.addEventListener('resize',()=>{
 initGraph();
 if(R&&!prevDisp)drawRun();
});
renderHistory();
function schedulePoll(){
 setTimeout(()=>{
  if(busy){schedulePoll();return;}
  poll().finally(schedulePoll);
 },750);
}
poll().finally(schedulePoll);
</script>
</body>
</html>)rawhtml";

#endif // _WEB_PAGE_H

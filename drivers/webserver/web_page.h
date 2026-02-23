#ifndef _WEB_PAGE_H
#define _WEB_PAGE_H

static const char WEB_PAGE[] = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Korn Dispenser</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,system-ui,sans-serif;background:#1a1a2e;color:#e0e0e0;
 max-width:480px;margin:0 auto;padding:12px;-webkit-tap-highlight-color:transparent}
h1{text-align:center;color:#e94560;font-size:1.4em;margin-bottom:12px}
.card{background:#16213e;border-radius:12px;padding:14px;margin-bottom:12px}
.card h2{font-size:1em;color:#0f3460;margin-bottom:8px;text-transform:uppercase;
 letter-spacing:1px;color:#e94560}
.weight-display{font-size:3em;font-weight:700;text-align:center;padding:8px 0;
 font-variant-numeric:tabular-nums;transition:color .3s}
.w-under{color:#4da6ff}.w-target{color:#4dff91}.w-over{color:#ff4d6a}
.progress-bar{height:8px;background:#0a0a1a;border-radius:4px;overflow:hidden;margin:8px 0}
.progress-fill{height:100%;background:linear-gradient(90deg,#4da6ff,#4dff91);
 border-radius:4px;transition:width .3s;width:0}
.scale-btns{display:flex;gap:8px;justify-content:center;margin-bottom:8px}
.scale-btn{flex:1;padding:10px;border:2px solid #0f3460;background:transparent;
 color:#e0e0e0;border-radius:8px;font-size:1em;cursor:pointer;transition:all .2s}
.scale-btn.active{background:#0f3460;color:#fff;border-color:#e94560}
.row{display:flex;gap:8px;margin-top:8px}
.btn{flex:1;padding:12px;border:none;border-radius:8px;font-size:1em;font-weight:600;
 cursor:pointer;transition:all .15s;color:#fff}
.btn:active{transform:scale(.95)}
.btn-start{background:#27ae60}.btn-stop{background:#e94560}
.btn-tare{background:#2980b9}.btn-cal{background:#8e44ad}
.btn-sec{background:#34495e}
input[type=number]{width:100%;padding:10px;border:2px solid #0f3460;border-radius:8px;
 background:#0a0a1a;color:#e0e0e0;font-size:1.1em;text-align:center;
 -moz-appearance:textfield}
input::-webkit-outer-spin-button,input::-webkit-inner-spin-button{-webkit-appearance:none}
.slider-row{display:flex;align-items:center;gap:10px;margin-top:8px}
.slider-row label{min-width:60px;font-size:.9em}
.slider-row input[type=range]{flex:1;accent-color:#e94560}
.slider-row span{min-width:40px;text-align:right;font-variant-numeric:tabular-nums}
.status-dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px}
.dot-ok{background:#4dff91}.dot-off{background:#666}
.info{font-size:.85em;color:#888;text-align:center;margin-top:4px}
</style>
</head>
<body>
<h1>Korn Dispenser</h1>

<div class="card">
<h2>Scale</h2>
<div class="scale-btns" id="scaleBtns">
<button class="scale-btn active" onclick="selScale(0)">1</button>
<button class="scale-btn" onclick="selScale(1)">2</button>
<button class="scale-btn" onclick="selScale(2)">3</button>
</div>
<div class="weight-display w-under" id="weight">-- g</div>
<div class="progress-bar"><div class="progress-fill" id="progress"></div></div>
<div class="info" id="statusText">Connecting...</div>
</div>

<div class="card">
<h2>Dispense</h2>
<label style="font-size:.9em">Target (g)</label>
<input type="number" id="targetInput" value="100" min="1" max="9999"
 onchange="setTarget()">
<div class="row">
<button class="btn btn-tare" onclick="doTare()">Tare</button>
<button class="btn btn-start" id="btnStart" onclick="startDisp()">Start</button>
<button class="btn btn-stop" id="btnStop" onclick="stopDisp()" style="display:none">Stop</button>
</div>
</div>

<div class="card">
<h2>Test</h2>
<div class="slider-row">
<label>Servo</label>
<input type="range" min="0" max="180" value="60" id="servoSlider"
 oninput="sendServo(this.value)">
<span id="servoVal">60&deg;</span>
</div>
<div class="slider-row">
<label>Vibrator</label>
<input type="range" min="0" max="100" value="0" id="vibSlider"
 oninput="sendVib(this.value)">
<span id="vibVal">0%</span>
</div>
<div class="row">
<button class="btn btn-sec" onclick="testStop()">Stop All</button>
</div>
</div>

<div class="card">
<h2>Calibrate</h2>
<div class="row">
<button class="btn btn-tare" onclick="doTare()">1. Tare</button>
</div>
<label style="font-size:.9em;display:block;margin-top:8px">Known weight (g)</label>
<input type="number" id="calWeight" value="1000" min="1" max="9999">
<div class="row">
<button class="btn btn-cal" onclick="doCal()">2. Calibrate</button>
</div>
<div class="info" id="calStatus"></div>
</div>

<script>
let S={scale:0,dispensing:false};
const $=id=>document.getElementById(id);

function api(method,url,body){
 return fetch(url,{method,headers:body?{'Content-Type':'application/json'}:{},
  body:body?JSON.stringify(body):undefined}).catch(()=>{});
}

function selScale(i){
 api('POST','/api/select-scale',{scale:i});
 document.querySelectorAll('.scale-btn').forEach((b,j)=>b.classList.toggle('active',j===i));
}

function setTarget(){
 let v=parseInt($('targetInput').value)||100;
 if(v<1)v=1;if(v>9999)v=9999;
 $('targetInput').value=v;
 api('POST','/api/target',{target:v});
}

function doTare(){api('POST','/api/tare');}

function startDisp(){
 setTarget();
 api('POST','/api/dispense',{action:'start'});
}
function stopDisp(){api('POST','/api/dispense',{action:'stop'});}

function sendServo(v){
 $('servoVal').textContent=v+'\u00B0';
 api('POST','/api/test/servo',{angle:parseInt(v)});
}
function sendVib(v){
 $('vibVal').textContent=v+'%';
 api('POST','/api/test/vibrator',{intensity:parseInt(v)/100});
}
function testStop(){
 $('servoSlider').value=60;$('servoVal').textContent='60\u00B0';
 $('vibSlider').value=0;$('vibVal').textContent='0%';
 api('POST','/api/test/stop');
}

function doCal(){
 let w=parseInt($('calWeight').value)||1000;
 api('POST','/api/calibrate',{weight:w});
 $('calStatus').textContent='Calibrating...';
 setTimeout(()=>$('calStatus').textContent='Done!',2000);
}

function poll(){
 fetch('/api/status').then(r=>r.json()).then(d=>{
  S=d;
  let w=d.weights[d.selected_scale];
  let tgt=d.target_grams;
  let disp=d.dispensed_grams;
  let displayW=d.dispensing?disp:w;
  $('weight').textContent=displayW.toFixed(0)+' g';

  // Color
  let cls='w-under';
  if(d.dispensing){
   if(disp>=tgt-5&&disp<=tgt+5)cls='w-target';
   else if(disp>tgt+5)cls='w-over';
  }else{
   if(w>=tgt-5&&w<=tgt+5)cls='w-target';
   else if(w>tgt+5)cls='w-over';
  }
  $('weight').className='weight-display '+cls;

  // Progress
  let pct=tgt>0?Math.min(100,Math.max(0,(d.dispensing?disp:0)/tgt*100)):0;
  $('progress').style.width=pct+'%';

  // Buttons
  $('btnStart').style.display=d.dispensing?'none':'block';
  $('btnStop').style.display=d.dispensing?'block':'none';

  // Scale buttons
  document.querySelectorAll('.scale-btn').forEach((b,j)=>
   b.classList.toggle('active',j===d.selected_scale));

  // Target sync (only when not focused)
  if(document.activeElement!==$('targetInput'))
   $('targetInput').value=tgt;

  // Status
  let cal=d.scale_calibrated[d.selected_scale];
  let st=d.dispensing?'Dispensing...':(d.dispense_done?'Complete!':'Ready');
  $('statusText').innerHTML='<span class="status-dot '+(cal?'dot-ok':'dot-off')+'"></span>'+
   'Scale '+(d.selected_scale+1)+' - '+st+(cal?'':' (not calibrated)');
 }).catch(()=>{
  $('statusText').textContent='Connection lost...';
 });
}

setInterval(poll,500);
poll();
</script>
</body>
</html>)rawhtml";

#endif // _WEB_PAGE_H

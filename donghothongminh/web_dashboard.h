#pragma once

// Giao diện được nhúng trực tiếp trong flash, không phụ thuộc CDN hay Internet.
static const char WEB_DASHBOARD_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="vi">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <meta name="theme-color" content="#101820">
  <title>Health Monitor ESP32-S3</title>
  <style>
    :root{color-scheme:dark;--bg:#071018;--panel:#101c26;--panel2:#152531;--line:#29404e;--text:#f4f7f8;--muted:#9fb1bc;--ok:#55d6a1;--warn:#ffc857;--danger:#ff6b6b;--blue:#63b3ff}
    *{box-sizing:border-box} body{margin:0;background:radial-gradient(circle at 90% 0,#173246 0,transparent 34%),var(--bg);color:var(--text);font:15px/1.5 system-ui,-apple-system,"Segoe UI",sans-serif}
    .shell{width:min(1180px,calc(100% - 28px));margin:auto;padding:26px 0 40px}.top{display:flex;align-items:flex-start;justify-content:space-between;gap:18px;margin-bottom:22px}
    h1{font-size:clamp(26px,4vw,42px);line-height:1.08;margin:4px 0 8px}.eyebrow{letter-spacing:.14em;text-transform:uppercase;color:var(--blue);font-weight:750;font-size:12px}.sub,.muted{color:var(--muted)}
    .live{display:flex;align-items:center;gap:9px;border:1px solid var(--line);border-radius:999px;padding:9px 13px;background:#0b1720;white-space:nowrap}.dot{width:9px;height:9px;border-radius:50%;background:var(--ok);box-shadow:0 0 14px var(--ok)}
    .grid{display:grid;grid-template-columns:repeat(12,1fr);gap:14px}.card{background:linear-gradient(145deg,var(--panel2),var(--panel));border:1px solid var(--line);border-radius:18px;padding:18px;box-shadow:0 18px 45px #0004}
    .metric{grid-column:span 3;min-height:150px}.metric .value{font-size:clamp(36px,5vw,58px);font-weight:800;line-height:1;margin:16px 0 8px}.unit{font-size:16px;color:var(--muted);margin-left:5px}.label{font-size:13px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);font-weight:700}
    .wide{grid-column:span 8}.side{grid-column:span 4}.half{grid-column:span 6}.section-title{font-size:20px;margin:0 0 14px}.rows{display:grid;gap:2px}.row{display:flex;justify-content:space-between;gap:18px;padding:11px 0;border-bottom:1px solid var(--line)}.row:last-child{border-bottom:0}.row b{text-align:right}
    .sensors{display:grid;grid-template-columns:repeat(2,1fr);gap:10px}.sensor{border:1px solid var(--line);border-radius:13px;padding:12px;background:#0b1720}.sensor strong{display:block;margin-bottom:4px}.status{font-weight:750}.ok{color:var(--ok)}.warn{color:var(--warn)}.danger{color:var(--danger)}
    .progress{height:9px;background:#09131a;border-radius:99px;overflow:hidden;margin-top:12px}.progress>i{display:block;height:100%;width:0;background:linear-gradient(90deg,var(--blue),var(--ok));transition:width .35s}
    .actions{display:flex;flex-wrap:wrap;gap:10px;margin-top:14px}button,a.button{border:1px solid var(--line);background:#1a2c38;color:var(--text);border-radius:11px;padding:11px 14px;font:inherit;font-weight:700;text-decoration:none;cursor:pointer}button:hover,a.button:hover{border-color:var(--blue)}button.danger-btn{border-color:#6d3434;color:#ffd7d7}
    .foot{margin-top:16px;padding:15px 4px;color:var(--muted);font-size:13px}.offline .dot{background:var(--danger);box-shadow:none}.offline #liveText{color:var(--danger)}
    @media(max-width:820px){.metric{grid-column:span 6}.wide,.side,.half{grid-column:1/-1}.top{align-items:flex-start}.sensors{grid-template-columns:1fr}}
    @media(max-width:480px){.metric{grid-column:1/-1}.shell{width:min(100% - 20px,1180px);padding-top:18px}.live{padding:7px 10px}.card{border-radius:15px}}
  </style>
</head>
<body>
  <main class="shell">
    <header class="top"><div><div class="eyebrow">ESP32-S3 · wearable prototype</div><h1>Health Monitor</h1><div class="sub">Bảng điều khiển sức khỏe, chuyển động và định vị theo thời gian thực</div></div><div class="live" id="live"><span class="dot"></span><span id="liveText">Đang kết nối</span></div></header>
    <section class="grid">
      <article class="card metric"><div class="label">Nhịp tim</div><div class="value"><span id="bpm">--</span><span class="unit">BPM</span></div><div class="muted" id="finger">Chưa đặt ngón tay</div></article>
      <article class="card metric"><div class="label">Oxy máu ước lượng</div><div class="value"><span id="spo2">--</span><span class="unit">%</span></div><div class="muted">Nguyên mẫu, chưa hiệu chuẩn y tế</div></article>
      <article class="card metric"><div class="label">Số bước</div><div class="value" id="steps">0</div><div class="muted">Bộ đếm trong phiên hiện tại</div></article>
      <article class="card metric"><div class="label">Trạng thái té ngã</div><div class="value status" id="fall">OK</div><div class="muted" id="acceleration">Gia tốc 1.00 g</div></article>

      <article class="card wide"><h2 class="section-title">Tín hiệu PPG</h2><div class="rows"><div class="row"><span>Chất lượng tín hiệu</span><b id="qualityText">0%</b></div><div class="progress"><i id="qualityBar"></i></div><div class="row"><span>Mẫu hồng ngoại thô</span><b id="irRaw">0</b></div><div class="row"><span>MAX30102</span><b class="status" id="maxStatus">Đang kiểm tra</b></div></div></article>
      <article class="card side"><h2 class="section-title">Thiết bị</h2><div class="sensors"><div class="sensor"><strong>OLED</strong><span id="oledStatus">--</span></div><div class="sensor"><strong>IMU</strong><span id="imuStatus">--</span></div><div class="sensor"><strong>Buzzer</strong><span id="buzzerStatus">--</span></div><div class="sensor"><strong>Wi-Fi AP</strong><span class="ok">ONLINE</span></div></div></article>

      <article class="card half"><h2 class="section-title">GPS</h2><div class="rows"><div class="row"><span>Trạng thái</span><b id="gpsState">WAIT</b></div><div class="row"><span>Vệ tinh</span><b id="satellites">0</b></div><div class="row"><span>Vĩ độ</span><b id="latitude">--</b></div><div class="row"><span>Kinh độ</span><b id="longitude">--</b></div></div><div class="actions"><a class="button" id="mapLink" href="#" target="_blank" rel="noreferrer">Mở bản đồ</a></div></article>
      <article class="card half"><h2 class="section-title">Hệ thống và điều khiển</h2><div class="rows"><div class="row"><span>Mạng</span><b>health-monitor</b></div><div class="row"><span>Địa chỉ truy cập</span><b>192.168.4.1</b></div><div class="row"><span>Thiết bị đang kết nối</span><b id="clients">0</b></div><div class="row"><span>Thời gian hoạt động</span><b id="uptime">0 s</b></div></div><div class="actions"><button id="resetSteps">Đặt lại số bước</button><button class="danger-btn" id="resetAlert">Hủy cảnh báo té ngã</button></div></article>
    </section>
    <footer class="foot">Thiết bị nghiên cứu học thuật - không dùng để chẩn đoán, điều trị hoặc thay thế hệ thống cảnh báo khẩn cấp.</footer>
  </main>
  <script>
    const $=id=>document.getElementById(id); const num=(v,d=1)=>Number(v).toFixed(d);
    function state(el,ok){el.textContent=ok?'ONLINE':'OFFLINE';el.className=ok?'ok':'danger'}
    function fallClass(v){return v==='FALL'?'danger':(v==='FREE'||v==='CHECK'?'warn':'ok')}
    async function refresh(){try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw Error();const d=await r.json();
      $('bpm').textContent=d.heartRateValid?Math.round(d.bpm):'--';$('spo2').textContent=d.spo2Valid?Math.round(d.spo2):'--';$('steps').textContent=d.steps;
      $('finger').textContent=d.fingerPresent?'Đang nhận ngón tay':'Chưa đặt ngón tay';$('fall').textContent=d.fall;$('fall').className='value status '+fallClass(d.fall);$('acceleration').textContent='Gia tốc '+num(d.acceleration,2)+' g';
      $('qualityText').textContent=Math.round(d.signalQuality)+'%';$('qualityBar').style.width=Math.max(0,Math.min(100,d.signalQuality))+'%';$('irRaw').textContent=Number(d.irRaw).toLocaleString('vi-VN');
      state($('maxStatus'),d.maxOK);state($('oledStatus'),d.oledOK);state($('imuStatus'),d.imuOK);state($('buzzerStatus'),d.buzzerOK);
      $('gpsState').textContent=d.gpsState;$('satellites').textContent=d.satellites;$('latitude').textContent=d.positionKnown?num(d.latitude,6):'--';$('longitude').textContent=d.positionKnown?num(d.longitude,6):'--';
      $('mapLink').style.opacity=d.positionKnown?'1':'.45';$('mapLink').href=d.positionKnown?'https://www.google.com/maps?q='+d.latitude+','+d.longitude:'#';$('clients').textContent=d.clients;$('uptime').textContent=Math.floor(d.uptime/60)+' phút '+(d.uptime%60)+' giây';
      $('live').classList.remove('offline');$('liveText').textContent='Cập nhật trực tiếp';}catch(e){$('live').classList.add('offline');$('liveText').textContent='Mất kết nối'}}
    async function post(url){await fetch(url,{method:'POST'});refresh()}
    $('resetSteps').onclick=()=>post('/api/reset-steps');$('resetAlert').onclick=()=>post('/api/reset-alert');refresh();setInterval(refresh,1000);
  </script>
</body>
</html>
)HTML";

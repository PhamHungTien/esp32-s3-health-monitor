#pragma once

// Trang điều khiển nằm hoàn toàn trong flash, không tải font hoặc mã từ Internet.
static const char WEB_DASHBOARD_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="vi">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <meta name="theme-color" content="#171917">
  <title>Health Monitor</title>
  <style>
    :root{--paper:#f3f3ef;--card:#fff;--ink:#171917;--muted:#696d68;--line:#d9dbd6;--soft:#eceee9;--green:#16734a;--amber:#9a6500;--red:#b62e2e}
    *{box-sizing:border-box}body{margin:0;background:var(--paper);color:var(--ink);font:15px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}button{font:inherit}
    .wrap{width:min(1080px,calc(100% - 32px));margin:auto;padding:24px 0 34px}
    header{display:flex;align-items:center;justify-content:space-between;gap:20px;padding:4px 0 22px;border-bottom:1px solid var(--ink)}
    .brand{display:flex;align-items:center;gap:13px}.mark{display:grid;place-items:center;width:42px;height:42px;border-radius:50%;background:var(--ink);color:#fff;font-weight:800}.brand b{display:block;font-size:20px}.brand small,.note{color:var(--muted)}
    .live{display:flex;align-items:center;gap:8px;font-size:13px}.dot{width:8px;height:8px;border-radius:50%;background:var(--green)}.offline .dot{background:var(--red)}.offline #liveText{color:var(--red)}
    .vitals{display:grid;grid-template-columns:repeat(4,1fr);border-bottom:1px solid var(--ink)}
    .vital{min-width:0;padding:25px 20px 23px;border-right:1px solid var(--line)}.vital:first-child{padding-left:0}.vital:last-child{border-right:0}
    .label{color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}.number{margin:10px 0 6px;font-size:clamp(34px,5vw,54px);font-weight:750;line-height:1;letter-spacing:-.04em}.unit{margin-left:5px;color:var(--muted);font-size:14px;letter-spacing:0}.status{font-weight:700}.ok{color:var(--green)}.warn{color:var(--amber)}.danger{color:var(--red)}
    .layout{display:grid;grid-template-columns:1.45fr 1fr;gap:18px;padding-top:18px}.panel{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:19px}.panel h2{margin:0 0 13px;font-size:17px}.panel.full{grid-column:1/-1}
    .rows{display:grid}.row{display:flex;justify-content:space-between;gap:18px;padding:10px 0;border-top:1px solid var(--soft)}.row span:first-child{color:var(--muted)}.row b{text-align:right;font-weight:650}
    .bar{height:5px;margin:3px 0 12px;background:var(--soft);overflow:hidden}.bar i{display:block;width:0;height:100%;background:var(--green);transition:width .2s}
    .devices{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}.device{padding:11px;border:1px solid var(--line);border-radius:8px}.device b{display:block;margin-bottom:2px}.device span{font-size:13px}
    .actions{display:flex;flex-wrap:wrap;gap:9px;margin-top:15px}button,.map{border:1px solid var(--ink);border-radius:7px;background:var(--ink);color:#fff;padding:9px 12px;text-decoration:none;cursor:pointer}button.secondary{background:#fff;color:var(--ink)}button:active,.map:active{transform:translateY(1px)}
    footer{display:flex;justify-content:space-between;gap:16px;padding:18px 1px 0;color:var(--muted);font-size:12px}
    @media(max-width:760px){.vitals{grid-template-columns:repeat(2,1fr)}.vital:nth-child(2){border-right:0}.vital:nth-child(-n+2){border-bottom:1px solid var(--line)}.vital:nth-child(3){padding-left:0}.layout{grid-template-columns:1fr}.panel.full{grid-column:auto}.devices{grid-template-columns:repeat(2,1fr)}}
    @media(max-width:460px){.wrap{width:calc(100% - 22px);padding-top:14px}header{align-items:flex-start}.brand small{display:none}.vital{padding:20px 12px}.vital:first-child,.vital:nth-child(3){padding-left:2px}.number{font-size:38px}.panel{padding:16px}footer{display:block}footer span{display:block;margin-top:5px}}
  </style>
</head>
<body>
  <main class="wrap">
    <header>
      <div class="brand"><div class="mark">H</div><div><b>Health Monitor</b><small>ESP32-S3 · 192.168.4.1</small></div></div>
      <div class="live" id="live"><span class="dot"></span><span id="liveText">Đang kết nối</span></div>
    </header>
    <section class="vitals" aria-label="Chỉ số chính">
      <div class="vital"><div class="label">Nhịp tim</div><div class="number"><span id="bpm">--</span><span class="unit">BPM</span></div><div class="note" id="finger">Chưa đặt ngón tay</div></div>
      <div class="vital"><div class="label">SpO₂ ước lượng</div><div class="number"><span id="spo2">--</span><span class="unit">%</span></div><div class="note">Chưa hiệu chuẩn y tế</div></div>
      <div class="vital"><div class="label">Số bước</div><div class="number" id="steps">0</div><div class="note">Trong phiên hiện tại</div></div>
      <div class="vital"><div class="label">Té ngã</div><div class="number status" id="fall">OK</div><div class="note" id="acceleration">Gia tốc 1.00 g</div></div>
    </section>
    <section class="layout">
      <article class="panel"><h2>Tín hiệu đo</h2><div class="rows">
        <div class="row"><span>Chất lượng PPG</span><b id="qualityText">0%</b></div><div class="bar"><i id="qualityBar"></i></div>
        <div class="row"><span>Hồng ngoại</span><b id="irRaw">0</b></div><div class="row"><span>MAX30102</span><b id="maxStatus">Đang kiểm tra</b></div>
      </div></article>
      <article class="panel"><h2>Vị trí</h2><div class="rows">
        <div class="row"><span>GPS</span><b id="gpsState">WAIT</b></div><div class="row"><span>Vệ tinh</span><b id="satellites">0</b></div>
        <div class="row"><span>Vĩ độ</span><b id="latitude">--</b></div><div class="row"><span>Kinh độ</span><b id="longitude">--</b></div>
      </div><div class="actions"><a class="map" id="mapLink" href="#" target="_blank" rel="noreferrer">Mở bản đồ</a></div></article>
      <article class="panel full"><h2>Thiết bị và điều khiển</h2><div class="devices">
        <div class="device"><b>OLED</b><span id="oledStatus">--</span></div><div class="device"><b>IMU</b><span id="imuStatus">--</span></div>
        <div class="device"><b>Buzzer</b><span id="buzzerStatus">--</span></div><div class="device"><b>Wi-Fi</b><span class="ok">ONLINE</span></div>
      </div><div class="actions"><button id="resetSteps">Đặt lại bước</button><button class="secondary" id="resetAlert">Hủy cảnh báo</button></div></article>
    </section>
    <footer><span>Wi-Fi: health-monitor · <span id="clients">0</span> thiết bị · <span id="uptime">0 giây</span></span><span>Nguyên mẫu học thuật, không dùng để chẩn đoán.</span></footer>
  </main>
  <script>
    const $=id=>document.getElementById(id),num=(v,d=1)=>Number(v).toFixed(d);
    function state(el,ok){el.textContent=ok?'ONLINE':'OFFLINE';el.className=ok?'ok':'danger'}
    function fallClass(v){return v==='FALL'?'danger':(v==='FREE'||v==='CHECK'?'warn':'ok')}
    async function refresh(){try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw Error();const d=await r.json();
      $('bpm').textContent=d.heartRateValid?Math.round(d.bpm):'--';$('spo2').textContent=d.spo2Valid?Math.round(d.spo2):'--';$('steps').textContent=d.steps;
      $('finger').textContent=d.fingerPresent?(d.heartRateValid?(d.heartRateProvisional?'BPM sơ bộ · đang xác nhận':'Đã xác nhận nhịp'):'Đang bắt mạch…'):'Chưa đặt ngón tay';$('fall').textContent=d.fall;$('fall').className='number status '+fallClass(d.fall);$('acceleration').textContent='Gia tốc '+num(d.acceleration,2)+' g';
      $('qualityText').textContent=Math.round(d.signalQuality)+'%';$('qualityBar').style.width=Math.max(0,Math.min(100,d.signalQuality))+'%';$('irRaw').textContent=Number(d.irRaw).toLocaleString('vi-VN');
      state($('maxStatus'),d.maxOK);state($('oledStatus'),d.oledOK);state($('imuStatus'),d.imuOK);state($('buzzerStatus'),d.buzzerOK);
      $('gpsState').textContent=d.gpsState;$('satellites').textContent=d.satellites;$('latitude').textContent=d.positionKnown?num(d.latitude,6):'--';$('longitude').textContent=d.positionKnown?num(d.longitude,6):'--';
      $('mapLink').style.opacity=d.positionKnown?'1':'.45';$('mapLink').href=d.positionKnown?'https://www.google.com/maps?q='+d.latitude+','+d.longitude:'#';$('clients').textContent=d.clients;$('uptime').textContent=Math.floor(d.uptime/60)+' phút '+(d.uptime%60)+' giây';
      $('live').classList.remove('offline');$('liveText').textContent='Trực tiếp';}catch(e){$('live').classList.add('offline');$('liveText').textContent='Mất kết nối'}}
    async function post(url){await fetch(url,{method:'POST'});refresh()}
    $('resetSteps').onclick=()=>post('/api/reset-steps');$('resetAlert').onclick=()=>post('/api/reset-alert');refresh();setInterval(refresh,250);
  </script>
</body>
</html>
)HTML";

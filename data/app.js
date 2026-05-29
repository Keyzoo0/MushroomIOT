'use strict';

// ----------------------------- util ---------------------------------------
const $ = id => document.getElementById(id);
const toast = (msg, ok = true) => {
  $('toastMsg').textContent = msg;
  const t = $('toast');
  t.className = 'toast align-items-center border-0 text-bg-' + (ok ? 'success' : 'danger');
  bootstrap.Toast.getOrCreateInstance(t).show();
};
const post = (url, body) => fetch(url, {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify(body || {})
}).then(r => r.json());

// --------------------------- aktuator UI -----------------------------------
const ACTS = [
  { key: 'fan',  name: 'Fan (Pendingin)', icon: 'bi-fan',        cls: 'act-fan' },
  { key: 'lamp', name: 'Lampu (Pemanas)', icon: 'bi-lightbulb',  cls: 'act-lamp' },
  { key: 'mist', name: 'Mist Maker',      icon: 'bi-cloud-fog2', cls: 'act-mist' }
];

function buildActuators() {
  $('actuators').innerHTML = ACTS.map(a => `
    <div class="col-md-4">
      <div class="actuator-box ${a.cls}" id="box_${a.key}">
        <div class="actuator-head">
          <span class="actuator-title">
            <span class="actuator-ic"><i class="bi ${a.icon}"></i></span> ${a.name}
          </span>
          <span class="status-pill" id="st_${a.key}">OFF</span>
        </div>
        <div class="actuator-foot">
          <div class="form-check form-switch mb-0">
            <input class="form-check-input mode-sw" type="checkbox" id="mode_${a.key}" data-act="${a.key}">
            <label class="form-check-label mode-tag" for="mode_${a.key}" id="modelbl_${a.key}">AUTO</label>
          </div>
          <div class="btn-group btn-group-sm" role="group">
            <button class="btn btn-outline-success btn-on"  data-act="${a.key}" data-on="1">ON</button>
            <button class="btn btn-outline-secondary btn-on" data-act="${a.key}" data-on="0">OFF</button>
          </div>
        </div>
      </div>
    </div>`).join('');

  // mode switch: checked = MANUAL, unchecked = AUTO
  document.querySelectorAll('.mode-sw').forEach(sw => sw.addEventListener('change', e => {
    const act = e.target.dataset.act;
    const auto = !e.target.checked;
    post('/api/mode', { actuator: act, auto }).then(() => {
      toast((auto ? 'AUTO' : 'MANUAL') + ' : ' + act);
      refreshData();
    });
  }));

  document.querySelectorAll('.btn-on').forEach(b => b.addEventListener('click', e => {
    const act = e.target.dataset.act;
    const on = e.target.dataset.on === '1';
    post('/api/actuator', { actuator: act, on }).then(() => refreshData());
  }));
}

// --------------------------- data realtime ---------------------------------
function refreshData() {
  fetch('/api/data').then(r => r.json()).then(d => {
    $('vTemp').textContent = d.temp;
    $('vHum').textContent  = d.hum;
    $('vSoil').textContent = d.soil;
    $('vSaw').textContent  = d.saw;

    const badge = $('vSawCond');
    badge.textContent = d.sawCond;
    badge.className = 'badge mt-1 ' +
      (d.sawCond === 'Ideal' ? 'bg-success' : d.sawCond === 'Kering' ? 'bg-warning text-dark' : 'bg-info text-dark');

    ACTS.forEach(a => {
      const s = d.act[a.key];
      const pill = $('st_' + a.key);
      pill.textContent = s.on ? 'ON' : 'OFF';
      pill.classList.toggle('on', s.on);
      $('box_' + a.key).classList.toggle('is-on', s.on);
      const sw = $('mode_' + a.key);
      sw.checked = !s.auto;                       // checked = MANUAL
      $('modelbl_' + a.key).textContent = s.auto ? 'AUTO' : 'MANUAL';
      // tombol ON/OFF hanya aktif saat MANUAL
      document.querySelectorAll(`.btn-on[data-act="${a.key}"]`).forEach(b => b.disabled = s.auto);
    });

    const chip = $('wifiChip');
    if (d.wifi) {
      chip.classList.remove('off');
      $('wifiIcon').className = 'bi bi-wifi';
      $('wifiText').textContent = d.ip + ' · ' + d.rssi + ' dBm';
      $('footIp').textContent = d.ip;
    } else {
      chip.classList.add('off');
      $('wifiIcon').className = 'bi bi-wifi-off';
      $('wifiText').textContent = 'Terputus';
    }
  }).catch(() => {});
}

// ------------------------------ grafik -------------------------------------
const charts = {};
function makeChart(canvasId, label, color, unit) {
  const ctx = $(canvasId).getContext('2d');
  const grad = ctx.createLinearGradient(0, 0, 0, 180);
  grad.addColorStop(0, color + '40');
  grad.addColorStop(1, color + '05');
  return new Chart(ctx, {
    type: 'line',
    data: {
      labels: [],
      datasets: [{
        label, data: [], borderColor: color, backgroundColor: grad,
        tension: .35, pointRadius: 0, borderWidth: 2.5, fill: true
      }]
    },
    options: {
      responsive: true,
      animation: false,
      interaction: { mode: 'index', intersect: false },
      scales: {
        y: { beginAtZero: false, grid: { color: '#eef1ee' }, ticks: { callback: v => v + unit } },
        x: { grid: { display: false }, ticks: { maxTicksLimit: 6, autoSkip: true } }
      },
      plugins: { legend: { display: false }, tooltip: { callbacks: { label: c => c.parsed.y + unit } } }
    }
  });
}

function initChart() {
  charts.temp = makeChart('chartTemp', 'Suhu',            '#ef5350', '°C');
  charts.hum  = makeChart('chartHum',  'Kelembapan Udara','#29b6f6', '%');
  charts.soil = makeChart('chartSoil', 'Kelembapan Tanah','#26a69a', '%');
  charts.saw  = makeChart('chartSaw',  'Serbuk Gergaji',  '#ff9800', '%');
}

function refreshGraph() {
  fetch('/api/graph').then(r => r.json()).then(g => {
    const n = (g.temp || []).length;
    // label waktu relatif: titik tiap 3 detik, terbaru = 0s
    const labels = Array.from({ length: n }, (_, i) => '-' + ((n - 1 - i) * 3) + 's');
    const apply = (c, data) => { c.data.labels = labels; c.data.datasets[0].data = data; c.update(); };
    apply(charts.temp, g.temp);
    apply(charts.hum,  g.hum);
    apply(charts.soil, g.soil);
    apply(charts.saw,  g.saw);
  }).catch(() => {});
}

// ------------------------------ settings -----------------------------------
function loadSettings() {
  fetch('/api/settings').then(r => r.json()).then(s => {
    $('sTempMin').value = s.tempMin;
    $('sTempMax').value = s.tempMax;
    $('sHumMin').value  = s.humMin;
    $('sSoilDry').value = s.soilDryADC;
    $('sSoilWet').value = s.soilWetADC;
    $('sSawDry').value  = s.sawDryMax;
    $('sSawWet').value  = s.sawWetMin;
    $('sRevFan').checked  = s.revFan;
    $('sRevLamp').checked = s.revLamp;
    $('sRevMist').checked = s.revMist;
  });
}

$('btnSave').addEventListener('click', () => {
  const body = {
    tempMin: parseFloat($('sTempMin').value),
    tempMax: parseFloat($('sTempMax').value),
    humMin:  parseFloat($('sHumMin').value),
    soilDryADC: parseInt($('sSoilDry').value),
    soilWetADC: parseInt($('sSoilWet').value),
    sawDryMax: parseFloat($('sSawDry').value),
    sawWetMin: parseFloat($('sSawWet').value),
    revFan:  $('sRevFan').checked,
    revLamp: $('sRevLamp').checked,
    revMist: $('sRevMist').checked
  };
  post('/api/settings', body).then(() => { toast('Pengaturan disimpan'); refreshData(); });
});

$('btnReset').addEventListener('click', () => {
  if (!confirm('Hapus semua data grafik?')) return;
  post('/api/graph/reset').then(() => { toast('Data grafik direset'); refreshGraph(); });
});

// ------------------------------- start -------------------------------------
buildActuators();
initChart();
loadSettings();
refreshData();
refreshGraph();
setInterval(refreshData, 2000);
setInterval(refreshGraph, 3000);

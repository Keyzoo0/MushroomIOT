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
  { key: 'fan',  name: 'Fan (Pendingin)', icon: 'bi-fan',       cls: 'on-fan',  color: 'text-primary' },
  { key: 'lamp', name: 'Lampu (Pemanas)', icon: 'bi-lightbulb', cls: 'on-lamp', color: 'text-warning' },
  { key: 'mist', name: 'Mist Maker',      icon: 'bi-cloud-fog2',cls: 'on-mist', color: 'text-info' }
];

function buildActuators() {
  $('actuators').innerHTML = ACTS.map(a => `
    <div class="col-md-4">
      <div class="actuator-box">
        <div class="d-flex justify-content-between align-items-center mb-2">
          <span class="actuator-title"><i class="bi ${a.icon} actuator-icon ${a.color}"></i> ${a.name}</span>
          <span><span class="led" id="led_${a.key}"></span><span id="st_${a.key}" class="small text-muted">OFF</span></span>
        </div>
        <div class="d-flex justify-content-between align-items-center">
          <div class="form-check form-switch mb-0">
            <input class="form-check-input mode-sw" type="checkbox" id="mode_${a.key}" data-act="${a.key}">
            <label class="form-check-label small" for="mode_${a.key}" id="modelbl_${a.key}">AUTO</label>
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
      const led = $('led_' + a.key);
      led.className = 'led' + (s.on ? ' on ' + a.cls : '');
      $('st_' + a.key).textContent = s.on ? 'ON' : 'OFF';
      const sw = $('mode_' + a.key);
      sw.checked = !s.auto;                       // checked = MANUAL
      $('modelbl_' + a.key).textContent = s.auto ? 'AUTO' : 'MANUAL';
      // tombol ON/OFF hanya aktif saat MANUAL
      document.querySelectorAll(`.btn-on[data-act="${a.key}"]`).forEach(b => b.disabled = s.auto);
    });

    if (d.wifi) {
      $('wifiIcon').className = 'bi bi-wifi';
      $('wifiText').textContent = d.ip + ' (' + d.rssi + ' dBm)';
    } else {
      $('wifiIcon').className = 'bi bi-wifi-off';
      $('wifiText').textContent = 'Disconnected';
    }
  }).catch(() => {});
}

// ------------------------------ grafik -------------------------------------
let chart;
function initChart() {
  const ctx = $('chart').getContext('2d');
  const ds = (label, color) => ({
    label, data: [], borderColor: color, backgroundColor: color + '22',
    tension: .3, pointRadius: 0, borderWidth: 2
  });
  chart = new Chart(ctx, {
    type: 'line',
    data: {
      labels: [],
      datasets: [
        ds('Suhu (°C)', '#dc3545'),
        ds('Kelembapan (%)', '#0d6efd'),
        ds('Kelembapan Media (%)', '#0dcaf0'),
        ds('Serbuk Gergaji (%)', '#fd7e14')
      ]
    },
    options: {
      responsive: true,
      animation: false,
      interaction: { mode: 'index', intersect: false },
      scales: { y: { beginAtZero: true, suggestedMax: 100 } },
      plugins: { legend: { position: 'top' } }
    }
  });
}

function refreshGraph() {
  fetch('/api/graph').then(r => r.json()).then(g => {
    const n = (g.temp || []).length;
    // label waktu relatif: titik tiap 3 detik, terbaru = 0s
    chart.data.labels = Array.from({ length: n }, (_, i) => '-' + ((n - 1 - i) * 3) + 's');
    chart.data.datasets[0].data = g.temp;
    chart.data.datasets[1].data = g.hum;
    chart.data.datasets[2].data = g.soil;
    chart.data.datasets[3].data = g.saw;
    chart.update();
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

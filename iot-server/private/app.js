'use strict';

let nodoAttivo  = null;
let chartRaw    = null;
let chartOrarie = null;
let chartGiorn  = null;

const socket = io();

// ---- Socket.IO: Ricezione Dati ----
socket.on('connect', () => {
  document.getElementById('stato-connessione').className = 'badge badge-online';
  document.getElementById('stato-connessione').textContent = 'Online';
});

socket.on('sensor_data', (d) => {
  if (d.id !== nodoAttivo) return;
  aggiornaLive(d.temp, d.hum, d.soil, d.ts);
  if (chartRaw) {
    const label = new Date(d.ts).toLocaleTimeString('it-IT');
    pushPointChart(chartRaw, label, d.temp, d.hum, d.soil, 60);
  }
});

// ---- Inizializzazione ----
document.addEventListener('DOMContentLoaded', async () => {
  await caricaNodi();
  initTabBar();
});

async function caricaNodi() {
  const nodi = await fetchJSON('/api/nodi');
  const sel = document.getElementById('selNodo');
  sel.innerHTML = '';
  nodi.forEach(n => {
    const opt = document.createElement('option');
    opt.value = n.id;
    opt.textContent = `${n.nome} (ID:${n.id})`;
    sel.appendChild(opt);
  });
  sel.addEventListener('change', () => selezionaNodo(parseInt(sel.value)));
  if (nodi.length) selezionaNodo(nodi[0].id);
}

async function selezionaNodo(id) {
  nodoAttivo = id;
  const live = await fetchJSON(`/api/nodo/${id}/live`);
  if (live && !live.error) aggiornaLive(live.temp, live.hum, live.soil, live.ts);
  
  await caricaGraficoRaw(id);
  await caricaGraficoOrarie(id);
  await caricaGraficoGiornaliere(id);
}

// ---- UI Updates ----
function aggiornaLive(temp, hum, soil, ts) {
  document.getElementById('val-temp').textContent = temp?.toFixed(1) || '--';
  document.getElementById('val-hum').textContent  = hum?.toFixed(1) || '--';
  document.getElementById('val-soil').textContent = soil || '--';
  document.getElementById('val-ts').textContent   = ts ? new Date(ts).toLocaleString('it-IT') : '--';
}

// ---- MQTT: Invio Comandi ----
async function inviaComando(azione) {
  const statusEl = document.getElementById('status-comando');
  statusEl.textContent = 'Invio in corso...';
  
  try {
    const res = await fetch('/api/comando', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ id: nodoAttivo, azione: azione })
    });
    const data = await res.json();
    statusEl.textContent = `Risposta: ${data.message}`;
    setTimeout(() => statusEl.textContent = '', 3000);
  } catch (err) {
    statusEl.textContent = 'Errore invio comando.';
  }
}

// ---- Grafici (Chart.js) ----
function buildChartConfig(labels, datasets, yLabel) {
  return {
    type: 'line',
    data: { labels, datasets },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      plugins: { legend: { labels: { color: '#e8eaf0' } } },
      scales: {
        x: { ticks: { color: '#6b7280' }, grid: { color: '#2a2d3a' } },
        y: { ticks: { color: '#6b7280' }, grid: { color: '#2a2d3a' }, title: { display: true, text: yLabel, color: '#6b7280' } }
      }
    }
  };
}

async function caricaGraficoRaw(id) {
  const data = await fetchJSON(`/api/nodo/${id}/raw?n=60`);
  const labels = data.map(r => new Date(r.ts).toLocaleTimeString('it-IT'));
  const datasets = makeDatasets(data, 'temp', 'hum', 'soil');
  if (chartRaw) chartRaw.destroy();
  chartRaw = new Chart(document.getElementById('chart-raw'), buildChartConfig(labels, datasets, 'Valori Real-time'));
}

async function caricaGraficoOrarie(id) {
  const data = await fetchJSON(`/api/nodo/${id}/orarie?n=24`);
  const labels = data.map(r => new Date(r.ora).getHours() + ':00');
  const datasets = makeDatasets(data, 'temp_media', 'hum_media', 'soil_media');
  if (chartOrarie) chartOrarie.destroy();
  chartOrarie = new Chart(document.getElementById('chart-orarie'), buildChartConfig(labels, datasets, 'Media Oraria'));
}

async function caricaGraficoGiornaliere(id) {
  const data = await fetchJSON(`/api/nodo/${id}/giornaliere?n=10`);
  const labels = data.map(r => r.giorno);
  const datasets = makeDatasets(data, 'temp_media', 'hum_media', 'soil_media');
  if (chartGiorn) chartGiorn.destroy();
  chartGiorn = new Chart(document.getElementById('chart-giornaliere'), buildChartConfig(labels, datasets, 'Media Giornaliera'));
}

function makeDatasets(data, tK, hK, sK) {
  return [
    { label: 'Temp °C', data: data.map(r => r[tK]), borderColor: '#e74c3c', tension: 0.3 },
    { label: 'Umidità %', data: data.map(r => r[hK]), borderColor: '#3b82f6', tension: 0.3 },
    { label: 'Suolo %', data: data.map(r => r[sK]), borderColor: '#2ecc71', tension: 0.3 }
  ];
}

function pushPointChart(chart, label, temp, hum, soil, max) {
  chart.data.labels.push(label);
  chart.data.datasets[0].data.push(temp);
  chart.data.datasets[1].data.push(hum);
  chart.data.datasets[2].data.push(soil);
  if (chart.data.labels.length > max) {
    chart.data.labels.shift();
    chart.data.datasets.forEach(ds => ds.data.shift());
  }
  chart.update('none');
}

function initTabBar() {
  document.querySelectorAll('.tab-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.tab-btn, .tab-panel').forEach(el => el.classList.remove('active'));
      btn.classList.add('active');
      document.getElementById(`tab-${btn.dataset.tab}`).classList.add('active');
    });
  });
}

async function fetchJSON(url) {
  const res = await fetch(url);
  return res.ok ? res.json() : [];
}

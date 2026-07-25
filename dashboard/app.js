const POLL_MS = 2000;
const HISTORY_POINTS = 30;

let currentHost = null;
let lastNet = null;

const series = { cpu: [], mem: [], net: [] };

const $ = (id) => document.getElementById(id);

function setStatus(ok) {
  $('statusDot').classList.toggle('ok', ok);
  $('statusText').textContent = ok ? 'live' : 'disconnected';
}

const css = getComputedStyle(document.documentElement);
const COLOR_GOOD = css.getPropertyValue('--good').trim() || '#3ddc97';
const COLOR_WARN = css.getPropertyValue('--warn').trim() || '#ffb454';
const COLOR_BAD = css.getPropertyValue('--bad').trim() || '#ff5d5d';
const COLOR_GRID = '#262a37';
const COLOR_DIM = '#8b90a3';

function barColor(pct) {
  if (pct < 60) return COLOR_GOOD;
  if (pct < 85) return COLOR_WARN;
  return COLOR_BAD;
}

function drawLineChart(canvas, points, color) {
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  const w = Math.max(rect.width, 50);
  const h = Math.max(rect.height, 50);
  canvas.width = w * dpr;
  canvas.height = h * dpr;
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);

  if (points.length < 2) {
    ctx.fillStyle = COLOR_DIM;
    ctx.font = '13px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText('waiting for data…', w / 2, h / 2);
    return;
  }

  const padL = 36, padR = 8, padT = 8, padB = 18;
  const plotW = w - padL - padR;
  const plotH = h - padT - padB;

  const values = points.map((p) => p.v);
  let maxV = Math.max(...values, 1);
  let minV = Math.min(...values, 0);
  if (minV > 0) minV = 0;
  maxV *= 1.15;

  const x = (i) => padL + (i / (points.length - 1)) * plotW;
  const y = (v) => padT + plotH - ((v - minV) / (maxV - minV || 1)) * plotH;

  ctx.strokeStyle = COLOR_GRID;
  ctx.fillStyle = COLOR_DIM;
  ctx.font = '10px sans-serif';
  ctx.textAlign = 'right';
  ctx.lineWidth = 1;
  [0, 0.5, 1].forEach((f) => {
    const yy = padT + plotH * (1 - f);
    ctx.beginPath();
    ctx.moveTo(padL, yy);
    ctx.lineTo(w - padR, yy);
    ctx.stroke();
    const val = minV + f * (maxV - minV);
    ctx.fillText(val >= 100 ? val.toFixed(0) : val.toFixed(1), padL - 6, yy + 3);
  });

  ctx.beginPath();
  ctx.moveTo(x(0), y(points[0].v));
  points.forEach((p, i) => ctx.lineTo(x(i), y(p.v)));
  ctx.lineTo(x(points.length - 1), padT + plotH);
  ctx.lineTo(x(0), padT + plotH);
  ctx.closePath();
  ctx.fillStyle = color + '2a';
  ctx.fill();

  ctx.beginPath();
  points.forEach((p, i) => {
    const px = x(i), py = y(p.v);
    if (i === 0) ctx.moveTo(px, py);
    else ctx.lineTo(px, py);
  });
  ctx.strokeStyle = color;
  ctx.lineWidth = 2;
  ctx.lineJoin = 'round';
  ctx.stroke();

  const lastIdx = points.length - 1;
  ctx.beginPath();
  ctx.arc(x(lastIdx), y(points[lastIdx].v), 3, 0, Math.PI * 2);
  ctx.fillStyle = color;
  ctx.fill();
}

function pushSeries(key, t, v, maxPoints) {
  series[key].push({ t, v });
  if (series[key].length > maxPoints) series[key].shift();
}

function renderCharts() {
  drawLineChart($('cpuChart'), series.cpu, '#5aa9ff');
  drawLineChart($('memChart'), series.mem, '#3ddc97');
  drawLineChart($('netChart'), series.net, '#ffb454');
}

async function fetchJson(url) {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${url} -> ${res.status}`);
  return res.json();
}

async function loadHosts() {
  const hosts = await fetchJson('/api/hosts');
  const select = $('hostSelect');
  const prev = select.value;
  select.innerHTML = '';
  hosts.forEach((h) => {
    const opt = document.createElement('option');
    opt.value = h.hostname;
    opt.textContent = h.hostname;
    select.appendChild(opt);
  });
  if (hosts.length === 0) return null;
  const keep = hosts.find((h) => h.hostname === prev);
  select.value = keep ? prev : hosts[0].hostname;
  return select.value;
}

function updateCards(m) {
  const cpuPct = m.cpu_percent;
  $('cpuValue').textContent = `${cpuPct.toFixed(1)}%`;
  $('cpuBar').style.width = `${Math.min(cpuPct, 100)}%`;
  $('cpuBar').style.background = barColor(cpuPct);

  const memPct = (m.mem_used_mb / m.mem_total_mb) * 100;
  $('memValue').textContent = `${memPct.toFixed(1)}%`;
  $('memBar').style.width = `${Math.min(memPct, 100)}%`;
  $('memBar').style.background = barColor(memPct);
  $('memSub').textContent = `${(m.mem_used_mb / 1024).toFixed(2)} / ${(m.mem_total_mb / 1024).toFixed(2)} GB`;

  const diskPct = (m.disk_used_mb / m.disk_total_mb) * 100;
  $('diskValue').textContent = `${diskPct.toFixed(1)}%`;
  $('diskBar').style.width = `${Math.min(diskPct, 100)}%`;
  $('diskBar').style.background = barColor(diskPct);
  $('diskSub').textContent = `${(m.disk_used_mb / 1024).toFixed(1)} / ${(m.disk_total_mb / 1024).toFixed(1)} GB`;

  $('procValue').textContent = m.proc_count;
}

function updateProcessTable(procs) {
  const body = $('procTableBody');
  body.innerHTML = '';
  procs.slice(0, 10).forEach((p) => {
    const tr = document.createElement('tr');
    tr.innerHTML = `<td>${p.pid}</td><td>${p.name}</td><td>${p.cpu_percent.toFixed(1)}</td><td>${p.mem_mb.toFixed(1)}</td>`;
    body.appendChild(tr);
  });
}

function netKbs(curr, prev) {
  if (!prev || prev.hostname !== curr.hostname) return 0;
  const dt = curr.timestamp - prev.timestamp;
  if (dt <= 0) return 0;
  const totalDelta = (curr.net_rx_bytes - prev.net_rx_bytes) + (curr.net_tx_bytes - prev.net_tx_bytes);
  return Math.max(0, totalDelta / dt / 1024);
}

async function loadHistory(host) {
  const rows = await fetchJson(`/api/history?host=${encodeURIComponent(host)}&limit=${HISTORY_POINTS}`);
  series.cpu = [];
  series.mem = [];
  series.net = [];
  let prev = null;
  rows.forEach((r) => {
    pushSeries('cpu', r.timestamp, r.cpu_percent, HISTORY_POINTS);
    pushSeries('mem', r.timestamp, r.mem_used_mb, HISTORY_POINTS);
    pushSeries('net', r.timestamp, netKbs(r, prev), HISTORY_POINTS);
    prev = r;
  });
  lastNet = prev;
  renderCharts();
}

async function pollOnce() {
  if (!currentHost) return;
  const [latestArr, procs] = await Promise.all([
    fetchJson(`/api/latest?host=${encodeURIComponent(currentHost)}`),
    fetchJson(`/api/processes?host=${encodeURIComponent(currentHost)}`),
  ]);
  if (latestArr.length === 0) return;
  const m = latestArr[0];
  updateCards(m);
  updateProcessTable(procs);

  pushSeries('cpu', m.timestamp, m.cpu_percent, HISTORY_POINTS);
  pushSeries('mem', m.timestamp, m.mem_used_mb, HISTORY_POINTS);
  pushSeries('net', m.timestamp, netKbs(m, lastNet), HISTORY_POINTS);
  lastNet = m;
  renderCharts();
}

async function tick() {
  try {
    const host = await loadHosts();
    if (host && host !== currentHost) {
      currentHost = host;
      await loadHistory(currentHost);
    }
    await pollOnce();
    setStatus(true);
  } catch (e) {
    console.error(e);
    setStatus(false);
  }
}

$('hostSelect').addEventListener('change', async (e) => {
  currentHost = e.target.value;
  await loadHistory(currentHost);
});

window.addEventListener('resize', renderCharts);

tick();
setInterval(tick, POLL_MS);

#pragma once
#include <string>

namespace sclip {

inline std::string GetObservatoryHtmlPage() {
    std::string html;
    html.reserve(36000);

    html += R"HTML(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>SchrödingerClip // 波函数坍缩观测台 Quantum Clipboard Observatory</title>
<style>
  :root {
    --bg-deep: #050811;
    --bg-panel: rgba(12, 19, 36, 0.86);
    --bg-card: rgba(17, 27, 50, 0.75);
    --border-cyan: rgba(56, 189, 248, 0.28);
    --cyan: #38bdf8;
    --violet: #a855f7;
    --emerald: #10b981;
    --amber: #f59e0b;
    --rose: #f43f5e;
    --txt-main: #f1f5f9;
    --txt-dim: #94a3b8;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    background: radial-gradient(circle at 20% 15%, #0f1d3a 0%, #050811 55%, #03050a 100%);
    color: var(--txt-main);
    font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;
    min-height: 100vh;
    overflow-x: hidden;
  }
  header {
    display: flex; align-items: center; justify-content: space-between;
    padding: 14px 26px;
    border-bottom: 1px solid var(--border-cyan);
    background: rgba(6, 11, 23, 0.92);
    backdrop-filter: blur(12px);
    position: sticky; top: 0; z-index: 50;
  }
  .brand { display: flex; align-items: center; gap: 14px; }
  .brand-orb {
    width: 36px; height: 36px; border-radius: 50%;
    background: conic-gradient(from 45deg, var(--cyan), var(--violet), var(--emerald), var(--cyan));
    box-shadow: 0 0 18px rgba(56, 189, 248, 0.65);
    animation: spin 8s linear infinite;
  }
  @keyframes spin { to { transform: rotate(360deg); } }
  .brand h1 { font-size: 18px; letter-spacing: 0.8px; font-weight: 700; }
  .brand span { font-size: 12px; color: var(--cyan); font-family: Consolas, monospace; display: block; }
  .header-controls { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
  .badge-zero {
    padding: 6px 12px; border-radius: 999px;
    background: rgba(16, 185, 129, 0.14);
    border: 1px solid rgba(16, 185, 129, 0.55);
    color: #34d399; font-family: Consolas, monospace; font-size: 12px; font-weight: 700;
  }
  button, select {
    background: rgba(30, 41, 59, 0.85); color: var(--txt-main);
    border: 1px solid var(--border-cyan); border-radius: 8px;
    padding: 7px 13px; font-size: 12.5px; cursor: pointer;
    transition: all 0.16s ease;
  }
  button:hover, select:hover {
    border-color: var(--cyan);
    box-shadow: 0 0 12px rgba(56, 189, 248, 0.28);
  }
  button.bypass-on {
    background: rgba(245, 158, 11, 0.22);
    border-color: var(--amber); color: #fcd34d; font-weight: 700;
  }
  .crosshair-bar {
    margin: 16px 24px 0;
    padding: 14px 20px;
    border-radius: 12px;
    background: linear-gradient(90deg, rgba(14, 116, 144, 0.24), rgba(88, 28, 135, 0.22));
    border: 1px solid rgba(56, 189, 248, 0.45);
    display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 12px;
    box-shadow: 0 6px 24px rgba(0,0,0,0.35);
  }
  .crosshair-main {
    font-family: Consolas, 'Cascadia Code', monospace;
    font-size: 14.5px; display: flex; align-items: center; gap: 10px; flex-wrap: wrap;
  }
  .crosshair-arrow { color: var(--cyan); font-weight: 700; }
  .crosshair-outcome {
    padding: 4px 10px; border-radius: 6px;
    background: rgba(56, 189, 248, 0.18); border: 1px solid var(--cyan);
    color: #bae6fd; font-weight: 700;
  }
  .stats-row {
    display: grid; grid-template-columns: repeat(4, 1fr); gap: 14px;
    padding: 16px 24px 0;
  }
  .stat-card {
    background: var(--bg-panel); border: 1px solid rgba(148, 163, 184, 0.16);
    border-radius: 12px; padding: 14px 18px;
  }
  .stat-label { font-size: 11.5px; color: var(--txt-dim); text-transform: uppercase; letter-spacing: 0.7px; }
  .stat-val { font-size: 24px; font-weight: 700; font-family: Consolas, monospace; margin-top: 5px; color: var(--cyan); }
  .grid-main {
    display: grid; grid-template-columns: 1.22fr 1fr; gap: 18px;
    padding: 16px 24px 24px;
  }
  .panel {
    background: var(--bg-panel); border: 1px solid var(--border-cyan);
    border-radius: 14px; padding: 18px; display: flex; flex-direction: column; gap: 14px;
    box-shadow: 0 12px 32px rgba(0, 0, 0, 0.45);
  }
  .panel-title {
    display: flex; align-items: center; justify-content: space-between;
    font-size: 14px; font-weight: 700; letter-spacing: 0.6px; color: #e2e8f0;
    border-bottom: 1px solid rgba(148, 163, 184, 0.14); padding-bottom: 10px;
  }
  #starCanvas {
    width: 100%; height: 420px; border-radius: 10px;
    background: radial-gradient(circle at center, rgba(15, 29, 58, 0.85) 0%, rgba(5, 9, 20, 0.96) 78%);
    border: 1px solid rgba(56, 189, 248, 0.18);
  }
  .seed-box {
    background: rgba(7, 12, 24, 0.9); border: 1px solid rgba(148, 163, 184, 0.2);
    border-radius: 10px; padding: 12px; font-family: Consolas, monospace; font-size: 12px;
    color: #cbd5e1; max-height: 96px; overflow: auto; white-space: pre-wrap; word-break: break-all;
  }
  .preset-grid { display: flex; flex-wrap: wrap; gap: 8px; }
  .preset-btn {
    background: rgba(30, 58, 138, 0.35); border-color: rgba(96, 165, 250, 0.4);
    font-size: 12px; padding: 6px 11px;
  }
  .sim-bar { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
  .preview-box {
    background: #070b14; border: 1px solid rgba(56, 189, 248, 0.25);
    border-radius: 10px; padding: 14px; min-height: 175px; max-height: 290px;
    overflow: auto; font-family: Consolas, monospace; font-size: 12px; color: #bae6fd;
    white-space: pre-wrap;
  }
  .preview-box img { max-width: 100%; border-radius: 8px; border: 1px solid rgba(255,255,255,0.12); }
  .log-list { display: flex; flex-direction: column; gap: 9px; max-height: 520px; overflow-y: auto; }
  .log-item {
    background: var(--bg-card); border-left: 3px solid var(--cyan);
    border-radius: 8px; padding: 10px 13px; font-size: 12px;
  }
  .log-item.sniffer { border-left-color: var(--rose); background: rgba(127, 29, 29, 0.22); }
  .log-top { display: flex; justify-content: space-between; font-family: Consolas, monospace; margin-bottom: 4px; }
  .log-meta { color: var(--txt-dim); font-size: 11.5px; margin-bottom: 5px; }
  .log-snip {
    font-family: Consolas, monospace; font-size: 11px; color: #93c5fd;
    background: rgba(4, 8, 18, 0.65); padding: 5px 8px; border-radius: 5px;
    overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  }
  @media (max-width: 1100px) {
    .grid-main { grid-template-columns: 1fr; }
    .stats-row { grid-template-columns: repeat(2, 1fr); }
  }
</style>
</head>
<body>
<header>
  <div class="brand">
    <div class="brand-orb"></div>
    <div>
      <h1>SCHRÖDINGER·CLIP // 波函数坍缩观测台</h1>
      <span>OLE/COM Delayed IDataObject + TYMED_ISTREAM + Dual-Layer Observer Engine</span>
    </div>
  </div>
  <div class="header-controls">
    <div class="badge-zero" id="auditBadge">0 Bytes Evaluated @ Copy</div>
    <button id="btnBypass" onclick="toggleBypass()">⚡ 反坍缩直通: OFF (Alt+Shift+V)</button>
    <select id="lockSelect" onchange="changeLockMode(this.value)">
      <option value="0">🎯 自动感知宿主语境 (Auto Observer)</option>
      <option value="1">⚡ 原味直通锁定 (Raw Bypass)</option>
      <option value="2">🔒 锁定 TypeScript Interface (.ts)</option>
      <option value="3">🔒 锁定 Python @dataclass (.py)</option>
      <option value="4">🔒 锁定 Go Struct (.go)</option>
      <option value="5">🔒 锁定 Rust Serde Struct (.rs)</option>
      <option value="6">🔒 锁定 SQL DDL+INSERT (.sql)</option>
      <option value="7">🔒 锁定 Excel 多列表格 (Grid)</option>
      <option value="8">🔒 锁定 Markdown 语境 (.md)</option>
      <option value="9">🔒 锁定 2x 抗锯齿视觉卡片 (PNG)</option>
      <option value="10">🔒 锁定 JS/TS Async fetch()</option>
    </select>
    <button onclick="disarmSeed()" title="释放当前 OLE 叠加态句柄">🧹 清空叠加态</button>
  </div>
</header>

<section class="crosshair-bar">
  <div class="crosshair-main">
    <span>🎯 当前前台观测者:</span>
    <strong id="obsHost" style="color:#f8fafc;">探测中...</strong>
    <span id="obsMicro" style="color:#a78bfa;"></span>
    <span class="crosshair-arrow">──▶ 按下 Ctrl+V 将坍缩为:</span>
    <span class="crosshair-outcome" id="obsOutcome">UTF-16 Native Text</span>
  </div>
  <div style="font-family:Consolas,monospace;font-size:12px;color:var(--txt-dim);">
    嗅探耗时: <span id="obsLatency" style="color:#34d399;">0.08 ms</span>
  </div>
</section>

<section class="stats-row">
  <div class="stat-card">
    <div class="stat-label">累计异构波函数坍缩</div>
    <div class="stat-val" id="stCollapses">0</div>
  </div>
  <div class="stat-card">
    <div class="stat-label">拦截后台剪贴板偷读 (Shield)</div>
    <div class="stat-val" id="stBlocked" style="color:#f43f5e;">0</div>
  </div>
  <div class="stat-card">
    <div class="stat-label">复制瞬间新增求值内存</div>
    <div class="stat-val" style="color:#10b981;">0 Bytes</div>
  </div>
  <div class="stat-card">
    <div class="stat-label">零落盘流式节省磁盘写入</div>
    <div class="stat-val" id="stSaved" style="color:#a855f7;">0 KB</div>
  </div>
</section>

<main class="grid-main">
  <div style="display:flex;flex-direction:column;gap:18px;">
    <section class="panel">
      <div class="panel-title">
        <span>🌌 实时叠加态星图 (Superposition Wavefunction Graph)</span>
        <span id="seedKindBadge" style="font-family:Consolas,monospace;font-size:12px;color:var(--cyan);">STANDBY</span>
      </div>
      <canvas id="starCanvas" width="820" height="420"></canvas>
      <div style="font-size:12px;color:var(--txt-dim);">当前叠加态原始种子 (Seed Envelope — 未执行任何预转换或预下载):</div>
      <div class="seed-box" id="seedPreview">(在任意软件按下 Ctrl+C，或点击下方演练场一键武装量子种子)</div>
    </section>

    <section class="panel">
      <div class="panel-title">
        <span>🧪 内置交互演练场 (Built-in Quantum Sandbox Playground)</span>
        <span style="font-size:11.5px;color:var(--emerald);">真 COM OleGetClipboard 实时坍缩测试</span>
      </div>
      <div class="preset-grid">
        <button class="preset-btn" onclick="armSample('json')">🧬 武装复杂嵌套 JSON</button>
        <button class="preset-btn" onclick="armSample('svg')">🎨 武装矢量量子图标 SVG</button>
        <button class="preset-btn" onclick="armSample('curl')">🌐 武装浏览器 Copy-as-cURL</button>
        <button class="preset-btn" onclick="armSample('stream24m')">📦 武装 24MB 远程直链 (可去资源管理器 Ctrl+V!)</button>
        <button class="preset-btn" onclick="armSample('hex')">🌈 武装 #3B82F6 色值</button>
        <button class="preset-btn" onclick="armSample('mermaid')">📊 武装 Mermaid 架构图</button>
        <button class="preset-btn" onclick="armSample('math')">🧮 武装薛定谔波动方程 LaTeX</button>
        <button class="preset-btn" onclick="armSample('epoch')">⏰ 武装 Unix 毫秒时间戳</button>
      </div>
      <div class="sim-bar">
        <span style="font-size:12px;color:var(--txt-dim);">模拟观测宿主语境:</span>
        <select id="simTarget">
          <option value="Code.exe|schema.ts - Visual Studio Code|text">Code.exe [schema.ts] (.ts)</option>
          <option value="Cursor.exe|models.py - Cursor|text">Cursor.exe [models.py] (.py)</option>
          <option value="goland64.exe|types.go - GoLand|text">GoLand [types.go] (.go)</option>
          <option value="rustrover64.exe|lib.rs - RustRover|text">RustRover [lib.rs] (.rs)</option>
          <option value="datagrip64.exe|migration.sql - DataGrip|text">DataGrip [migration.sql] (.sql)</option>
          <option value="EXCEL.EXE|Book1 - Excel|html">EXCEL.EXE [CF_HTML 多列表格]</option>
          <option value="WeChat.exe|技术架构群|png">WeChat.exe [2x HiDPI PNG 图像卡片]</option>
          <option value="explorer.exe|Documents|istream">explorer.exe [CFSTR_FILECONTENTS 虚拟文件流]</option>
          <option value="WindowsTerminal.exe|PowerShell 7|text">WindowsTerminal.exe [Shell 命令]</option>
          <option value="Typora.exe|notes.md - Typora|text">Typora.exe [notes.md] (.md)</option>
        </select>
        <button onclick="triggerSimCollapse()" style="background:rgba(16,185,129,0.22);border-color:#10b981;color:#6ee7b7;font-weight:700;">
          ⚡ 触发真实 COM GetData 坍缩观测
        </button>
      </div>
      <div class="preview-box" id="simOutput">点击上方「武装...」按钮注入种子后，选择任意目标宿主点击「触发真实 COM GetData 坍缩观测」，或直接切去桌面任意软件按 Ctrl+V！</div>
    </section>
  </div>

  <section class="panel">
    <div class="panel-title">
      <span>⏱️ 历史坍缩事件时间轴 (Collapse Telemetry Log)</span>
      <span style="font-family:Consolas,monospace;font-size:11.5px;color:var(--txt-dim);">Precision: 0.01 ms</span>
    </div>
    <div class="log-list" id="telemetryList">
      <div style="color:var(--txt-dim);font-size:12.5px;padding:18px;text-align:center;">
        等待首次 Ctrl+V 观测坍缩事件...
      </div>
    </div>
  </section>
</main>
)HTML";

    html += R"HTML(
<script>
const SAMPLES = {
  json: JSON.stringify({
    orderId: "ORD-2026-9981",
    priority: 1,
    settled: true,
    discountNote: null,
    customer: { userId: 4096, handle: "schrodinger_cat", vipTier: "QUANTUM" },
    items: [
      { sku: "NV-H200", title: "Tensor Core Module", qty: 2, unitPrice: 3999.5 },
      { sku: "OPT-LINK", title: "Photonic Interconnect", qty: 4, unitPrice: 249.0, badge: "HOT" }
    ]
  }, null, 2),
  svg: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 320 220" width="320" height="220">
  <defs>
    <linearGradient id="qgrad" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#38BDF8"/>
      <stop offset="100%" stop-color="#A855F7"/>
    </linearGradient>
  </defs>
  <rect x="16" y="16" width="288" height="188" rx="26" fill="#0F172A" stroke="url(#qgrad)" stroke-width="4"/>
  <ellipse cx="160" cy="110" rx="105" ry="42" fill="none" stroke="#38BDF8" stroke-width="3"/>
  <ellipse cx="160" cy="110" rx="42" ry="82" fill="none" stroke="#A855F7" stroke-width="3"/>
  <circle cx="160" cy="110" r="18" fill="url(#qgrad)"/>
  <circle cx="255" cy="96" r="7" fill="#10B981"/>
  <text x="160" y="188" font-size="15" fill="#E2E8F0" text-anchor="middle">SCHRODINGER QUANTUM CORE</text>
</svg>`,
  curl: `curl 'https://api.quantum-lab.dev/v2/wavefunction/collapse' \\
  -H 'Authorization: Bearer qk_live_98fa72bc10' \\
  -H 'X-Trace-Mode: strict-lazy' \\
  -H 'Cookie: session_id=xyz987; cluster=ap-east' \\
  --data-raw '{"experimentId":42,"observer":"Code.exe","streaming":true}'`,
  stream24m: window.location.origin + "/api/demo-stream/quantum_dataset_24mb.bin",
  hex: "#3B82F6",
  mermaid: `flowchart LR
  Seed[Ctrl+C Raw Seed] --> OLE[COM IDataObject]
  OLE -->|0 Bytes RAM| Shield{Sniffer Shield}
  Shield -->|Ctrl+V Paste| Obs[Observer Detector]
  Obs --> Stream[TYMED_ISTREAM / AST / 2x PNG]`,
  math: String.raw`$$i\hbar \frac{\partial \Psi}{\partial t} = -\frac{\hbar^2}{2m}\nabla^2 \Psi + V(x)\Psi$$`,
  epoch: "1731912000000"
};

let appState = {
  orbits: [],
  hasSeed: false,
  seedKind: "STANDBY",
  seedBytes: 0,
  lockMode: 0,
  lastEventId: 0
};
let particles = [];
let angleTick = 0;

function formatBytes(b) {
  if (b < 1024) return b + " B";
  if (b < 1048576) return (b / 1024).toFixed(1) + " KB";
  return (b / 1048576).toFixed(2) + " MB";
}

function spawnCollapseParticles(orbitId) {
  let targetIdx = appState.orbits.findIndex(o => o.id === orbitId);
  if (targetIdx < 0) targetIdx = 0;
  for (let i = 0; i < 28; i++) {
    particles.push({
      orbitIdx: targetIdx,
      t: Math.random() * 0.25,
      speed: 0.022 + Math.random() * 0.025,
      size: 2.5 + Math.random() * 3.5,
      alpha: 1.0
    });
  }
}

async function pollState() {
  try {
    const res = await fetch('/api/state');
    const data = await res.json();

    document.getElementById('obsHost').textContent = data.observer.processName;
    document.getElementById('obsMicro').textContent = data.observer.detectedFile ? `[${data.observer.detectedFile}]` : '';
    document.getElementById('obsOutcome').textContent = data.predictedOutcome;
    document.getElementById('obsLatency').textContent = data.observer.detectTimeMs.toFixed(2) + ' ms';

    document.getElementById('stCollapses').textContent = data.stats.totalCollapses;
    document.getElementById('stBlocked').textContent = data.stats.totalSniffersBlocked;
    document.getElementById('stSaved').textContent = formatBytes(data.stats.totalZeroDiskSavedBytes);

    const btnBypass = document.getElementById('btnBypass');
    if (data.observer.lockMode === 1) {
      btnBypass.className = 'bypass-on';
      btnBypass.textContent = '⚡ 反坍缩直通: ON (Alt+Shift+V)';
    } else {
      btnBypass.className = '';
      btnBypass.textContent = '⚡ 反坍缩直通: OFF (Alt+Shift+V)';
    }
    const sel = document.getElementById('lockSelect');
    if (document.activeElement !== sel) sel.value = String(data.observer.lockMode);

    document.getElementById('seedKindBadge').textContent = data.seed.badgeZh + ' // ' + data.seed.kindName;
    if (data.seed.hasSeed) {
      document.getElementById('seedPreview').textContent = data.seed.rawPreview || '(Empty Seed)';
    } else {
      document.getElementById('seedPreview').textContent = '(在任意软件按下 Ctrl+C，或点击下方演练场一键武装量子种子)';
    }

    if (data.stats.latestEventId > appState.lastEventId && data.events.length > 0) {
      if (appState.lastEventId !== 0 || data.stats.totalCollapses > 0) {
        spawnCollapseParticles(data.events[0].orbitId);
      }
      appState.lastEventId = data.stats.latestEventId;
      renderEvents(data.events);
    }

    appState.orbits = data.orbits || [];
    appState.hasSeed = data.seed.hasSeed;
    appState.seedKind = data.seed.kindName;
    appState.seedBytes = data.seed.seedRawBytes;
  } catch (e) {}
}

function renderEvents(events) {
  const box = document.getElementById('telemetryList');
  if (!events || events.length === 0) return;
  box.innerHTML = events.map(ev => {
    const cls = ev.blockedSniffer ? 'log-item sniffer' : 'log-item';
    const tp = ev.throughputMBps > 0 ? ` | 🚀 ${ev.throughputMBps.toFixed(2)} MB/s` : '';
    return `<div class="${cls}">
      <div class="log-top">
        <strong style="color:${ev.blockedSniffer ? '#fda4af' : '#38bdf8'}">${escapeHtml(ev.transformerName)}</strong>
        <span style="color:#34d399">${ev.synthesisMs.toFixed(2)} ms${tp}</span>
      </div>
      <div class="log-meta">
        [${ev.timestamp}] 宿主: <b>${escapeHtml(ev.observerProcess)}</b> ${ev.detectedMicroContext ? '(' + escapeHtml(ev.detectedMicroContext) + ')' : ''}
        · 格式: <code>${escapeHtml(ev.requestedFormat)}</code> · 产物: ${formatBytes(ev.outputBytes)}
      </div>
      <div class="log-snip">${escapeHtml(ev.previewSnippet)}</div>
    </div>`;
  }).join('');
}

function escapeHtml(s) {
  return String(s || '').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

async function armSample(key) {
  const text = SAMPLES[key];
  await fetch('/api/arm', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ text })
  });
  await pollState();
}

async function disarmSeed() {
  await fetch('/api/disarm', { method: 'POST' });
  await pollState();
}

async function toggleBypass() {
  await fetch('/api/lock', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ toggleBypass: true })
  });
  await pollState();
}

async function changeLockMode(val) {
  await fetch('/api/lock', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ mode: parseInt(val, 10) })
  });
  await pollState();
}

async function triggerSimCollapse() {
  const raw = document.getElementById('simTarget').value.split('|');
  const res = await fetch('/api/preview-collapse', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ processName: raw[0], windowTitle: raw[1], format: raw[2] })
  });
  const data = await res.json();
  const out = document.getElementById('simOutput');
  if (!data.ok) {
    out.textContent = '坍缩提示: ' + (data.error || '当前种子不支持该格式');
  } else if (data.kind === 'png') {
    out.innerHTML = `<div style="margin-bottom:8px;color:#34d399;">✅ 真实 COM PNG 位图合成成功 (${data.bytes} bytes):</div><img src="data:image/png;base64,${data.base64}" />`;
  } else if (data.kind === 'html') {
    out.innerHTML = `<div style="margin-bottom:8px;color:#34d399;">✅ 真实 COM CF_HTML 合成成功:</div><div style="background:#fff;color:#0f172a;padding:10px;border-radius:6px;">${data.htmlFragment}</div>`;
  } else {
    out.textContent = data.text;
  }
  await pollState();
}

function drawStarGraph() {
  const canvas = document.getElementById('starCanvas');
  const ctx = canvas.getContext('2d');
  const W = canvas.width, H = canvas.height;
  ctx.clearRect(0, 0, W, H);
  angleTick += 0.006;

  const cx = W * 0.5, cy = H * 0.5;
  const orbits = appState.orbits;
  const count = orbits.length || 10;

  // Draw dashed quantum rings & orbiting eigenstate nodes
  const nodePositions = [];
  for (let i = 0; i < count; i++) {
    const o = orbits[i] || { label: 'Eigenstate', armed: false };
    const ring = (i % 3);
    const rx = 112 + ring * 62;
    const ry = 66 + ring * 44;
    const baseAngle = (i / count) * Math.PI * 2 + angleTick * (ring % 2 === 0 ? 1 : -0.8);

    ctx.save();
    ctx.beginPath();
    ctx.setLineDash([5, 7]);
    ctx.strokeStyle = o.armed ? 'rgba(56, 189, 248, 0.24)' : 'rgba(100, 116, 139, 0.11)';
    ctx.lineWidth = 1;
    ctx.ellipse(cx, cy, rx, ry, 0, 0, Math.PI * 2);
    ctx.stroke();
    ctx.restore();

    const nx = cx + Math.cos(baseAngle) * rx;
    const ny = cy + Math.sin(baseAngle) * ry;
    nodePositions.push({ x: nx, y: ny, armed: o.armed });

    // Faint tether line when armed
    if (o.armed) {
      ctx.beginPath();
      ctx.setLineDash([3, 5]);
      ctx.strokeStyle = 'rgba(56, 189, 248, 0.22)';
      ctx.moveTo(cx, cy);
      ctx.lineTo(nx, ny);
      ctx.stroke();
      ctx.setLineDash([]);
    }

    ctx.beginPath();
    ctx.arc(nx, ny, o.armed ? 7.5 : 4.5, 0, Math.PI * 2);
    ctx.fillStyle = o.armed ? '#38bdf8' : '#334155';
    if (o.armed) {
      ctx.shadowColor = '#38bdf8';
      ctx.shadowBlur = 14;
    }
    ctx.fill();
    ctx.shadowBlur = 0;

    ctx.font = (o.armed ? 'bold ' : '') + '11.5px Consolas, Segoe UI, sans-serif';
    ctx.fillStyle = o.armed ? '#e0f2fe' : '#64748b';
    ctx.textAlign = nx >= cx ? 'left' : 'right';
    ctx.fillText(o.label, nx + (nx >= cx ? 12 : -12), ny + 4);
  }

  // Draw active collapse particle beams
  for (let i = particles.length - 1; i >= 0; i--) {
    const p = particles[i];
    p.t += p.speed;
    if (p.t >= 1.0) {
      particles.splice(i, 1);
      continue;
    }
    const target = nodePositions[p.orbitIdx] || { x: cx + 160, y: cy };
    const px = cx + (target.x - cx) * p.t;
    const py = cy + (target.y - cy) * p.t;
    ctx.beginPath();
    ctx.arc(px, py, p.size, 0, Math.PI * 2);
    ctx.fillStyle = `rgba(52, 211, 153, ${1 - p.t * 0.6})`;
    ctx.shadowColor = '#10b981';
    ctx.shadowBlur = 16;
    ctx.fill();
    ctx.shadowBlur = 0;
  }

  // Central Superposition Seed Core
  const pulse = 36 + Math.sin(angleTick * 5) * 3.5;
  const grad = ctx.createRadialGradient(cx, cy, 6, cx, cy, pulse + 18);
  grad.addColorStop(0, '#38bdf8');
  grad.addColorStop(0.55, '#6366f1');
  grad.addColorStop(1, 'rgba(15, 23, 42, 0)');
  ctx.beginPath();
  ctx.arc(cx, cy, pulse + 16, 0, Math.PI * 2);
  ctx.fillStyle = grad;
  ctx.fill();

  ctx.beginPath();
  ctx.arc(cx, cy, 30, 0, Math.PI * 2);
  ctx.fillStyle = '#091022';
  ctx.strokeStyle = '#38bdf8';
  ctx.lineWidth = 2.2;
  ctx.fill();
  ctx.stroke();

  ctx.fillStyle = '#f8fafc';
  ctx.font = 'bold 11px Consolas, monospace';
  ctx.textAlign = 'center';
  ctx.fillText(appState.hasSeed ? 'ARMED SEED' : 'IDLE CORE', cx, cy - 3);
  ctx.fillStyle = '#34d399';
  ctx.font = '10px Consolas, monospace';
  ctx.fillText('0B EVAL', cx, cy + 12);

  requestAnimationFrame(drawStarGraph);
}

setInterval(pollState, 240);
pollState();
requestAnimationFrame(drawStarGraph);
</script>
</body>
</html>
)HTML";

    return html;
}

} // namespace sclip

import streamlit as st
import streamlit.components.v1 as components

st.set_page_config(
    page_title="Monitor Pro",
    layout="wide",
    initial_sidebar_state="expanded",
)

THEME_COLOR = "#FF4D88"
BG_COLOR = "#000000"
CARD_BG = "#121212"

#  Sidebar config 
with st.sidebar:
    st.markdown("## Connection")

    pi_ip = st.text_input("Pi IP", value="172.20.10.2")
    ws_port = st.number_input("WS Port", min_value=1, max_value=65535, value=8765, step=1)
    mjpeg_port = st.number_input("MJPEG Port", min_value=1, max_value=65535, value=8080, step=1)

    st.markdown("---")
    st.markdown("## Display")
    video_h = st.slider("Video height", 300, 900, 560, 10)   
    show_overlay = st.toggle("Overlay BBox", value=True)
    show_beep = st.toggle("Beep on PERSON", value=True)

ws_url = f"ws://{pi_ip}:{ws_port}"
mjpeg_url = f"http://{pi_ip}:{mjpeg_port}/stream.mjpg"
snapshot_url = f"http://{pi_ip}:{mjpeg_port}/snapshot.jpg"

# Page header 
st.markdown(
    f"""
    <style>
      .stApp {{ background-color: {BG_COLOR}; }}
      #MainMenu, footer, header {{ visibility: hidden; }}
    </style>
    """,
    unsafe_allow_html=True,
)

st.markdown(
    "<h2 style='text-align:center; color:#FF4D88; font-weight:900; letter-spacing:1px; margin: 8px 0 14px 0;'>"
    "REAL-TIME PERSON DETECTION (PI → PC Dashboard)"
    "</h2>",
    unsafe_allow_html=True,
)

#  HTML/JS dashboard  
html = f"""
<!doctype html>
<html>
<head>
<meta charset="utf-8"/>
<style>
  :root {{
    --pink: {THEME_COLOR};
    --bg: {BG_COLOR};
    --card: {CARD_BG};
    --muted: #888;
  }}
  body {{
    margin:0; padding:0; background:var(--bg); color:var(--pink);
    font-family: ui-sans-serif, system-ui, -apple-system, Segoe UI, Roboto, Helvetica, Arial;
  }}

  .wrap {{
    display: grid;
    grid-template-columns: 2fr 1.15fr;
    gap: 18px;
    align-items: start;
  }}

  .videoCard {{
    background: rgba(18,18,18,0.35);
    border: 1px solid #222;
    border-radius: 14px;
    padding: 14px;
  }}

  .videoStage {{
    position: relative;
    width: 100%;
    height: {video_h}px;
    border-radius: 14px;
    overflow: hidden;
    background: #000;
    border: 1px solid #222;
  }}

  /* MJPEG image fill the stage */
  #mjpeg {{
    position:absolute; inset:0;
    width:100%; height:100%;
    object-fit: contain;  /* fill to avoid letterbox => bbox align */
    transform: translateZ(0);
  }}

  /* SVG overlay aligns with the same box */
  #overlay {{
    position:absolute;
    pointer-events:none;
    transform-origin: top left;
  }}

  .badge {{
    position:absolute;
    left: 12px; top: 12px;
    background: rgba(0,0,0,0.55);
    border: 1px solid #333;
    padding: 8px 10px;
    border-radius: 10px;
    font-weight: 800;
    color: var(--pink);
    backdrop-filter: blur(4px);
  }}

  .controls {{
    margin-top: 12px;
    display:grid;
    grid-template-columns: 1fr 1fr 1fr;
    gap: 12px;
  }}
  .btn {{
    width: 100%;
    border-radius: 10px;
    border: 1px solid #333;
    background: var(--card);
    color: var(--pink);
    font-weight: 900;
    padding: 12px 10px;
    cursor: pointer;
    text-transform: uppercase;
    transition: 0.2s ease;
  }}
  .btn:hover {{
    border-color: var(--pink);
    box-shadow: 0 0 10px rgba(255,77,136,0.18);
    color: white;
    background: #1E000A;
  }}

  .rightCol {{
    display:flex; flex-direction: column; gap: 12px;
  }}

  .tabs {{
    display:flex; gap: 10px;
  }}
  .tabBtn {{
    border-radius: 10px;
    border: 1px solid #333;
    padding: 10px 12px;
    background: var(--card);
    color: #aaa;
    font-weight: 900;
    cursor:pointer;
  }}
  .tabBtn.active {{
    background: var(--pink);
    color: white;
    border-color: transparent;
  }}

  .panel {{
    background: rgba(18,18,18,0.35);
    border: 1px solid #222;
    border-radius: 14px;
    padding: 14px;
    min-height: 240px;
  }}

  .status {{
    border-left: 6px solid var(--pink);
    border-radius: 12px;
    padding: 14px;
    background: rgba(18,18,18,0.75);
  }}
  .status.alert {{
    border-left-color: #ff0000;
    background: rgba(42,0,0,0.8);
    color: #ff7777;
  }}
  .status h3 {{
    margin: 0;
    font-size: 22px;
    font-weight: 1000;
    letter-spacing: 0.5px;
  }}
  .status p {{
    margin: 6px 0 0 0;
    color: var(--muted);
    font-weight: 700;
  }}

  .small {{
    color: #bbb;
    font-weight: 800;
    font-size: 13px;
  }}

  table {{
    width: 100%;
    border-collapse: collapse;
    font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New";
    font-size: 13px;
  }}
  thead th {{
    text-align:left;
    padding: 8px 6px;
    border-bottom: 1px solid #2b2b2b;
    color: #ffd1e1;
  }}
  tbody td {{
    padding: 7px 6px;
    border-bottom: 1px solid #1f1f1f;
    color: #e8e8e8;
  }}
  .rowAlert td {{
    color: #ff9aa9;
  }}

  /* simple chart */
  #chart {{
    width: 100%;
    height: 160px;
    background: rgba(0,0,0,0.35);
    border: 1px solid #222;
    border-radius: 12px;
  }}
</style>
</head>

<body>
<div class="wrap">
  <!-- LEFT -->
  <div class="videoCard">
    <div class="videoStage">
      <img id="mjpeg" src="{mjpeg_url}" alt="mjpeg"/>
      <svg id="overlay" viewBox="0 0 640 480" preserveAspectRatio="none"></svg>
      <div class="badge" id="badge">WS: connecting... | LoopFPS:0.0 DetFPS:0.0 | ALERT:NO</div>
    </div>

    <div class="controls">
      <button class="btn" id="btnStart">START NEW</button>
      <button class="btn" id="btnPause">⏸ PAUSE</button>
      <button class="btn" id="btnReset">RESET LOGS</button>
    </div>

    <div class="small" style="margin-top:10px;">
      MJPEG: <span style="color:#fff">{mjpeg_url}</span> &nbsp;|&nbsp;
      Snapshot: <span style="color:#fff">{snapshot_url}</span> &nbsp;|&nbsp;
      WS: <span style="color:#fff">{ws_url}</span>
    </div>
  </div>

  <!-- RIGHT -->
  <div class="rightCol">
    <div class="tabs">
      <button class="tabBtn active" id="tabAnalytics">ANALYTICS</button>
      <button class="tabBtn" id="tabLogs">DATA LOGS</button>
    </div>

    <div class="panel" id="panelAnalytics">
      <canvas id="chart"></canvas>
      <div style="height:12px;"></div>
      <div class="status" id="statusCard">
        <h3>SYSTEM READY</h3>
        <p>Click START NEW to begin</p>
      </div>
    </div>

    <div class="panel" id="panelLogs" style="display:none;">
      <table>
        <thead>
          <tr><th>TIME</th><th>OBJECT</th><th>CONF</th><th>STATUS</th></tr>
        </thead>
        <tbody id="logBody"></tbody>
      </table>
    </div>
  </div>
</div>

<script>
  const WS_URL = "{ws_url}";
  const OVERLAY_ON = {str(show_overlay).lower()};
  const BEEP_ON = {str(show_beep).lower()};

  // State
  let running = false;
  let paused = false;
  let ws = null;

  const badge = document.getElementById("badge");
  const overlay = document.getElementById("overlay");
  const statusCard = document.getElementById("statusCard");
  const logBody = document.getElementById("logBody");

  // Tabs
  const tabAnalytics = document.getElementById("tabAnalytics");
  const tabLogs = document.getElementById("tabLogs");
  const panelAnalytics = document.getElementById("panelAnalytics");
  const panelLogs = document.getElementById("panelLogs");

  tabAnalytics.onclick = () => {{
    tabAnalytics.classList.add("active"); tabLogs.classList.remove("active");
    panelAnalytics.style.display = "block"; panelLogs.style.display = "none";
  }};
  tabLogs.onclick = () => {{
    tabLogs.classList.add("active"); tabAnalytics.classList.remove("active");
    panelLogs.style.display = "block"; panelAnalytics.style.display = "none";
  }};

  // Buttons
  document.getElementById("btnStart").onclick = () => {{
    running = true; paused = false;
    logs = [];
    confSeries = [];
    renderLogs();
    updateStatus(false, "SYSTEM RUNNING", "Receiving detections from Pi...");
    connectWS(true);
  }};

  document.getElementById("btnPause").onclick = () => {{
    if (!running) return;
    paused = !paused;
    if (paused) {{
      updateStatus(false, "SYSTEM PAUSED", "Click PAUSE again to resume");
    }} else {{
      updateStatus(false, "SYSTEM RUNNING", "Receiving detections from Pi...");
    }}
  }};

  document.getElementById("btnReset").onclick = () => {{
    running = false; paused = false;
    logs = [];
    confSeries = [];
    clearOverlay();
    renderLogs();
    drawChart();
    updateStatus(false, "SYSTEM READY", "Click START NEW to begin");
    if (ws) {{ try {{ ws.close(); }} catch(e){{}} ws=null; }}
    badge.textContent = "WS: disconnected | LoopFPS:0.0 DetFPS:0.0 | ALERT:NO";
  }};

  // Simple beep (tiny)
  function beep() {{
    if (!BEEP_ON) return;
    try {{
      const ctx = new (window.AudioContext || window.webkitAudioContext)();
      const o = ctx.createOscillator();
      const g = ctx.createGain();
      o.type = "sine";
      o.frequency.value = 880;
      o.connect(g); g.connect(ctx.destination);
      g.gain.value = 0.05;
      o.start();
      setTimeout(() => {{ o.stop(); ctx.close(); }}, 120);
    }} catch(e) {{}}
  }}

  function clamp01(x) {{
    x = Number(x);
    if (!isFinite(x)) return 0;
    return Math.max(0, Math.min(1, x));
  }}

  function fmtTime(ts) {{
    // ts can be number seconds or string
    if (typeof ts === "number") {{
      const d = new Date(ts * 1000);
      return d.toLocaleTimeString();
    }}
    if (typeof ts === "string") {{
      // ISO or "HH:MM:SS"
      if (ts.includes("T")) {{
        try {{ return ts.split("T")[1].split(".")[0]; }} catch(e) {{ return ts; }}
      }}
      return ts;
    }}
    return new Date().toLocaleTimeString();
  }}
    function fitOverlayToImage() {{
    const img = document.getElementById("mjpeg");
    const svg = document.getElementById("overlay");
    if (!img || !svg) return false;

    const imgRect = img.getBoundingClientRect();
    const stageRect = img.parentElement.getBoundingClientRect();

    const left = imgRect.left - stageRect.left;
    const top  = imgRect.top  - stageRect.top;

    svg.style.width  = imgRect.width + "px";
    svg.style.height = imgRect.height + "px";
    svg.style.left   = left + "px";
    svg.style.top    = top  + "px";

    svg.setAttribute("viewBox", "0 0 640 480");
    return true;
    }}
  function clearOverlay() {{
    while (overlay.firstChild) overlay.removeChild(overlay.firstChild);
  }}

  function drawOverlay(dets) {{
    if (!OVERLAY_ON) return;
    if (!fitOverlayToImage()) return; 
    clearOverlay();
    if (!dets || !Array.isArray(dets)) return;

    for (const d of dets) {{
      const bb = d.bbox;
      if (!bb || bb.length !== 4) continue;

      const x1 = bb[0], y1 = bb[1], x2 = bb[2], y2 = bb[3];
      const w = Math.max(0, x2 - x1);
      const h = Math.max(0, y2 - y1);

      const rect = document.createElementNS("http://www.w3.org/2000/svg","rect");
      rect.setAttribute("x", x1);
      rect.setAttribute("y", y1);
      rect.setAttribute("width", w);
      rect.setAttribute("height", h);
      rect.setAttribute("fill", "none");
      rect.setAttribute("stroke", "{THEME_COLOR}");
      rect.setAttribute("stroke-width", "3");
      overlay.appendChild(rect);

      const label = document.createElementNS("http://www.w3.org/2000/svg","text");
      label.setAttribute("x", x1 + 4);
      label.setAttribute("y", Math.max(14, y1 - 6));
      label.setAttribute("fill", "{THEME_COLOR}");
      label.setAttribute("font-size", "18");
      label.setAttribute("font-weight", "900");
      const cls = d.cls || "obj";
      const conf = clamp01(d.conf || 0);
      label.textContent = `${{cls}} ${{conf.toFixed(2)}}`;
      overlay.appendChild(label);
    }}
  }}

  function updateStatus(isAlert, title, note) {{
    statusCard.classList.toggle("alert", !!isAlert);
    statusCard.innerHTML = `<h3>${{title}}</h3><p>${{note}}</p>`;
  }}

  // Logs + chart
  let logs = [];
  let confSeries = [];  // last 60 points
  const MAX_LOG = 60;

  function renderLogs() {{
    logBody.innerHTML = "";
    for (const row of logs) {{
      const tr = document.createElement("tr");
      if (row.STATUS === "ALERT") tr.className = "rowAlert";
      tr.innerHTML = `<td>${{row.TIME}}</td><td>${{row.OBJECT}}</td><td>${{row.CONF}}</td><td>${{row.STATUS}}</td>`;
      logBody.appendChild(tr);
    }}
  }}

  const canvas = document.getElementById("chart");
  function drawChart() {{
    const ctx = canvas.getContext("2d");
    const r = canvas.getBoundingClientRect();
    canvas.width = Math.max(300, Math.floor(r.width));
    canvas.height = Math.max(140, Math.floor(r.height));

    ctx.clearRect(0,0,canvas.width,canvas.height);

    // axes
    ctx.strokeStyle = "rgba(255,255,255,0.15)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(10, 10);
    ctx.lineTo(10, canvas.height - 10);
    ctx.lineTo(canvas.width - 10, canvas.height - 10);
    ctx.stroke();

    if (confSeries.length < 2) return;

    ctx.strokeStyle = "{THEME_COLOR}";
    ctx.lineWidth = 2;

    const pad = 12;
    const W = canvas.width - pad*2;
    const H = canvas.height - pad*2;

    ctx.beginPath();
    for (let i=0;i<confSeries.length;i++) {{
      const x = pad + (i/(confSeries.length-1))*W;
      const y = pad + (1-confSeries[i])*H;
      if (i===0) ctx.moveTo(x,y);
      else ctx.lineTo(x,y);
    }}
    ctx.stroke();
  }}

  window.addEventListener("resize", () => drawChart());

  function connectWS(force=false) {{
    if (!running) return;
    if (ws && ws.readyState === WebSocket.OPEN && !force) return;

    try {{
      ws = new WebSocket(WS_URL);

      ws.onopen = () => {{
        badge.textContent = "WS: connected | LoopFPS:0.0 DetFPS:0.0 | ALERT:NO";
      }};

      ws.onmessage = (ev) => {{
        if (!running || paused) return;

        let msg = null;
        try {{ msg = JSON.parse(ev.data); }} catch(e) {{ return; }}

        // Compatible keys:
        const person = !!(msg.person ?? msg.person_found ?? false);
        const dets = msg.detections || [];
        const ts = msg.ts ?? msg.timestamp ?? Date.now()/1000;

        const loop_fps = Number(msg.loop_fps || 0);
        const det_fps  = Number(msg.det_fps || 0);

        // Choose best person conf
        let bestObj = "None";
        let bestConf = 0;
        if (Array.isArray(dets) && dets.length) {{
          for (const d of dets) {{
            const c = clamp01(d.conf || 0);
            if (c > bestConf) {{
              bestConf = c;
              bestObj = d.cls || "obj";
            }}
          }}
        }}

        // Update overlay
        drawOverlay(dets);

        // Badge + status
        badge.textContent = `WS: connected | LoopFPS:${{loop_fps.toFixed(1)}} DetFPS:${{det_fps.toFixed(1)}} | ALERT:${{person ? "YES":"NO"}}`;

        if (person) {{
          updateStatus(true, `WARNING: Person`, `conf ${{Math.round(bestConf*100)}}%`);
          beep();
        }} else {{
          updateStatus(false, "SYSTEM NORMAL", "No person detected");
        }}

        // Logs
        const tstr = fmtTime(ts);
        const row = {{
          TIME: tstr,
          OBJECT: person ? "Person" : bestObj,
          CONF: `${{Math.round(bestConf*100)}}%`,
          STATUS: person ? "ALERT" : "OK"
        }};
        logs.unshift(row);
        logs = logs.slice(0, 50);
        renderLogs();

        // Chart
        confSeries.push(bestConf);
        if (confSeries.length > MAX_LOG) confSeries.shift();
        drawChart();
      }};

      ws.onclose = () => {{
        badge.textContent = "WS: disconnected | LoopFPS:0.0 DetFPS:0.0 | ALERT:NO";
        if (running) setTimeout(() => connectWS(true), 800);
      }};

      ws.onerror = () => {{
        // will trigger close too
      }};

    }} catch(e) {{
      badge.textContent = "WS: error | check URL";
    }}
  }}

  // init (ready screen)
  drawChart();
  updateStatus(false, "SYSTEM READY", "Click START NEW to begin");
</script>

</body>
</html>
"""

components.html(html, height=video_h + 220, scrolling=False)

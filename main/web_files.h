#pragma once

const char index_html[] = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>OBD-II Diagnostic Dashboard</title>
<style>
  .fas, .far, .fab { font-style: normal; font-family: sans-serif; }
  .fa-tachometer-alt::before { content: "⏱"; }
  .fa-chart-line::before     { content: "📈"; }
  .fa-database::before       { content: "💾"; }
  .fa-thermometer-half::before{ content: "🌡"; }
  .fa-cog::before            { content: "⚙"; }
  .fa-signal::before         { content: "▶"; }
  .fa-microchip::before      { content: "⬡"; }
  .fa-snowflake::before      { content: "❄"; }
  .fa-play::before           { content: "▶"; }
  .fa-pause::before          { content: "⏸"; }
  .fa-stop::before           { content: "⏹"; }
  .fa-circle::before         { content: "⏺"; }
  .fa-trash::before          { content: "🗑"; }
  .fa-file-export::before    { content: "↗"; }
  .fa-stream::before         { content: "≡"; }
  .fa-chevron-up::before     { content: "▲"; }
  .fa-plug::before           { content: "🔌"; }
  .fa-wind::before           { content: "💨"; }
  .fa-gas-pump::before       { content: "⛽"; }
  .fa-oil-can::before        { content: "🛢"; }
  .fa-sun::before            { content: "☀"; }
  .fa-stopwatch::before      { content: "⏱"; }
  .fa-wave-square::before    { content: "∿"; }
</style>
<link rel="stylesheet" href="style.css">
</head>
<body>

<div class="top-bar">
  <div class="time" id="clock">10:24 AM</div>
  <div class="bus-leds">
    <div class="led-group">
      <span class="led" id="led-can-rx"></span><span class="led-text">CAN RX</span>
    </div>
    <div class="led-group">
      <span class="led" id="led-can-tx"></span><span class="led-text">CAN TX</span>
    </div>
    <div class="led-group">
      <span class="led led-off" id="led-kline"></span><span class="led-text">K-LINE</span>
    </div>
  </div>
  <div class="connection"><i class="fas fa-signal"></i><span id="bus-proto">CAN 500k</span></div>
  <div class="temperature"><i class="fas fa-microchip"></i><span id="refresh-rate">12 Hz</span></div>
</div>

<!-- RAW FRAME DRAWER -->
<div class="frame-drawer" id="frame-drawer">
  <button class="frame-drawer-tab" id="frame-drawer-toggle">
    <i class="fas fa-stream"></i> <span>Raw Frame Log</span> <span class="frame-drawer-count" id="frame-drawer-count">0</span>
    <i class="fas fa-chevron-up drawer-chevron" id="drawer-chevron"></i>
  </button>
  <div class="frame-drawer-body" id="frame-drawer-body">
    <div class="frame-drawer-controls">
      <button class="control-button" id="frame-pause-btn"><i class="fas fa-pause"></i> Pause</button>
      <button class="control-button" id="frame-clear-btn"><i class="fas fa-trash"></i> Clear</button>
    </div>
    <div class="frame-list" id="frame-list"></div>
  </div>
</div>

<div class="dashboard">

  <!-- LEFT PANEL: horizontal tabs -->
  <div class="left-panel">
    <div class="left-tabs">
      <button class="left-tab active" data-section="session">Session</button>
      <button class="left-tab" data-section="pids">Live PIDs</button>
      <button class="left-tab" data-section="dtc">DTC / Status</button>
    </div>
    <div class="left-content">

      <!-- Session -->
      <div class="left-section active" id="section-session">
        <div class="stat-row">
          <div class="stat-label">Session time</div>
          <div class="stat-value" id="session-time">00:00</div>
          <div class="stat-sub" id="session-started">Started --:--</div>
        </div>
        <div class="stat-row">
          <div class="stat-label">Bus protocol</div>
          <div class="stat-value" id="stat-protocol" style="font-size:18px">CAN 11-bit, 500 kbps</div>
          <div class="stat-sub" id="stat-link">K-Line: idle</div>
        </div>
        <div class="stat-row">
          <div class="stat-label">Frames received</div>
          <div class="stat-value" id="frame-count">0</div>
          <div class="stat-sub">since session start</div>
        </div>
        <div class="stat-row">
          <div class="stat-label">Refresh rate</div>
          <div class="stat-value" id="refresh-rate-big">12 <span style="font-size:13px;color:#555">Hz</span></div>
          <div class="stat-bar"><div class="stat-bar-fill" id="refresh-bar" style="width:60%"></div></div>
          <div class="stat-sub">avg poll-response time: 8 ms</div>
        </div>
        <div class="stat-row">
          <div class="stat-label">PIDs tracked</div>
          <div class="stat-value" id="pid-count">6</div>
          <div class="stat-sub">supported PIDs auto-detected at connect</div>
        </div>
      </div>

      <!-- Live PIDs table -->
      <div class="left-section" id="section-pids">
        <div style="font-size:10px;color:#444;text-transform:uppercase;letter-spacing:0.05em;margin-bottom:8px">Polled parameters</div>
        <table class="pid-table">
          <thead>
            <tr><th>Param</th><th>Value</th><th>Trend</th><th>Updated</th></tr>
          </thead>
          <tbody id="pid-table-body">
            <!-- rows injected by script.js -->
          </tbody>
        </table>
        <div class="pid-controls">
          <button class="control-button" id="freeze-btn"><i class="fas fa-snowflake"></i> Freeze display</button>
        </div>
        <div class="pid-hint">Rows populate dynamically as PIDs respond on the bus. Unsupported parameters show as N/A.</div>
      </div>

      <!-- DTC / Status -->
      <div class="left-section" id="section-dtc">
        <div style="font-size:10px;color:#444;text-transform:uppercase;letter-spacing:0.05em;margin-bottom:8px">Diagnostic trouble codes</div>
        <div id="dtc-list">
          <div class="dtc-empty">No DTCs reported</div>
        </div>
        <div style="font-size:10px;color:#444;text-transform:uppercase;letter-spacing:0.05em;margin:16px 0 8px">Bus health</div>
        <div class="data-grid">
          <div class="data-card">
            <div class="data-card-label">CAN state</div>
            <div class="data-card-val" id="can-state" style="font-size:14px;color:#4CAF50">Active</div>
          </div>
          <div class="data-card">
            <div class="data-card-label">Error frames</div>
            <div class="data-card-val" id="error-frames">0</div>
          </div>
          <div class="data-card">
            <div class="data-card-label">K-Line wake</div>
            <div class="data-card-val" id="kline-wake" style="font-size:14px">5-baud OK</div>
          </div>
          <div class="data-card">
            <div class="data-card-label">ECU resp.</div>
            <div class="data-card-val" id="ecu-resp" style="font-size:14px;color:#4CAF50">OK</div>
          </div>
        </div>
      </div>

    </div>
  </div>

  <!-- CENTER PANEL -->
  <div class="center-panel">

    <div class="panel-view car-view active">
      <div class="map-container">
        <div class="map-placeholder">
          <div class="car-icon-area"><i class="fas fa-microchip"></i></div>
          <div style="color:#333;font-size:13px;letter-spacing:0.08em;text-transform:uppercase">Live diagnostics</div>
        </div>
        <div style="position:absolute;bottom:16px;right:16px;font-size:11px;color:#2a2a2a;text-align:right">
          <div id="bus-status-corner">CAN bus active</div>
        </div>
      </div>
      <div class="gauge-grid" id="gauge-grid">
        <div class="freeze-badge" id="freeze-badge" style="display:none"><i class="fas fa-snowflake"></i> FROZEN</div>
        <div class="gauge-tile">
          <div class="gauge-value" id="gauge-rpm">—</div>
          <div class="gauge-label">RPM</div>
        </div>
        <div class="gauge-tile">
          <div class="gauge-value" id="gauge-speed">—</div>
          <div class="gauge-label">Speed (km/h)</div>
        </div>
        <div class="gauge-tile">
          <div class="gauge-value" id="gauge-coolant">—</div>
          <div class="gauge-label">Coolant (°C)</div>
        </div>
        <div class="gauge-tile">
          <div class="gauge-value" id="gauge-voltage">—</div>
          <div class="gauge-label">Voltage (V)</div>
        </div>
      </div>
    </div>

    <div class="panel-view nav-view">
      <div class="map-container">
        <div class="map-placeholder">
          <i class="fas fa-chart-line" style="font-size:40px;color:#333"></i>
          <span style="color:#333;font-size:13px;letter-spacing:0.08em;text-transform:uppercase">Parameter graph</span>
        </div>
        <div class="route-info">
          <div class="route-text" id="graph-param">Engine RPM &middot; live trend</div>
          <div class="route-details">
            <span><i class="fas fa-stopwatch"></i> last 60s</span>
            <span><i class="fas fa-wave-square"></i> refresh 12 Hz</span>
          </div>
        </div>
      </div>
    </div>

    <div class="panel-view media-view">
      <div class="media-player">
        <div class="album-art"><i class="fas fa-database" style="font-size:40px;color:#333"></i></div>
        <div>
          <div class="song-title">Datalogger</div>
          <div class="song-artist">Recording session to flash</div>
        </div>
        <div class="media-controls">
          <button class="media-btn" id="log-clear" title="Clear log"><i class="fas fa-trash"></i></button>
          <button class="media-btn play-pause" id="play-btn" title="Start/stop logging"><i class="fas fa-circle"></i></button>
          <button class="media-btn" id="log-export" title="Export CSV"><i class="fas fa-file-export"></i></button>
        </div>
        <div class="progress-container">
          <div class="progress-bar"><div class="progress-fill" id="progress-fill"></div></div>
          <div class="time-info"><span id="log-size">0 KB logged</span><span id="log-status">Stopped</span></div>
        </div>
      </div>
    </div>

    <div class="panel-view climate-view">
      <div class="climate-control">
        <div class="temp-display"><span id="big-coolant">—</span><span class="temp-unit">°C</span></div>
        <div style="text-align:center">
          <i class="fas fa-thermometer-half fan-icon pulse"></i>
          <div class="fan-speed">Coolant temperature</div>
        </div>
        <div class="seat-controls">
          <button class="seat-btn" title="Intake air temp"><i class="fas fa-wind"></i></button>
          <button class="seat-btn" title="Fuel pressure"><i class="fas fa-gas-pump"></i></button>
          <button class="seat-btn" title="Oil temp"><i class="fas fa-oil-can"></i></button>
          <button class="seat-btn" title="Ambient temp"><i class="fas fa-sun"></i></button>
        </div>
      </div>
    </div>

    <div class="panel-view settings-view">
      <div class="settings-section">
        <div class="settings-title">Display</div>
        <div class="setting-item">
          <span class="setting-label">Dark mode</span>
          <div class="setting-control">
            <label class="toggle-switch"><input type="checkbox" checked><span class="toggle-slider"></span></label>
          </div>
        </div>
        <div class="setting-item">
          <span class="setting-label">Brightness</span>
          <div class="setting-control">
            <div class="slider-control">
              <input type="range" min="0" max="100" value="70" class="slider" id="brightness-slider">
              <span class="slider-value" id="brightness-val">70</span>
            </div>
          </div>
        </div>
      </div>
      <div class="settings-section">
        <div class="settings-title">Bus configuration</div>
        <div class="setting-item">
          <span class="setting-label">Protocol</span>
          <div class="setting-control">
            <span style="font-size:13px;color:#888">Auto-detect</span>
          </div>
        </div>
        <div class="setting-item">
          <span class="setting-label">Poll interval (ms)</span>
          <div class="setting-control">
            <div class="slider-control">
              <input type="range" min="20" max="500" value="80" class="slider" id="poll-slider">
              <span class="slider-value" id="poll-val">80</span>
            </div>
          </div>
        </div>
      </div>
      <div class="settings-section">
        <div class="settings-title">Device</div>
        <div class="setting-item">
          <span class="setting-label">Firmware</span>
          <div class="setting-control"><span style="font-size:13px;color:#888">v0.1.0-dev</span></div>
        </div>
        <div class="setting-item">
          <span class="setting-label">Reset session</span>
          <div class="setting-control">
            <button class="control-button" id="reset-session-btn">Reset</button>
          </div>
        </div>
      </div>
    </div>

    <!-- Serial monitor (moved here from a stray copy that had ended up
         inside style_css - it must live in index_html to actually render -->
    <div class="panel-view serial-view">
      <div class="serial-monitor">
        <div class="serial-toolbar">
          <span class="serial-title"><i class="fas fa-stream"></i> Serial Monitor (live device log)</span>
          <div class="serial-toolbar-btns">
            <button class="control-button" id="serial-pause-btn"><i class="fas fa-pause"></i> Pause</button>
            <button class="control-button" id="serial-clear-btn"><i class="fas fa-trash"></i> Clear</button>
          </div>
        </div>
        <pre class="serial-output" id="serial-output">Connecting...</pre>
      </div>
    </div>

  </div>

  <!-- RIGHT PANEL -->
  <div class="right-panel">
    <div class="passenger-temp">
      <div class="passenger-temp-label">Engine Load</div>
      <div class="passenger-temp-display">
        <span id="engine-load-value">—</span><span class="temp-unit" style="font-size:18px;margin-left:3px">%</span>
      </div>
      <div class="passenger-temp-controls" style="visibility:hidden">
        <button class="temp-btn"><i class="fas fa-minus"></i></button>
        <button class="temp-btn"><i class="fas fa-plus"></i></button>
      </div>
    </div>

    <div class="bluetooth-info">
      <i class="fas fa-plug bluetooth-icon" id="ecu-icon"></i>
      <span class="bluetooth-text" id="ecu-status-text">ECU Connected</span>
    </div>

    <div class="additional-controls">
      <div class="control-section">
        <div class="control-title">Device Info</div>
        <div class="control-buttons">
          <button class="control-button" id="info-fw">FW v0.1.0</button>
          <button class="control-button" id="info-proto">CAN 11-bit</button>
        </div>
      </div>
      <div class="control-section">
        <div class="control-title">Data Logging</div>
        <div class="control-buttons">
          <button class="control-button" id="quick-log-toggle"><i class="fas fa-circle"></i> Start log</button>
          <button class="control-button" id="quick-log-export"><i class="fas fa-file-export"></i> Export CSV</button>
        </div>
      </div>
    </div>
  </div>

</div>

<!-- BOTTOM DOCK -->
<div class="bottom-dock">
  <button class="dock-btn active" data-view="car-view" title="Live gauges"><i class="fas fa-tachometer-alt"></i></button>
  <button class="dock-btn" data-view="nav-view" title="Parameter graph"><i class="fas fa-chart-line"></i></button>
  <button class="dock-btn" data-view="media-view" title="Datalogger"><i class="fas fa-database"></i></button>
  <button class="dock-btn" data-view="climate-view" title="Thermal PIDs"><i class="fas fa-thermometer-half"></i></button>
  <button class="dock-btn" data-view="settings-view" title="Settings"><i class="fas fa-cog"></i></button>
  <button class="dock-btn" data-view="serial-view" title="Serial monitor"><i class="fas fa-stream"></i></button>
</div>

<script src="script.js"></script>
</body>
</html>
)=====";

const char style_css[] = R"=====(
*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',sans-serif}
body{background:#000;color:#fff;height:100vh;overflow:hidden;display:flex;flex-direction:column}
.top-bar{display:flex;justify-content:space-between;padding:10px 20px;font-size:14px;background:rgba(0,0,0,0.7);border-bottom:1px solid #333}
.top-bar .time{font-weight:bold}
.top-bar .connection{display:flex;align-items:center;gap:5px}
.top-bar .connection i{color:#4CAF50}
.top-bar .temperature{display:flex;align-items:center;gap:5px}
.dashboard{display:flex;flex:1;height:calc(100% - 120px)}

/* LEFT PANEL */
.left-panel{width:25%;display:flex;flex-direction:column;border-right:1px solid #333}
.left-tabs{display:flex;border-bottom:1px solid #333}
.left-tab{flex:1;padding:10px 4px;font-size:10px;background:none;border:none;color:#666;cursor:pointer;text-align:center;border-bottom:2px solid transparent;transition:all 0.2s;letter-spacing:0.03em;text-transform:uppercase}
.left-tab.active{color:#fff;border-bottom:2px solid #fff}
.left-tab:hover{color:#aaa}
.left-content{flex:1;overflow-y:auto;padding:16px}
.left-section{display:none}
.left-section.active{display:block}

/* History section */
.history-item{padding:10px 0;border-bottom:1px solid #1f1f1f;display:flex;align-items:flex-start;gap:10px}
.history-item:last-child{border-bottom:none}
.history-dot{width:8px;height:8px;border-radius:50%;background:#4CAF50;margin-top:4px;flex-shrink:0}
.history-dot.old{background:#444}
.history-meta{flex:1}
.history-label{font-size:13px;color:#fff;margin-bottom:2px}
.history-time{font-size:11px;color:#555}
.history-detail{font-size:11px;color:#888;margin-top:2px}

/* Serial monitor section */
.serial-monitor{flex:1;display:flex;flex-direction:column;background:#000;border:1px solid #1f1f1f;border-radius:8px;overflow:hidden}
.serial-toolbar{display:flex;justify-content:space-between;align-items:center;padding:8px 12px;border-bottom:1px solid #1a1a1a;background:#0a0a0a}
.serial-toolbar-btns{display:flex;gap:8px}
.serial-title{font-size:12px;color:#888;display:flex;align-items:center;gap:6px}
.serial-output{flex:1;margin:0;padding:10px 14px;overflow-y:auto;font-family:'Courier New',monospace;font-size:11px;line-height:1.5;color:#4CAF50;white-space:pre-wrap;word-break:break-all}

/* Session section */
.stat-row{display:flex;flex-direction:column;padding:12px 0;border-bottom:1px solid #1f1f1f}
.stat-row:last-child{border-bottom:none}
.stat-label{font-size:11px;color:#555;text-transform:uppercase;letter-spacing:0.05em;margin-bottom:4px}
.stat-value{font-size:22px;font-weight:300;color:#fff}
.stat-sub{font-size:11px;color:#666;margin-top:2px}
.stat-bar{height:3px;background:#1a1a1a;border-radius:2px;margin-top:6px;overflow:hidden}
.stat-bar-fill{height:100%;background:#4CAF50;border-radius:2px;transition:width 1s ease}

/* Data section */
.data-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.data-card{background:#111;border-radius:8px;padding:10px;border:1px solid #1f1f1f}
.data-card-label{font-size:10px;color:#555;text-transform:uppercase;letter-spacing:0.04em;margin-bottom:4px}
.data-card-val{font-size:18px;font-weight:300;color:#fff}
.data-card-unit{font-size:10px;color:#555;margin-left:2px}
.data-divider{grid-column:1/-1;height:1px;background:#1a1a1a;margin:4px 0}

/* CENTER */
.center-panel{width:50%;padding:20px;display:flex;flex-direction:column;position:relative;overflow:hidden}
.panel-view{flex:1;display:none;flex-direction:column;opacity:0;transition:opacity 0.3s ease}
.panel-view.active{display:flex;opacity:1}
.map-container{flex:1;background:#222;border-radius:10px;margin-bottom:15px;position:relative;overflow:hidden;display:flex;justify-content:center;align-items:center}
.map-placeholder{width:100%;height:100%;background:linear-gradient(135deg,#1a1a1a,#111);display:flex;justify-content:center;align-items:center;color:#444;font-size:16px;flex-direction:column;gap:10px}
.route-info{background:rgba(0,0,0,0.8);padding:10px 15px;border-radius:8px;position:absolute;bottom:20px;left:20px}
.route-text{font-size:14px;margin-bottom:4px}
.route-details{display:flex;gap:12px;font-size:12px;color:#888}
.media-player{flex:1;display:flex;flex-direction:column;justify-content:center;align-items:center;gap:18px}
.album-art{width:130px;height:130px;background:#1a1a1a;border-radius:10px;display:flex;justify-content:center;align-items:center;border:1px solid #2a2a2a}
.song-title{font-size:18px;text-align:center;margin-bottom:4px}
.song-artist{font-size:14px;color:#888;text-align:center}
.media-controls{display:flex;align-items:center;gap:20px;margin-top:10px}
.media-btn{background:none;border:none;color:#fff;font-size:22px;cursor:pointer;padding:8px;border-radius:50%;transition:all 0.2s}
.media-btn.play-pause{font-size:28px;background:rgba(255,255,255,0.08);width:48px;height:48px;display:flex;justify-content:center;align-items:center}
.media-btn:hover{background:rgba(255,255,255,0.1)}
.progress-container{width:80%;margin-top:10px}
.progress-bar{height:3px;background:#2a2a2a;border-radius:2px;margin-bottom:6px;overflow:hidden}
.progress-fill{height:100%;width:30%;background:#fff;border-radius:2px}
.time-info{display:flex;justify-content:space-between;font-size:11px;color:#666}
.climate-control{flex:1;display:flex;flex-direction:column;justify-content:center;align-items:center;gap:20px}
.temp-display{font-size:48px;font-weight:300;display:flex;align-items:center}
.temp-unit{font-size:22px;color:#666;margin-left:4px}
.fan-icon{font-size:28px;color:#4CAF50}
.fan-speed{font-size:13px;color:#888;margin-top:4px}
.seat-controls{display:flex;gap:24px;margin-top:16px}
.seat-btn{background:none;border:none;color:#fff;font-size:22px;cursor:pointer;padding:10px;border-radius:50%;transition:all 0.2s}
.seat-btn:hover{background:rgba(255,255,255,0.08)}
.settings-view{flex:1;padding:10px;overflow-y:auto}
.settings-section{margin-bottom:20px}
.settings-title{font-size:13px;margin-bottom:10px;color:#4CAF50;border-bottom:1px solid #222;padding-bottom:4px;text-transform:uppercase;letter-spacing:0.05em}
.setting-item{display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-bottom:1px solid #1a1a1a}
.setting-label{font-size:14px}
.setting-control{display:flex;align-items:center;gap:8px}
.toggle-switch{position:relative;display:inline-block;width:44px;height:22px}
.toggle-switch input{opacity:0;width:0;height:0}
.toggle-slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background:#2a2a2a;transition:.3s;border-radius:22px}
.toggle-slider:before{position:absolute;content:"";height:14px;width:14px;left:4px;bottom:4px;background:#fff;transition:.3s;border-radius:50%}
input:checked+.toggle-slider{background:#4CAF50}
input:checked+.toggle-slider:before{transform:translateX(22px)}
.slider-control{display:flex;align-items:center;gap:8px;width:140px}
.slider{flex:1;-webkit-appearance:none;height:3px;background:#2a2a2a;border-radius:2px;outline:none}
.slider::-webkit-slider-thumb{-webkit-appearance:none;width:14px;height:14px;border-radius:50%;background:#fff;cursor:pointer}
.slider-value{width:28px;text-align:right;font-size:13px;color:#888}
.control-button{background:rgba(255,255,255,0.05);border:none;color:#fff;padding:6px 10px;border-radius:4px;font-size:12px;cursor:pointer;transition:all 0.2s}
.control-button:hover{background:rgba(255,255,255,0.1)}

/* RIGHT PANEL */
.right-panel{width:25%;padding:16px;display:flex;flex-direction:column;justify-content:space-between;border-left:1px solid #333}
.passenger-temp{display:flex;flex-direction:column;align-items:center;margin-top:10px}
.passenger-temp-label{font-size:11px;color:#555;margin-bottom:4px;text-transform:uppercase;letter-spacing:0.05em}
.passenger-temp-display{font-size:30px;font-weight:300;display:flex;align-items:center}
.passenger-temp-controls{display:flex;gap:12px;margin-top:8px}
.temp-btn{background:none;border:1px solid #2a2a2a;color:#fff;font-size:16px;cursor:pointer;padding:4px 14px;border-radius:4px;transition:all 0.2s}
.temp-btn:hover{background:rgba(255,255,255,0.08)}
.bluetooth-info{display:flex;align-items:center;gap:8px;background:rgba(255,255,255,0.04);padding:10px 12px;border-radius:8px;margin-top:16px;border:1px solid #1f1f1f}
.bluetooth-icon{font-size:18px;color:#4CAF50}
.bluetooth-text{font-size:12px;color:#aaa}
.additional-controls{margin-top:20px}
.control-section{margin-bottom:16px}
.control-title{font-size:11px;color:#555;margin-bottom:8px;text-transform:uppercase;letter-spacing:0.05em}
.control-buttons{display:flex;flex-wrap:wrap;gap:8px}

/* BOTTOM DOCK */
.bottom-dock{height:60px;background:rgba(0,0,0,0.85);border-top:1px solid #222;display:flex;justify-content:space-around;align-items:center}
.dock-btn{background:none;border:none;color:#555;font-size:22px;cursor:pointer;padding:8px 18px;border-radius:6px;transition:all 0.2s;position:relative}
.dock-btn.active{color:#fff}
.dock-btn.active::after{content:'';position:absolute;bottom:4px;left:50%;transform:translateX(-50%);width:18px;height:2px;background:#fff;border-radius:1px}
.dock-btn:hover{color:#aaa;background:rgba(255,255,255,0.06)}
@keyframes pulse{0%{opacity:0.6}50%{opacity:1}100%{opacity:0.6}}
.pulse{animation:pulse 2s infinite}
::-webkit-scrollbar{width:3px}
::-webkit-scrollbar-track{background:#0a0a0a}
::-webkit-scrollbar-thumb{background:#2a2a2a;border-radius:2px}

/* Car view */
.car-view-content{flex:1;display:flex;flex-direction:column;justify-content:center;align-items:center;gap:24px}
.speed-big{font-size:80px;font-weight:200;line-height:1;letter-spacing:-2px}
.speed-unit{font-size:18px;color:#555;letter-spacing:0.1em}
.gear-row{display:flex;gap:14px;margin-top:4px}
.gear{font-size:16px;color:#444;padding:4px 10px;border-radius:4px}
.gear.active{color:#fff;background:rgba(255,255,255,0.08)}
.car-icon-area{opacity:0.15}
.car-icon-area i{font-size:80px}

/* OBD-II additions */
.pid-table{width:100%;border-collapse:collapse;font-size:12px}
.pid-table thead th{text-align:left;color:#444;font-size:10px;text-transform:uppercase;letter-spacing:0.04em;padding-bottom:6px;border-bottom:1px solid #222;font-weight:400}
.pid-table tbody td{padding:7px 4px;border-bottom:1px solid #161616;color:#ccc}
.pid-table tbody tr:last-child td{border-bottom:none}
.pid-table .pid-name{color:#fff}
.pid-table .pid-val{color:#4CAF50;font-variant-numeric:tabular-nums}
.pid-table .pid-time{color:#555;font-size:10px}
.pid-hint{font-size:10px;color:#444;margin-top:10px;line-height:1.5}

.dtc-empty{font-size:13px;color:#444;padding:14px 0;text-align:center;border:1px dashed #1f1f1f;border-radius:6px}
.dtc-item{display:flex;align-items:center;gap:10px;padding:8px 0;border-bottom:1px solid #1a1a1a}
.dtc-code{font-size:13px;color:#E74C3C;font-weight:600;width:60px;flex-shrink:0}
.dtc-desc{font-size:12px;color:#aaa}

.gauge-grid{display:grid;grid-template-columns:1fr 1fr;gap:14px;padding:20px 10px}
.gauge-tile{background:#0d0d0d;border:1px solid #1f1f1f;border-radius:10px;padding:18px 10px;text-align:center}
.gauge-value{font-size:36px;font-weight:200;color:#fff;font-variant-numeric:tabular-nums}
.gauge-label{font-size:11px;color:#555;text-transform:uppercase;letter-spacing:0.05em;margin-top:4px}

/* ===== RX/TX LED indicators ===== */
.bus-leds{display:flex;gap:14px;align-items:center}
.led-group{display:flex;align-items:center;gap:5px}
.led{width:8px;height:8px;border-radius:50%;background:#222;box-shadow:0 0 0 rgba(0,0,0,0);transition:all 0.1s ease;flex-shrink:0}
.led.lit-rx{background:#4CAF50;box-shadow:0 0 6px 1px rgba(76,175,80,0.8)}
.led.lit-tx{background:#3498DB;box-shadow:0 0 6px 1px rgba(52,152,219,0.8)}
.led.led-off{background:#222}
.led.lit-kline{background:#F39C12;box-shadow:0 0 6px 1px rgba(243,156,18,0.8)}
.led-text{font-size:10px;color:#555;letter-spacing:0.04em}

/* ===== Raw frame drawer ===== */
.frame-drawer{background:#000;border-bottom:1px solid #222;position:relative;z-index:5}
.frame-drawer-tab{width:100%;background:#0a0a0a;border:none;color:#888;padding:6px 20px;font-size:11px;display:flex;align-items:center;gap:8px;cursor:pointer;text-transform:uppercase;letter-spacing:0.04em;transition:background 0.2s}
.frame-drawer-tab:hover{background:#111}
.frame-drawer-tab span{margin-right:4px}
.frame-drawer-count{background:#1a1a1a;color:#4CAF50;border-radius:10px;padding:1px 8px;font-size:10px;font-variant-numeric:tabular-nums}
.drawer-chevron{margin-left:auto;transition:transform 0.25s ease;font-size:10px}
.frame-drawer.open .drawer-chevron{transform:rotate(180deg)}
.frame-drawer-body{max-height:0;overflow:hidden;transition:max-height 0.25s ease;background:#060606}
.frame-drawer.open .frame-drawer-body{max-height:220px}
.frame-drawer-controls{display:flex;gap:8px;padding:8px 20px;border-bottom:1px solid #161616}
.frame-list{max-height:170px;overflow-y:auto;padding:6px 20px;font-family:'Courier New',monospace;font-size:11px}
.frame-row{display:flex;gap:14px;padding:3px 0;color:#666;border-bottom:1px solid #111}
.frame-row .f-time{color:#444;width:70px;flex-shrink:0}
.frame-row .f-dir{width:24px;flex-shrink:0;font-weight:bold}
.frame-row .f-dir.rx{color:#4CAF50}
.frame-row .f-dir.tx{color:#3498DB}
.frame-row .f-id{color:#F39C12;width:60px;flex-shrink:0}
.frame-row .f-data{color:#999}

/* ===== Sparklines ===== */
.pid-table td.spark-cell{padding:4px}
.spark-canvas{display:block;width:60px;height:20px}
.pid-controls{margin-top:12px}

/* ===== Gauge color-coding ===== */
.gauge-tile{position:relative;transition:border-color 0.3s ease}
.gauge-value.gauge-ok{color:#fff}
.gauge-value.gauge-warn{color:#F39C12}
.gauge-value.gauge-crit{color:#E74C3C}
.gauge-tile.gauge-tile-warn{border-color:rgba(243,156,18,0.4)}
.gauge-tile.gauge-tile-crit{border-color:rgba(231,76,60,0.5)}

/* ===== Freeze ===== */
.freeze-badge{position:absolute;top:8px;left:8px;background:rgba(52,152,219,0.15);border:1px solid #3498DB;color:#3498DB;font-size:10px;letter-spacing:0.06em;padding:4px 10px;border-radius:20px;display:flex;align-items:center;gap:6px;z-index:3}
.gauge-grid{position:relative}
.frozen .gauge-value{opacity:0.55}

/* ===== Mobile layout ===== */
@media (max-width: 600px) {
  body { height: auto; overflow-y: auto; }

  .dashboard {
    flex-direction: column;
    height: auto;
  }

  .left-panel, .center-panel, .right-panel {
    width: 100%;
    border: none;
    border-bottom: 1px solid #333;
  }

  .left-content { max-height: 240px; }

  .gauge-grid {
    grid-template-columns: 1fr 1fr;
    padding: 14px 6px;
  }

  .gauge-value { font-size: 28px; }

  .top-bar {
    flex-wrap: wrap;
    gap: 6px;
    padding: 8px 12px;
  }

  .bus-leds { order: 3; width: 100%; justify-content: center; }

  .right-panel {
    flex-direction: column;
    align-items: stretch;
  }

  .passenger-temp { margin-top: 0; }

  .bottom-dock {
    position: sticky;
    bottom: 0;
  }
}
)=====";

const char script_js[] = R"=====( document.addEventListener('DOMContentLoaded', function() {

  // ---- Serial monitor: polls /log over HTTP since USB serial isn't
  // available while running off car power. Guarded with null-checks so
  // that if this HTML block is ever missing/misplaced again, it logs a
  // warning instead of throwing and killing the rest of this script. ----
  var serialOutput = document.getElementById('serial-output');
  var serialPauseBtn = document.getElementById('serial-pause-btn');
  var serialClearBtn = document.getElementById('serial-clear-btn');
  var serialPaused = false;
  var lastLogText = '';

  if (!serialOutput || !serialPauseBtn || !serialClearBtn) {
    console.warn('Serial monitor elements missing from DOM - skipping setup');
  } else {
    serialPauseBtn.addEventListener('click', function(){
      serialPaused = !serialPaused;
      this.innerHTML = serialPaused ? '<i class="fas fa-play"></i> Resume' : '<i class="fas fa-pause"></i> Pause';
    });
    serialClearBtn.addEventListener('click', function(){
      serialOutput.textContent = ''; // clears the view only, not the device buffer
    });
    (function pollSerialLog() {
      if (!serialPaused) {
        fetch('/log').then(function(r){ return r.text(); }).then(function(text){
          if (text !== lastLogText) {
            lastLogText = text;
            var atBottom = serialOutput.scrollTop + serialOutput.clientHeight >= serialOutput.scrollHeight - 10;
            serialOutput.textContent = text;
            if (atBottom) serialOutput.scrollTop = serialOutput.scrollHeight;
          }
        }).catch(function(){ /* device out of WiFi range momentarily - next poll retries */ });
      }
      setTimeout(pollSerialLog, 700);
    })();
  }

  // ---- Clock ----
  function updateClock() {
    var now = new Date();
    var h = now.getHours(), m = now.getMinutes();
    var ampm = h >= 12 ? 'PM' : 'AM';
    h = h % 12 || 12;
    document.getElementById('clock').textContent = h + ':' + (m < 10 ? '0' + m : m) + ' ' + ampm;
  }
  updateClock();
  setInterval(updateClock, 30000);

  // ---- Left tabs ----
  document.querySelectorAll('.left-tab').forEach(function(tab) {
    tab.addEventListener('click', function() {
      document.querySelectorAll('.left-tab').forEach(function(t){ t.classList.remove('active'); });
      document.querySelectorAll('.left-section').forEach(function(s){ s.classList.remove('active'); });
      tab.classList.add('active');
      document.getElementById('section-' + tab.dataset.section).classList.add('active');
    });
  });

  // ---- Bottom dock / center panel switching ----
  var dockBtns = document.querySelectorAll('.dock-btn');
  var panelViews = document.querySelectorAll('.panel-view');
  dockBtns.forEach(function(btn) {
    btn.addEventListener('click', function() {
      var target = document.querySelector('.' + btn.dataset.view);
      if (!target) { console.warn('No panel-view found for', btn.dataset.view); return; }
      dockBtns.forEach(function(b){ b.classList.remove('active'); });
      panelViews.forEach(function(v){ v.classList.remove('active'); });
      btn.classList.add('active');
      target.classList.add('active');
    });
  });

  // ---- Sliders ----
  [['brightness-slider','brightness-val'],['poll-slider','poll-val']].forEach(function(pair) {
    var sl = document.getElementById(pair[0]), val = document.getElementById(pair[1]);
    if (sl && val) sl.addEventListener('input', function(){ val.textContent = sl.value; });
  });

  var pidState = [
    { id: 'rpm',     name: 'Engine RPM',      unit: 'rpm',  value: null, supported: true, history: [] },
    { id: 'speed',   name: 'Vehicle Speed',   unit: 'km/h', value: null, supported: true, history: [] },
    { id: 'coolant', name: 'Coolant Temp',    unit: '\u00b0C',   value: null, supported: true, history: [] },
    { id: 'voltage', name: 'Battery Voltage', unit: 'V',    value: null, supported: true, history: [] },
    { id: 'load',    name: 'Engine Load',     unit: '%',    value: null, supported: true, history: [] },
    { id: 'throttle',name: 'Throttle Pos.',   unit: '%',    value: null, supported: true, history: [] }
  ];
  var SPARK_LEN = 20;

  function pushHistory(p, val) {
    p.history.push(val);
    if (p.history.length > SPARK_LEN) p.history.shift();
  }

  function drawSparkline(canvas, history) {
    if (!canvas || history.length < 2) return;
    var ctx = canvas.getContext('2d');
    var w = canvas.width, h = canvas.height;
    ctx.clearRect(0, 0, w, h);
    var nums = history.map(Number);
    var min = Math.min.apply(null, nums), max = Math.max.apply(null, nums);
    var range = (max - min) || 1;
    ctx.beginPath();
    ctx.strokeStyle = '#4CAF50';
    ctx.lineWidth = 1.3;
    nums.forEach(function(v, i) {
      var x = (i / (SPARK_LEN - 1)) * w;
      var y = h - ((v - min) / range) * (h - 4) - 2;
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    });
    ctx.stroke();
  }

  function renderPidTable() {
    var body = document.getElementById('pid-table-body');
    body.innerHTML = '';
    pidState.forEach(function(p) {
      var valText = p.supported ? (p.value === null ? '\u2014' : p.value + ' ' + p.unit) : 'N/A';
      var tr = document.createElement('tr');
      tr.innerHTML =
        '<td class="pid-name">' + p.name + '</td>' +
        '<td class="pid-val">' + valText + '</td>' +
        '<td class="spark-cell"><canvas class="spark-canvas" width="60" height="20" id="spark-' + p.id + '"></canvas></td>' +
        '<td class="pid-time">' + (p.supported ? 'now' : '\u2014') + '</td>';
      body.appendChild(tr);
    });
    pidState.forEach(function(p) {
      drawSparkline(document.getElementById('spark-' + p.id), p.history);
    });
    document.getElementById('pid-count').textContent = pidState.filter(function(p){ return p.supported; }).length;
  }
  renderPidTable();

  // ---- Freeze Display ----
  var isFrozen = false;
  var freezeBtn = document.getElementById('freeze-btn');
  var freezeBadge = document.getElementById('freeze-badge');
  var gaugeGrid = document.getElementById('gauge-grid');
  freezeBtn.addEventListener('click', function() {
    isFrozen = !isFrozen;
    freezeBtn.innerHTML = isFrozen
      ? '<i class="fas fa-play"></i> Resume display'
      : '<i class="fas fa-snowflake"></i> Freeze display';
    freezeBadge.style.display = isFrozen ? 'flex' : 'none';
    gaugeGrid.classList.toggle('frozen', isFrozen);
  });

  // ---- RX/TX LEDs ----
  var ledRx = document.getElementById('led-can-rx');
  var ledTx = document.getElementById('led-can-tx');
  var ledKline = document.getElementById('led-kline');
  function blinkLed(el, cls) {
    el.classList.add(cls);
    setTimeout(function(){ el.classList.remove(cls); }, 120);
  }

  // ---- Raw frame drawer ----
  var frameLog = [];
  var FRAME_LOG_MAX = 200;
  var frameListEl = document.getElementById('frame-list');
  var frameCountEl = document.getElementById('frame-drawer-count');
  var frameDrawer = document.getElementById('frame-drawer');
  var drawerPaused = false;

  document.getElementById('frame-drawer-toggle').addEventListener('click', function() {
    frameDrawer.classList.toggle('open');
  });
  document.getElementById('frame-pause-btn').addEventListener('click', function() {
    drawerPaused = !drawerPaused;
    this.innerHTML = drawerPaused
      ? '<i class="fas fa-play"></i> Resume'
      : '<i class="fas fa-pause"></i> Pause';
  });
  document.getElementById('frame-clear-btn').addEventListener('click', function() {
    frameLog = [];
    frameListEl.innerHTML = '';
    frameCountEl.textContent = '0';
  });

  function logFrame(dir, id, dataHex) {
    var t = new Date();
    var ts = t.toTimeString().slice(0,8) + '.' + String(t.getMilliseconds()).padStart(3,'0');
    frameLog.push({ ts: ts, dir: dir, id: id, data: dataHex });
    if (frameLog.length > FRAME_LOG_MAX) frameLog.shift();
    frameCountEl.textContent = frameLog.length;
    if (drawerPaused) return;
    var row = document.createElement('div');
    row.className = 'frame-row';
    row.innerHTML =
      '<span class="f-time">' + ts + '</span>' +
      '<span class="f-dir ' + dir.toLowerCase() + '">' + dir + '</span>' +
      '<span class="f-id">' + id + '</span>' +
      '<span class="f-data">' + dataHex + '</span>';
    frameListEl.appendChild(row);
    if (frameListEl.children.length > FRAME_LOG_MAX) frameListEl.removeChild(frameListEl.firstChild);
    frameListEl.scrollTop = frameListEl.scrollHeight;
  }

  function randomHexByte() {
    return Math.floor(Math.random() * 256).toString(16).toUpperCase().padStart(2, '0');
  }
  function randomCanId() {
    return '0x' + Math.floor(Math.random() * 0x7FF).toString(16).toUpperCase().padStart(3, '0');
  }

  // ---- Gauge thresholds / color-coding ----
  var THRESHOLDS = {
    coolant: { warn: 100, crit: 115 },
    voltage: { warnLow: 11.5, critLow: 10.8 },
    rpm:     { warn: 5500, crit: 6500 }
  };

  function applyGaugeColor(elId, tileEl, value, kind) {
    var el = document.getElementById(elId);
    if (!el || value === null || value === undefined) return;
    el.classList.remove('gauge-ok', 'gauge-warn', 'gauge-crit');
    if (tileEl) tileEl.classList.remove('gauge-tile-warn', 'gauge-tile-crit');
    var v = Number(value);
    var level = 'ok';
    if (kind === 'coolant') {
      if (v >= THRESHOLDS.coolant.crit) level = 'crit';
      else if (v >= THRESHOLDS.coolant.warn) level = 'warn';
    } else if (kind === 'voltage') {
      if (v <= THRESHOLDS.voltage.critLow) level = 'crit';
      else if (v <= THRESHOLDS.voltage.warnLow) level = 'warn';
    } else if (kind === 'rpm') {
      if (v >= THRESHOLDS.rpm.crit) level = 'crit';
      else if (v >= THRESHOLDS.rpm.warn) level = 'warn';
    }
    el.classList.add(level === 'ok' ? 'gauge-ok' : (level === 'warn' ? 'gauge-warn' : 'gauge-crit'));
    if (tileEl && level !== 'ok') tileEl.classList.add(level === 'warn' ? 'gauge-tile-warn' : 'gauge-tile-crit');
  }

  var frameCount = 0;
  var wsConnected = false;
  var ws = null;

  function applyWsData(d) {
    frameCount = d.frames || frameCount;
    blinkLed(ledRx, 'lit-rx');

    if (!isFrozen) {
      pidState.forEach(function(p) {
        var val = null;
        switch (p.id) {
          case 'rpm':      val = d.rpm; break;
          case 'speed':    val = d.speed; break;
          case 'coolant':  val = d.coolant; break;
          case 'voltage':  val = d.voltage; break;
          case 'load':     val = d.load; break;
          case 'throttle': val = d.throttle; break;
        }
        p.value = val;
        p.supported = (val !== undefined && val !== null);
        if (p.supported) pushHistory(p, val);
      });
      renderPidTable();
      updateGauges();
      document.getElementById('frame-count').textContent = frameCount;
    }

    logFrame('RX', '0x7E8', 'live-data');
  }

  function connectWebSocket() {
    var proto = (location.protocol === 'https:') ? 'wss://' : 'ws://';
    var url = proto + location.host + '/ws';
    try {
      ws = new WebSocket(url);
    } catch (e) {
      console.warn('WS fallback:', e);
      return;
    }
    ws.onopen = function() {
      wsConnected = true;
      document.getElementById('bus-status-corner').textContent = 'CAN bus active';
      document.getElementById('ecu-status-text').textContent = 'ECU Connected';
      document.getElementById('ecu-icon').style.color = '#4CAF50';
    };
    ws.onmessage = function(evt) {
      try {
        var d = JSON.parse(evt.data);
        applyWsData(d);
      } catch (e) { }
    };
    ws.onclose = function() {
      wsConnected = false;
      document.getElementById('bus-status-corner').textContent = 'Reconnecting...';
      document.getElementById('ecu-status-text').textContent = 'Connection Lost';
      document.getElementById('ecu-icon').style.color = '#E74C3C';

      // MAGIA AUTO-RECONNECTU - Próbuje połączyć się ponownie co 1.5 sekundy!
      setTimeout(connectWebSocket, 1500);
    };
  }

  connectWebSocket();

  function getPid(id) {
    var p = pidState.find(function(x){ return x.id === id; });
    return p ? p.value : null;
  }

  function updateGauges() {
    document.getElementById('gauge-rpm').textContent     = (getPid('rpm')     === null ? '\u2014' : getPid('rpm'));
    document.getElementById('gauge-speed').textContent   = (getPid('speed')   === null ? '\u2014' : getPid('speed'));
    document.getElementById('gauge-coolant').textContent = (getPid('coolant') === null ? '\u2014' : getPid('coolant'));
    document.getElementById('gauge-voltage').textContent = (getPid('voltage') === null ? '\u2014' : getPid('voltage'));
    var coolantEl = document.getElementById('big-coolant');
    if (coolantEl) coolantEl.textContent = (getPid('coolant') === null ? '\u2014' : getPid('coolant'));
    var loadEl = document.getElementById('engine-load-value');
    if (loadEl) loadEl.textContent = (getPid('load') === null ? '\u2014' : getPid('load'));

    applyGaugeColor('gauge-coolant', document.getElementById('gauge-coolant').closest('.gauge-tile'), getPid('coolant'), 'coolant');
    applyGaugeColor('gauge-voltage', document.getElementById('gauge-voltage').closest('.gauge-tile'), getPid('voltage'), 'voltage');
    applyGaugeColor('gauge-rpm',     document.getElementById('gauge-rpm').closest('.gauge-tile'),     getPid('rpm'),     'rpm');
  }

  // ---- DTC list ----
  var dtcs = [];
  function renderDtcs() {
    var list = document.getElementById('dtc-list');
    if (!dtcs.length) {
      list.innerHTML = '<div class="dtc-empty">No DTCs reported</div>';
      return;
    }
    list.innerHTML = dtcs.map(function(d) {
      return '<div class="dtc-item"><span class="dtc-code">' + d.code + '</span><span class="dtc-desc">' + d.desc + '</span></div>';
    }).join('');
  }
  renderDtcs();

  // ---- Session timer ----
  var sessionSeconds = 0;
  var sessionEl = document.getElementById('session-time');
  var sessionStartEl = document.getElementById('session-started');
  var sessionStartTime = new Date();
  (function setStart() {
    var h = sessionStartTime.getHours(), m = sessionStartTime.getMinutes();
    var ampm = h >= 12 ? 'PM' : 'AM';
    var hh = h % 12 || 12;
    sessionStartEl.textContent = 'Started ' + hh + ':' + (m < 10 ? '0' + m : m) + ' ' + ampm;
  })();
  function updateSession() {
    sessionSeconds++;
    var m = Math.floor(sessionSeconds / 60), s = sessionSeconds % 60;
    sessionEl.textContent = (m < 10 ? '0' : '') + m + ':' + (s < 10 ? '0' : '') + s;
    setTimeout(updateSession, 1000);
  }
  updateSession();

  document.getElementById('reset-session-btn').addEventListener('click', function() {
    sessionSeconds = 0;
    frameCount = 0;
    sessionStartTime = new Date();
    (function setStart() {
      var h = sessionStartTime.getHours(), m = sessionStartTime.getMinutes();
      var ampm = h >= 12 ? 'PM' : 'AM';
      var hh = h % 12 || 12;
      sessionStartEl.textContent = 'Started ' + hh + ':' + (m < 10 ? '0' + m : m) + ' ' + ampm;
    })();
  });

  // ---- Datalogger controls ----
  var isLogging = false, logSizeKb = 0;
  var playBtn = document.getElementById('play-btn');
  var progressFill = document.getElementById('progress-fill');
  var logStatusEl = document.getElementById('log-status');
  var logSizeEl = document.getElementById('log-size');
  var quickLogBtn = document.getElementById('quick-log-toggle');

  function toggleLogging() {
    isLogging = !isLogging;
    var icon = isLogging ? 'fa-stop' : 'fa-circle';
    playBtn.querySelector('i').className = 'fas ' + icon;
    logStatusEl.textContent = isLogging ? 'Recording' : 'Stopped';
    quickLogBtn.innerHTML = '<i class="fas ' + icon + '"></i> ' + (isLogging ? 'Stop log' : 'Start log');
  }
  playBtn.addEventListener('click', toggleLogging);
  quickLogBtn.addEventListener('click', toggleLogging);

  document.getElementById('log-clear').addEventListener('click', function() {
    logSizeKb = 0;
    logSizeEl.textContent = '0 KB logged';
    progressFill.style.width = '0%';
  });

  function exportCsv() {
    alert('CSV export will pull logged PID samples from the device once datalogging storage is implemented.');
  }
  document.getElementById('log-export').addEventListener('click', exportCsv);
  document.getElementById('quick-log-export').addEventListener('click', exportCsv);

  function simLogGrowth() {
    if (isLogging) {
      logSizeKb += 0.4;
      logSizeEl.textContent = logSizeKb.toFixed(1) + ' KB logged';
      var pct = Math.min(100, logSizeKb % 100);
      progressFill.style.width = pct + '%';
    }
    setTimeout(simLogGrowth, 1000);
  }
  simLogGrowth();

});
)=====";
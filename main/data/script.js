document.addEventListener('DOMContentLoaded', function() {

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
      dockBtns.forEach(function(b){ b.classList.remove('active'); });
      panelViews.forEach(function(v){ v.classList.remove('active'); });
      btn.classList.add('active');
      document.querySelector('.' + btn.dataset.view).classList.add('active');
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
      console.warn('WebSocket unavailable, falling back to simulator:', e);
      startSimulator();
      return;
    }
    ws.onopen = function() {
      wsConnected = true;
      console.log('WebSocket connected:', url);
    };
    ws.onmessage = function(evt) {
      try {
        var d = JSON.parse(evt.data);
        applyWsData(d);
      } catch (e) {
        console.warn('Bad WS payload:', evt.data);
      }
    };
    ws.onerror = function(e) {
      console.warn('WebSocket error:', e);
    };
    ws.onclose = function() {
      wsConnected = false;
      console.warn('WebSocket closed - connection lost');
      document.getElementById('bus-status-corner').textContent = 'CAN bus disconnected';
      document.getElementById('ecu-status-text').textContent = 'ECU Disconnected';
      document.getElementById('ecu-icon').style.color = '#E74C3C';
    };
  }

  var simulatorRunning = false;
  function startSimulator() {
    if (simulatorRunning || wsConnected) return;
    simulatorRunning = true;
    simulatePids();
  }
  function simulatePids() {
    if (wsConnected) { simulatorRunning = false; return; }
    frameCount += 1;
    blinkLed(ledRx, 'lit-rx');
    if (frameCount % 4 === 0) blinkLed(ledTx, 'lit-tx');
    if (frameCount % 9 === 0) blinkLed(ledKline, 'lit-kline');

    if (!isFrozen) {
      pidState.forEach(function(p) {
        switch (p.id) {
          case 'rpm':     p.value = Math.round(800 + Math.random() * 6200); break;
          case 'speed':   p.value = Math.round(Math.random() * 110); break;
          case 'coolant': p.value = Math.round(75 + Math.random() * 45); break;
          case 'voltage': p.value = (10.5 + Math.random() * 3.5).toFixed(1); break;
          case 'load':    p.value = Math.round(Math.random() * 100); break;
          case 'throttle':p.value = Math.round(Math.random() * 100); break;
        }
        pushHistory(p, p.value);
      });
      renderPidTable();
      updateGauges();
      document.getElementById('frame-count').textContent = frameCount;
    }

    logFrame('RX (sim)', randomCanId(), randomHexByte()+' '+randomHexByte()+' '+randomHexByte()+' '+randomHexByte());

    setTimeout(simulatePids, 600);
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
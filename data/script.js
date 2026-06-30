document.addEventListener('DOMContentLoaded', function() {

  // Clock
  function updateClock() {
    var now = new Date();
    var h = now.getHours(), m = now.getMinutes();
    var ampm = h >= 12 ? 'PM' : 'AM';
    h = h % 12 || 12;
    document.getElementById('clock').textContent = h + ':' + (m < 10 ? '0' + m : m) + ' ' + ampm;
  }
  updateClock();
  setInterval(updateClock, 30000);

  // Left tabs
  document.querySelectorAll('.left-tab').forEach(function(tab) {
    tab.addEventListener('click', function() {
      document.querySelectorAll('.left-tab').forEach(function(t){ t.classList.remove('active'); });
      document.querySelectorAll('.left-section').forEach(function(s){ s.classList.remove('active'); });
      tab.classList.add('active');
      document.getElementById('section-' + tab.dataset.section).classList.add('active');
    });
  });

  // Bottom dock / center panel switching
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

  // Passenger temp
  var passengerTemp = 22;
  document.getElementById('temp-down').addEventListener('click', function() {
    if (passengerTemp > 15) { passengerTemp--; document.getElementById('passenger-temp-value').textContent = passengerTemp; }
  });
  document.getElementById('temp-up').addEventListener('click', function() {
    if (passengerTemp < 30) { passengerTemp++; document.getElementById('passenger-temp-value').textContent = passengerTemp; }
  });

  // Sliders
  [['brightness-slider','brightness-val'],['vol-slider','vol-val'],['bal-slider','bal-val']].forEach(function(pair) {
    var sl = document.getElementById(pair[0]), val = document.getElementById(pair[1]);
    if (sl && val) sl.addEventListener('input', function(){ val.textContent = sl.value; });
  });

  // Speed simulation
  var speed = 63, accel = false;
  var speedEl = document.getElementById('speed-display');
  function simSpeed() {
    if (accel) { speed++; if (speed >= 75) accel = false; }
    else { speed--; if (speed <= 0) { speed = 0; accel = true; } }
    speedEl.textContent = speed;
    setTimeout(simSpeed, 2000);
  }
  simSpeed();

  // Media play/pause
  var isPlaying = true, progress = 30;
  var playBtn = document.getElementById('play-btn');
  var progressFill = document.getElementById('progress-fill');
  playBtn.addEventListener('click', function() {
    isPlaying = !isPlaying;
    playBtn.querySelector('i').className = isPlaying ? 'fas fa-pause' : 'fas fa-play';
  });
  function simProgress() {
    if (isPlaying) { progress += 0.5; if (progress >= 100) progress = 0; progressFill.style.width = progress + '%'; }
    setTimeout(simProgress, 1000);
  }
  simProgress();

  // Session timer
  var sessionSeconds = 48 * 60;
  var sessionEl = document.getElementById('session-time');
  function updateSession() {
    sessionSeconds++;
    var m = Math.floor(sessionSeconds / 60), s = sessionSeconds % 60;
    sessionEl.textContent = (m < 10 ? '0' : '') + m + ':' + (s < 10 ? '0' : '') + s;
    setTimeout(updateSession, 1000);
  }
  updateSession();

});
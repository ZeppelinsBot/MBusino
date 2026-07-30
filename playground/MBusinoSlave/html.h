/*
 * MBusino Slave Simulator - Web GUI HTML
 * Simple page: set address, show stats
 */

#ifndef HTML_H
#define HTML_H

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MBusino Slave Simulator</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      background: #1a1a2e;
      color: #e0e0e0;
      padding: 20px;
      max-width: 500px;
      margin: 0 auto;
    }
    h1 {
      text-align: center;
      color: #00d4ff;
      margin-bottom: 8px;
      font-size: 1.4em;
    }
    .version {
      text-align: center;
      color: #666;
      font-size: 0.8em;
      margin-bottom: 20px;
    }
    .card {
      background: #16213e;
      border-radius: 12px;
      padding: 20px;
      margin-bottom: 16px;
      border: 1px solid #0f3460;
    }
    .card h2 {
      color: #00d4ff;
      font-size: 1em;
      margin-bottom: 12px;
    }
    .form-row {
      display: flex;
      gap: 10px;
      align-items: center;
    }
    input[type="number"] {
      flex: 1;
      padding: 10px;
      border: 2px solid #0f3460;
      border-radius: 8px;
      background: #1a1a2e;
      color: #e0e0e0;
      font-size: 1.2em;
      text-align: center;
      width: 80px;
    }
    input[type="number"]:focus {
      outline: none;
      border-color: #00d4ff;
    }
    button {
      padding: 10px 20px;
      border: none;
      border-radius: 8px;
      font-size: 1em;
      cursor: pointer;
      font-weight: bold;
    }
    .btn-primary {
      background: #00d4ff;
      color: #1a1a2e;
    }
    .btn-primary:hover { background: #00b8d9; }
    .stat-row {
      display: flex;
      justify-content: space-between;
      padding: 6px 0;
      border-bottom: 1px solid #0f3460;
    }
    .stat-row:last-child { border-bottom: none; }
    .stat-label { color: #888; }
    .stat-value { color: #e0e0e0; font-weight: bold; }
    .status-dot {
      display: inline-block;
      width: 10px;
      height: 10px;
      border-radius: 50%;
      margin-right: 6px;
    }
    .dot-green { background: #00e676; }
    .dot-red { background: #ff5252; }
    .current-addr {
      font-size: 2em;
      text-align: center;
      color: #00d4ff;
      font-weight: bold;
      margin: 10px 0;
    }
    .hint {
      text-align: center;
      color: #666;
      font-size: 0.8em;
      margin-top: 10px;
    }
    .flash-msg {
      text-align: center;
      padding: 8px;
      border-radius: 8px;
      margin-top: 10px;
      display: none;
    }
    .flash-ok { background: #1b5e20; color: #a5d6a7; }
    .flash-err { background: #b71c1c; color: #ef9a9a; }
    a { color: #00d4ff; }
  </style>
</head>
<body>
  <h1>MBusino Slave Simulator</h1>
  <div class="version">%s &bull; <a href="/update">OTA Update</a></div>

  <div class="card">
    <h2>Slave Address</h2>
    <div class="current-addr" id="currentAddr">%d</div>
    <div class="form-row">
      <input type="number" id="newAddr" min="1" max="254" placeholder="1-254">
      <button class="btn-primary" onclick="setAddress()">Set</button>
    </div>
    <div id="flashMsg" class="flash-msg"></div>
  </div>

  <div class="card">
    <h2>WiFi Settings</h2>
    <div class="form-row">
      <input type="text" id="wifiSsid" placeholder="SSID" style="flex:2">
      <input type="password" id="wifiPass" placeholder="Password" style="flex:2">
    </div>
    <div style="margin-top:10px">
      <button class="btn-primary" onclick="setWifi()" style="width:100%">Save & Reboot</button>
    </div>
    <div id="wifiFlashMsg" class="flash-msg"></div>
  </div>

  <div class="card">
    <h2>Statistics</h2>
    <div class="stat-row">
      <span class="stat-label">Total Requests</span>
      <span class="stat-value" id="reqCount">-</span>
    </div>
    <div class="stat-row">
      <span class="stat-label">Normalize (NKE)</span>
      <span class="stat-value" id="nkeCount">-</span>
    </div>
    <div class="stat-row">
      <span class="stat-label">Data Requests</span>
      <span class="stat-value" id="dataCount">-</span>
    </div>
    <div class="stat-row">
      <span class="stat-label">Bad Frames</span>
      <span class="stat-value" id="badCount">-</span>
    </div>
    <div class="stat-row">
      <span class="stat-label">Addr Mismatches</span>
      <span class="stat-value" id="mismatchCount">-</span>
    </div>
    <div class="stat-row">
      <span class="stat-label">Last Request</span>
      <span class="stat-value" id="lastReq">-</span>
    </div>
    <div class="stat-row">
      <span class="stat-label">WiFi</span>
      <span class="stat-value" id="wifiStatus">-</span>
    </div>
    <div class="stat-row">
      <span class="stat-label">Free Heap</span>
      <span class="stat-value" id="freeHeap">-</span>
    </div>
  </div>

  <script>
    function setAddress() {
      var addr = document.getElementById('newAddr').value;
      if (!addr || addr < 1 || addr > 254) {
        showFlash('Invalid address (1-254)', true);
        return;
      }
      fetch('/setAddress?addr=' + addr)
        .then(r => r.text())
        .then(t => {
          if (t === 'ok') {
            document.getElementById('currentAddr').innerText = addr;
            document.getElementById('newAddr').value = '';
            showFlash('Address set to ' + addr, false);
            setTimeout(function(){ location.reload(); }, 1500);
          } else {
            showFlash('Error: ' + t, true);
          }
        })
        .catch(e => showFlash('Error: ' + e, true));
    }

    function showFlash(msg, isErr) {
      var el = document.getElementById('flashMsg');
      el.innerText = msg;
      el.className = 'flash-msg ' + (isErr ? 'flash-err' : 'flash-ok');
      el.style.display = 'block';
      setTimeout(function(){ el.style.display = 'none'; }, 3000);
    }

    function setWifi() {
      var ssid = document.getElementById('wifiSsid').value;
      var pass = document.getElementById('wifiPass').value;
      if (!ssid) { showWifiFlash('SSID required', true); return; }
      fetch('/setWifi?ssid=' + encodeURIComponent(ssid) + '&pass=' + encodeURIComponent(pass))
        .then(function(r) { return r.text(); })
        .then(function(t) {
          if (t === 'ok') {
            showWifiFlash('Saved! Rebooting...', false);
          } else {
            showWifiFlash('Error: ' + t, true);
          }
        })
        .catch(function(e) { showWifiFlash('Error: ' + e, true); });
    }

    function showWifiFlash(msg, isErr) {
      var el = document.getElementById('wifiFlashMsg');
      el.innerText = msg;
      el.className = 'flash-msg ' + (isErr ? 'flash-err' : 'flash-ok');
      el.style.display = 'block';
      if (!isErr) setTimeout(function(){ el.style.display = 'none'; }, 5000);
    }

    function refreshStats() {
      fetch('/stats')
        .then(r => r.json())
        .then(d => {
          document.getElementById('reqCount').innerText = d.requests;
          document.getElementById('nkeCount').innerText = d.normalize;
          document.getElementById('dataCount').innerText = d.dataRequests;
          document.getElementById('badCount').innerText = d.badFrames;
          document.getElementById('mismatchCount').innerText = d.mismatches;
          document.getElementById('wifiStatus').innerText = d.wifi;
          document.getElementById('freeHeap').innerText = d.freeHeap + ' B';
          if (d.lastRequest > 0) {
            document.getElementById('lastReq').innerText = d.lastRequest + 'ms ago';
          } else {
            document.getElementById('lastReq').innerText = 'never';
          }
        })
        .catch(e => {});
    }

    refreshStats();
    setInterval(refreshStats, 2000);
  </script>
</body>
</html>
)rawliteral";

const char update_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MBusino Slave - OTA Update</title>
  <style>
    body { font-family: sans-serif; background: #1a1a2e; color: #e0e0e0; padding: 40px; text-align: center; }
    h1 { color: #00d4ff; margin-bottom: 20px; }
    input[type="file"] { margin: 20px 0; }
    button { padding: 12px 30px; font-size: 1em; border: none; border-radius: 8px; background: #00d4ff; color: #1a1a2e; font-weight: bold; cursor: pointer; }
    #status { margin-top: 20px; font-size: 1.1em; }
    a { color: #00d4ff; }
  </style>
</head>
<body>
  <h1>OTA Firmware Update</h1>
  <p>Select .bin file and upload</p>
  <form id="uploadForm">
    <input type="file" id="firmware" accept=".bin">
    <br>
    <button type="submit">Upload</button>
  </form>
  <div id="status"></div>
  <p style="margin-top:20px"><a href="/">Back to Slave</a></p>
  <script>
    document.getElementById('uploadForm').addEventListener('submit', function(e) {
      e.preventDefault();
      var file = document.getElementById('firmware').files[0];
      if (!file) { alert('Select a file'); return; }
      var fd = new FormData();
      fd.append('update', file);
      var status = document.getElementById('status');
      status.innerText = 'Uploading...';
      fetch('/update', { method: 'POST', body: fd })
        .then(r => r.text())
        .then(t => { status.innerText = t; })
        .catch(e => { status.innerText = 'Error: ' + e; });
    });
  </script>
</body>
</html>
)rawliteral";

const size_t index_htmlLength = sizeof(index_html);
const size_t update_htmlLength = sizeof(update_html);

#endif // HTML_H

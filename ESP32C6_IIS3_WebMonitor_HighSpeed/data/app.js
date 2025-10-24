// Chart Configuration
const maxPoints = 5000;
const buffers = { x: [], y: [], z: [] };
let selectedScale = 'auto';
let manualScale = 2;
let isPaused = false;

// Metrics elements
const metrics = {
  pps: document.getElementById('metricPps'),
  sps: document.getElementById('metricSps'),
  fifo: document.getElementById('metricFifo'),
  batch: document.getElementById('metricBatch'),
  mag: document.getElementById('metricMag'),
  fs: document.getElementById('metricFs')
};

const connectionStatus = document.getElementById('connectionStatus');
const deviceIp = document.getElementById('deviceIp');
const eventLog = document.getElementById('eventLog');

// Canvas setup
const canvas = document.getElementById('accelChart');
const ctx = canvas.getContext('2d');
const dpr = window.devicePixelRatio || 1;
let drawHandle = null;

// Event log
function addLog(message) {
  const now = new Date();
  const timestamp = now.toLocaleTimeString();
  const entry = document.createElement('div');
  entry.textContent = `[${timestamp}] ${message}`;
  eventLog.insertBefore(entry, eventLog.firstChild);
  
  // Keep max 50 entries
  while (eventLog.children.length > 50) {
    eventLog.removeChild(eventLog.lastChild);
  }
}

// Canvas resizing
function resizeCanvas() {
  const width = canvas.clientWidth || canvas.width;
  const height = canvas.clientHeight || canvas.height;
  canvas.width = Math.max(1, width * dpr);
  canvas.height = Math.max(1, height * dpr);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  scheduleDraw();
}

window.addEventListener('resize', () => resizeCanvas());
resizeCanvas();

// Utilities
function formatNumber(val, digits) {
  if (val === null || val === undefined || !isFinite(val)) return '-';
  return Number(val).toFixed(digits);
}

// Chart range calculation
function computeRange() {
  if (selectedScale !== 'auto') {
    return [-manualScale, manualScale];
  }
  
  let min = Infinity;
  let max = -Infinity;
  
  for (const key of ['x', 'y', 'z']) {
    for (const val of buffers[key]) {
      if (!Number.isFinite(val)) continue;
      if (val < min) min = val;
      if (val > max) max = val;
    }
  }
  
  if (!Number.isFinite(min) || !Number.isFinite(max)) {
    return [-1, 1];
  }
  
  if (min === max) {
    const pad = Math.max(Math.abs(min) * 0.1, 0.1);
    return [min - pad, min + pad];
  }
  
  const span = max - min;
  const pad = span * 0.1 || 0.1;
  return [min - pad, max + pad];
}

// Drawing
function scheduleDraw() {
  if (drawHandle !== null) return;
  drawHandle = requestAnimationFrame(() => {
    drawHandle = null;
    drawChart();
  });
}

function drawChart() {
  const width = canvas.clientWidth || canvas.width / dpr;
  const height = canvas.clientHeight || canvas.height / dpr;
  
  console.log('DEBUG: drawChart called - canvas size:', width, 'x', height, 'buffer lengths:', buffers.x.length, buffers.y.length, buffers.z.length);
  
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, width, height);
  
  // Background
  ctx.fillStyle = '#f8fafc';
  ctx.fillRect(0, 0, width, height);
  
  const [minVal, maxVal] = computeRange();
  const span = maxVal - minVal || 1;
  
  console.log('DEBUG: Y-axis range:', minVal, 'to', maxVal, 'span:', span);
  
  const colors = { x: '#ef4444', y: '#10b981', z: '#3b82f6' };
  const gridCount = 4;
  
  // Grid lines and labels
  ctx.strokeStyle = 'rgba(148,163,184,0.3)';
  ctx.lineWidth = 1;
  ctx.font = '11px sans-serif';
  ctx.fillStyle = '#475569';
  
  for (let i = 0; i <= gridCount; i++) {
    const ratio = i / gridCount;
    const y = height - ratio * height;
    
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(width, y);
    ctx.stroke();
    
    const value = minVal + ratio * span;
    ctx.fillText(formatNumber(value, 2), 4, Math.min(height - 4, Math.max(12, y - 2)));
  }
  
  // Plot lines
  ctx.lineWidth = 1.4;
  
  for (const key of ['x', 'y', 'z']) {
    const data = buffers[key];
    if (data.length < 2) continue;
    
    ctx.strokeStyle = colors[key];
    ctx.beginPath();
    
    for (let i = 0; i < data.length; i++) {
      const x = (i / (maxPoints - 1)) * width;
      const val = data[i];
      const ratio = (maxVal - val) / span;
      const y = height - ratio * height;
      
      if (i === 0) {
        ctx.moveTo(x, y);
      } else {
        ctx.lineTo(x, y);
      }
    }
    
    ctx.stroke();
  }
}

// Scale controls
const scaleControls = document.getElementById('scaleControls');
// Only select buttons that have a data-scale attribute (exclude Pause/Resume button)
const scaleButtons = scaleControls ? Array.from(scaleControls.querySelectorAll('button[data-scale]')) : [];

function applyScale(mode, opts) {
  const options = opts || {};
  
  // Validate mode before assigning to selectedScale to avoid invalid states
  if (mode !== 'auto') {
    const parsed = Number(mode);
    if (!Number.isFinite(parsed)) {
      addLog('Invalid scale ' + mode);
      return;
    }
    manualScale = parsed;
  }
  selectedScale = mode;
  
  if (scaleButtons.length) {
    scaleButtons.forEach(btn => btn.classList.toggle('active', btn.dataset.scale === mode));
  }
  
  if (mode === 'auto') {
    metrics.fs.textContent = 'auto';
  } else {
    metrics.fs.textContent = '+/-' + manualScale + 'g';
  }
  
  if (!options.silent) {
    addLog('Chart scale set to ' + (mode === 'auto' ? 'auto' : ('+/-' + manualScale + 'g')));
  }
  
  scheduleDraw();
}

if (scaleButtons.length) {
  applyScale('auto', { silent: true });
  
  scaleButtons.forEach(btn => {
    btn.addEventListener('click', () => {
      const target = btn.dataset.scale;
      
      if (target === 'auto') {
        applyScale('auto');
        return;
      }
      
      if (selectedScale === target) return;
      
      scaleButtons.forEach(b => b.classList.remove('loading'));
      btn.classList.add('loading');
      
      fetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ full_scale_g: Number(target) })
      })
      .then(resp => {
        if (!resp.ok) throw new Error('HTTP ' + resp.status);
        return resp.json();
      })
      .then(cfg => {
        const applied = cfg && cfg.full_scale_g ? String(cfg.full_scale_g) : target;
        applyScale(applied, { silent: true });
      })
      .catch(err => {
        addLog('Scale update failed: ' + err.message);
      })
      .finally(() => {
        btn.classList.remove('loading');
      });
    });
  });
} else {
  applyScale('auto', { silent: true });
}

// Pause/Resume button handler
const pauseResumeBtn = document.getElementById('pauseResumeBtn');
if (pauseResumeBtn) {
  pauseResumeBtn.addEventListener('click', () => {
    isPaused = !isPaused;
    
    if (isPaused) {
      pauseResumeBtn.textContent = 'Resume';
      pauseResumeBtn.classList.add('paused');
      addLog('Chart paused');
    } else {
      pauseResumeBtn.textContent = 'Pause';
      pauseResumeBtn.classList.remove('paused');
      addLog('Chart resumed');
      scheduleDraw(); // Resume drawing immediately
    }
  });
}

// Config refresh
function refreshConfig() {
  fetch('/api/config')
    .then(resp => {
      if (!resp.ok) throw new Error('HTTP ' + resp.status);
      return resp.json();
    })
    .then(cfg => {
      if (cfg && cfg.imu_full_scale_g) {
        const fsVal = String(cfg.imu_full_scale_g);
        if (selectedScale === 'auto') {
          metrics.fs.textContent = '+/-' + fsVal + 'g';
        } else {
          applyScale(fsVal, { silent: true });
        }
      }
    })
    .catch(err => {
      addLog('Config fetch failed: ' + err.message);
    });
}

refreshConfig();

// Data processing
function pushValues(payload) {
  if (!payload || !payload.chunks) {
    console.log('DEBUG: No payload or chunks', payload);
    return;
  }
  
  // Update metrics even when paused
  if (payload.mag !== undefined) {
    metrics.mag.textContent = formatNumber(payload.mag, 3);
  }
  
  if (payload.fs !== undefined && selectedScale === 'auto') {
    metrics.fs.textContent = '+/-' + payload.fs + 'g';
  }
  
  if (payload.s) {
    const s = payload.s;
    if (s.pps !== undefined && metrics.pps) metrics.pps.textContent = formatNumber(s.pps, 1);
    if (s.sps !== undefined && metrics.sps) metrics.sps.textContent = formatNumber(s.sps, 0);
    if (s.fifo !== undefined && metrics.fifo) metrics.fifo.textContent = s.fifo;
    if (s.batch !== undefined && metrics.batch) metrics.batch.textContent = s.batch;
  }
  
  // Skip adding data to buffers if paused
  if (isPaused) {
    console.log('DEBUG: Chart paused, skipping data');
    return;
  }
  
  const chunk = payload.chunks;
  const xs = Array.isArray(chunk.x) ? chunk.x : [];
  const ys = Array.isArray(chunk.y) ? chunk.y : [];
  const zs = Array.isArray(chunk.z) ? chunk.z : [];
  
  const len = Math.min(xs.length, ys.length, zs.length);
  console.log('DEBUG: Received chunk data - x:', xs.length, 'y:', ys.length, 'z:', zs.length, 'min:', len);
  
  if (!len) {
    console.log('DEBUG: No data in chunks');
    return;
  }
  
  for (let i = 0; i < len; i++) {
    if (buffers.x.length >= maxPoints) {
      buffers.x.shift();
      buffers.y.shift();
      buffers.z.shift();
    }
    
    buffers.x.push(Number(xs[i]));
    buffers.y.push(Number(ys[i]));
    buffers.z.push(Number(zs[i]));
  }
  
  console.log('DEBUG: Buffer sizes after push - x:', buffers.x.length, 'y:', buffers.y.length, 'z:', buffers.z.length);
  scheduleDraw();
}

// WebSocket
let ws = null;
let wsUrl = '';

function getDeviceIp() {
  return fetch('/api/stats')
    .then(resp => {
      if (!resp.ok) throw new Error('HTTP ' + resp.status);
      return resp.json();
    })
    .then(data => {
      if (data && data.ip) {
        deviceIp.textContent = 'IP: ' + data.ip;
        addLog('Device IP: ' + data.ip);
        return data.ip;
      }
      return window.location.hostname;
    })
    .catch(err => {
      addLog('Failed to get device IP: ' + err.message);
      return window.location.hostname;
    });
}

function connectWebSocket(ip) {
  wsUrl = 'ws://' + ip + '/ws/data';
  addLog('Connecting to ' + wsUrl);
  
  ws = new WebSocket(wsUrl);
  
  ws.onopen = () => {
    connectionStatus.textContent = 'Connected';
    connectionStatus.style.color = '#10b981';
    addLog('WebSocket connected from ' + ip);
  };
  
  ws.onmessage = (event) => {
    try {
      console.log('DEBUG: WebSocket message received, length:', event.data.length);
      const data = JSON.parse(event.data);
      console.log('DEBUG: Parsed JSON data:', data);
      pushValues(data);
    } catch (err) {
      addLog('Parse error: ' + err.message);
      console.error('DEBUG: Parse error:', err, 'Raw data:', event.data);
    }
  };
  
  ws.onerror = (err) => {
    connectionStatus.textContent = 'Error';
    connectionStatus.style.color = '#ef4444';
    addLog('WebSocket error');
  };
  
  ws.onclose = () => {
    connectionStatus.textContent = 'Disconnected';
    connectionStatus.style.color = '#f59e0b';
    addLog('WebSocket closed');
    
    // Reconnect after 3s
    setTimeout(() => {
      getDeviceIp().then(ip => connectWebSocket(ip));
    }, 3000);
  };
}

// Initialize
addLog('Waiting for IIS3DWB data...');
getDeviceIp().then(ip => connectWebSocket(ip));

// Debug helpers - accessible from browser console
window.debugChart = {
  getBuffers: () => ({
    x: buffers.x.slice(0, 10), // First 10 samples
    y: buffers.y.slice(0, 10),
    z: buffers.z.slice(0, 10),
    lengths: { x: buffers.x.length, y: buffers.y.length, z: buffers.z.length }
  }),
  forceRedraw: () => {
    console.log('Forcing redraw...');
    drawChart();
  },
  injectTestData: () => {
    console.log('Injecting test data...');
    for (let i = 0; i < 100; i++) {
      buffers.x.push(Math.sin(i * 0.1));
      buffers.y.push(Math.cos(i * 0.1));
      buffers.z.push(Math.sin(i * 0.05) * 0.5);
    }
    console.log('Test data injected. Buffer lengths:', buffers.x.length);
    scheduleDraw();
  },
  clearBuffers: () => {
    buffers.x = [];
    buffers.y = [];
    buffers.z = [];
    console.log('Buffers cleared');
    scheduleDraw();
  },
  isPaused: () => isPaused,
  togglePause: () => {
    isPaused = !isPaused;
    console.log('isPaused:', isPaused);
  }
};

console.log('DEBUG: Debug helpers loaded. Use window.debugChart in console.');
console.log('Available commands: getBuffers(), forceRedraw(), injectTestData(), clearBuffers(), isPaused(), togglePause()');

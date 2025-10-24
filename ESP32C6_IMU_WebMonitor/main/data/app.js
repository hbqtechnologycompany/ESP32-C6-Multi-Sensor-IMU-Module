(() => {
  const statusEl = document.getElementById('status');
  const statusText = document.getElementById('statusText');
  const statusIp = document.getElementById('statusIp');
  const logEl = document.getElementById('log');
  const pauseButton = document.getElementById('pauseButton');
  const metrics = {
    msg: document.getElementById('metricMsg'),
    sensors: document.getElementById('metricSensors'),
    msgRate: document.getElementById('metricMsgRate')
  };

  statusIp.textContent = window.location.host || window.location.hostname || '-';
  document.title = `ESP32-C6 - ${statusIp.textContent}`;

  const chartDefinitions = [
    {
      key: 'mag_iis2',
      containerId: 'chart-mag',
      defaultTitle: 'IIS2MDC Magnetometer',
      defaultUnit: 'mG',
      labels: ['X', 'Y', 'Z'],
      colors: ['#ef4444', '#22c55e', '#3b82f6']
    },
    {
      key: 'gyr_icm_rate',
      containerId: 'chart-icm-gyro',
      defaultTitle: 'ICM45686 Gyroscope Rate',
      defaultUnit: 'rad/s',
      labels: ['Rate X', 'Rate Y', 'Rate Z'],
      colors: ['#f97316', '#a855f7', '#0ea5e9']
    },
    {
      key: 'inc_scl',
      containerId: 'chart-scl',
      defaultTitle: 'SCL3300 Inclinometer',
      defaultUnit: 'deg',
      labels: ['Angle X', 'Angle Y', 'Angle Z'],
      colors: ['#22c55e', '#3b82f6', '#1e293b']
    },
    {
      key: 'acc_icm',
      containerId: 'chart-icm-accel',
      defaultTitle: 'ICM45686 Tilt',
      defaultUnit: 'deg',
      labels: ['Tilt X', 'Tilt Y', 'Tilt Z'],
      colors: ['#3b82f6', '#22c55e', '#1e293b']
    },
    {
      key: 'acc_iis3_g',
      containerId: 'chart-iis3-g',
      defaultTitle: 'IIS3DWB Acceleration',
      defaultUnit: 'g',
      labels: ['Accel X', 'Accel Y', 'Accel Z'],
      colors: ['#fb7185', '#22c55e', '#6366f1']
    },
    {
      key: 'acc_iis3_ms2',
      containerId: 'chart-iis3-ms2',
      defaultTitle: 'IIS3DWB Acceleration',
      defaultUnit: 'm/s²',
      labels: ['Accel X', 'Accel Y', 'Accel Z'],
      colors: ['#f97316', '#22c55e', '#3b82f6']
    }
  ];

  const sensorGroups = {
    IIS2MDC: ['mag_iis2'],
    IIS3DWB: ['acc_iis3_g', 'acc_iis3_ms2'],
    ICM45686: ['acc_icm', 'gyr_icm_rate'],
    SCL3300: ['inc_scl']
  };

  const extractors = {
    mag_iis2: payload => payload ? { values: [payload.x, payload.y, payload.z], unit: payload.unit, name: payload.name } : null,
    acc_icm: payload => payload ? { values: [payload.x, payload.y, payload.z], unit: payload.unit, name: payload.name, labels: ['Tilt X', 'Tilt Y', 'Tilt Z'] } : null,
    inc_scl: payload => payload ? { values: [payload.angle_x, payload.angle_y, payload.angle_z], unit: payload.unit, name: payload.name, labels: ['Angle X', 'Angle Y', 'Angle Z'] } : null,
    gyr_icm_rate: payload => payload ? { values: [payload.x, payload.y, payload.z], unit: payload.unit, name: payload.name, labels: ['Rate X', 'Rate Y', 'Rate Z'] } : null,
    acc_iis3_g: payload => payload ? { values: [payload.x, payload.y, payload.z], unit: payload.unit, name: payload.name, labels: ['Accel X', 'Accel Y', 'Accel Z'] } : null,
    acc_iis3_ms2: payload => payload ? { values: [payload.x, payload.y, payload.z], unit: payload.unit, name: payload.name, labels: ['Accel X', 'Accel Y', 'Accel Z'] } : null
  };

  const charts = {};
  const maxPoints = 120;
  let msgCount = 0;
  let paused = false;

  const chartOptions = {
    type: 'line',
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: false,
      interaction: {
        mode: 'nearest',
        intersect: false
      },
      plugins: {
        legend: {
          position: 'bottom',
          labels: {
            usePointStyle: true
          }
        }
      },
      scales: {
        x: {
          display: false
        },
        y: {
          beginAtZero: false,
          grid: {
            color: 'rgba(226,232,240,0.9)'
          }
        }
      }
    }
  };

  function addLog(message) {
    const entry = document.createElement('div');
    entry.textContent = `[${new Date().toLocaleTimeString()}] ${message}`;
    logEl.prepend(entry);
    while (logEl.childNodes.length > 200) {
      logEl.removeChild(logEl.lastChild);
    }
  }

  function setStatus(state) {
    switch (state) {
      case 'connected':
        statusEl.style.borderLeftColor = 'var(--success)';
        statusText.textContent = 'Connected';
        break;
      case 'error':
        statusEl.style.borderLeftColor = 'var(--error)';
        statusText.textContent = 'Error';
        break;
      default:
        statusEl.style.borderLeftColor = 'var(--warn)';
        statusText.textContent = 'Connecting...';
    }
  }

  function buildCharts() {
    chartDefinitions.forEach(def => {
      const container = document.getElementById(def.containerId);
      if (!container) {
        console.warn(`Missing container for chart ${def.key}`);
        return;
      }
      const canvas = container.querySelector('canvas');
      const titleEl = container.querySelector('h3');
      const datasets = def.labels.map((label, index) => ({
        label,
        data: [],
        borderColor: def.colors[index % def.colors.length],
        borderWidth: 1.8,
        pointRadius: 0
      }));
      charts[def.key] = {
        def,
        titleEl,
        chart: new Chart(canvas.getContext('2d'), {
          ...chartOptions,
          data: {
            labels: [],
            datasets
          }
        })
      };
    });
    metrics.sensors.textContent = '0';
  }

  function updateTitle(key, payloadInfo) {
    const chartInfo = charts[key];
    if (!chartInfo) {
      return;
    }
    const { def, titleEl } = chartInfo;
    const unit = payloadInfo.unit || def.defaultUnit;
    const base = payloadInfo.name || def.defaultTitle;
    titleEl.textContent = unit ? `${base} (${unit})` : base;
  }

  function pushValues(key, payloadInfo) {
    const chartInfo = charts[key];
    if (!chartInfo) {
      return;
    }
    const { chart } = chartInfo;
    const { values } = payloadInfo;
    if (!Array.isArray(values) || values.length === 0) {
      return;
    }
    const label = msgCount;
    chart.data.labels.push(label);
    chart.data.datasets.forEach((dataset, index) => {
      dataset.data.push(values[index] ?? null);
      if (dataset.data.length > maxPoints) {
        dataset.data.shift();
      }
    });
    if (chart.data.labels.length > maxPoints) {
      chart.data.labels.shift();
    }
    chart.update('none');
  }

  pauseButton.addEventListener('click', () => {
    paused = !paused;
    pauseButton.textContent = paused ? 'Resume' : 'Pause';
    pauseButton.classList.toggle('paused', paused);
    addLog(paused ? 'Paused chart updates' : 'Resumed chart updates');
  });

  function countActiveSensors(payload) {
    return Object.values(sensorGroups).reduce((count, keys) => {
      const hasData = keys.some(key => {
        const segment = payload[key];
        return segment && typeof segment === 'object' && Object.keys(segment).length > 0;
      });
      return count + (hasData ? 1 : 0);
    }, 0);
  }

  function handlePayload(payload) {
    if (payload.ip) {
      statusIp.textContent = payload.ip;
      document.title = `ESP32-C6 - ${payload.ip}`;
      addLog(`Updated IP: ${payload.ip}`);
      return;
    }

    msgCount += 1;
    metrics.msg.textContent = msgCount.toString();

    if (payload.statistics && typeof payload.statistics.msg_per_second === 'number') {
      metrics.msgRate.textContent = payload.statistics.msg_per_second.toFixed(1);
    }

    const enabledSensors = payload.statistics && Number.isFinite(payload.statistics.sensor_count) ? payload.statistics.sensor_count : countActiveSensors(payload);
    metrics.sensors.textContent = enabledSensors.toString();

    chartDefinitions.forEach(def => {
      const extractor = extractors[def.key];
      if (!extractor) {
        return;
      }
      const info = extractor(payload[def.key]);
      if (!info) {
        return;
      }
      updateTitle(def.key, info);
      if (!paused) {
        pushValues(def.key, info);
      }
    });
  }

  function connectWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
    const wsUrl = `${protocol}://${window.location.host}/ws/data`;
    addLog(`Connecting to ${wsUrl}`);

    const ws = new WebSocket(wsUrl);

    ws.onopen = () => {
      addLog('WebSocket connected');
      setStatus('connected');
    };

    ws.onerror = () => {
      addLog('WebSocket error');
      setStatus('error');
    };

    ws.onclose = () => {
      addLog('WebSocket closed');
      setStatus('pending');
      setTimeout(connectWebSocket, 2000);
    };

    ws.onmessage = event => {
      try {
        const payload = JSON.parse(event.data);
        handlePayload(payload);
      } catch (error) {
        addLog(`Parse error: ${error.message}`);
      }
    };
  }

  buildCharts();
  addLog('Dashboard ready');
  connectWebSocket();

  fetch('/api/ip')
    .then(response => response.json())
    .then(data => {
      if (data && data.ip) {
        statusIp.textContent = data.ip;
        document.title = `ESP32-C6 - ${data.ip}`;
        addLog(`REST IP: ${data.ip}`);
      }
    })
    .catch(() => {
      /* optional */
    });
})();

(() => {
  const fileInput = document.getElementById('csvInput');
  const canvas = document.getElementById('filamentCanvas');
  const ctx = canvas.getContext('2d');
  const playButton = document.getElementById('playButton');
  const pauseButton = document.getElementById('pauseButton');
  const stepButton = document.getElementById('stepButton');
  const speedInput = document.getElementById('speedRange');
  const speedLabel = document.getElementById('speedLabel');
  const testSelect = document.getElementById('testSelect');
  const status = document.getElementById('status');
  const frameInfo = document.getElementById('frameInfo');

  let datasetAll = [];
  let dataset = [];
  let timerId = null;
  let currentIndex = 0;
  let intervalMs = Number(speedInput.value);
  let maxFlow = 1;

  function parseCsvLine(line) {
    const values = [];
    let current = '';
    let inQuotes = false;
    for (let i = 0; i < line.length; i++) {
      const char = line[i];
      if (char === '"') {
        if (inQuotes && line[i + 1] === '"') {
          current += '"';
          i += 1;
        } else {
          inQuotes = !inQuotes;
        }
      } else if (char === ',' && !inQuotes) {
        values.push(current);
        current = '';
      } else {
        current += char;
      }
    }
    values.push(current);
    return values;
  }

  function parseCsvText(text) {
    const lines = text
      .split(/\r?\n/)
      .map((row) => row.trim())
      .filter(Boolean);
    if (!lines.length) return [];
    const rows = lines.slice(1);
    return rows
      .map((line) => {
        const cells = parseCsvLine(line);
        if (cells.length < 8) return null;
        return {
          test: cells[0],
          label: cells[1],
          timestamp: Number(cells[2]) || 0,
          expected: Number(cells[3]) || 0,
          actual: Number(cells[4]) || 0,
          deficit: Number(cells[5]) || 0,
          ratio: Number(cells[6]) || 0,
          jam: cells[7] === '1' || cells[7].toLowerCase() === 'true',
        };
      })
      .filter(Boolean);
  }

  function updateStatus(text) {
    status.textContent = text;
  }

  function enableControls(enabled) {
    [playButton, pauseButton, stepButton].forEach((button) => {
      button.disabled = !enabled;
    });
    testSelect.disabled = !enabled;
  }

  function recalcMaxFlow() {
    if (!dataset.length) {
      maxFlow = 1;
      return;
    }
    maxFlow = Math.max(...dataset.map((entry) => Math.max(entry.expected, entry.actual, 1)));
  }

  function populateTestSelect() {
    const uniqueTests = [...new Set(datasetAll.map((entry) => entry.test))];
    testSelect.innerHTML = '<option value="all">All tests</option>';
    uniqueTests.forEach((test) => {
      const option = document.createElement('option');
      option.value = test;
      option.textContent = test;
      testSelect.appendChild(option);
    });
  }

  function applyTestFilter() {
    const selected = testSelect.value;
    if (selected && selected !== 'all') {
      dataset = datasetAll.filter((entry) => entry.test === selected);
    } else {
      dataset = datasetAll.slice();
    }
    currentIndex = 0;
    recalcMaxFlow();
    stopPlayback();
    if (dataset.length) {
      advanceFrame();
      updateStatus(selected && selected !== 'all'
        ? `Showing ${selected} (${dataset.length} frames)`
        : `Showing all tests (${dataset.length} frames)`);
    } else {
      updateStatus('No frames available for this test.');
    }
  }

  function stopPlayback() {
    if (timerId) {
      clearInterval(timerId);
      timerId = null;
      updateStatus('Paused');
    }
  }

  function renderFrame(index) {
    if (!dataset.length) return;
    const entry = dataset[index];
    const barWidth = canvas.width - 80;
    const expectedWidth = Math.min(1, entry.expected / maxFlow) * barWidth;
    const actualWidth = Math.min(1, entry.actual / maxFlow) * barWidth;

    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = '#1a2134';
    ctx.fillRect(40, 70, barWidth, 60);
    ctx.fillStyle = '#2c2f3e';
    ctx.fillRect(40, 70, expectedWidth, 60);
    ctx.fillStyle = entry.jam ? '#d9534f' : '#68d464';
    ctx.fillRect(40, 70, actualWidth, 60);
    ctx.lineWidth = 2;
    ctx.strokeStyle = '#ffffff';
    ctx.strokeRect(40, 70, barWidth, 60);
    const indicatorX = 40 + Math.min(actualWidth, Math.max(barWidth - 10, 0));
    ctx.beginPath();
    ctx.arc(indicatorX, 100, 8, 0, Math.PI * 2);
    ctx.fillStyle = entry.jam ? '#ffbcbc' : '#99ffb9';
    ctx.fill();

    frameInfo.innerHTML = `<strong>${entry.test}</strong> &mdash; ${entry.label}<br>` +
      `Time: ${entry.timestamp} ms &middot; Expected: ${entry.expected.toFixed(2)} mm &middot; Actual: ${entry.actual.toFixed(2)} mm<br>` +
      `Deficit: ${entry.deficit.toFixed(2)} mm &middot; Ratio: ${(entry.ratio * 100).toFixed(1)}% &middot; ` +
      `${entry.jam ? '<span class="jam">JAM</span>' : '<span class="ok">OK</span>'}`;
  }

  function advanceFrame() {
    if (!dataset.length) return;
    if (currentIndex >= dataset.length) {
      currentIndex = 0;
    }
    renderFrame(currentIndex);
    currentIndex = (currentIndex + 1) % dataset.length;
  }

  function startPlayback() {
    if (!dataset.length || timerId) return;
    timerId = setInterval(advanceFrame, intervalMs);
    updateStatus('Playing');
  }

  fileInput.addEventListener('change', (event) => {
    const file = event.target.files && event.target.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => {
      const text = typeof reader.result === 'string' ? reader.result : '';
      datasetAll = parseCsvText(text);
      if (!datasetAll.length) {
        updateStatus('No samples found in the CSV.');
        enableControls(false);
        return;
      }
      populateTestSelect();
      testSelect.value = 'all';
      dataset = datasetAll.slice();
      recalcMaxFlow();
      currentIndex = 0;
      stopPlayback();
      advanceFrame();
      updateStatus(`Loaded ${dataset.length} frames from ${file.name}`);
      enableControls(true);
    };
    reader.readAsText(file);
  });

  playButton.addEventListener('click', () => {
    startPlayback();
  });

  pauseButton.addEventListener('click', () => {
    stopPlayback();
  });

  stepButton.addEventListener('click', () => {
    stopPlayback();
    advanceFrame();
  });

  testSelect.addEventListener('change', () => {
    if (!datasetAll.length) return;
    applyTestFilter();
  });

  speedInput.addEventListener('input', (event) => {
    intervalMs = Number(event.target.value) || 600;
    speedLabel.textContent = `${intervalMs} ms/frame`;
    if (timerId) {
      clearInterval(timerId);
      timerId = setInterval(advanceFrame, intervalMs);
    }
  });
})();

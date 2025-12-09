import { initWifiPatcher } from './wifiPatcher.js';
import { EspFlasher, FLASH_STATES } from './flasher.js';

const selectors = {
    boardSelect: document.getElementById('boardSelect'),
    boardStatus: document.getElementById('boardStatus'),
    boardVersion: document.getElementById('boardVersion'),
    boardRelease: document.getElementById('boardRelease'),
    notesList: document.getElementById('notesList'),
    releaseNotesTitle: document.getElementById('releaseNotesTitle'),
    flashTrigger: document.getElementById('flashTrigger'),
    downloadOtaBtn: document.getElementById('downloadOtaBtn'),
    logStream: document.getElementById('logStream'),
    copyLogBtn: document.getElementById('copyLogBtn'),
    clearLogBtn: document.getElementById('clearLogBtn'),
    heroLabel: document.getElementById('activeFirmwareLabel'),
    heroDate: document.getElementById('activeFirmwareDate'),
    wifiForm: document.getElementById('wifiPatchForm'),
    wifiStatus: document.getElementById('wifiPatchStatus'),
    wifiAcceptBtn: document.getElementById('wifiAcceptBtn'),
    wifiResetBtn: document.getElementById('wifiResetBtn'),
    wifiPatchDialog: document.getElementById('wifiPatchDialog'),
    // Warning dialog elements
    warningDialog: document.getElementById('warningDialog'),
    eraseDeviceCheckbox: document.getElementById('eraseDeviceCheckbox'),
    confirmFlashBtn: document.getElementById('confirmFlashBtn'),
    boardCount: document.getElementById('boardCount'),
    boardNotes: document.getElementById('boardNotes'),
    webSerialWarning: document.getElementById('webSerialWarning'),
    // Port selector elements
    portSelectorDialog: document.getElementById('portSelectorDialog'),
    portList: document.getElementById('portList'),
    authorizeNewPort: document.getElementById('authorizeNewPort'),
    // Flash overlay elements
    flashOverlay: document.getElementById('flashOverlay'),
    flashOverlayClose: document.getElementById('flashOverlayClose'),
    flashStage: document.getElementById('flashStage'),
    flashPercent: document.getElementById('flashPercent'),
    flashProgressFill: document.getElementById('flashProgressFill'),
    flashLogStream: document.getElementById('flashLogStream')
};

const state = {
    boards: [],
    selected: null,
    flashing: false,
    logHistoryLimit: 400,
    wifiPatcher: null,
    versioning: null,
    flasher: null,
    selectedPort: null,
    monitoring: false
};

const normalizeVersioning = (raw) => {
    if (!raw || typeof raw !== 'object') {
        return { version: null, buildDate: null, status: null, releaseNotes: [], boards: {} };
    }

    const boards = raw.boards && typeof raw.boards === 'object' && !Array.isArray(raw.boards)
        ? raw.boards
        : {};

    return {
        version: typeof raw.version === 'string' ? raw.version : null,
        buildDate: typeof raw.build_date === 'string' ? raw.build_date : null,
        status: typeof raw.status === 'string' ? raw.status : null,
        releaseNotes: Array.isArray(raw.release_notes) ? raw.release_notes.filter(Boolean) : [],
        boards
    };
};

const formatDate = (value) => {
    if (!value) return '—';
    const date = new Date(value);
    if (Number.isNaN(date.getTime())) return value;
    return new Intl.DateTimeFormat('en-US', { month: 'short', day: 'numeric', year: 'numeric' }).format(date);
};

const resolveAssetUrl = (path) => {
    if (!path) return '';
    return new URL(path, import.meta.url).href;
};

// Scroll state tracking for log streams
const isAtBottom = (element, threshold = 50) => {
    if (!element) return true;
    return element.scrollHeight - element.scrollTop - element.clientHeight < threshold;
};

const debounce = (fn, delay) => {
    let timer;
    return (...args) => {
        clearTimeout(timer);
        timer = setTimeout(() => fn.apply(this, args), delay);
    };
};

const appendLog = (message, level = 'info', skipFlashMirror = false) => {
    const entry = document.createElement('p');
    entry.className = `log-entry ${level}`;
    const time = new Date().toLocaleTimeString();
    entry.textContent = `[${time}] ${message}`;

    const wasAtBottom = isAtBottom(selectors.logStream);

    selectors.logStream.appendChild(entry);

    // Enforce history limit
    if (selectors.logStream.children.length > state.logHistoryLimit) {
        selectors.logStream.removeChild(selectors.logStream.firstChild);
    }

    // Conditional autoscroll - only if user was at bottom
    if (wasAtBottom) {
        requestAnimationFrame(() => {
            selectors.logStream.scrollTop = selectors.logStream.scrollHeight;
        });
    }

    // Mirror to flash overlay log if visible (unless already handled by appendFlashLog)
    if (!skipFlashMirror && selectors.flashLogStream && !selectors.flashOverlay.classList.contains('hidden')) {
        const flashWasAtBottom = isAtBottom(selectors.flashLogStream);
        const mirror = entry.cloneNode(true);
        selectors.flashLogStream.appendChild(mirror);
        if (selectors.flashLogStream.children.length > state.logHistoryLimit) {
            selectors.flashLogStream.removeChild(selectors.flashLogStream.firstChild);
        }
        if (flashWasAtBottom) {
            requestAnimationFrame(() => {
                selectors.flashLogStream.scrollTop = selectors.flashLogStream.scrollHeight;
            });
        }
    }
};

const appendFlashLog = (message, level = 'info') => {
    // Append to flash overlay log
    if (selectors.flashLogStream) {
        const wasAtBottom = isAtBottom(selectors.flashLogStream);

        const entry = document.createElement('p');
        entry.className = `log-entry ${level}`;
        const time = new Date().toLocaleTimeString();
        entry.textContent = `[${time}] ${message}`;
        selectors.flashLogStream.appendChild(entry);

        if (selectors.flashLogStream.children.length > state.logHistoryLimit) {
            selectors.flashLogStream.removeChild(selectors.flashLogStream.firstChild);
        }

        // Conditional autoscroll - only if user was at bottom
        if (wasAtBottom) {
            requestAnimationFrame(() => {
                selectors.flashLogStream.scrollTop = selectors.flashLogStream.scrollHeight;
            });
        }
    }

    // Also append to main log (skip flash mirror since we already added it above)
    appendLog(message, level, true);
};

const renderBoards = () => {
    selectors.boardSelect.innerHTML = '';

    // Group boards by chipFamily
    const groupedBoards = state.boards.reduce((groups, board) => {
        if (!groups[board.chipFamily]) {
            groups[board.chipFamily] = [];
        }
        groups[board.chipFamily].push(board);
        return groups;
    }, {});

    // Create optgroups for each chip family
    Object.entries(groupedBoards).forEach(([family, boards]) => {
        const optgroup = document.createElement('optgroup');
        optgroup.label = family;
        boards.forEach((board) => {
            const option = document.createElement('option');
            option.value = board.id;
            option.textContent = board.variant;
            optgroup.appendChild(option);
        });
        selectors.boardSelect.appendChild(optgroup);
    });
};

const applyVersioningToBoard = (board) => {
    if (!board) return board;
    if (!state.versioning) {
        return {
            ...board,
            boardNotes: Array.isArray(board.notes) ? board.notes : []
        };
    }

    const overrides = state.versioning.boards?.[board.id] || {};
    const boardNotes = Array.isArray(overrides.release_notes)
        ? overrides.release_notes.filter(Boolean)
        : (Array.isArray(board.notes) ? board.notes : []);

    return {
        ...board,
        version: overrides.version || state.versioning.version || board.version,
        status: overrides.status || state.versioning.status || board.status,
        released: overrides.build_date || state.versioning.buildDate || board.released,
        boardNotes
    };
};

const renderNotes = (target, notes = [], emptyText = 'No release notes provided for this build.') => {
    if (!target) return;
    target.innerHTML = '';
    if (!notes.length) {
        const fallback = document.createElement('li');
        fallback.textContent = emptyText;
        fallback.style.color = 'var(--text-secondary)';
        target.appendChild(fallback);
        return;
    }
    notes.forEach((note) => {
        const li = document.createElement('li');
        li.textContent = note;
        target.appendChild(li);
    });
};

const updateButtonStates = () => {
    const hasValidBoard = state.selected !== null && selectors.boardSelect.value !== '';
    selectors.flashTrigger.disabled = !hasValidBoard || state.flashing;
    selectors.downloadOtaBtn.disabled = !hasValidBoard || state.flashing;
};

const hydrateBoardDetails = (board) => {
    if (!board) {
        state.selected = null;
        selectors.boardStatus.textContent = '--';
        selectors.boardStatus.dataset.state = 'unknown';
        selectors.boardVersion.textContent = '--';
        selectors.boardRelease.textContent = '--';
        selectors.releaseNotesTitle.textContent = 'Release notes';
        selectors.heroLabel.textContent = 'Please select a board';
        selectors.heroDate.textContent = '--';
        renderNotes(selectors.boardNotes, [], 'Select a board to view release notes');
        updateButtonStates();
        state.wifiPatcher?.updateBaseManifest('');
        return;
    }

    state.selected = board;
    const status = board.status || state.versioning?.status || 'unknown';
    const version = board.version || state.versioning?.version || '--';

    selectors.boardStatus.textContent = status;
    selectors.boardStatus.dataset.state = status;
    selectors.boardVersion.textContent = version;
    selectors.boardRelease.textContent = formatDate(board.released);
    selectors.releaseNotesTitle.textContent = 'Release notes';
    selectors.heroLabel.textContent = board.variant;
    selectors.heroDate.textContent = formatDate(board.released);
    renderNotes(selectors.boardNotes, board.boardNotes, 'No release notes for this board.');
    updateButtonStates();

    const manifestUrl = resolveAssetUrl(board.manifest);
    if (manifestUrl) {
        state.wifiPatcher?.updateBaseManifest(manifestUrl);
    } else {
        state.wifiPatcher?.updateBaseManifest('');
    }
};

const fetchBoards = async () => {
    try {
        const response = await fetch('./firmware/boards.json', { cache: 'no-store' });
        if (!response.ok) throw new Error(`failed to load board list (${response.status})`);
        const data = await response.json();
        const boards = data.boards || [];
        state.boards = boards.map(applyVersioningToBoard);
        selectors.boardCount.textContent = state.boards.length;
        renderBoards();
        // Start with empty selection
        selectors.boardSelect.value = '';
        updateButtonStates();
        hydrateBoardDetails(null);
    } catch (error) {
        appendLog(`Unable to load board list: ${error.message}`, 'error');
        selectors.boardSelect.disabled = true;
        selectors.flashTrigger.disabled = true;
        selectors.downloadOtaBtn.disabled = true;
    }
};

const fetchVersioning = async () => {
    try {
        const response = await fetch('./assets/versioning', { cache: 'no-store' });
        if (!response.ok) throw new Error(`failed to load versioning (${response.status})`);
        const text = await response.text();
        const parsed = JSON.parse(text);
        state.versioning = normalizeVersioning(parsed);
        renderNotes(selectors.notesList, state.versioning.releaseNotes, 'No release notes provided for this build.');
    } catch (error) {
        state.versioning = null;
        renderNotes(selectors.notesList, [], 'No release notes provided for this build.');
        appendLog(`Version info unavailable; using board metadata instead: ${error.message}`, 'warn');
    }
};

// Flash overlay management
const showFlashOverlay = () => {
    selectors.flashOverlay.classList.remove('hidden');
    selectors.flashLogStream.innerHTML = '<p class="muted">Flash output will appear here.</p>';
    updateProgressUI(FLASH_STATES.IDLE, 0, 'Ready');
};

const hideFlashOverlay = () => {
    selectors.flashOverlay.classList.add('hidden');
};

const updateProgressUI = (stage, percent, message) => {
    if (selectors.flashStage) {
        selectors.flashStage.textContent = message || stage;
    }
    if (selectors.flashPercent) {
        selectors.flashPercent.textContent = `${Math.round(percent)}%`;
    }
    if (selectors.flashProgressFill) {
        selectors.flashProgressFill.style.width = `${percent}%`;
    }
};

// Port selector management
const showPortSelector = () => {
    return new Promise(async (resolve) => {
        const dialog = selectors.portSelectorDialog;
        const portList = selectors.portList;

        // Get previously authorized ports
        let ports = [];
        try {
            ports = await state.flasher.getAuthorizedPorts();
        } catch (e) {
            appendLog(`Failed to get ports: ${e.message}`, 'warn');
        }

        // Clear and populate port list
        portList.innerHTML = '';

        if (ports.length === 0) {
            portList.innerHTML = '<p class="muted">No previously authorized ports found.</p>';
        } else {
            ports.forEach((port, index) => {
                const info = EspFlasher.getPortInfo(port);
                const item = document.createElement('div');
                item.className = 'port-item';
                item.innerHTML = `
                    <div class="port-item-icon">USB</div>
                    <div class="port-item-info">
                        <strong>Port ${index + 1}</strong>
                        <small>VID: ${info.vendorId} PID: ${info.productId}</small>
                    </div>
                `;
                item.addEventListener('click', () => {
                    dialog.classList.add('hidden');
                    resolve(port);
                });
                portList.appendChild(item);
            });
        }

        // Handle "authorize new" button
        const authorizeHandler = async () => {
            dialog.classList.add('hidden');
            try {
                appendLog('Opening browser port selector...', 'info');
                const newPort = await state.flasher.requestNewPort();
                resolve(newPort);
            } catch (e) {
                if (e.name !== 'NotAllowedError') {
                    appendLog(`Port selection cancelled or failed: ${e.message}`, 'warn');
                }
                resolve(null);
            }
        };

        // Clean up old listener and add new one
        const newAuthorizeBtn = selectors.authorizeNewPort.cloneNode(true);
        selectors.authorizeNewPort.parentNode.replaceChild(newAuthorizeBtn, selectors.authorizeNewPort);
        selectors.authorizeNewPort = newAuthorizeBtn;
        newAuthorizeBtn.addEventListener('click', authorizeHandler);

        // Handle backdrop click to close
        const backdrop = dialog.querySelector('.port-selector-backdrop');
        const closeBtn = dialog.querySelector('.port-selector-close');

        const closeHandler = () => {
            dialog.classList.add('hidden');
            resolve(null);
        };

        backdrop.onclick = closeHandler;
        if (closeBtn) closeBtn.onclick = closeHandler;

        // Show dialog
        dialog.classList.remove('hidden');
    });
};

const showWarningDialog = () => {
    return new Promise((resolve) => {
        const dialog = selectors.warningDialog;
        const confirmBtn = selectors.confirmFlashBtn;
        const checkbox = selectors.eraseDeviceCheckbox;

        // Reset state
        checkbox.checked = true;

        const closeHandler = () => {
            dialog.classList.add('hidden');
            resolve(null);
        };

        const confirmHandler = () => {
            dialog.classList.add('hidden');
            resolve({ eraseFirst: checkbox.checked });
        };

        // Re-bind confirm button
        const newConfirmBtn = confirmBtn.cloneNode(true);
        confirmBtn.parentNode.replaceChild(newConfirmBtn, confirmBtn);
        selectors.confirmFlashBtn = newConfirmBtn;
        selectors.confirmFlashBtn.addEventListener('click', confirmHandler);

        // Bind close actions
        const backdrop = dialog.querySelector('.wifi-dialog-backdrop');
        const closeIcon = dialog.querySelector('.wifi-dialog-close');
        const cancelBtn = dialog.querySelector('.start-flash-actions .ghost-btn');

        if (backdrop) backdrop.onclick = closeHandler;
        if (closeIcon) closeIcon.onclick = closeHandler;
        if (cancelBtn) cancelBtn.onclick = closeHandler;

        dialog.classList.remove('hidden');
    });
};

const startFlashing = async () => {
    if (!state.selected) {
        appendLog('Select a board before flashing.', 'warn');
        return;
    }

    if (!state.selected.manifest) {
        appendLog(`Error: ${state.selected.variant} does not have a manifest file for flashing.`, 'error');
        return;
    }

    state.flashing = true;
    updateButtonStates();

    appendLog(`Preparing to flash ${state.selected.variant}...`, 'info');

    // Show port selector
    const port = await showPortSelector();

    if (!port) {
        appendLog('No port selected. Flash cancelled.', 'warn');
        state.flashing = false;
        updateButtonStates();
        return;
    }

    // Show warning/confirmation dialog
    const confirmation = await showWarningDialog();
    if (!confirmation) {
        appendLog('Flash cancelled by user.', 'info');
        state.flashing = false;
        updateButtonStates();
        return;
    }

    // Show flash overlay
    showFlashOverlay();
    appendFlashLog(`Starting flash for ${state.selected.variant}`, 'info');

    try {
        // Get manifest URL (use patched if WiFi credentials provided)
        let manifestUrl = resolveAssetUrl(state.selected.manifest);

        // Check if WiFi patcher has a patched manifest
        if (state.wifiPatcher && state.wifiPatcher.getPatchedManifestUrl) {
            const patchedUrl = state.wifiPatcher.getPatchedManifestUrl();
            if (patchedUrl) {
                manifestUrl = patchedUrl;
                appendFlashLog('Using WiFi-patched firmware', 'info');
            }
        }

        // Start flashing
        await state.flasher.flash(port, manifestUrl, { eraseFirst: confirmation.eraseFirst });

        appendFlashLog('Flash completed successfully!', 'success');

        // Start serial monitor
        appendFlashLog('Starting serial monitor... (Close overlay to stop)', 'info');
        // Do not await this, let it run in background
        state.flasher.startMonitor(port).catch(err => {
            appendFlashLog(`Serial monitor error: ${err.message}`, 'warn');
        });
        state.monitoring = true;
    } catch (error) {
        appendFlashLog(`Flash failed: ${error.message}`, 'error');
    } finally {
        state.flashing = false;
        updateButtonStates();
    }
};

const downloadOtaFiles = async (boardId) => {
    const originalText = selectors.downloadOtaBtn.textContent;
    try {
        appendLog(`Starting OTA download for ${boardId}...`, 'info');

        // Update button state during download
        selectors.downloadOtaBtn.textContent = 'Downloading...';
        selectors.downloadOtaBtn.disabled = true;

        const otaFiles = ['firmware.bin', 'littlefs.bin', 'OTA_readme.md'];
        const zip = new JSZip();
        const wifiForm = document.getElementById('wifiPatchForm');
        const ssidInput = wifiForm?.querySelector('#wifiSsid');
        const passwdInput = wifiForm?.querySelector('#wifiPass');
        const ssid = (ssidInput?.value || '').trim();
        const passwd = (passwdInput?.value || '').trim();
        const shouldPatchLittlefs = Boolean(
            state.wifiPatcher &&
            ssid &&
            passwd &&
            ssid !== 'your_ssid' &&
            passwd !== 'your_pass'
        );

        for (const file of otaFiles) {
            try {
                const fileUrl = `./firmware/${boardId}/OTA/${file}`;

                if (file === 'littlefs.bin' && shouldPatchLittlefs) {
                    try {
                        appendLog('Applying Wi-Fi patch to littlefs.bin...', 'info');
                        const patchedBuffer = await state.wifiPatcher.patchFirmware(ssid, passwd, fileUrl);
                        zip.file(file, patchedBuffer);
                        appendLog('Wi-Fi patch applied to littlefs.bin', 'success');
                        continue;
                    } catch (patchError) {
                        appendLog(`Warning: Failed to apply Wi-Fi patch: ${patchError.message}`, 'warn');
                    }
                }

                const response = await fetch(fileUrl);
                if (response.ok) {
                    const blob = await response.blob();
                    const patchNote = file === 'littlefs.bin' && shouldPatchLittlefs ? ' (no Wi-Fi patch)' : '';
                    zip.file(file, blob);
                    appendLog(`Added ${file} to OTA package${patchNote}`, 'info');
                } else {
                    appendLog(`Warning: ${file} not found in OTA directory`, 'warn');
                }
            } catch (error) {
                appendLog(`Warning: Failed to fetch ${file}: ${error.message}`, 'warn');
            }
        }

        // Generate and download zip
        appendLog('Creating OTA package...', 'info');
        const content = await zip.generateAsync({ type: "blob" });
        const url = URL.createObjectURL(content);
        const a = document.createElement('a');
        a.href = url;
        a.download = `centauri-carbon-${boardId}-ota.zip`;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);

        // Restore button state
        selectors.downloadOtaBtn.textContent = originalText;
        updateButtonStates();

        appendLog(`OTA files downloaded successfully for ${boardId}`, 'success');
    } catch (error) {
        // Restore button state on error
        selectors.downloadOtaBtn.textContent = originalText;
        updateButtonStates();
        appendLog(`OTA download failed: ${error.message}`, 'error');
    }
};

const attachEvents = () => {
    selectors.boardSelect.addEventListener('change', (event) => {
        const board = state.boards.find((item) => item.id === event.target.value);
        hydrateBoardDetails(board);
        appendLog(`Switched to ${board?.variant || 'unknown board'}`, 'info');
    });

    selectors.flashTrigger.addEventListener('click', startFlashing);

    selectors.downloadOtaBtn.addEventListener('click', async () => {
        if (!state.selected) {
            appendLog('Select a board before downloading OTA files.', 'warn');
            return;
        }

        // Check if board has OTA directory
        try {
            const otaCheckResponse = await fetch(`./firmware/${state.selected.id}/OTA/firmware.bin`, { method: 'HEAD' });
            if (!otaCheckResponse.ok) {
                appendLog(`Warning: OTA files not available for ${state.selected.variant}. Download may be incomplete.`, 'warn');
            }
        } catch (error) {
            appendLog(`Warning: Unable to verify OTA files for ${state.selected.variant}: ${error.message}`, 'warn');
        }

        await downloadOtaFiles(state.selected.id);
    });

    selectors.copyLogBtn.addEventListener('click', async () => {
        const text = selectors.logStream.innerText.trim();
        if (!text) return;
        try {
            await navigator.clipboard.writeText(text);
            appendLog('Log copied to clipboard.', 'info');
        } catch (error) {
            appendLog(`Clipboard copy failed: ${error.message}`, 'error');
        }
    });

    selectors.clearLogBtn.addEventListener('click', () => {
        selectors.logStream.innerHTML = '<p class="muted">Log cleared.</p>';
    });

    // Flash overlay close button
    if (selectors.flashOverlayClose) {
        selectors.flashOverlayClose.addEventListener('click', async () => {
            if (!state.flashing) {
                if (state.monitoring) {
                    await state.flasher.stopMonitor();
                    state.monitoring = false;
                }
                hideFlashOverlay();
            }
        });
    }

    // Add scroll event listeners for autoscroll management
    // Note: We don't need to track scroll state since isAtBottom() checks it on-demand
    // The scroll listeners would only be needed if we wanted to show a "jump to bottom" button
};

const init = async () => {
    // Check Web Serial support
    if (!('serial' in navigator)) {
        appendLog('Web Serial API is unavailable in this browser.', 'warn');
        if (selectors.webSerialWarning) {
            selectors.webSerialWarning.style.display = 'block';
        }
        selectors.flashTrigger.disabled = true;
    }

    // Initialize flasher
    state.flasher = new EspFlasher({
        onProgress: (stage, percent, message) => {
            updateProgressUI(stage, percent, message);
        },
        onLog: (message, level) => {
            appendFlashLog(message, level);
        },
        onStateChange: (newState) => {
            // Handle state changes if needed
        }
    });

    attachEvents();

    // Initialize WiFi patcher (without installButton reference)
    state.wifiPatcher = initWifiPatcher({
        openButton: selectors.wifiAcceptBtn,
        resetButton: selectors.wifiResetBtn,
        dialog: selectors.wifiPatchDialog,
        form: selectors.wifiForm,
        statusEl: selectors.wifiStatus,
        log: appendLog
    });

    await fetchVersioning();
    await fetchBoards();
};

document.addEventListener('DOMContentLoaded', init);

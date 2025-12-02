import { initWifiPatcher } from './wifiPatcher.js';

const selectors = {
    boardSelect: document.getElementById('boardSelect'),
    boardStatus: document.getElementById('boardStatus'),
    boardVersion: document.getElementById('boardVersion'),
    boardRelease: document.getElementById('boardRelease'),
    notesList: document.getElementById('notesList'),
    releaseNotesTitle: document.getElementById('releaseNotesTitle'),
    installButton: document.getElementById('installButton'),
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
    wifiPatchDialog: document.getElementById('wifiPatchDialog'),
    boardCount: document.getElementById('boardCount'),
    boardNotes: document.getElementById('boardNotes')
};

const state = {
    boards: [],
    selected: null,
    capturing: false,
    dialogOpen: false,
    dialogLogOverlay: null,
    dialogLogStream: null,
    dialogSlot: null,
    logHistoryLimit: 400,
    consoleOriginals: {},
    wifiPatcher: null,
    versioning: null
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

const appendLog = (message, level = 'info') => {
    const entry = document.createElement('p');
    entry.className = `log-entry ${level}`;
    const time = new Date().toLocaleTimeString();
    entry.textContent = `[${time}] ${message}`;
    selectors.logStream.appendChild(entry);
    selectors.logStream.scrollTop = selectors.logStream.scrollHeight;
    if (state.dialogLogStream) {
        const mirror = entry.cloneNode(true);
        state.dialogLogStream.appendChild(mirror);
        if (state.dialogLogStream.children.length > state.logHistoryLimit) {
            state.dialogLogStream.removeChild(state.dialogLogStream.firstChild);
        }
        state.dialogLogStream.scrollTop = state.dialogLogStream.scrollHeight;
    }
};

const installConsoleCapture = () => {
    ['log', 'info', 'warn', 'error'].forEach((level) => {
        const original = console[level].bind(console); // eslint-disable-line no-console
        state.consoleOriginals[level] = original;
        console[level] = (...args) => { // eslint-disable-line no-console
            if (state.capturing && args.length) {
                appendLog(args.map((item) => {
                    if (typeof item === 'string') return item;
                    try {
                        return JSON.stringify(item);
                    } catch (err) {
                        return String(item);
                    }
                }).join(' '), level === 'log' ? 'info' : level);
            }
            original(...args);
        };
    });
};

const startCapture = (boardLabel) => {
    if (!state.capturing) {
        state.capturing = true;
        toggleDialogLogOverlay(true);
        appendLog(`Starting flashing session for ${boardLabel}`, 'info');
    }
};

const stopCapture = (reason = 'Installer closed') => {
    if (!state.capturing) return;
    appendLog(reason, 'info');
    state.capturing = false;
    toggleDialogLogOverlay(false);
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

const toggleDialogLogOverlay = (visible) => {
    const overlay = state.dialogLogOverlay;
    if (!overlay) return;
    overlay.classList.toggle('visible', visible);
    overlay.classList.toggle('hidden', !visible);
};

const createDialogLogOverlay = () => {
    if (state.dialogLogOverlay) return;
    const overlay = document.createElement('section');
    overlay.id = 'install-dialog-log-overlay';
    overlay.className = 'install-dialog-log-overlay hidden';

    const header = document.createElement('div');
    header.className = 'install-dialog-log-header';
    header.innerHTML = '<span>Installer console</span>';

    const closeBtn = document.createElement('button');
    closeBtn.type = 'button';
    closeBtn.className = 'install-dialog-log-close';
    closeBtn.setAttribute('aria-label', 'Hide installer console');
    closeBtn.textContent = '×';
    closeBtn.addEventListener('click', () => toggleDialogLogOverlay(false));
    header.appendChild(closeBtn);

    const stream = document.createElement('div');
    stream.className = 'log-stream dialog';
    stream.setAttribute('role', 'log');
    stream.setAttribute('aria-live', 'polite');
    stream.setAttribute('aria-relevant', 'additions');
    stream.innerHTML = '<p class="muted">Installer console output will appear here.</p>';

    const panel = document.createElement('div');
    panel.className = 'install-dialog-log-panel';
    panel.appendChild(header);
    panel.appendChild(stream);

    const slot = document.createElement('div');
    slot.className = 'install-dialog-slot';
    panel.appendChild(slot);

    overlay.appendChild(panel);

    state.dialogSlot = slot;
    document.body.appendChild(overlay);

    state.dialogLogOverlay = overlay;
    state.dialogLogStream = stream;
};

const updateButtonStates = () => {
    const hasValidBoard = state.selected !== null && selectors.boardSelect.value !== '';
    selectors.flashTrigger.disabled = !hasValidBoard;
    selectors.downloadOtaBtn.disabled = !hasValidBoard;
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
        selectors.installButton?.removeAttribute('manifest');
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
        selectors.installButton?.setAttribute('manifest', manifestUrl);
        state.wifiPatcher?.updateBaseManifest(manifestUrl);
    } else {
        selectors.installButton?.removeAttribute('manifest');
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

const attachEvents = () => {
    selectors.boardSelect.addEventListener('change', (event) => {
        const board = state.boards.find((item) => item.id === event.target.value);
        hydrateBoardDetails(board);
        appendLog(`Switched to ${board?.variant || 'unknown board'}`, 'info');
    });

    selectors.flashTrigger.addEventListener('click', () => {
        if (!state.selected) {
            appendLog('Select a board before flashing.', 'warn');
            return;
        }

        // Validate that board has required files for flashing
        if (!state.selected.manifest) {
            appendLog(`Error: ${state.selected.variant} does not have a manifest file for flashing.`, 'error');
            return;
        }

        startCapture(state.selected.variant);
    });

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
        if (state.dialogLogStream) {
            state.dialogLogStream.innerHTML = '<p class="muted">Log cleared.</p>';
        }
    });
};

const observeInstallerDialog = () => {
    const observer = new MutationObserver(() => {
        const dialog = document.querySelector('ewt-install-dialog');
        if (dialog && !state.dialogOpen) {
            state.dialogOpen = true;
            toggleDialogLogOverlay(true);
            if (state.dialogSlot && dialog.parentElement !== state.dialogSlot) {
                state.dialogSlot.appendChild(dialog);
            }
            appendLog('Installer dialog opened. Follow the prompts to select the serial port.', 'info');
        } else if (!dialog && state.dialogOpen) {
            state.dialogOpen = false;
            stopCapture('Installer dialog closed.');
        }
    });
    observer.observe(document.body, { childList: true, subtree: true });
};

const downloadOtaFiles = async (boardId) => {
    try {
        appendLog(`Starting OTA download for ${boardId}...`, 'info');

        // Update button state during download
        const originalText = selectors.downloadOtaBtn.textContent;
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
        const content = await zip.generateAsync({type: "blob"});
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

const init = async () => {
    createDialogLogOverlay();
    installConsoleCapture();
    attachEvents();
    observeInstallerDialog();
    state.wifiPatcher = initWifiPatcher({
        installButton: selectors.installButton,
        openButton: selectors.wifiAcceptBtn,
        dialog: selectors.wifiPatchDialog,
        form: selectors.wifiForm,
        statusEl: selectors.wifiStatus,
        log: appendLog
    });
    await fetchVersioning();
    await fetchBoards();
    if (!('serial' in navigator)) {
        appendLog('Web Serial API is unavailable in this browser.', 'warn');
    }
};

document.addEventListener('DOMContentLoaded', init);

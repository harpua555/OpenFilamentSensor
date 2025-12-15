import { initWifiPatcher, PLACEHOLDER_TOKEN } from './wifiPatcher.js';
import { EspFlasher, FLASH_STATES } from './flasher.js';

const GITHUB_OWNER = 'harpua555';
const GITHUB_REPO = 'centauri-carbon-motion-detector';
const DEFAULT_RELEASE_TAG = null; // Use "latest" unless a tag is provided via query string

let selectors = {};

const initSelectors = () => {
    selectors = {
        releaseSelect: document.getElementById('releaseSelect'),
        releaseNotesLink: document.getElementById('releaseNotesLink'),
        boardSelect: document.getElementById('boardSelect'),
        boardStatus: document.getElementById('boardStatus'),
        notesList: document.getElementById('notesList'),
        releaseNotesTitle: document.getElementById('releaseNotesTitle'),
        flashTrigger: document.getElementById('flashTrigger'),
        downloadOtaBtn: document.getElementById('downloadOtaBtn'),
        logStream: document.getElementById('logStream'),
        copyLogBtn: document.getElementById('copyLogBtn'),
        clearLogBtn: document.getElementById('clearLogBtn'),
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
        flashLogStream: document.getElementById('flashLogStream'),
        finishedDialog: document.getElementById('finishedDialog')
    };
};

const state = {
    boards: [],
    baseBoards: [],
    releases: [],
    currentReleaseTag: null,
    selected: null,
    flashing: false,
    logHistoryLimit: 400,
    wifiPatcher: null,
    versioning: null,
    flasher: null,
    selectedPort: null,
    monitoring: false,
    release: null,
    blobUrls: []
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

const stripLeadingV = (value) => {
    if (!value) return value;
    return value.replace(/^v/i, '');
};

const trackBlobUrl = (url) => {
    if (url) {
        state.blobUrls.push(url);
    }
    return url;
};

const cleanupBlobUrls = () => {
    state.blobUrls.forEach((url) => {
        try {
            URL.revokeObjectURL(url);
        } catch (e) {
            // Ignore failures when cleaning up
        }
    });
    state.blobUrls = [];
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

const buildAssetMap = (assets = []) => {
    const map = {};
    assets.forEach((asset) => {
        if (asset?.name && asset?.browser_download_url) {
            map[asset.name] = asset.browser_download_url;
        }
    });
    return map;
};

const normalizeRelease = (release, index = 0) => {
    const rawTag = release.tag_name || '';
    const normalizedTag = rawTag ? `v${stripLeadingV(rawTag)}` : '';
    return {
        tag: rawTag,
        normalizedTag,
        publishedAt: release.published_at,
        url: release.html_url,
        assetMap: buildAssetMap(release.assets || []),
        body: typeof release.body === 'string' ? release.body : '',
        isPrerelease: !!release.prerelease,
        isDraft: !!release.draft,
        isCurrent: !release.draft && index === 0
    };
};

const fetchReleasesList = async (perPage = 20) => {
    const url = `https://api.github.com/repos/${GITHUB_OWNER}/${GITHUB_REPO}/releases?per_page=${perPage}`;
    const response = await fetch(url, {
        headers: { Accept: 'application/vnd.github+json' },
        cache: 'no-store'
    });
    if (!response.ok) {
        throw new Error(`Release lookup failed (${response.status})`);
    }
    const releases = await response.json();
    return (releases || []).map((rel, idx) => normalizeRelease(rel, idx));
};

const buildManifestForBoard = (board, mergedUrl, version, releaseTag) => {
    const manifest = {
        name: board.name || board.variant || board.id,
        version: stripLeadingV(version || releaseTag || ''),
        build_id: `${releaseTag || 'local'}-${board.id || 'board'}`,
        new_install_prompt_erase: true,
        builds: [
            {
                chipFamily: board.chipFamily,
                parts: [
                    {
                        path: mergedUrl,
                        offset: 0
                    }
                ]
            }
        ]
    };
    const blob = new Blob([JSON.stringify(manifest)], { type: 'application/json' });
    return trackBlobUrl(URL.createObjectURL(blob));
};

const renderReleaseOptions = () => {
    const select = selectors.releaseSelect;
    if (!select) return;

    select.innerHTML = '';
    if (!state.releases.length) {
        const option = document.createElement('option');
        option.value = '';
        option.textContent = 'No releases available';
        select.appendChild(option);
        select.disabled = true;
        return;
    }

    select.disabled = false;
    state.releases.forEach((rel) => {
        const option = document.createElement('option');
        option.value = rel.normalizedTag || rel.tag;
        const markers = [];
        if (rel.isCurrent) markers.push('current');
        if (rel.isPrerelease) markers.push('pre');
        const markerText = markers.length ? ` (${markers.join(', ')})` : '';
        option.textContent = `${rel.normalizedTag || rel.tag}${markerText}`;
        select.appendChild(option);
    });

    const targetTag =
        state.release?.normalizedTag ||
        state.currentReleaseTag ||
        state.releases[0]?.normalizedTag ||
        state.releases[0]?.tag ||
        '';
    select.value = targetTag;
};

const renderActiveRelease = () => {
    if (selectors.releaseSelect) {
        const targetTag = state.release?.normalizedTag || state.release?.tag || '';
        if (targetTag && selectors.releaseSelect.value !== targetTag) {
            selectors.releaseSelect.value = targetTag;
        }
    }
    const published = state.release?.publishedAt ? formatDate(state.release.publishedAt) : '—';
    if (selectors.heroDate) {
        selectors.heroDate.textContent = published;
    }
    if (selectors.releaseNotesLink) {
        const href = state.release?.url || '';
        const hasUrl = Boolean(href);
        selectors.releaseNotesLink.href = hasUrl ? href : '#';
        selectors.releaseNotesLink.classList.toggle('disabled', !hasUrl);
        selectors.releaseNotesLink.setAttribute('aria-disabled', hasUrl ? 'false' : 'true');
        selectors.releaseNotesLink.title = hasUrl
            ? `Open ${state.release?.normalizedTag || state.release?.tag || 'release'} on GitHub`
            : 'Release notes are unavailable for this selection.';
    }
};

const buildBoardModel = (board, releaseInfo) => {
    const normalizedTag = releaseInfo?.normalizedTag || (releaseInfo?.tag ? `v${stripLeadingV(releaseInfo.tag)}` : null);
    const releaseVersion = stripLeadingV(normalizedTag || releaseInfo?.tag || '');
    const version = releaseVersion || board.version;
    const released = releaseInfo?.publishedAt || board.released;
    const status = releaseInfo?.isPrerelease ? 'pre-release' : board.status;

    const pagesBase = normalizedTag
        ? `https://harpua555.github.io/centauri-carbon-motion-detector/releases/${normalizedTag}/`
        : '';
    const pageAsset = (suffix) => (pagesBase ? `${pagesBase}${board.id}-${suffix}` : '');

    const mergedRemote = pageAsset('firmware_merged.bin');
    const firmwareRemote = pageAsset('firmware.bin');
    const littlefsRemote = pageAsset('littlefs.bin');
    const bootloaderRemote = pageAsset('bootloader.bin');
    const partitionsRemote = pageAsset('partitions.bin');

    const mergedUrl = mergedRemote;
    const firmwareUrl = firmwareRemote;
    const littlefsUrl = littlefsRemote;

    const manifestUrl = buildManifestForBoard(board, mergedUrl, version, releaseInfo?.tag || 'local');

    return {
        ...board,
        version,
        status,
        released,
        boardNotes: Array.isArray(board.notes) ? board.notes : [],
        manifest: manifestUrl,
        manifestUrl,
        releaseTag: normalizedTag || releaseInfo?.tag || null,
        available: Boolean(mergedRemote),
        assetUrls: {
            merged: mergedUrl,
            firmware: firmwareUrl,
            littlefs: littlefsUrl,
            bootloader: bootloaderRemote || '',
            partitions: partitionsRemote || '',
            remote: {
                merged: mergedRemote,
                firmware: firmwareRemote,
                littlefs: littlefsRemote
            }
        }
    };
};

const refreshBoardsForRelease = () => {
    if (!state.baseBoards.length) return;
    cleanupBlobUrls();
    const mapped = state.baseBoards.map((board) => buildBoardModel(board, state.release));
    const availableBoards = mapped.filter((board) => board.available);
    state.boards = availableBoards;
    if (selectors.boardCount) {
        selectors.boardCount.textContent = state.boards.length;
    }
    if (!availableBoards.length && mapped.length && state.release) {
        appendLog(`No boards in release ${state.release.tag} have firmware assets on Pages.`, 'warn');
    }
    renderBoards();
    if (selectors.boardSelect) {
        selectors.boardSelect.value = '';
    }
    updateButtonStates();
    hydrateBoardDetails(null);
};

const setActiveRelease = (tagHint = null) => {
    const matchRelease = (hint) =>
        state.releases.find((r) => r.normalizedTag === hint || r.tag === hint);

    const preferred =
        matchRelease(tagHint) ||
        state.releases.find((r) => r.isCurrent) ||
        state.releases[0] ||
        null;

    state.release = preferred;
    state.currentReleaseTag =
        state.releases.find((r) => r.isCurrent)?.normalizedTag ||
        state.release?.normalizedTag ||
        null;

    renderReleaseOptions();
    renderActiveRelease();
    refreshBoardsForRelease();

    if (state.release?.tag) {
        appendLog(`Release set to ${state.release.normalizedTag || state.release.tag}`, 'info');
        if (tagHint && state.release.tag !== tagHint && state.release.normalizedTag !== tagHint) {
            appendLog(`Requested release ${tagHint} not found; using ${state.release.normalizedTag || state.release.tag} instead.`, 'warn');
        }
    }
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
    if (!selectors.boardSelect) return;
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
    return buildBoardModel(board, state.release);
};

/**
 * Parse basic markdown to HTML for release notes display
 * Handles: headings, bold, inline code, links
 */
const parseMarkdownToHtml = (text) => {
    if (!text) return '';
    return text
        // Escape HTML entities first
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        // Headings (### before ## before #)
        .replace(/^### (.+)$/gm, '<strong>$1</strong>')
        .replace(/^## (.+)$/gm, '<strong>$1</strong>')
        .replace(/^# (.+)$/gm, '<strong>$1</strong>')
        // Bold
        .replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>')
        // Inline code
        .replace(/`([^`]+)`/g, '<code>$1</code>')
        // Links
        .replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<a href="$2" target="_blank" rel="noopener">$1</a>');
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
        li.innerHTML = parseMarkdownToHtml(note);
        target.appendChild(li);
    });
};

const updateButtonStates = () => {
    const hasValidBoard = state.selected !== null && selectors.boardSelect && selectors.boardSelect.value !== '';
    if (selectors.flashTrigger) {
        selectors.flashTrigger.disabled = !hasValidBoard || state.flashing;
    }
    if (selectors.downloadOtaBtn) {
        selectors.downloadOtaBtn.disabled = !hasValidBoard || state.flashing;
    }
};

const hydrateBoardDetails = (board) => {
    if (!board) {
        state.selected = null;
        selectors.boardStatus.textContent = '--';
        selectors.boardStatus.dataset.state = 'unknown';
        selectors.releaseNotesTitle.textContent = 'Release notes';
        renderNotes(selectors.boardNotes, [], 'Select a board to view release notes');
        updateButtonStates();
        state.wifiPatcher?.updateBaseManifest('');
        return;
    }

    state.selected = board;
    const status = board.status || state.versioning?.status || 'unknown';

    selectors.boardStatus.textContent = status;
    selectors.boardStatus.dataset.state = status;
    selectors.releaseNotesTitle.textContent = 'Release notes';
    renderNotes(selectors.boardNotes, board.boardNotes, 'No release notes for this board.');
    updateButtonStates();

    const manifestUrl = (board.manifest || '').startsWith('blob:')
        || (board.manifest || '').startsWith('http')
        ? board.manifest
        : resolveAssetUrl(board.manifest);
    if (manifestUrl) {
        state.wifiPatcher?.updateBaseManifest(manifestUrl);
    } else {
        state.wifiPatcher?.updateBaseManifest('');
    }
};

const fetchBoards = async () => {
    try {
        const response = await fetch('./boards.json', { cache: 'no-store' });
        if (!response.ok) throw new Error(`failed to load board list (${response.status})`);
        const data = await response.json();
        state.baseBoards = data.boards || [];
        refreshBoardsForRelease();
        if (state.release?.tag) {
            appendLog(`Using GitHub release ${state.release.tag} for firmware URLs.`, 'info');
        } else {
            appendLog('No GitHub release metadata available; falling back to local firmware paths.', 'warn');
        }
    } catch (error) {
        appendLog(`Unable to load board list: ${error.message}`, 'error');
        selectors.boardSelect.disabled = true;
        selectors.flashTrigger.disabled = true;
        selectors.downloadOtaBtn.disabled = true;
    }
};

const fetchVersioning = async () => {
    if (!state.release) {
        state.versioning = null;
        renderNotes(selectors.notesList, [], 'No release notes provided for this build.');
        return;
    }
    // Use release description/changelog from GitHub Releases API
    const releaseNotes = [];
    const body = state.release.body || '';
    if (body) {
        const lines = body.split('\n').map((line) => line.trim()).filter(Boolean);
        lines.forEach((line) => {
            if (line.startsWith('-') || line.startsWith('*')) {
                releaseNotes.push(line.replace(/^[-*]\s*/, ''));
            } else {
                releaseNotes.push(line);
            }
        });
    }
    state.versioning = {
        version: stripLeadingV(state.release.normalizedTag || state.release.tag || ''),
        buildDate: state.release.publishedAt || null,
        status: state.release.isPrerelease ? 'pre-release' : 'release',
        releaseNotes,
        boards: {}
    };
    renderNotes(selectors.notesList, releaseNotes, releaseNotes.length ? '' : 'No release notes provided for this build.');
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
        let manifestUrl = state.selected.manifest;
        if (manifestUrl && !manifestUrl.startsWith('http') && !manifestUrl.startsWith('blob:')) {
            manifestUrl = resolveAssetUrl(manifestUrl);
        }

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

const downloadOtaFiles = async (board) => {
    if (!board) {
        appendLog('Select a board before downloading OTA files.', 'warn');
        return;
    }

    const boardId = board.id;
    const originalText = selectors.downloadOtaBtn.textContent;
    try {
        appendLog(`Starting OTA download for ${boardId}...`, 'info');

        // Update button state during download
        selectors.downloadOtaBtn.textContent = 'Downloading...';
        selectors.downloadOtaBtn.disabled = true;

        const otaFiles = [
            {
                name: 'firmware.bin',
                remoteUrl: board.assetUrls?.remote?.firmware || ''
            },
            {
                name: 'littlefs.bin',
                remoteUrl: board.assetUrls?.remote?.littlefs || '',
                patchable: true
            },
            {
                name: 'OTA_readme.md', generator: () => {
                    const tag = board.releaseTag || board.version || 'unknown';
                    const published = board.released ? formatDate(board.released) : 'unknown date';
                    const content = `Centauri Carbon Motion Detector OTA\nBoard: ${board.variant || boardId}\nRelease: ${tag}\nPublished: ${published}\nSource: GitHub Releases (${GITHUB_OWNER}/${GITHUB_REPO})\n\nIncludes firmware.bin and littlefs.bin from the selected release.`;
                    return new Blob([content], { type: 'text/markdown' });
                }
            }
        ];
        const zip = new JSZip();
        const wifiForm = document.getElementById('wifiPatchForm');
        const ssidInput = wifiForm?.querySelector('#wifiSsid');
        const passwdInput = wifiForm?.querySelector('#wifiPass');
        const ssid = (ssidInput?.value || '').trim();
        const passwd = (passwdInput?.value || '').trim();
        const blankPadding = ' '.repeat(PLACEHOLDER_TOKEN.length);
        const hasUserCreds = ssid && passwd && ssid !== 'your_ssid' && passwd !== 'your_pass';
        const appliedSsid = hasUserCreds ? ssid : blankPadding;
        const appliedPass = hasUserCreds ? passwd : blankPadding;

        appendLog(
            hasUserCreds
                ? 'Wi-Fi credentials provided; will patch them into littlefs.bin.'
                : 'No Wi-Fi credentials provided; placeholder will be scrubbed (blanked) to preserve layout.',
            hasUserCreds ? 'info' : 'warn'
        );
        for (const file of otaFiles) {
            try {
                if (file.generator) {
                    zip.file(file.name, file.generator());
                    appendLog(`Added ${file.name} to OTA package`, 'info');
                    continue;
                }

                if (file.name === 'littlefs.bin') {
                    try {
                        appendLog(hasUserCreds ? 'Applying Wi-Fi patch to littlefs.bin...' : 'Scrubbing placeholder Wi-Fi credentials...', 'info');
                        const sourceUrl = file.remoteUrl;
                        if (!sourceUrl) throw new Error('No littlefs source URL available.');
                        const patchedBuffer = await state.wifiPatcher.patchFirmware(appliedSsid, appliedPass, sourceUrl);
                        zip.file(file.name, patchedBuffer);
                        appendLog(hasUserCreds ? 'Wi-Fi patch applied to littlefs.bin' : 'Placeholder credentials scrubbed in littlefs.bin', 'success');
                        continue;
                    } catch (patchError) {
                        appendLog(`Warning: Failed to apply Wi-Fi patch: ${patchError.message}`, 'warn');
                    }
                }

                let blob = null;
                const patchNote = '';

                if (file.remoteUrl) {
                    try {
                        const response = await fetch(file.remoteUrl);
                        if (response.ok) {
                            blob = await response.blob();
                            appendLog(`Added ${file.name} from release assets${patchNote}`, 'info');
                        } else {
                            appendLog(`Warning: ${file.name} not accessible from release assets (HTTP ${response.status})`, 'warn');
                        }
                    } catch (err) {
                        appendLog(`Warning: Failed to fetch ${file.name} from release assets: ${err.message}`, 'warn');
                    }
                }

                if (!blob) {
                    appendLog(`Warning: ${file.name} not available for ${board.variant || boardId}`, 'warn');
                    continue;
                }

                zip.file(file.name, blob);
            } catch (error) {
                appendLog(`Warning: Failed to fetch ${file.name}: ${error.message}`, 'warn');
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
    if (selectors.releaseSelect) {
        selectors.releaseSelect.addEventListener('change', async (event) => {
            setActiveRelease(event.target.value);
            await fetchVersioning();
        });
    }

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

        await downloadOtaFiles(state.selected);
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
    // Initialize selectors after DOM is ready
    initSelectors();

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
            if (newState === FLASH_STATES.FINISHED) {
                const dialog = selectors.finishedDialog;
                if (dialog) {
                    dialog.classList.remove('hidden');
                    const closeTriggers = dialog.querySelectorAll('[data-dialog-close]');
                    closeTriggers.forEach(trigger => {
                        trigger.onclick = () => dialog.classList.add('hidden');
                    });
                }
            }
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

    const params = new URLSearchParams(window.location.search);
    const requestedTag = params.get('tag') || params.get('release') || DEFAULT_RELEASE_TAG;
    try {
        state.releases = await fetchReleasesList();
        setActiveRelease(requestedTag);
    } catch (error) {
        state.releases = [];
        state.release = null;
        renderReleaseOptions();
        renderActiveRelease();
        appendLog(`GitHub release metadata unavailable: ${error.message}`, 'warn');
    }

    await fetchVersioning();
    await fetchBoards();

    window.addEventListener('beforeunload', cleanupBlobUrls);
};

document.addEventListener('DOMContentLoaded', init);

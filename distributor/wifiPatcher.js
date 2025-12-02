const PLACEHOLDER_PATTERN = /"ssid"\s*:\s*"([^"]*)"\s*,\s*"passwd"\s*:\s*"([^"]*)"/i;
const DEFAULT_STATUS_MESSAGE =
    'Optional: patch firmware with Wi-Fi credentials before flashing.';
const MAX_FIELD_LENGTH = 20;
const isoDecoder = new TextDecoder('iso-8859-1');

const toIsoBytes = (value) => {
    const bytes = new Uint8Array(value.length);
    for (let i = 0; i < value.length; i += 1) {
        bytes[i] = value.charCodeAt(i) & 0xff;
    }
    return bytes;
};

const fillRange = (target, start, length, value) => {
    const padded = value.padEnd(length, ' ');
    const bytes = toIsoBytes(padded);
    target.set(bytes.subarray(0, length), start);
};

const patchBuffer = (buffer, ssid, passwd) => {
    const text = isoDecoder.decode(buffer);
    const match = PLACEHOLDER_PATTERN.exec(text);
    if (!match) {
        throw new Error('Unable to locate SSID/passwd block in the firmware image.');
    }

    const snippet = match[0];
    const snippetStart = match.index;

    const extractRange = (regex) => {
        const inner = regex.exec(snippet);
        if (!inner) {
            throw new Error('Failed to isolate placeholder range.');
        }
        const colonIdx = inner[0].indexOf(':');
        const startQuote = inner[0].indexOf('"', colonIdx + 1);
        const endQuote = inner[0].indexOf('"', startQuote + 1);
        const relativeStart = inner.index + startQuote + 1;
        const relativeLength = endQuote - (startQuote + 1);
        return {
            start: snippetStart + relativeStart,
            length: relativeLength
        };
    };

    const ssidRange = extractRange(/"ssid"\s*:\s*"([^"]*)"/i);
    const passwdRange = extractRange(/"passwd"\s*:\s*"([^"]*)"/i);

    const patched = new Uint8Array(buffer.slice(0));
    fillRange(patched, ssidRange.start, ssidRange.length, ssid);
    fillRange(patched, passwdRange.start, passwdRange.length, passwd);
    return patched.buffer;
};

const defaultLog = () => {};

export function initWifiPatcher({ installButton, openButton, dialog, form, statusEl, log = defaultLog }) {
    if (!installButton || !openButton || !dialog || !form || !statusEl) {
        return {
            updateBaseManifest: () => {},
            clearPatch: () => {}
        };
    }

    let baseManifest = installButton.getAttribute('manifest') || '';
    let manifestBlobUrl = null;
    let binaryBlobUrl = null;
    let patching = false;
    let patchApplied = false;
    const defaultOpenLabel = openButton.textContent;

    const setStatus = (message) => {
        statusEl.textContent = message;
    };

    const closeDialog = () => {
        dialog.classList.add('hidden');
    };

    const openDialog = () => {
        if (patching || patchApplied) return;
        dialog.classList.remove('hidden');
    };

    const clearPatches = () => {
        if (manifestBlobUrl) {
            URL.revokeObjectURL(manifestBlobUrl);
            manifestBlobUrl = null;
        }
        if (binaryBlobUrl) {
            URL.revokeObjectURL(binaryBlobUrl);
            binaryBlobUrl = null;
        }
        patchApplied = false;
        openButton.disabled = false;
        openButton.textContent = defaultOpenLabel;
        if (baseManifest) {
            installButton.setAttribute('manifest', baseManifest);
        } else {
            installButton.removeAttribute('manifest');
        }
        setStatus(DEFAULT_STATUS_MESSAGE);
    };

    const updateBaseManifest = (manifestUrl) => {
        baseManifest = manifestUrl;
        clearPatches();
    };

    const withDialogControls = () => {
        dialog.querySelectorAll('[data-dialog-close]').forEach((element) => {
            element.addEventListener('click', (event) => {
                event.preventDefault();
                closeDialog();
            });
        });
    };

    const patchFirmware = async (ssid, passwd, firmwareUrl = null) => {
        if (!baseManifest && !firmwareUrl) {
            throw new Error('Manifest URL or firmware URL is required for Wi-Fi patching.');
        }
        patching = true;
        setStatus('Fetching firmware to patch...');

        let patchedBuffer;

        if (firmwareUrl) {
            // Direct firmware patching for OTA files
            const dataResponse = await fetch(firmwareUrl, { cache: 'no-store' });
            if (!dataResponse.ok) throw new Error('Failed to download firmware blob');
            const buffer = await dataResponse.arrayBuffer();
            patchedBuffer = patchBuffer(buffer, ssid, passwd);
        } else {
            // Manifest-based patching for flashing
            const manifestResponse = await fetch(baseManifest, { cache: 'no-store' });
            if (!manifestResponse.ok) throw new Error('Failed to download manifest');
            const manifest = await manifestResponse.json();
            const part = manifest?.builds?.[0]?.parts?.[0];
            if (!part || !part.path) throw new Error('Manifest missing firmware part');
            const binUrl = new URL(part.path, baseManifest).href;
            const dataResponse = await fetch(binUrl, { cache: 'no-store' });
            if (!dataResponse.ok) throw new Error('Failed to download firmware blob');
            const buffer = await dataResponse.arrayBuffer();
            patchedBuffer = patchBuffer(buffer, ssid, passwd);

            if (binaryBlobUrl) {
                URL.revokeObjectURL(binaryBlobUrl);
            }
            binaryBlobUrl = URL.createObjectURL(new Blob([patchedBuffer], { type: 'application/octet-stream' }));

            const patchedManifest = JSON.parse(JSON.stringify(manifest));
            patchedManifest.builds[0].parts[0].path = binaryBlobUrl;

            if (manifestBlobUrl) {
                URL.revokeObjectURL(manifestBlobUrl);
            }
            manifestBlobUrl = URL.createObjectURL(new Blob([JSON.stringify(patchedManifest)], { type: 'application/json' }));
            installButton.setAttribute('manifest', manifestBlobUrl);
        }

        patchApplied = true;
        openButton.disabled = true;
        openButton.textContent = 'Patch applied';
        setStatus('Patched SSID/password applied.');
        log('Wi-Fi settings patched locally before flashing.');

        // Return patched buffer for OTA usage
        return patchedBuffer;
    };

    form.addEventListener('submit', async (event) => {
        event.preventDefault();
        const formData = new FormData(form);
        const ssid = (formData.get('ssid') || '').trim();
        const passwd = (formData.get('passwd') || '').trim();
        if (!ssid || !passwd) {
            setStatus('Provide both SSID and password.');
            return;
        }
        if (ssid.length > MAX_FIELD_LENGTH || passwd.length > MAX_FIELD_LENGTH) {
            setStatus('SSID/password must be 20 characters or fewer.');
            return;
        }
        if (patching) {
            setStatus('Patch already in progress...');
            return;
        }
        try {
            await patchFirmware(ssid, passwd);
        } catch (error) {
            setStatus('Patch failed.');
            log(`Wi-Fi patch error: ${error.message}`, 'error');
        } finally {
            patching = false;
            closeDialog();
        }
    });

    const resetButton = form.querySelector('[data-reset]');
    if (resetButton) {
        resetButton.addEventListener('click', (event) => {
            event.preventDefault();
            clearPatches();
            closeDialog();
            log('Wi-Fi patch cleared, using default manifest.');
        });
    }

    openButton.addEventListener('click', openDialog);
    withDialogControls();
    setStatus(DEFAULT_STATUS_MESSAGE);

    return {
        updateBaseManifest,
        clearPatch: clearPatches,
        patchFirmware
    };
}

(function () {
    const MAX_BIN_SIZE_BYTES = 4 * 1024 * 1024; // 4MB per file ceiling
    const MIN_BIN_SIZE_BYTES = 100 * 1024; // 100KB per file floor
    const BANNED_NAME_TOKENS = ['merged', 'bootloader', 'partition'];
    const toMb = (bytes) => (bytes / (1024 * 1024)).toFixed(1);

    class LiteOtaUploader {
        constructor(options = {}) {
            this.uploadArea = options.uploadArea;
            this.fileInput = options.fileInput;
            this.progressBar = options.progressBar;
            this.progressFill = options.progressFill;
            this.toast = options.toast || (() => {});
            this.queue = [];
            this.uploading = false;
            this.lastFirmwareUploadMs = null;
            this.lastVersionFirstOkMs = null;
            this.lastVersionSettleMs = null;

            if (this.uploadArea) {
                this.uploadArea.addEventListener('click', () => this.fileInput?.click());
                this.uploadArea.addEventListener('dragover', (e) => {
                    e.preventDefault();
                    this.uploadArea.classList.add('dragover');
                });
                this.uploadArea.addEventListener('dragleave', () => {
                    this.uploadArea.classList.remove('dragover');
                });
                this.uploadArea.addEventListener('drop', (e) => {
                    e.preventDefault();
                    this.uploadArea.classList.remove('dragover');
                    if (e.dataTransfer?.files?.length) {
                        this.enqueueFiles(e.dataTransfer.files);
                    }
                });
            }

            if (this.fileInput) {
                this.fileInput.addEventListener('change', (event) => {
                    const files = event.target.files;
                    if (files && files.length) {
                        this.enqueueFiles(files);
                    }
                    event.target.value = '';
                });
            }
        }

        enqueueFiles(fileList) {
            if (this.uploading) {
                this.toast('Upload already in progress. Please wait for it to finish.', 'warning');
                return;
            }
            const files = Array.from(fileList || []);
            if (!files.length) {
                this.toast('No files detected.', 'warning');
                return;
            }

            const validationError = this.validateFiles(files);
            if (validationError) {
                this.toast(validationError, 'error');
                return;
            }

            const tasks = files.map((file) => ({
                file,
                mode: this.inferMode(file)
            }));

            // Ensure firmware is always uploaded before filesystem so we
            // can handle the firmware reboot, then continue with LittleFS.
            tasks.sort((a, b) => {
                if (a.mode === b.mode) return 0;
                return a.mode === 'fw' ? -1 : 1;
            });

            this.queue = tasks;
            if (!this.queue.length) {
                this.toast('No files detected.', 'warning');
                return;
            }
            this.startQueue();
        }

        validateFiles(files) {
            if (files.length > 2) {
                return 'Upload up to two files (firmware and optional LittleFS).';
            }

            for (const file of files) {
                const name = file.name.toLowerCase();
                if (BANNED_NAME_TOKENS.some((token) => name.includes(token))) {
                    return 'Files containing "merged", "bootloader", or "partition" are not allowed. Upload firmware and LittleFS images only.';
                }

                if (file.size > MAX_BIN_SIZE_BYTES) {
                    return `${file.name} is too large (${toMb(file.size)} MB). Max size is 4 MB per file.`;
                }
                if (file.size < MIN_BIN_SIZE_BYTES) {
                    return `${file.name} is too small (${toMb(file.size)} MB). Min size is 100 KB per file.`;
                }
            }

            return null;
        }

        inferMode(file) {
            const name = file.name.toLowerCase();
            if (name.includes('little') || name.includes('lfs') || name.includes('fs')) {
                return 'fs';
            }
            return 'fw';
        }

        async startQueue() {
            this.uploading = true;
            this.setBusyState(true);
            this.lastFirmwareUploadMs = null;
            this.lastVersionFirstOkMs = null;
            this.lastVersionSettleMs = null;
            try {
                for (let i = 0; i < this.queue.length; i++) {
                    const task = this.queue[i];
                    const hasFsAfter = this.queue.slice(i + 1).some((t) => t.mode === 'fs');
                    await this.runTask(task, hasFsAfter);
                }
                this.toast('Uploads complete. Firmware uploads will reboot the ESP32 automatically.', 'success');
            } catch (error) {
                this.toast(error.message || 'Upload failed', 'error');
            } finally {
                this.uploading = false;
                this.queue = [];
                this.setBusyState(false);
            }
        }

        async runTask(task, hasFsAfter) {
            const label = task.mode === 'fs' ? 'filesystem' : 'firmware';
            this.toast(`Starting ${label} upload: ${task.file.name}`, 'info');

            if (task.mode === 'fs' && this.lastFirmwareUploadMs) {
                const now = Date.now();
                const sinceFirmware = ((now - this.lastFirmwareUploadMs) / 1000).toFixed(1);
                const sinceFirstOk = this.lastVersionFirstOkMs
                    ? ((now - this.lastVersionFirstOkMs) / 1000).toFixed(1)
                    : 'n/a';
                const settle = this.lastVersionSettleMs ? (this.lastVersionSettleMs / 1000).toFixed(1) : '0';
                const timingMsg = `Filesystem OTA starting: ${sinceFirmware}s after firmware, ${sinceFirstOk}s after first /version, settled ${settle}s.`;
                // this.toast(timingMsg, 'info');
                console.info(`[LiteOTA] ${timingMsg}`);
            }

            await this.startOtaMode(task.mode);
            await this.uploadFile(task.file, label);
            this.toast(`${task.file.name} uploaded (${label})`, 'success');

            // If firmware was just uploaded and filesystem uploads are queued,
            // wait for the ESP32 to reboot and come back online before
            // continuing with LittleFS uploads.
            if (task.mode === 'fw' && hasFsAfter) {
                this.lastFirmwareUploadMs = Date.now();
                this.toast('Firmware uploaded. Waiting for device reboot before filesystem update...', 'info');
                const rebootInfo = await this.waitForReboot(120000, 2000, {
                    minSettlingMs: 5000,
                    requiredConsecutive: 2
                });
                this.lastVersionFirstOkMs = rebootInfo.firstOkMs;
                this.lastVersionSettleMs = rebootInfo.settleMs;
            }
        }

        async startOtaMode(mode) {
            const response = await fetch(`/ota/start?mode=${mode === 'fs' ? 'fs' : 'fw'}`);
            if (!response.ok) {
                const text = await response.text();
                throw new Error(text || 'Failed to initiate OTA session');
            }
        }

        uploadFile(file, label) {
            return new Promise((resolve, reject) => {
                const xhr = new XMLHttpRequest();
                xhr.open('POST', '/ota/upload');

                xhr.upload.onprogress = (event) => {
                    if (event.lengthComputable) {
                        const percent = (event.loaded / event.total) * 100;
                        this.updateProgress(percent, `${label}: ${Math.round(percent)}%`);
                    }
                };

                xhr.onload = () => {
                    if (xhr.status === 200) {
                        this.updateProgress(100, `${label}: 100%`);
                        resolve();
                    } else {
                        reject(new Error(xhr.responseText || `Upload failed (${xhr.status})`));
                    }
                };

                xhr.onerror = () => reject(new Error('Network error during upload'));

                const formData = new FormData();
                formData.append('firmware', file, file.name);
                xhr.send(formData);
            });
        }

        async waitForReboot(timeoutMs = 120000, pollMs = 2000, options = {}) {
            const start = Date.now();
            const { minSettlingMs = 0, requiredConsecutive = 1 } = options;
            let firstOkMs = null;
            let consecutiveOk = 0;

            while (Date.now() - start < timeoutMs) {
                // Small delay between polls
                await new Promise((resolve) => setTimeout(resolve, pollMs));

                try {
                    // /version is a lightweight JSON endpoint exposed by the firmware.
                    const response = await fetch('/version', { cache: 'no-store' });
                    if (response.ok) {
                        consecutiveOk += 1;
                        if (firstOkMs === null) {
                            firstOkMs = Date.now();
                        }
                        const settled = consecutiveOk >= requiredConsecutive && (Date.now() - firstOkMs) >= minSettlingMs;
                        if (settled) {
                            return {
                                firstOkMs,
                                settleMs: Date.now() - firstOkMs
                            };
                        }
                        continue;
                    }
                    consecutiveOk = 0;
                } catch (e) {
                    consecutiveOk = 0;
                    // While the ESP32 is rebooting, requests will fail – ignore and keep polling.
                }
            }

            throw new Error('Timed out waiting for device reboot after firmware update.');
        }

        setBusyState(isBusy) {
            if (this.progressBar) {
                this.progressBar.style.display = isBusy ? 'block' : 'none';
            }
            if (this.uploadArea) {
                this.uploadArea.classList.toggle('disabled', isBusy);
                this.uploadArea.querySelector('h3')?.classList.toggle('hidden', isBusy);
            }
            if (!isBusy) {
                this.updateProgress(0, '0%');
            }
        }

        updateProgress(percent, label) {
            if (this.progressFill) {
                this.progressFill.style.width = `${Math.min(percent, 100)}%`;
                this.progressFill.textContent = label;
            }
        }
    }

    window.LiteOtaUploader = LiteOtaUploader;
})();

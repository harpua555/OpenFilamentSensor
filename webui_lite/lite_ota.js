(function () {
    class LiteOtaUploader {
        constructor(options = {}) {
            this.uploadArea = options.uploadArea;
            this.fileInput = options.fileInput;
            this.progressBar = options.progressBar;
            this.progressFill = options.progressFill;
            this.toast = options.toast || (() => {});
            this.queue = [];
            this.uploading = false;

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
            const tasks = Array.from(fileList).map((file) => ({
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
            await this.startOtaMode(task.mode);
            await this.uploadFile(task.file, label);
            this.toast(`${task.file.name} uploaded (${label})`, 'success');

            // If firmware was just uploaded and filesystem uploads are queued,
            // wait for the ESP32 to reboot and come back online before
            // continuing with LittleFS uploads.
            if (task.mode === 'fw' && hasFsAfter) {
                this.toast('Firmware uploaded. Waiting for device reboot before filesystem update...', 'info');
                await this.waitForReboot();
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

        async waitForReboot(timeoutMs = 120000, pollMs = 2000) {
            const start = Date.now();

            while (Date.now() - start < timeoutMs) {
                // Small delay between polls
                await new Promise((resolve) => setTimeout(resolve, pollMs));

                try {
                    // /version is a lightweight JSON endpoint exposed by the firmware.
                    const response = await fetch('/version', { cache: 'no-store' });
                    if (response.ok) {
                        return;
                    }
                } catch (e) {
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

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
            this.queue = Array.from(fileList).map((file) => ({
                file,
                mode: this.inferMode(file)
            }));
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
                for (const task of this.queue) {
                    await this.runTask(task);
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

        async runTask(task) {
            const label = task.mode === 'fs' ? 'filesystem' : 'firmware';
            this.toast(`Starting ${label} upload: ${task.file.name}`, 'info');
            await this.startOtaMode(task.mode);
            await this.uploadFile(task.file, label);
            this.toast(`${task.file.name} uploaded (${label})`, 'success');
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

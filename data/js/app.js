const appCreatedEvent = new CustomEvent('appCreated')

const vueApp = Vue.createApp({
    data() {
        return {
            // Parameters (settings pages)
            parameters: [],
            originalValues: {},
            parametersHelpTexts: [],
            isPostingForm: false,
            showPostSucceeded: false,

            // Reboot notification
            showRebootBanner: false,
            changedRebootParams: [],

            // Live status (home page, updated via SSE)
            status: {
                currentWeight: 0,
                goalWeight: 0,
                weightOffset: 0,
                brewing: false,
                shotTimer: 0,
                brewByTimeOnly: false,
                freeHeap: 0,
                uptime: 0
            },
            scaleConnected: false,

            // Config upload
            selectedFile: null,
            isUploading: false,
            uploadMessage: '',
            uploadSuccess: false,

            // Factory reset
            factoryResetMessage: '',
            factoryResetSuccess: false
        }
    },

    mounted() {
        // Fetch all parameters for settings pages
        this.fetchParameters();

        // Start SSE for live status updates
        this.startSSE();

        // Initial status fetch
        this.fetchStatus();
    },

    methods: {
        // --- SSE for live data ---
        startSSE() {
            const evtSource = new EventSource('/events');

            evtSource.addEventListener('status', (event) => {
                try {
                    const data = JSON.parse(event.data);
                    Object.assign(this.status, data);
                } catch (e) {
                    console.error('SSE parse error:', e);
                }
            });

            evtSource.onerror = () => {
                console.warn('SSE connection lost, will auto-reconnect');
            };
        },

        async fetchStatus() {
            try {
                const response = await fetch('/status');
                const data = await response.json();
                Object.assign(this.status, data);
            } catch (e) {
                console.error('Status fetch error:', e);
            }
        },

        // --- Parameter management ---
        async fetchParameters() {
            this.parameters = [];
            this.originalValues = {};
            let offset = 0;
            const limit = 20;
            let moreData = true;

            while (moreData) {
                let url = `/parameters?offset=${offset}&limit=${limit}`;

                try {
                    const response = await fetch(url);
                    const json = await response.json();

                    if (!json.parameters || json.parameters.length === 0) {
                        moreData = false;
                        break;
                    }

                    json.parameters.forEach(param => {
                        this.parameters.push(param);
                        this.originalValues[param.name] = param.value;
                    });

                    if (json.parameters.length < limit) {
                        moreData = false;
                    } else {
                        offset += limit;
                    }
                } catch (err) {
                    console.error('Error fetching parameters:', err);
                    moreData = false;
                }
            }
        },

        postParameters() {
            const formBody = [];

            const displayedSections = this.parameterSectionsComputed;

            const displayedParameters = [];
            Object.values(displayedSections).forEach(section => {
                Object.values(section).forEach(group => {
                    group.forEach(param => {
                        if (param.show) {
                            displayedParameters.push(param);
                        }
                    });
                });
            });

            const rebootParamsChanged = displayedParameters
                .filter(param => {
                    if (!param.reboot) return false;
                    return String(param.value) !== String(this.originalValues[param.name]);
                })
                .map(param => param.displayName);

            displayedParameters.forEach(param => {
                formBody.push(param.name + "=" + encodeURIComponent(param.value));
            });

            if (formBody.length === 0) return;

            this.isPostingForm = true;

            fetch("/parameters", {
                method: "POST",
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                cache: 'no-cache',
                body: formBody.join("&")
            })
            .then(response => {
                if (!response.ok) throw new Error(`HTTP ${response.status}`);
                return response.text();
            })
            .then(() => {
                this.fetchParameters();

                if (rebootParamsChanged.length > 0) {
                    this.changedRebootParams = rebootParamsChanged;
                    this.showRebootBanner = true;
                }
            })
            .catch(err => console.error("Error saving parameters:", err))
            .finally(() => {
                this.isPostingForm = false;
                this.showPostSucceeded = true;
                setTimeout(() => { this.showPostSucceeded = false; }, 2000);
            });
        },

        fetchHelpText(paramName) {
            if (!(paramName in this.parametersHelpTexts)) {
                const param = this.parameters.find(p => p.name === paramName);
                const requiresReboot = param && param.reboot;

                fetch("/parameterHelp/?param=" + paramName)
                    .then(response => response.json())
                    .then(data => {
                        let helpText = data['helpText'] || '';
                        if (requiresReboot) {
                            helpText += '<br><strong>Changes require a reboot</strong>';
                        }
                        this.parametersHelpTexts[paramName] = helpText;
                    });
            }
        },

        sectionName(sectionId) {
            const names = {
                0: 'Brew',
                1: 'Scale',
                2: 'Switch',
                3: 'System',
                4: 'Other'
            };
            return names[sectionId] || 'Unknown';
        },

        getGroupFromPosition(position) {
            return Math.floor((position % 100) / 10);
        },

        getInputType(param) {
            switch(param.type) {
                case 5: return 'select';
                case 4: return 'text';
                default: return 'number';
            }
        },

        getNumberStep(param) {
            switch(param.type) {
                case 0: case 1: return '1';
                case 2: case 3: return '0.01';
                default: return '1';
            }
        },

        isBoolean(param) {
            return param.type === 1 && param.min === 0 && param.max === 1;
        },

        // --- System actions ---
        async confirmReset() {
            if (!window.confirm("Reset WiFi settings? This will erase saved credentials and restart the device.")) return;
            try {
                const response = await fetch("/wifireset", { method: "POST" });
                const text = await response.text();
                alert(text);
            } catch (err) {
                alert("Reset failed: " + err.message);
            }
        },

        confirmRestart() {
            if (confirm('Are you sure you want to restart the device?')) {
                this.restartMachine();
            }
        },

        async confirmFactoryReset() {
            if (!window.confirm("Reset config to defaults and restart? This can't be undone.")) return;
            this.factoryResetMessage = 'Factory reset initiated. Device is restarting...';
            this.factoryResetSuccess = true;
            try {
                await fetch("/factoryreset", { method: "POST" });
            } catch (err) {
                console.log('Device restarting after factory reset...');
            }
        },

        dismissRebootBanner() {
            this.showRebootBanner = false;
            this.changedRebootParams = [];
        },

        async restartMachine() {
            try {
                await fetch('/restart', { method: 'POST' });
                alert('Device is restarting...');
            } catch (e) {
                // Expected — device is restarting
            }
        },

        // --- Config upload ---
        handleFileSelect(event) {
            const file = event.target.files[0];
            this.selectedFile = file;
            this.uploadMessage = '';

            if (file) {
                if (!file.name.toLowerCase().endsWith('.json')) {
                    this.uploadMessage = 'Please select a valid JSON file.';
                    this.uploadSuccess = false;
                    this.selectedFile = null;
                    return;
                }
                if (file.size > 50 * 1024) {
                    this.uploadMessage = 'File too large. Maximum 50KB.';
                    this.uploadSuccess = false;
                    this.selectedFile = null;
                    return;
                }
                this.uploadMessage = `Selected: ${file.name} (${(file.size / 1024).toFixed(1)} KB)`;
                this.uploadSuccess = true;
            }
        },

        async uploadConfig() {
            if (!this.selectedFile) {
                this.uploadMessage = 'Please select a file first.';
                this.uploadSuccess = false;
                return;
            }

            this.isUploading = true;
            this.uploadMessage = 'Uploading...';

            try {
                const formData = new FormData();
                formData.append('config', this.selectedFile);

                const response = await fetch('/upload/config', { method: 'POST', body: formData });

                let result;
                try { result = await response.json(); } catch (e) { result = { success: response.ok }; }

                this.uploadSuccess = result.success;
                this.uploadMessage = result.message || (result.success ? 'Upload successful!' : 'Upload failed.');

                await new Promise(r => setTimeout(r, 2000));
                this.uploadMessage += ' Restarting device...';
                await new Promise(r => setTimeout(r, 1000));

                try { await this.restartMachine(); } catch (e) { /* restarting */ }

            } catch (error) {
                this.uploadMessage = 'Upload failed. Please try again.';
                this.uploadSuccess = false;
            } finally {
                this.isUploading = false;
            }
        },

        // --- Helpers ---
        formatUptime(seconds) {
            const h = Math.floor(seconds / 3600);
            const m = Math.floor((seconds % 3600) / 60);
            const s = seconds % 60;
            if (h > 0) return `${h}h ${m}m`;
            if (m > 0) return `${m}m ${s}s`;
            return `${s}s`;
        }
    },

    computed: {
        parameterSectionsComputed() {
            const sections = groupBy(this.parameters.filter(p => p.show), "section");
            const result = {};
            Object.keys(sections).forEach(key => {
                result[key] = groupBy(sections[key], p => this.getGroupFromPosition(p.position));
            });
            return result;
        }
    }
});

window.vueApp = vueApp;
window.dispatchEvent(appCreatedEvent);
window.appCreated = true;

function groupBy(array, key) {
    const result = {};
    array.forEach(item => {
        const k = typeof key === 'function' ? key(item) : item[key];
        if (!result[k]) result[k] = [];
        result[k].push(item);
    });
    return result;
}

// Bootstrap Popovers — close on outside click
document.querySelector('body').addEventListener('click', function (e) {
    if (!e.target.classList.contains("popover-header")) {
        if (e.target.parentElement && e.target.parentElement.getAttribute("data-bs-toggle") !== "popover") {
            document.querySelectorAll('[data-bs-toggle="popover"]').forEach(el => {
                const popover = bootstrap.Popover.getInstance(el);
                if (popover) popover.hide();
            });
        } else {
            e.preventDefault();
            const popover = bootstrap.Popover.getOrCreateInstance(e.target.parentElement);
            popover.show();
        }
    }
});

/**
 * Shot profile chart using uPlot.
 * Tracks weight and flow rate (g/s) during a brew and renders a dual-axis chart.
 */

const shotChart = {
    data: {
        time: [],
        weight: [],
        flowRate: []
    },
    instance: null,
    wasBrewing: false,
    _resizeObserver: null,

    /**
     * Called on every SSE status update. Accumulates data during brewing,
     * computes flow rate, and triggers chart rendering.
     */
    update(status) {
        // Detect brew start → clear previous data
        if (status.brewing && !this.wasBrewing) {
            this.reset();
        }
        this.wasBrewing = status.brewing;

        // Nothing to do if not brewing and no data collected yet
        if (!status.brewing && this.data.time.length === 0) {
            return;
        }

        if (status.brewing) {
            const t = status.shotTimer;
            const w = status.currentWeight;

            this.data.time.push(t);
            this.data.weight.push(w);

            // Compute flow rate from last two samples
            const n = this.data.time.length;

            if (n >= 2) {
                const dt = this.data.time[n - 1] - this.data.time[n - 2];
                const dw = this.data.weight[n - 1] - this.data.weight[n - 2];
                this.data.flowRate.push(dt > 0 ? Math.max(0, dw / dt) : 0);
            } else {
                this.data.flowRate.push(0);
            }
        }

        this.render();
    },

    /**
     * Create or update the uPlot chart.
     * @param {Function} nextTick - Vue's $nextTick to wait for DOM updates
     */
    render(nextTick) {
        if (this.data.time.length < 2) return;

        const seriesData = [
            this.data.time,
            this.data.weight,
            this.data.flowRate
        ];

        if (this.instance) {
            this.instance.setData(seriesData);
            return;
        }

        // Chart div may not be in DOM yet (behind v-if), wait for Vue to update
        const create = () => {
            const el = document.getElementById('flow-chart');
            if (!el) return;

            this.instance = new uPlot(this._opts(el.clientWidth || 600), seriesData, el);

            // Resize chart when container width changes
            this._resizeObserver = new ResizeObserver(entries => {
                if (this.instance) {
                    const newWidth = entries[0].contentRect.width;

                    if (newWidth > 0) {
                        this.instance.setSize({ width: newWidth, height: 250 });
                    }
                }
            });
            this._resizeObserver.observe(el);
        };

        if (nextTick) {
            nextTick(create);
        } else {
            create();
        }
    },

    /** Clear all data and destroy the chart instance. */
    reset() {
        this.data = { time: [], weight: [], flowRate: [] };
        this.destroy();
    },

    /** Destroy the uPlot instance. */
    destroy() {
        if (this._resizeObserver) {
            this._resizeObserver.disconnect();
            this._resizeObserver = null;
        }

        if (this.instance) {
            this.instance.destroy();
            this.instance = null;
        }
    },

    /** @returns {boolean} Whether there is enough data to display a chart. */
    hasData() {
        return this.data.time.length >= 2;
    },

    /** Build uPlot options object. */
    _opts(width) {
        return {
            width,
            height: 250,
            cursor: { show: true },
            legend: { show: true },
            scales: {
                x: { time: false },
                y: { auto: true, range: [0, null] },
                y2: { auto: true, range: [0, null] }
            },
            axes: [
                { label: 'Time (s)', stroke: '#666', grid: { stroke: '#eee' } },
                { label: 'Weight (g)', stroke: '#198754', grid: { stroke: '#eee' }, scale: 'y' },
                { label: 'Flow (g/s)', stroke: '#0d6efd', side: 1, grid: { show: false }, scale: 'y2' }
            ],
            series: [
                {},
                {
                    label: 'Weight (g)',
                    stroke: '#198754',
                    width: 2,
                    scale: 'y',
                    fill: 'rgba(25, 135, 84, 0.08)'
                },
                {
                    label: 'Flow (g/s)',
                    stroke: '#0d6efd',
                    width: 2,
                    scale: 'y2'
                }
            ]
        };
    }
};


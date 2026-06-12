class CrackGradient {
    constructor(options = {}) {
        this.maxDepth = options.maxDepth || 200;
        this.stops = options.stops || [
            { depth: 0,   color: { r: 0.19, g: 0.51, b: 0.81 } },
            { depth: 50,  color: { r: 0.22, g: 0.63, b: 0.41 } },
            { depth: 100, color: { r: 0.93, g: 0.79, b: 0.29 } },
            { depth: 150, color: { r: 0.93, g: 0.53, b: 0.21 } },
            { depth: 200, color: { r: 0.90, g: 0.24, b: 0.24 } },
        ];

        this._buildLUT();
    }

    _buildLUT() {
        this.lutSize = 256;
        this.colorLUT = new Float32Array(this.lutSize * 3);
        this.colorLUTUint8 = new Uint8Array(this.lutSize * 3);

        for (let i = 0; i < this.lutSize; i++) {
            const depth = (i / (this.lutSize - 1)) * this.maxDepth;
            const c = this.sampleColorAtDepth(depth);
            this.colorLUT[i * 3]     = c.r;
            this.colorLUT[i * 3 + 1] = c.g;
            this.colorLUT[i * 3 + 2] = c.b;
            this.colorLUTUint8[i * 3]     = Math.round(c.r * 255);
            this.colorLUTUint8[i * 3 + 1] = Math.round(c.g * 255);
            this.colorLUTUint8[i * 3 + 2] = Math.round(c.b * 255);
        }
    }

    sampleColorAtDepth(depth) {
        if (depth <= this.stops[0].depth) return this.stops[0].color;
        if (depth >= this.stops[this.stops.length - 1].depth) {
            return this.stops[this.stops.length - 1].color;
        }

        for (let i = 0; i < this.stops.length - 1; i++) {
            if (depth >= this.stops[i].depth && depth <= this.stops[i + 1].depth) {
                const t = (depth - this.stops[i].depth) /
                          (this.stops[i + 1].depth - this.stops[i].depth);
                return {
                    r: this.stops[i].color.r + (this.stops[i + 1].color.r - this.stops[i].color.r) * t,
                    g: this.stops[i].color.g + (this.stops[i + 1].color.g - this.stops[i].color.g) * t,
                    b: this.stops[i].color.b + (this.stops[i + 1].color.b - this.stops[i].color.b) * t,
                };
            }
        }

        return this.stops[this.stops.length - 1].color;
    }

    sampleColor(depth) {
        const idx = Math.min(this.lutSize - 1, Math.max(0,
            Math.round((depth / this.maxDepth) * (this.lutSize - 1))));
        return {
            r: this.colorLUT[idx * 3],
            g: this.colorLUT[idx * 3 + 1],
            b: this.colorLUT[idx * 3 + 2],
        };
    }

    sampleColorHex(depth) {
        const c = this.sampleColor(depth);
        const r = Math.round(c.r * 255).toString(16).padStart(2, '0');
        const g = Math.round(c.g * 255).toString(16).padStart(2, '0');
        const b = Math.round(c.b * 255).toString(16).padStart(2, '0');
        return `#${r}${g}${b}`;
    }

    getColorScaleFn() {
        return (depth) => this.sampleColor(depth);
    }

    createLegendCanvas(width = 20, height = 200) {
        const canvas = document.createElement('canvas');
        canvas.width = width;
        canvas.height = height;
        const ctx = canvas.getContext('2d');

        for (let i = 0; i < height; i++) {
            const depth = (1 - i / height) * this.maxDepth;
            const c = this.sampleColor(depth);
            ctx.fillStyle = `rgb(${Math.round(c.r * 255)}, ${Math.round(c.g * 255)}, ${Math.round(c.b * 255)})`;
            ctx.fillRect(0, i, width, 1);
        }

        return canvas;
    }

    createLegendDOM(container) {
        const wrapper = document.createElement('div');
        wrapper.style.cssText = 'display:flex;align-items:flex-start;gap:8px;';

        const canvas = this.createLegendCanvas(16, 160);
        canvas.style.cssText = 'border:1px solid #444;border-radius:2px;';

        const labels = document.createElement('div');
        labels.style.cssText = 'display:flex;flex-direction:column;justify-content:space-between;height:160px;font-size:11px;color:#a0aec0;';
        labels.innerHTML = `
            <div>${this.maxDepth}μm</div>
            <div>${(this.maxDepth * 0.75).toFixed(0)}μm</div>
            <div>${(this.maxDepth * 0.5).toFixed(0)}μm</div>
            <div>${(this.maxDepth * 0.25).toFixed(0)}μm</div>
            <div>0μm</div>
        `;

        wrapper.appendChild(canvas);
        wrapper.appendChild(labels);

        if (container) {
            container.appendChild(wrapper);
        }
        return wrapper;
    }

    setMaxDepth(maxDepth) {
        this.maxDepth = maxDepth;
        this._buildLUT();
    }

    setStops(stops) {
        this.stops = stops;
        this._buildLUT();
    }
}

class StressHeatmap extends THREE.Object3D {
    constructor(porcelainMesh, options = {}) {
        super();

        this.porcelainMesh = porcelainMesh;
        this.visible = true;
        this.stressData = [];
        this.gridBounds = null;
        this.gridSpacing = null;
        this.useShaderMaterial = options.useShaderMaterial !== undefined ? options.useShaderMaterial : false;

        this.minStress = options.minStress || 0;
        this.maxStress = options.maxStress || 100;
        this.opacity = options.opacity !== undefined ? options.opacity : 0.85;

        this._buildColorLUT();

        this.heatmapMesh = null;
        this.originalMaterial = null;

        if (this.porcelainMesh) {
            this._initHeatmap();
        }
    }

    _buildColorLUT() {
        this.lutSize = 256;
        this.colorLUT = new Float32Array(this.lutSize * 3);
        this.colorLUTUint8 = new Uint8Array(this.lutSize * 3);

        for (let i = 0; i < this.lutSize; i++) {
            const t = i / (this.lutSize - 1);
            const c = this._jetColor(t);
            this.colorLUT[i * 3]     = c.r;
            this.colorLUT[i * 3 + 1] = c.g;
            this.colorLUT[i * 3 + 2] = c.b;
            this.colorLUTUint8[i * 3]     = Math.round(c.r * 255);
            this.colorLUTUint8[i * 3 + 1] = Math.round(c.g * 255);
            this.colorLUTUint8[i * 3 + 2] = Math.round(c.b * 255);
        }
    }

    _jetColor(t) {
        t = Math.max(0, Math.min(1, t));

        let r, g, b;

        if (t < 0.125) {
            r = 0;
            g = 0;
            b = 0.5 + 4 * t;
        } else if (t < 0.375) {
            r = 0;
            g = 4 * (t - 0.125);
            b = 1;
        } else if (t < 0.625) {
            r = 4 * (t - 0.375);
            g = 1;
            b = 1 - 4 * (t - 0.375);
        } else if (t < 0.875) {
            r = 1;
            g = 1 - 4 * (t - 0.625);
            b = 0;
        } else {
            r = 1 - 4 * (t - 0.875);
            g = 0;
            b = 0;
        }

        return { r, g, b };
    }

    sampleColorLUT(stressValue) {
        const normalized = (stressValue - this.minStress) / (this.maxStress - this.minStress);
        const idx = Math.min(this.lutSize - 1, Math.max(0,
            Math.round(normalized * (this.lutSize - 1))));
        return {
            r: this.colorLUT[idx * 3],
            g: this.colorLUT[idx * 3 + 1],
            b: this.colorLUT[idx * 3 + 2],
        };
    }

    sampleColorHex(stressValue) {
        const c = this.sampleColorLUT(stressValue);
        const r = Math.round(c.r * 255).toString(16).padStart(2, '0');
        const g = Math.round(c.g * 255).toString(16).padStart(2, '0');
        const b = Math.round(c.b * 255).toString(16).padStart(2, '0');
        return `#${r}${g}${b}`;
    }

    _initHeatmap() {
        if (this.useShaderMaterial) {
            this._initShaderMaterial();
        } else {
            this._initVertexColorMaterial();
        }
    }

    _initVertexColorMaterial() {
        const geometry = this.porcelainMesh.geometry.clone();
        const positionAttr = geometry.getAttribute('position');
        const vertexCount = positionAttr.count;

        const colors = new Float32Array(vertexCount * 3);
        for (let i = 0; i < vertexCount; i++) {
            colors[i * 3]     = 0.5;
            colors[i * 3 + 1] = 0.5;
            colors[i * 3 + 2] = 0.5;
        }

        geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));

        this.originalMaterial = this.porcelainMesh.material;

        this.heatmapMaterial = new THREE.MeshPhysicalMaterial({
            vertexColors: true,
            metalness: 0.1,
            roughness: 0.3,
            clearcoat: 0.5,
            clearcoatRoughness: 0.2,
            transparent: true,
            opacity: this.opacity,
            side: THREE.DoubleSide,
        });

        this.heatmapMesh = new THREE.Mesh(geometry, this.heatmapMaterial);
        this.heatmapMesh.castShadow = true;
        this.heatmapMesh.receiveShadow = true;
        this.heatmapMesh.visible = this.visible;

        this.add(this.heatmapMesh);
    }

    _initShaderMaterial() {
        const geometry = this.porcelainMesh.geometry.clone();

        this.originalMaterial = this.porcelainMesh.material;

        const vertexShader = `
            varying vec3 vPosition;
            varying vec3 vNormal;

            void main() {
                vPosition = position;
                vNormal = normalize(normalMatrix * normal);
                gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
            }
        `;

        const fragmentShader = `
            varying vec3 vPosition;
            varying vec3 vNormal;

            uniform float uMinStress;
            uniform float uMaxStress;
            uniform float uOpacity;
            uniform vec3 uGridMin;
            uniform vec3 uGridMax;
            uniform vec3 uGridSpacing;
            uniform int uGridSizeX;
            uniform int uGridSizeY;
            uniform int uGridSizeZ;
            uniform float uStressValues[1000];

            vec3 jetColor(float t) {
                t = clamp(t, 0.0, 1.0);
                vec3 color;
                if (t < 0.125) {
                    color = vec3(0.0, 0.0, 0.5 + 4.0 * t);
                } else if (t < 0.375) {
                    color = vec3(0.0, 4.0 * (t - 0.125), 1.0);
                } else if (t < 0.625) {
                    color = vec3(4.0 * (t - 0.375), 1.0, 1.0 - 4.0 * (t - 0.375));
                } else if (t < 0.875) {
                    color = vec3(1.0, 1.0 - 4.0 * (t - 0.625), 0.0);
                } else {
                    color = vec3(1.0 - 4.0 * (t - 0.875), 0.0, 0.0);
                }
                return color;
            }

            int getGridIndex(int ix, int iy, int iz) {
                return iz * uGridSizeX * uGridSizeY + iy * uGridSizeX + ix;
            }

            float trilinearInterpolation(vec3 pos) {
                vec3 gridPos = (pos - uGridMin) / uGridSpacing;

                int ix = int(floor(gridPos.x));
                int iy = int(floor(gridPos.y));
                int iz = int(floor(gridPos.z));

                ix = clamp(ix, 0, uGridSizeX - 2);
                iy = clamp(iy, 0, uGridSizeY - 2);
                iz = clamp(iz, 0, uGridSizeZ - 2);

                float fx = gridPos.x - float(ix);
                float fy = gridPos.y - float(iy);
                float fz = gridPos.z - float(iz);

                float c000 = uStressValues[getGridIndex(ix, iy, iz)];
                float c100 = uStressValues[getGridIndex(ix + 1, iy, iz)];
                float c010 = uStressValues[getGridIndex(ix, iy + 1, iz)];
                float c110 = uStressValues[getGridIndex(ix + 1, iy + 1, iz)];
                float c001 = uStressValues[getGridIndex(ix, iy, iz + 1)];
                float c101 = uStressValues[getGridIndex(ix + 1, iy, iz + 1)];
                float c011 = uStressValues[getGridIndex(ix, iy + 1, iz + 1)];
                float c111 = uStressValues[getGridIndex(ix + 1, iy + 1, iz + 1)];

                float c00 = c000 * (1.0 - fx) + c100 * fx;
                float c10 = c010 * (1.0 - fx) + c110 * fx;
                float c01 = c001 * (1.0 - fx) + c101 * fx;
                float c11 = c011 * (1.0 - fx) + c111 * fx;

                float c0 = c00 * (1.0 - fy) + c10 * fy;
                float c1 = c01 * (1.0 - fy) + c11 * fy;

                return c0 * (1.0 - fz) + c1 * fz;
            }

            void main() {
                float stress = trilinearInterpolation(vPosition);
                float normalized = (stress - uMinStress) / (uMaxStress - uMinStress);
                vec3 color = jetColor(normalized);

                vec3 lightDir = normalize(vec3(0.5, 1.0, 0.5));
                float diffuse = max(dot(vNormal, lightDir), 0.0);
                float ambient = 0.3;
                vec3 finalColor = color * (ambient + diffuse * 0.7);

                gl_FragColor = vec4(finalColor, uOpacity);
            }
        `;

        this.heatmapMaterial = new THREE.ShaderMaterial({
            vertexShader: vertexShader,
            fragmentShader: fragmentShader,
            uniforms: {
                uMinStress: { value: this.minStress },
                uMaxStress: { value: this.maxStress },
                uOpacity: { value: this.opacity },
                uGridMin: { value: new THREE.Vector3(0, 0, 0) },
                uGridMax: { value: new THREE.Vector3(1, 1, 1) },
                uGridSpacing: { value: new THREE.Vector3(1, 1, 1) },
                uGridSizeX: { value: 1 },
                uGridSizeY: { value: 1 },
                uGridSizeZ: { value: 1 },
                uStressValues: { value: new Array(1000).fill(0) },
            },
            transparent: true,
            side: THREE.DoubleSide,
        });

        this.heatmapMesh = new THREE.Mesh(geometry, this.heatmapMaterial);
        this.heatmapMesh.castShadow = true;
        this.heatmapMesh.receiveShadow = true;
        this.heatmapMesh.visible = this.visible;

        this.add(this.heatmapMesh);
    }

    updateStressData(stressData) {
        this.stressData = stressData || [];

        if (this.stressData.length === 0) {
            return;
        }

        this._computeGridBounds();

        if (this.useShaderMaterial) {
            this._updateShaderUniforms();
        } else {
            this._updateVertexColors();
        }
    }

    _computeGridBounds() {
        if (this.stressData.length === 0) return;

        let minX = Infinity, minY = Infinity, minZ = Infinity;
        let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;

        for (const pt of this.stressData) {
            minX = Math.min(minX, pt.x);
            minY = Math.min(minY, pt.y);
            minZ = Math.min(minZ, pt.z);
            maxX = Math.max(maxX, pt.x);
            maxY = Math.max(maxY, pt.y);
            maxZ = Math.max(maxZ, pt.z);
        }

        const xs = [...new Set(this.stressData.map(p => p.x))].sort((a, b) => a - b);
        const ys = [...new Set(this.stressData.map(p => p.y))].sort((a, b) => a - b);
        const zs = [...new Set(this.stressData.map(p => p.z))].sort((a, b) => a - b);

        this.gridBounds = { minX, minY, minZ, maxX, maxY, maxZ };
        this.gridSize = { x: xs.length, y: ys.length, z: zs.length };

        let spacingX = xs.length > 1 ? xs[1] - xs[0] : 1;
        let spacingY = ys.length > 1 ? ys[1] - ys[0] : 1;
        let spacingZ = zs.length > 1 ? zs[1] - zs[0] : 1;

        this.gridSpacing = { x: spacingX, y: spacingY, z: spacingZ };

        this.stressGrid = new Map();
        for (const pt of this.stressData) {
            const ix = Math.round((pt.x - minX) / spacingX);
            const iy = Math.round((pt.y - minY) / spacingY);
            const iz = Math.round((pt.z - minZ) / spacingZ);
            const key = `${ix},${iy},${iz}`;
            this.stressGrid.set(key, pt);
        }

        let dataMin = Infinity, dataMax = -Infinity;
        for (const pt of this.stressData) {
            dataMin = Math.min(dataMin, pt.von_mises);
            dataMax = Math.max(dataMax, pt.von_mises);
        }
        if (isFinite(dataMin) && isFinite(dataMax)) {
            this.minStress = dataMin;
            this.maxStress = dataMax;
        }
    }

    _updateShaderUniforms() {
        if (!this.heatmapMaterial || !this.gridBounds) return;

        const u = this.heatmapMaterial.uniforms;
        u.uMinStress.value = this.minStress;
        u.uMaxStress.value = this.maxStress;
        u.uGridMin.value.set(this.gridBounds.minX, this.gridBounds.minY, this.gridBounds.minZ);
        u.uGridMax.value.set(this.gridBounds.maxX, this.gridBounds.maxY, this.gridBounds.maxZ);
        u.uGridSpacing.value.set(this.gridSpacing.x, this.gridSpacing.y, this.gridSpacing.z);
        u.uGridSizeX.value = this.gridSize.x;
        u.uGridSizeY.value = this.gridSize.y;
        u.uGridSizeZ.value = this.gridSize.z;

        const maxSize = 1000;
        const stressArray = new Array(maxSize).fill(0);
        const totalSize = this.gridSize.x * this.gridSize.y * this.gridSize.z;

        for (let i = 0; i < Math.min(totalSize, maxSize); i++) {
            const iz = Math.floor(i / (this.gridSize.x * this.gridSize.y));
            const rem = i % (this.gridSize.x * this.gridSize.y);
            const iy = Math.floor(rem / this.gridSize.x);
            const ix = rem % this.gridSize.x;
            const key = `${ix},${iy},${iz}`;
            const pt = this.stressGrid.get(key);
            if (pt) {
                stressArray[i] = pt.von_mises;
            }
        }

        u.uStressValues.value = stressArray;
        this.heatmapMaterial.needsUpdate = true;
    }

    _updateVertexColors() {
        if (!this.heatmapMesh || !this.gridBounds) return;

        const geometry = this.heatmapMesh.geometry;
        const positionAttr = geometry.getAttribute('position');
        const colorAttr = geometry.getAttribute('color');
        const vertexCount = positionAttr.count;

        const worldMatrix = this.porcelainMesh.matrixWorld;
        const tempVec = new THREE.Vector3();

        for (let i = 0; i < vertexCount; i++) {
            tempVec.set(
                positionAttr.getX(i),
                positionAttr.getY(i),
                positionAttr.getZ(i)
            );

            tempVec.applyMatrix4(worldMatrix);

            const stress = this._interpolateStressAt(tempVec.x, tempVec.y, tempVec.z);
            const c = this.sampleColorLUT(stress);

            colorAttr.setXYZ(i, c.r, c.g, c.b);
        }

        colorAttr.needsUpdate = true;
        geometry.computeVertexNormals();
    }

    _interpolateStressAt(x, y, z) {
        if (!this.gridBounds || this.stressData.length === 0) {
            return this.minStress;
        }

        const { minX, minY, minZ } = this.gridBounds;
        const { x: sx, y: sy, z: sz } = this.gridSpacing;
        const { x: gx, y: gy, z: gz } = this.gridSize;

        const fx = (x - minX) / sx;
        const fy = (y - minY) / sy;
        const fz = (z - minZ) / sz;

        let ix = Math.floor(fx);
        let iy = Math.floor(fy);
        let iz = Math.floor(fz);

        ix = Math.max(0, Math.min(gx - 2, ix));
        iy = Math.max(0, Math.min(gy - 2, iy));
        iz = Math.max(0, Math.min(gz - 2, iz));

        const tx = fx - ix;
        const ty = fy - iy;
        const tz = fz - iz;

        const getStress = (i, j, k) => {
            const key = `${i},${j},${k}`;
            const pt = this.stressGrid.get(key);
            return pt ? pt.von_mises : this.minStress;
        };

        const c000 = getStress(ix, iy, iz);
        const c100 = getStress(ix + 1, iy, iz);
        const c010 = getStress(ix, iy + 1, iz);
        const c110 = getStress(ix + 1, iy + 1, iz);
        const c001 = getStress(ix, iy, iz + 1);
        const c101 = getStress(ix + 1, iy, iz + 1);
        const c011 = getStress(ix, iy + 1, iz + 1);
        const c111 = getStress(ix + 1, iy + 1, iz + 1);

        const c00 = c000 * (1 - tx) + c100 * tx;
        const c10 = c010 * (1 - tx) + c110 * tx;
        const c01 = c001 * (1 - tx) + c101 * tx;
        const c11 = c011 * (1 - tx) + c111 * tx;

        const c0 = c00 * (1 - ty) + c10 * ty;
        const c1 = c01 * (1 - ty) + c11 * ty;

        return c0 * (1 - tz) + c1 * tz;
    }

    getStressAtPoint(x, y, z) {
        const von_mises = this._interpolateStressAt(x, y, z);

        if (!this.gridBounds || this.stressData.length === 0) {
            return { von_mises, sigma_xx: 0, sigma_yy: 0, sigma_zz: 0 };
        }

        const { minX, minY, minZ } = this.gridBounds;
        const { x: sx, y: sy, z: sz } = this.gridSpacing;

        let ix = Math.round((x - minX) / sx);
        let iy = Math.round((y - minY) / sy);
        let iz = Math.round((z - minZ) / sz);

        ix = Math.max(0, Math.min(this.gridSize.x - 1, ix));
        iy = Math.max(0, Math.min(this.gridSize.y - 1, iy));
        iz = Math.max(0, Math.min(this.gridSize.z - 1, iz));

        const key = `${ix},${iy},${iz}`;
        const pt = this.stressGrid.get(key);

        if (pt) {
            return {
                von_mises: pt.von_mises,
                sigma_xx: pt.sigma_xx,
                sigma_yy: pt.sigma_yy,
                sigma_zz: pt.sigma_zz,
            };
        }

        return { von_mises, sigma_xx: 0, sigma_yy: 0, sigma_zz: 0 };
    }

    toggleHeatmap() {
        this.visible = !this.visible;
        if (this.heatmapMesh) {
            this.heatmapMesh.visible = this.visible;
        }
        return this.visible;
    }

    setVisible(visible) {
        this.visible = visible;
        if (this.heatmapMesh) {
            this.heatmapMesh.visible = visible;
        }
    }

    setOpacity(opacity) {
        this.opacity = Math.max(0, Math.min(1, opacity));
        if (this.heatmapMaterial) {
            this.heatmapMaterial.opacity = this.opacity;
            if (this.heatmapMaterial.uniforms && this.heatmapMaterial.uniforms.uOpacity) {
                this.heatmapMaterial.uniforms.uOpacity.value = this.opacity;
            }
        }
    }

    setStressRange(minStress, maxStress) {
        this.minStress = minStress;
        this.maxStress = maxStress;

        if (this.useShaderMaterial && this.heatmapMaterial) {
            this.heatmapMaterial.uniforms.uMinStress.value = minStress;
            this.heatmapMaterial.uniforms.uMaxStress.value = maxStress;
        } else if (this.stressData.length > 0) {
            this._updateVertexColors();
        }
    }

    setPorcelainMesh(mesh) {
        this.porcelainMesh = mesh;
        this.clear();

        if (this.heatmapMesh) {
            this.heatmapMesh.geometry.dispose();
            this.heatmapMesh.material.dispose();
            this.heatmapMesh = null;
        }

        if (this.porcelainMesh) {
            this._initHeatmap();
            if (this.stressData.length > 0) {
                this.updateStressData(this.stressData);
            }
        }
    }

    createLegendCanvas(width = 20, height = 200) {
        const canvas = document.createElement('canvas');
        canvas.width = width;
        canvas.height = height;
        const ctx = canvas.getContext('2d');

        for (let i = 0; i < height; i++) {
            const t = 1 - i / height;
            const stress = this.minStress + t * (this.maxStress - this.minStress);
            const c = this.sampleColorLUT(stress);
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

        const range = this.maxStress - this.minStress;
        labels.innerHTML = `
            <div>${this.maxStress.toFixed(1)} MPa</div>
            <div>${(this.minStress + range * 0.75).toFixed(1)} MPa</div>
            <div>${(this.minStress + range * 0.5).toFixed(1)} MPa</div>
            <div>${(this.minStress + range * 0.25).toFixed(1)} MPa</div>
            <div>${this.minStress.toFixed(1)} MPa</div>
        `;

        wrapper.appendChild(canvas);
        wrapper.appendChild(labels);

        if (container) {
            container.appendChild(wrapper);
        }
        return wrapper;
    }

    dispose() {
        this.clear();

        if (this.heatmapMesh) {
            if (this.heatmapMesh.geometry) {
                this.heatmapMesh.geometry.dispose();
            }
            if (this.heatmapMesh.material) {
                this.heatmapMesh.material.dispose();
            }
            this.heatmapMesh = null;
        }

        this.stressData = [];
        this.stressGrid = null;
    }
}

function generateJetColorLUT(size = 256) {
    const lut = new Float32Array(size * 3);
    const lutUint8 = new Uint8Array(size * 3);

    const computeJetColor = (t) => {
        t = Math.max(0, Math.min(1, t));
        let r, g, b;

        if (t < 0.125) {
            r = 0;
            g = 0;
            b = 0.5 + 4 * t;
        } else if (t < 0.375) {
            r = 0;
            g = 4 * (t - 0.125);
            b = 1;
        } else if (t < 0.625) {
            r = 4 * (t - 0.375);
            g = 1;
            b = 1 - 4 * (t - 0.375);
        } else if (t < 0.875) {
            r = 1;
            g = 1 - 4 * (t - 0.625);
            b = 0;
        } else {
            r = 1 - 4 * (t - 0.875);
            g = 0;
            b = 0;
        }

        return { r, g, b };
    };

    for (let i = 0; i < size; i++) {
        const t = i / (size - 1);
        const c = computeJetColor(t);

        lut[i * 3]     = c.r;
        lut[i * 3 + 1] = c.g;
        lut[i * 3 + 2] = c.b;
        lutUint8[i * 3]     = Math.round(c.r * 255);
        lutUint8[i * 3 + 1] = Math.round(c.g * 255);
        lutUint8[i * 3 + 2] = Math.round(c.b * 255);
    }

    const sample = (t) => {
        const idx = Math.min(size - 1, Math.max(0, Math.round(t * (size - 1))));
        return {
            r: lut[idx * 3],
            g: lut[idx * 3 + 1],
            b: lut[idx * 3 + 2],
        };
    };

    const sampleHex = (t) => {
        const c = sample(t);
        const r = Math.round(c.r * 255).toString(16).padStart(2, '0');
        const g = Math.round(c.g * 255).toString(16).padStart(2, '0');
        const b = Math.round(c.b * 255).toString(16).padStart(2, '0');
        return `#${r}${g}${b}`;
    };

    return {
        size,
        lut,
        lutUint8,
        sample,
        sampleHex,
    };
}

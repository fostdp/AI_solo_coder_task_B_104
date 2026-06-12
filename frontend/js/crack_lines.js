class MergedCrackLines {
    constructor() {
        this.crackData = [];
        this.lineSegments = null;
        this.lineMaterial = null;
        this.visible = true;
        this.dirty_ = false;
        this.colorLUT = this.buildColorLUT();
    }

    buildColorLUT() {
        const stops = [
            { depth: 0,    r: 0.19, g: 0.51, b: 0.81 },
            { depth: 50,   r: 0.22, g: 0.63, b: 0.41 },
            { depth: 100,  r: 0.93, g: 0.79, b: 0.29 },
            { depth: 150,  r: 0.93, g: 0.53, b: 0.21 },
            { depth: 200,  r: 0.90, g: 0.24, b: 0.24 }
        ];
        const LUT_SIZE = 256;
        const lut = new Float32Array(LUT_SIZE * 3);
        for (let i = 0; i < LUT_SIZE; i++) {
            const depth = (i / (LUT_SIZE - 1)) * 200;
            let r, g, b;
            for (let s = 0; s < stops.length - 1; s++) {
                if (depth >= stops[s].depth && depth <= stops[s + 1].depth) {
                    const t = (depth - stops[s].depth) / (stops[s + 1].depth - stops[s].depth);
                    r = stops[s].r + (stops[s + 1].r - stops[s].r) * t;
                    g = stops[s].g + (stops[s + 1].g - stops[s].g) * t;
                    b = stops[s].b + (stops[s + 1].b - stops[s].b) * t;
                    break;
                }
            }
            if (r === undefined) { r = stops[stops.length-1].r; g = stops[stops.length-1].g; b = stops[stops.length-1].b; }
            lut[i * 3] = r;
            lut[i * 3 + 1] = g;
            lut[i * 3 + 2] = b;
        }
        return lut;
    }

    sampleColor(depth) {
        const idx = Math.min(255, Math.max(0, Math.round((depth / 200) * 255)));
        return {
            r: this.colorLUT[idx * 3],
            g: this.colorLUT[idx * 3 + 1],
            b: this.colorLUT[idx * 3 + 2]
        };
    }

    clear() {
        if (this.lineSegments) {
            this.lineSegments.geometry.dispose();
            this.lineSegments = null;
        }
        if (this.lineMaterial) {
            this.lineMaterial.dispose();
            this.lineMaterial = null;
        }
        this.crackData = [];
        this.dirty_ = false;
    }

    addCrack(crack) {
        const points = crack.points || crack.crack_points || [];
        if (points.length < 2) return;

        const scaled = new Array(points.length);
        for (let i = 0; i < points.length; i++) {
            const p = points[i];
            scaled[i] = {
                x: (p.x || 0) * 0.1,
                y: (p.y || 0) * 0.1 + 15,
                z: (p.z || 0) * 0.1,
                depth: p.depth || 0,
                width: p.width || 0
            };
        }

        this.crackData.push({
            id: crack.id,
            meta: crack,
            points: scaled
        });
        this.dirty_ = true;
    }

    build(scene) {
        this.clearFromScene(scene);

        if (this.crackData.length === 0) return;

        let totalVerts = 0;
        let totalSegs = 0;
        for (let c = 0; c < this.crackData.length; c++) {
            const n = this.crackData[c].points.length;
            totalVerts += n;
            totalSegs += n - 1;
        }

        const positions = new Float32Array(totalVerts * 3);
        const colors = new Float32Array(totalVerts * 3);
        const indices = new Uint32Array(totalSegs * 2);

        let vertOff = 0;
        let idxOff = 0;
        let indexOffset = 0;

        for (let c = 0; c < this.crackData.length; c++) {
            const pts = this.crackData[c].points;
            const n = pts.length;

            for (let i = 0; i < n; i++) {
                const p = pts[i];
                positions[vertOff] = p.x;
                positions[vertOff + 1] = p.y;
                positions[vertOff + 2] = p.z;

                const col = this.sampleColor(p.depth);
                colors[vertOff] = col.r;
                colors[vertOff + 1] = col.g;
                colors[vertOff + 2] = col.b;

                vertOff += 3;
            }

            for (let i = 0; i < n - 1; i++) {
                indices[idxOff++] = indexOffset + i;
                indices[idxOff++] = indexOffset + i + 1;
            }

            indexOffset += n;
        }

        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
        geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
        geometry.setIndex(new THREE.BufferAttribute(indices, 1));
        geometry.computeBoundingSphere();

        this.lineMaterial = new THREE.LineBasicMaterial({
            vertexColors: true,
            transparent: true,
            opacity: 0.95,
            depthTest: true,
            depthWrite: false,
            linewidth: 1
        });

        this.lineSegments = new THREE.LineSegments(geometry, this.lineMaterial);
        this.lineSegments.userData.isMergedCracks = true;
        this.lineSegments.visible = this.visible;
        this.dirty_ = false;

        if (scene) scene.add(this.lineSegments);
    }

    rebuildIfNeeded(scene) {
        if (this.dirty_) this.build(scene);
    }

    clearFromScene(scene) {
        if (this.lineSegments && scene) scene.remove(this.lineSegments);
    }

    setVisible(v) {
        this.visible = v;
        if (this.lineSegments) this.lineSegments.visible = v;
    }

    pickAt(raycaster) {
        if (!this.lineSegments) return null;
        const intersects = raycaster.intersectObject(this.lineSegments, false);
        if (intersects.length === 0) return null;
        const hit = intersects[0];
        let accumulated = 0;
        for (let i = 0; i < this.crackData.length; i++) {
            const segCount = (this.crackData[i].points.length - 1) * 2;
            if (hit.index < accumulated + segCount) {
                return this.crackData[i];
            }
            accumulated += segCount;
        }
        return null;
    }

    get drawCalls() {
        return this.lineSegments ? 1 : 0;
    }

    get totalVertices() {
        let n = 0;
        for (let i = 0; i < this.crackData.length; i++) n += this.crackData[i].points.length;
        return n;
    }

    get totalSegments() {
        let n = 0;
        for (let i = 0; i < this.crackData.length; i++) n += Math.max(0, this.crackData[i].points.length - 1);
        return n;
    }

    dispose() {
        this.clear();
        this.crackData = [];
    }
}

class MergedCrackTubes {
    constructor() {
        this.mergedMesh = null;
        this.material = null;
        this.visible = true;
    }

    clear() {
        if (this.mergedMesh) {
            this.mergedMesh.geometry.dispose();
            this.mergedMesh = null;
        }
        if (this.material) {
            this.material.dispose();
            this.material = null;
        }
    }

    buildFromCracks(crackData, scene) {
        this.clearFromScene(scene);

        if (!crackData || crackData.length === 0) return;

        const mergedPositions = [];
        const mergedNormals = [];
        const mergedColors = [];
        const mergedIndices = [];
        let indexOffset = 0;

        const colorScale = [
            { d: 0,   r: 0.19, g: 0.51, b: 0.81 },
            { d: 50,  r: 0.22, g: 0.63, b: 0.41 },
            { d: 100, r: 0.93, g: 0.79, b: 0.29 },
            { d: 150, r: 0.93, g: 0.53, b: 0.21 },
            { d: 200, r: 0.90, g: 0.24, b: 0.24 }
        ];
        const sampleColor = (depth) => {
            depth = Math.min(Math.max(depth, 0), 200);
            for (let i = 0; i < colorScale.length - 1; i++) {
                if (depth >= colorScale[i].d && depth <= colorScale[i + 1].d) {
                    const t = (depth - colorScale[i].d) / (colorScale[i + 1].d - colorScale[i].d);
                    return {
                        r: colorScale[i].r + (colorScale[i + 1].r - colorScale[i].r) * t,
                        g: colorScale[i].g + (colorScale[i + 1].g - colorScale[i].g) * t,
                        b: colorScale[i].b + (colorScale[i + 1].b - colorScale[i].b) * t
                    };
                }
            }
            return colorScale[colorScale.length - 1];
        };

        const radialSegments = 6;
        const tubeRadius = 0.15;

        crackData.forEach(crack => {
            const pts = crack.points || crack.crack_points || [];
            if (pts.length < 2) return;
            if (crackData.length > 100) return;

            const curve = new THREE.CatmullRomCurve3(
                pts.map(p => new THREE.Vector3(
                    (p.x || 0) * 0.1,
                    (p.y || 0) * 0.1 + 15,
                    (p.z || 0) * 0.1
                ))
            );
            const tubularSegments = Math.max(pts.length - 1, 1);
            const geo = new THREE.TubeGeometry(curve, tubularSegments, tubeRadius, radialSegments, false);

            const posAttr = geo.getAttribute('position');
            const normAttr = geo.getAttribute('normal');
            const indices = geo.index ? geo.index.array : null;

            const avgDepth = (crack.max_depth || crack.maxDepth || 100);
            const col = sampleColor(avgDepth);

            for (let i = 0; i < posAttr.count; i++) {
                mergedPositions.push(posAttr.getX(i), posAttr.getY(i), posAttr.getZ(i));
                mergedNormals.push(normAttr.getX(i), normAttr.getY(i), normAttr.getZ(i));
                mergedColors.push(col.r, col.g, col.b);
            }

            if (indices) {
                for (let i = 0; i < indices.length; i++) {
                    mergedIndices.push(indices[i] + indexOffset);
                }
            } else {
                for (let i = 0; i < posAttr.count; i++) {
                    mergedIndices.push(i + indexOffset);
                }
            }
            indexOffset += posAttr.count;

            geo.dispose();
        });

        if (mergedPositions.length === 0) return;

        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute('position', new THREE.Float32BufferAttribute(mergedPositions, 3));
        geometry.setAttribute('normal', new THREE.Float32BufferAttribute(mergedNormals, 3));
        geometry.setAttribute('color', new THREE.Float32BufferAttribute(mergedColors, 3));
        geometry.setIndex(mergedIndices);
        geometry.computeBoundingBox();
        geometry.computeBoundingSphere();

        this.material = new THREE.MeshBasicMaterial({
            vertexColors: true,
            transparent: true,
            opacity: 0.55,
            side: THREE.DoubleSide,
            depthWrite: false
        });

        this.mergedMesh = new THREE.Mesh(geometry, this.material);
        this.mergedMesh.userData.isMergedTubes = true;
        this.mergedMesh.visible = this.visible;

        if (scene) scene.add(this.mergedMesh);
    }

    clearFromScene(scene) {
        if (this.mergedMesh && scene) scene.remove(this.mergedMesh);
    }

    setVisible(v) {
        this.visible = v;
        if (this.mergedMesh) this.mergedMesh.visible = v;
    }

    dispose() {
        this.clear();
    }
}

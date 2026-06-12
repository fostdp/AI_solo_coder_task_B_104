class VirtualRepairTool {
    constructor(options = {}) {
        this.scene = null;
        this.camera = null;
        this.renderer = null;
        this.mergedCrackLines = null;
        this.mergedCrackTubes = null;
        this.porcelainMesh = null;

        this.enabled = false;
        this.repairRadius = options.repairRadius || 5;
        this.closureThreshold = options.closureThreshold || 0.8;

        this.raycaster = new THREE.Raycaster();
        this.mouse = new THREE.Vector2();

        this.repairMarkers = [];
        this.repairMarkerGroup = null;

        this.brushTube = null;
        this.brushPoints = [];
        this.isDrawing = false;
        this.lastBrushPosition = null;

        this.crackRepairData = new Map();
        this.closedCracks = new Set();
        this.animatingCracks = new Map();
        this.animationFrames = [];

        this.repairedPoints = [];
        this.crackClosureData = [];

        this._onMouseDown = null;
        this._onMouseMove = null;
        this._onMouseUp = null;
        this._onMouseLeave = null;
        this._animationId = null;

        this._totalCrackPoints = 0;
        this._repairedCrackPoints = 0;
    }

    applyToScene(viewer) {
        this.scene = viewer.scene;
        this.camera = viewer.model.camera;
        this.renderer = viewer.model.renderer;
        this.mergedCrackLines = viewer.mergedCrackLines;
        this.mergedCrackTubes = viewer.mergedCrackTubes;
        this.porcelainMesh = viewer.model.porcelainMesh;

        this.repairMarkerGroup = new THREE.Group();
        this.repairMarkerGroup.name = 'repairMarkers';
        this.scene.add(this.repairMarkerGroup);

        this._initCrackRepairData();
        this._bindEvents();
        this._startAnimationLoop();
    }

    _initCrackRepairData() {
        this.crackRepairData.clear();
        this._totalCrackPoints = 0;
        this._repairedCrackPoints = 0;

        if (!this.mergedCrackLines || !this.mergedCrackLines.crackData) return;

        this.mergedCrackLines.crackData.forEach(crack => {
            const points = crack.points || [];
            const repairedMap = new Map();
            points.forEach((_, idx) => repairedMap.set(idx, false));
            this.crackRepairData.set(crack.id, {
                crack,
                repairedPoints: repairedMap,
                totalPoints: points.length,
                repairedCount: 0,
                isClosed: false,
                closureProgress: 0
            });
            this._totalCrackPoints += points.length;
        });
    }

    _bindEvents() {
        const domElement = this.renderer.domElement;

        this._onMouseDown = (e) => this._handleMouseDown(e);
        this._onMouseMove = (e) => this._handleMouseMove(e);
        this._onMouseUp = (e) => this._handleMouseUp(e);
        this._onMouseLeave = (e) => this._handleMouseUp(e);

        domElement.addEventListener('mousedown', this._onMouseDown);
        domElement.addEventListener('mousemove', this._onMouseMove);
        domElement.addEventListener('mouseup', this._onMouseUp);
        domElement.addEventListener('mouseleave', this._onMouseLeave);
    }

    _unbindEvents() {
        if (!this.renderer) return;
        const domElement = this.renderer.domElement;

        if (this._onMouseDown) domElement.removeEventListener('mousedown', this._onMouseDown);
        if (this._onMouseMove) domElement.removeEventListener('mousemove', this._onMouseMove);
        if (this._onMouseUp) domElement.removeEventListener('mouseup', this._onMouseUp);
        if (this._onMouseLeave) domElement.removeEventListener('mouseleave', this._onMouseLeave);
    }

    _handleMouseDown(e) {
        if (!this.enabled) return;
        this.isDrawing = true;
        this.brushPoints = [];
        this.lastBrushPosition = null;
        this._tryRepairAt(e);
    }

    _handleMouseMove(e) {
        if (!this.enabled) return;
        this._updateMousePosition(e);

        if (this.isDrawing) {
            this._tryRepairAt(e);
        }
    }

    _handleMouseUp(e) {
        if (!this.enabled) return;
        this.isDrawing = false;
        this.brushPoints = [];
        this._removeBrushTube();
    }

    _updateMousePosition(e) {
        const rect = this.renderer.domElement.getBoundingClientRect();
        this.mouse.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
        this.mouse.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;
    }

    _tryRepairAt(e) {
        this._updateMousePosition(e);
        this.raycaster.setFromCamera(this.mouse, this.camera);

        let hitPoint = null;

        if (this.porcelainMesh) {
            const intersects = this.raycaster.intersectObject(this.porcelainMesh, false);
            if (intersects.length > 0) {
                hitPoint = intersects[0].point.clone();
            }
        }

        if (!hitPoint) {
            const farPoint = this.raycaster.ray.at(50, new THREE.Vector3());
            hitPoint = farPoint;
        }

        this._updateBrushTube(hitPoint);
        this._repairCracksNearPoint(hitPoint);
        this._addRepairMarker(hitPoint);
    }

    _updateBrushTube(position) {
        if (!this.isDrawing) return;

        if (this.lastBrushPosition) {
            const dist = this.lastBrushPosition.distanceTo(position);
            if (dist > 0.3) {
                this.brushPoints.push(position.clone());
                this.lastBrushPosition = position.clone();
            }
        } else {
            this.brushPoints.push(position.clone());
            this.lastBrushPosition = position.clone();
        }

        if (this.brushPoints.length < 2) return;

        this._removeBrushTube();

        const curve = new THREE.CatmullRomCurve3(this.brushPoints);
        const tubeGeo = new THREE.TubeGeometry(curve, Math.max(this.brushPoints.length * 2, 8), this.repairRadius * 0.1, 8, false);
        const tubeMat = new THREE.MeshBasicMaterial({
            color: 0x4fd1c5,
            transparent: true,
            opacity: 0.35,
            side: THREE.DoubleSide,
            depthWrite: false
        });
        this.brushTube = new THREE.Mesh(tubeGeo, tubeMat);
        this.scene.add(this.brushTube);
    }

    _removeBrushTube() {
        if (this.brushTube) {
            this.scene.remove(this.brushTube);
            this.brushTube.geometry.dispose();
            this.brushTube.material.dispose();
            this.brushTube = null;
        }
    }

    _repairCracksNearPoint(worldPoint) {
        if (!this.mergedCrackLines || !this.mergedCrackLines.crackData) return;

        const radiusScaled = this.repairRadius * 0.1;

        this.mergedCrackLines.crackData.forEach(crack => {
            const repairData = this.crackRepairData.get(crack.id);
            if (!repairData || repairData.isClosed) return;

            const points = crack.points || [];
            let newlyRepaired = 0;

            points.forEach((pt, idx) => {
                if (repairData.repairedPoints.get(idx)) return;

                const crackPoint = new THREE.Vector3(pt.x, pt.y, pt.z);
                const dist = worldPoint.distanceTo(crackPoint);

                if (dist <= radiusScaled) {
                    repairData.repairedPoints.set(idx, true);
                    repairData.repairedCount++;
                    newlyRepaired++;
                    this._repairedCrackPoints++;

                    this.repairedPoints.push({
                        crack_id: crack.id,
                        point_index: idx,
                        position: { x: pt.x, y: pt.y, z: pt.z },
                        world_position: { x: worldPoint.x, y: worldPoint.y, z: worldPoint.z },
                        repaired_at: Date.now()
                    });
                }
            });

            if (newlyRepaired > 0) {
                this._updateCrackLineOpacity(crack.id);

                const progress = repairData.repairedCount / repairData.totalPoints;
                if (progress >= this.closureThreshold && !repairData.isClosed) {
                    this.animateCrackClosure(crack.id);
                }
            }
        });
    }

    _updateCrackLineOpacity(crackId) {
        if (!this.mergedCrackLines || !this.mergedCrackLines.lineSegments) return;

        const repairData = this.crackRepairData.get(crackId);
        if (!repairData) return;

        const geometry = this.mergedCrackLines.lineSegments.geometry;
        const colors = geometry.getAttribute('color');
        const positions = geometry.getAttribute('position');

        if (!colors) return;

        let vertexOffset = 0;
        for (const c of this.mergedCrackLines.crackData) {
            if (c.id === crackId) {
                const pts = c.points || [];
                for (let i = 0; i < pts.length; i++) {
                    const isRepaired = repairData.repairedPoints.get(i);
                    const vertexIdx = vertexOffset + i;
                    if (isRepaired) {
                        colors.setX(vertexIdx, colors.getX(vertexIdx) * 0.3);
                        colors.setY(vertexIdx, colors.getY(vertexIdx) * 0.3);
                        colors.setZ(vertexIdx, colors.getZ(vertexIdx) * 0.3);
                    }
                }
                colors.needsUpdate = true;
                break;
            }
            vertexOffset += (c.points || []).length;
        }
    }

    _addRepairMarker(position) {
        const progress = this.getOverallProgress();
        const color = this._getProgressColor(progress);

        const geo = new THREE.SphereGeometry(this.repairRadius * 0.1, 16, 16);
        const mat = new THREE.MeshBasicMaterial({
            color: color,
            transparent: true,
            opacity: 0.25,
            depthWrite: false
        });
        const sphere = new THREE.Mesh(geo, mat);
        sphere.position.copy(position);
        this.repairMarkerGroup.add(sphere);

        this.repairMarkers.push(sphere);

        if (this.repairMarkers.length > 200) {
            const old = this.repairMarkers.shift();
            this.repairMarkerGroup.remove(old);
            old.geometry.dispose();
            old.material.dispose();
        }
    }

    _getProgressColor(progress) {
        const clamped = Math.min(1, Math.max(0, progress));
        const cyan = new THREE.Color(0x00ffff);
        const green = new THREE.Color(0x00ff00);
        return cyan.lerp(green, clamped);
    }

    enable() {
        this.enabled = true;
        if (this.mergedCrackLines) {
            this.mergedCrackLines.rebuildIfNeeded(this.scene);
        }
    }

    disable() {
        this.enabled = false;
        this.isDrawing = false;
        this._removeBrushTube();
        this.brushPoints = [];
    }

    setRepairRadius(radiusMm) {
        this.repairRadius = Math.max(0.5, Math.min(50, radiusMm));
    }

    getRepairedCrackIds() {
        const ids = [];
        this.crackRepairData.forEach((data, id) => {
            if (data.repairedCount > 0) {
                ids.push(id);
            }
        });
        return ids;
    }

    getRepairProgress(crackId) {
        const data = this.crackRepairData.get(crackId);
        if (!data) return 0;
        return (data.repairedCount / data.totalPoints) * 100;
    }

    getOverallProgress() {
        if (this._totalCrackPoints === 0) return 0;
        return this._repairedCrackPoints / this._totalCrackPoints;
    }

    animateCrackClosure(crackId) {
        const repairData = this.crackRepairData.get(crackId);
        if (!repairData || repairData.isClosed || this.animatingCracks.has(crackId)) return;

        repairData.isClosed = true;
        this.closedCracks.add(crackId);

        const crack = repairData.crack;
        const points = (crack.points || []).map(p => new THREE.Vector3(p.x, p.y, p.z));

        if (points.length < 2) {
            this._finishCrackClosure(crackId);
            return;
        }

        this.animatingCracks.set(crackId, {
            crack,
            points,
            startPoints: points.map(p => p.clone()),
            centerIndex: Math.floor(points.length / 2),
            startTime: performance.now(),
            duration: 1200,
            progress: 0
        });

        this.crackClosureData.push({
            crack_id: crackId,
            start_time: Date.now(),
            duration: 1200,
            start_points: points.map(p => ({ x: p.x, y: p.y, z: p.z }))
        });
    }

    _finishCrackClosure(crackId) {
        this.animatingCracks.delete(crackId);
        this._hideCrackLine(crackId);
    }

    _hideCrackLine(crackId) {
        if (!this.mergedCrackLines || !this.mergedCrackLines.lineSegments) return;

        const geometry = this.mergedCrackLines.lineSegments.geometry;
        const colors = geometry.getAttribute('color');
        if (!colors) return;

        let vertexOffset = 0;
        for (const c of this.mergedCrackLines.crackData) {
            if (c.id === crackId) {
                const pts = c.points || [];
                for (let i = 0; i < pts.length; i++) {
                    const vertexIdx = vertexOffset + i;
                    colors.setX(vertexIdx, 0);
                    colors.setY(vertexIdx, 0);
                    colors.setZ(vertexIdx, 0);
                }
                colors.needsUpdate = true;
                break;
            }
            vertexOffset += (c.points || []).length;
        }
    }

    _startAnimationLoop() {
        const animate = () => {
            this._animationId = requestAnimationFrame(animate);
            this._updateCrackAnimations();
        };
        animate();
    }

    _updateCrackAnimations() {
        if (this.animatingCracks.size === 0) return;

        const now = performance.now();

        for (const [crackId, animData] of this.animatingCracks.entries()) {
            const elapsed = now - animData.startTime;
            const t = Math.min(1, elapsed / animData.duration);
            animData.progress = t;

            this._animateCrackShrink(crackId, animData, t);
            this._recordAnimationFrame(crackId, animData, t, now);

            if (t >= 1) {
                this._finishCrackClosure(crackId);
            }
        }
    }

    _animateCrackShrink(crackId, animData, t) {
        if (!this.mergedCrackLines || !this.mergedCrackLines.lineSegments) return;

        const geometry = this.mergedCrackLines.lineSegments.geometry;
        const positions = geometry.getAttribute('position');
        const colors = geometry.getAttribute('color');
        if (!positions || !colors) return;

        const { crack, startPoints, centerIndex } = animData;
        const totalPoints = startPoints.length;

        let vertexOffset = 0;
        for (const c of this.mergedCrackLines.crackData) {
            if (c.id === crackId) {
                const center = startPoints[centerIndex].clone();

                for (let i = 0; i < totalPoints; i++) {
                    const vertexIdx = vertexOffset + i;
                    const distFromCenter = Math.abs(i - centerIndex) / Math.max(1, centerIndex);

                    if (distFromCenter <= t) {
                        const localT = Math.min(1, (distFromCenter - (t - 0.1)) / 0.1);
                        const shrinkT = 1 - Math.max(0, localT);

                        const sp = startPoints[i];
                        const newPos = sp.clone().lerp(center, shrinkT);
                        positions.setXYZ(vertexIdx, newPos.x, newPos.y, newPos.z);

                        const alpha = Math.max(0, 1 - t * 1.2);
                        const baseColor = colors ? {
                            r: colors.getX(vertexIdx),
                            g: colors.getY(vertexIdx),
                            b: colors.getZ(vertexIdx)
                        } : { r: 0.5, g: 0.5, b: 0.5 };
                        colors.setXYZ(vertexIdx, baseColor.r * alpha, baseColor.g * alpha, baseColor.b * alpha);
                    }
                }

                positions.needsUpdate = true;
                colors.needsUpdate = true;
                break;
            }
            vertexOffset += (c.points || []).length;
        }
    }

    _recordAnimationFrame(crackId, animData, t, timestamp) {
        const positions = [];
        for (const p of animData.points) {
            positions.push({ x: p.x, y: p.y, z: p.z });
        }
        this.animationFrames.push({
            crack_id: crackId,
            frame: Math.floor(t * 60),
            timestamp,
            progress: t,
            positions
        });
    }

    generateRepairResult() {
        return {
            repaired_points: this.repairedPoints.map(rp => ({
                crack_id: rp.crack_id,
                point_index: rp.point_index,
                position: rp.position,
                world_position: rp.world_position,
                repaired_at: rp.repaired_at
            })),
            crack_closure_data: this.crackClosureData.map(cd => ({
                crack_id: cd.crack_id,
                start_time: cd.start_time,
                duration: cd.duration,
                start_points: cd.start_points,
                closed: this.closedCracks.has(cd.crack_id)
            })),
            animation_frames: this.animationFrames.map(af => ({
                crack_id: af.crack_id,
                frame: af.frame,
                timestamp: af.timestamp,
                progress: af.progress,
                positions: af.positions
            })),
            summary: {
                total_crack_points: this._totalCrackPoints,
                repaired_crack_points: this._repairedCrackPoints,
                overall_progress: this.getOverallProgress(),
                closed_cracks: Array.from(this.closedCracks),
                repair_radius_mm: this.repairRadius,
                closure_threshold: this.closureThreshold,
                generated_at: Date.now()
            }
        };
    }

    clearMarkers() {
        if (this.repairMarkerGroup) {
            while (this.repairMarkerGroup.children.length > 0) {
                const child = this.repairMarkerGroup.children[0];
                this.repairMarkerGroup.remove(child);
                if (child.geometry) child.geometry.dispose();
                if (child.material) child.material.dispose();
            }
        }
        this.repairMarkers = [];
    }

    reset() {
        this.clearMarkers();
        this._removeBrushTube();
        this.brushPoints = [];
        this.isDrawing = false;
        this.repairedPoints = [];
        this.crackClosureData = [];
        this.animationFrames = [];
        this.closedCracks.clear();
        this.animatingCracks.clear();
        this._initCrackRepairData();

        if (this.mergedCrackLines) {
            this.mergedCrackLines.dirty_ = true;
            this.mergedCrackLines.rebuildIfNeeded(this.scene);
        }
    }

    dispose() {
        this.disable();
        this._unbindEvents();

        if (this._animationId) {
            cancelAnimationFrame(this._animationId);
            this._animationId = null;
        }

        this.clearMarkers();

        if (this.repairMarkerGroup && this.scene) {
            this.scene.remove(this.repairMarkerGroup);
            this.repairMarkerGroup = null;
        }

        this._removeBrushTube();

        this.crackRepairData.clear();
        this.closedCracks.clear();
        this.animatingCracks.clear();

        this.repairedPoints = [];
        this.crackClosureData = [];
        this.animationFrames = [];

        this.scene = null;
        this.camera = null;
        this.renderer = null;
        this.mergedCrackLines = null;
        this.mergedCrackTubes = null;
        this.porcelainMesh = null;
    }
}

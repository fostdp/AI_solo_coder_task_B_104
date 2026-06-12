class PorcelainViewer {
    constructor(containerId) {
        this.container = document.getElementById(containerId);
        this.showCracks = true;

        this.model = new PorcelainModel(this.container);
        this.gradient = new CrackGradient({ maxDepth: 200 });
        this.mergedCrackLines = new MergedCrackLines();
        this.mergedCrackTubes = new MergedCrackTubes();
        this.stressHeatmap = null;
        this.repairTool = null;
        this.stressData = null;

        this.model.startAnimation();
    }

    createPorcelainVase() {
        return this.model.createPorcelainVase();
    }

    loadPorcelain(porcelainData, cracksData = [], stressData = null) {
        this.clearScene();
        this.createPorcelainVase();

        if (cracksData.length > 0) {
            this.loadCracks(cracksData);
        }

        if (stressData) {
            this.stressData = stressData;
            this.updateStressHeatmap(stressData);
        }

        this.model.focusTarget();
    }

    loadCracks(cracksData) {
        this.clearCracks();

        const cracksWithPoints = cracksData.filter(c => {
            const pts = c.points || c.crack_points || [];
            return pts.length >= 2;
        });

        cracksWithPoints.forEach(crack => {
            this.mergedCrackLines.addCrack(crack);
        });

        this.mergedCrackLines.build(this.model.scene);

        if (cracksWithPoints.length <= 100) {
            this.mergedCrackTubes.buildFromCracks(cracksWithPoints, this.model.scene);
        }

        this.mergedCrackLines.setVisible(this.showCracks);
        this.mergedCrackTubes.setVisible(this.showCracks);

        console.log(`[CrackRender] 已加载 ${cracksWithPoints.length} 条裂纹, ` +
                    `顶点=${this.mergedCrackLines.totalVertices}, ` +
                    `线段=${this.mergedCrackLines.totalSegments}, ` +
                    `DrawCall=${this.mergedCrackLines.drawCalls + (this.mergedCrackTubes.mergedMesh ? 1 : 0)}`);
    }

    clearScene() {
        this.model.clearPorcelainMesh();
        this.clearCracks();

        if (this.stressHeatmap) {
            this.stressHeatmap.dispose();
            this.stressHeatmap = null;
        }
        if (this.repairTool) {
            this.repairTool.dispose();
            this.repairTool = null;
        }
    }

    clearCracks() {
        this.mergedCrackLines.clearFromScene(this.model.scene);
        this.mergedCrackLines.clear();
        this.mergedCrackTubes.clearFromScene(this.model.scene);
        this.mergedCrackTubes.clear();
    }

    setShowCracks(show) {
        this.showCracks = show;
        this.mergedCrackLines.setVisible(show);
        this.mergedCrackTubes.setVisible(show);
    }

    setDisplayMode(mode) {
        this.model.setDisplayMode(mode);
    }

    setAutoRotate(rotate) {
        this.model.setAutoRotate(rotate);
    }

    get scene() {
        return this.model.scene;
    }

    get gradient() {
        return this.gradient;
    }

    destroy() {
        this.clearCracks();

        if (this.stressHeatmap) {
            this.stressHeatmap.dispose();
            this.stressHeatmap = null;
        }
        if (this.repairTool) {
            this.repairTool.dispose();
            this.repairTool = null;
        }
        if (this.mergedCrackLines) {
            this.mergedCrackLines.dispose();
        }
        if (this.mergedCrackTubes) {
            this.mergedCrackTubes.dispose();
        }

        this.model.dispose();
    }

    initStressHeatmap() {
        if (this.stressHeatmap) {
            this.stressHeatmap.dispose();
        }
        this.stressHeatmap = new StressHeatmap(this.model.porcelainMesh);
        this.stressHeatmap.addToScene(this.model.scene);
    }

    updateStressHeatmap(gridPoints) {
        if (!this.stressHeatmap) {
            this.initStressHeatmap();
        }
        this.stressHeatmap.updateStressData(gridPoints);
        this.stressHeatmap.showHeatmap();
    }

    toggleStressHeatmap() {
        if (!this.stressHeatmap) {
            return false;
        }
        return this.stressHeatmap.toggleHeatmap();
    }

    getStressAtPoint(x, y, z) {
        if (!this.stressHeatmap) {
            return null;
        }
        return this.stressHeatmap.getStressAtPoint(x, y, z);
    }

    initRepairTool() {
        if (this.repairTool) {
            this.repairTool.dispose();
        }
        this.repairTool = new VirtualRepairTool();
        this.repairTool.applyToScene(this);
    }

    enableRepairTool() {
        if (this.repairTool) {
            this.repairTool.enable();
        }
    }

    disableRepairTool() {
        if (this.repairTool) {
            this.repairTool.disable();
        }
    }

    setRepairRadius(r) {
        if (this.repairTool) {
            this.repairTool.setRepairRadius(r);
        }
    }

    getRepairProgress() {
        if (!this.repairTool) {
            return 0;
        }
        return this.repairTool.getRepairProgress();
    }

    getRepairedCrackIds() {
        if (!this.repairTool) {
            return [];
        }
        return this.repairTool.getRepairedCrackIds();
    }
}

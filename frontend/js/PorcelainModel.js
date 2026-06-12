class PorcelainModel {
    constructor(container) {
        this.container = container;
        this.scene = null;
        this.camera = null;
        this.renderer = null;
        this.porcelainMesh = null;
        this.controls = null;
        this.animationId = null;
        this.autoRotate = false;
        this.spherical = { radius: 50, phi: Math.PI / 4, theta: 0 };
        this.isDragging = false;
        this.previousMousePosition = { x: 0, y: 0 };

        this._initScene();
        this._initLights();
        this._initControls();
        this._initHelpers();

        window.addEventListener('resize', () => this._onResize());
    }

    _initScene() {
        const width = this.container.clientWidth;
        const height = this.container.clientHeight;

        this.scene = new THREE.Scene();
        this.scene.background = new THREE.Color(0x1a202c);
        this.scene.fog = new THREE.Fog(0x1a202c, 50, 200);

        this.camera = new THREE.PerspectiveCamera(45, width / height, 0.1, 1000);
        this.camera.position.set(0, 30, 50);

        this.renderer = new THREE.WebGLRenderer({ antialias: true });
        this.renderer.setSize(width, height);
        this.renderer.setPixelRatio(window.devicePixelRatio);
        this.renderer.shadowMap.enabled = true;
        this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;

        this.container.appendChild(this.renderer.domElement);
    }

    _initLights() {
        const ambientLight = new THREE.AmbientLight(0xffffff, 0.4);
        this.scene.add(ambientLight);

        const directionalLight = new THREE.DirectionalLight(0xffffff, 0.8);
        directionalLight.position.set(50, 100, 50);
        directionalLight.castShadow = true;
        directionalLight.shadow.mapSize.width = 2048;
        directionalLight.shadow.mapSize.height = 2048;
        this.scene.add(directionalLight);

        const fillLight = new THREE.DirectionalLight(0x63b3ed, 0.3);
        fillLight.position.set(-50, 50, -50);
        this.scene.add(fillLight);

        const rimLight = new THREE.DirectionalLight(0x4fd1c5, 0.5);
        rimLight.position.set(0, 20, -80);
        this.scene.add(rimLight);
    }

    _initControls() {
        if (typeof THREE.OrbitControls !== 'undefined') {
            this.controls = new THREE.OrbitControls(this.camera, this.renderer.domElement);
            this.controls.enableDamping = true;
            this.controls.dampingFactor = 0.05;
            this.controls.minDistance = 10;
            this.controls.maxDistance = 200;
            this.controls.maxPolarAngle = Math.PI / 2 + 0.1;
        } else {
            this.container.addEventListener('mousedown', (e) => this._onMouseDown(e));
            this.container.addEventListener('mousemove', (e) => this._onMouseMove(e));
            this.container.addEventListener('mouseup', () => this._onMouseUp());
            this.container.addEventListener('mouseleave', () => this._onMouseUp());
            this.container.addEventListener('wheel', (e) => this._onWheel(e));
        }
    }

    _initHelpers() {
        const gridHelper = new THREE.GridHelper(100, 20, 0x2d3748, 0x2d3748);
        this.scene.add(gridHelper);

        const axesHelper = new THREE.AxesHelper(20);
        this.scene.add(axesHelper);
    }

    _onMouseDown(e) {
        this.isDragging = true;
        this.previousMousePosition = { x: e.clientX, y: e.clientY };
    }

    _onMouseMove(e) {
        if (!this.isDragging) return;

        const deltaX = e.clientX - this.previousMousePosition.x;
        const deltaY = e.clientY - this.previousMousePosition.y;

        this.spherical.theta -= deltaX * 0.01;
        this.spherical.phi = Math.max(0.1, Math.min(Math.PI - 0.1, this.spherical.phi + deltaY * 0.01));

        this._updateCameraPosition();
        this.previousMousePosition = { x: e.clientX, y: e.clientY };
    }

    _onMouseUp() {
        this.isDragging = false;
    }

    _onWheel(e) {
        e.preventDefault();
        const delta = e.deltaY > 0 ? 1.1 : 0.9;
        this.spherical.radius = Math.max(10, Math.min(200, this.spherical.radius * delta));
        this._updateCameraPosition();
    }

    _updateCameraPosition() {
        const x = this.spherical.radius * Math.sin(this.spherical.phi) * Math.sin(this.spherical.theta);
        const y = this.spherical.radius * Math.cos(this.spherical.phi);
        const z = this.spherical.radius * Math.sin(this.spherical.phi) * Math.cos(this.spherical.theta);

        this.camera.position.set(x, y + 20, z);
        this.camera.lookAt(0, 15, 0);
    }

    _onResize() {
        const width = this.container.clientWidth;
        const height = this.container.clientHeight;

        this.camera.aspect = width / height;
        this.camera.updateProjectionMatrix();

        this.renderer.setSize(width, height);
    }

    createPorcelainVase() {
        this.clearPorcelainMesh();

        const points = [];
        for (let i = 0; i < 50; i++) {
            const t = i / 49;
            const y = t * 40;
            let radius;

            if (t < 0.1) {
                radius = 8 - t * 30;
            } else if (t < 0.3) {
                const tt = (t - 0.1) / 0.2;
                radius = 5 + tt * 5;
            } else if (t < 0.7) {
                const tt = (t - 0.3) / 0.4;
                radius = 10 + Math.sin(tt * Math.PI) * 8;
            } else if (t < 0.9) {
                const tt = (t - 0.7) / 0.2;
                radius = 10 - tt * 3;
            } else {
                const tt = (t - 0.9) / 0.1;
                radius = 7 - tt * 2;
            }

            points.push(new THREE.Vector2(Math.max(0.5, radius), y));
        }

        const geometry = new THREE.LatheGeometry(points, 64);
        geometry.computeVertexNormals();

        const material = new THREE.MeshPhysicalMaterial({
            color: 0x1a5f7a,
            metalness: 0.1,
            roughness: 0.2,
            clearcoat: 0.8,
            clearcoatRoughness: 0.1,
            transparent: false,
            opacity: 1.0,
        });

        this.porcelainMesh = new THREE.Mesh(geometry, material);
        this.porcelainMesh.castShadow = true;
        this.porcelainMesh.receiveShadow = true;
        this.scene.add(this.porcelainMesh);

        const baseGeometry = new THREE.CylinderGeometry(7, 9, 2, 64);
        const baseMaterial = new THREE.MeshPhysicalMaterial({
            color: 0x2c3e50,
            metalness: 0.3,
            roughness: 0.4,
        });
        const base = new THREE.Mesh(baseGeometry, baseMaterial);
        base.position.y = -1;
        base.castShadow = true;
        base.receiveShadow = true;
        this.scene.add(base);

        return this.porcelainMesh;
    }

    clearPorcelainMesh() {
        if (this.porcelainMesh) {
            this.scene.remove(this.porcelainMesh);
            this.porcelainMesh.geometry.dispose();
            this.porcelainMesh.material.dispose();
            this.porcelainMesh = null;
        }
    }

    setDisplayMode(mode) {
        if (!this.porcelainMesh) return;

        const material = this.porcelainMesh.material;

        switch (mode) {
            case 'solid':
                material.wireframe = false;
                material.transparent = false;
                material.opacity = 1.0;
                break;
            case 'wireframe':
                material.wireframe = true;
                material.transparent = false;
                material.opacity = 1.0;
                break;
            case 'transparent':
                material.wireframe = false;
                material.transparent = true;
                material.opacity = 0.5;
                break;
        }
    }

    setAutoRotate(rotate) {
        this.autoRotate = rotate;
        if (this.controls) {
            this.controls.autoRotate = rotate;
            this.controls.autoRotateSpeed = 1.0;
        }
    }

    startAnimation(callback) {
        this._animationCallback = callback;
        this._animate();
    }

    _animate() {
        this.animationId = requestAnimationFrame(() => this._animate());

        if (this.controls) {
            this.controls.update();
        } else if (this.autoRotate) {
            this.spherical.theta += 0.005;
            this._updateCameraPosition();
        }

        if (this._animationCallback) {
            this._animationCallback();
        }

        this.renderer.render(this.scene, this.camera);
    }

    focusTarget() {
        if (this.controls) {
            this.controls.target.set(0, 15, 0);
            this.controls.update();
        }
    }

    dispose() {
        if (this.animationId) {
            cancelAnimationFrame(this.animationId);
        }

        this.clearPorcelainMesh();

        if (this.renderer) {
            this.renderer.dispose();
            if (this.renderer.domElement && this.renderer.domElement.parentNode) {
                this.renderer.domElement.parentNode.removeChild(this.renderer.domElement);
            }
        }

        window.removeEventListener('resize', () => this._onResize());
    }
}

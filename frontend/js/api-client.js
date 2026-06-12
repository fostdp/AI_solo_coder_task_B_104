class ApiClient {
    constructor(baseUrl = 'http://localhost:8080/api') {
        this.baseUrl = baseUrl;
    }

    async request(endpoint, options = {}) {
        const url = `${this.baseUrl}${endpoint}`;
        const defaultHeaders = {
            'Content-Type': 'application/json',
        };

        try {
            const response = await fetch(url, {
                ...options,
                headers: {
                    ...defaultHeaders,
                    ...options.headers,
                },
            });

            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }

            return await response.json();
        } catch (error) {
            console.error(`API请求失败 [${endpoint}]:`, error);
            throw error;
        }
    }

    async getPorcelains(page = 1, pageSize = 200, dynasty = 'all') {
        let endpoint = `/porcelains?page=${page}&page_size=${pageSize}`;
        if (dynasty !== 'all') {
            endpoint += `&dynasty=${dynasty}`;
        }
        return this.request(endpoint);
    }

    async getPorcelain(id) {
        return this.request(`/porcelains/${id}`);
    }

    async getPorcelainCracks(porcelainId, page = 1, pageSize = 50) {
        return this.request(`/porcelains/${porcelainId}/cracks?page=${page}&page_size=${pageSize}`);
    }

    async getCrack(crackId) {
        return this.request(`/cracks/${crackId}`);
    }

    async getCrackPoints(crackId) {
        return this.request(`/cracks/${crackId}/points`);
    }

    async predictCrack(crackId, horizonHours = 720) {
        return this.request(`/cracks/${crackId}/predict?horizon_hours=${horizonHours}`, {
            method: 'POST',
        });
    }

    async simulateRepair(crackId, materialId, particleCount = 1000, steps = 1000) {
        return this.request(`/cracks/${crackId}/simulate/${materialId}?particle_count=${particleCount}&steps=${steps}`, {
            method: 'POST',
        });
    }

    async getAlerts(status = 'ACTIVE', page = 1, pageSize = 50) {
        return this.request(`/alerts?status=${status}&page=${page}&page_size=${pageSize}`);
    }

    async updateAlertStatus(alertId, status, notes = '') {
        return this.request(`/alerts/${alertId}/status`, {
            method: 'PUT',
            body: JSON.stringify({ status, notes }),
        });
    }

    async getRepairMaterials() {
        return this.request('/repair-materials');
    }

    async getLaserData(porcelainId, startTime, endTime) {
        let endpoint = `/sensors/laser-data?porcelain_id=${porcelainId}`;
        if (startTime) endpoint += `&start_time=${startTime}`;
        if (endTime) endpoint += `&end_time=${endTime}`;
        return this.request(endpoint);
    }

    async getVibrationData(porcelainId, startTime, endTime) {
        let endpoint = `/sensors/vibration-data?porcelain_id=${porcelainId}`;
        if (startTime) endpoint += `&start_time=${startTime}`;
        if (endTime) endpoint += `&end_time=${endTime}`;
        return this.request(endpoint);
    }

    async getSystemStats() {
        return this.request('/system/stats');
    }

    async runStressAnalysis(porcelainId) {
        return this.request(`/porcelains/${porcelainId}/stress-analysis`, {
            method: 'POST',
        });
    }

    async getStressAnalysis(porcelainId) {
        return this.request(`/porcelains/${porcelainId}/stress-analysis`);
    }

    async predictPenetration(crackId, materialId, targetDepthUm) {
        const options = { method: 'POST' };
        if (targetDepthUm !== undefined && targetDepthUm !== null) {
            options.body = JSON.stringify({ target_depth_um: targetDepthUm });
        }
        return this.request(`/cracks/${crackId}/penetration/${materialId}`, options);
    }

    async runBendingTest(crackId, materialId, porcelainId) {
        const options = { method: 'POST' };
        if (porcelainId !== undefined && porcelainId !== null) {
            options.body = JSON.stringify({ porcelain_id: porcelainId });
        }
        return this.request(`/cracks/${crackId}/bending-test/${materialId}`, options);
    }

    async getActiveAlertsCount() {
        const response = await this.getAlerts('ACTIVE', 1, 1);
        return response.pagination?.total || 0;
    }
}

const api = new ApiClient();

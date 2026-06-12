class WebSocketClient {
    constructor(url = 'ws://localhost:8080/ws') {
        this.url = url;
        this.ws = null;
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = 10;
        this.reconnectDelay = 3000;
        this.heartbeatInterval = null;
        this.listeners = {};
        this.isConnected = false;
    }

    connect() {
        try {
            this.ws = new WebSocket(this.url);

            this.ws.onopen = () => {
                console.log('WebSocket 连接已建立');
                this.isConnected = true;
                this.reconnectAttempts = 0;
                this.startHeartbeat();
                this.emit('connected');
            };

            this.ws.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    this.handleMessage(data);
                } catch (error) {
                    console.error('WebSocket消息解析失败:', error);
                }
            };

            this.ws.onclose = (event) => {
                console.log('WebSocket 连接已关闭:', event.code, event.reason);
                this.isConnected = false;
                this.stopHeartbeat();
                this.emit('disconnected');
                this.scheduleReconnect();
            };

            this.ws.onerror = (error) => {
                console.error('WebSocket 错误:', error);
                this.emit('error', error);
            };

        } catch (error) {
            console.error('WebSocket 连接失败:', error);
            this.scheduleReconnect();
        }
    }

    disconnect() {
        if (this.ws) {
            this.ws.close();
        }
        this.stopHeartbeat();
        this.reconnectAttempts = this.maxReconnectAttempts;
    }

    handleMessage(data) {
        const messageType = data.type || data.message_type;

        switch (messageType) {
            case 'laser_data':
                this.emit('laser_data', data.data);
                break;
            case 'vibration_data':
                this.emit('vibration_data', data.data);
                break;
            case 'alert':
                this.emit('alert', data.data);
                break;
            case 'heartbeat':
                this.emit('heartbeat', data.data);
                break;
            case 'stats':
                this.emit('stats', data.data);
                break;
            default:
                console.log('未知消息类型:', messageType, data);
                this.emit('message', data);
        }
    }

    send(data) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify(data));
        } else {
            console.warn('WebSocket 未连接，无法发送消息');
        }
    }

    subscribe(topic) {
        this.send({
            type: 'subscribe',
            topic: topic
        });
    }

    unsubscribe(topic) {
        this.send({
            type: 'unsubscribe',
            topic: topic
        });
    }

    on(event, callback) {
        if (!this.listeners[event]) {
            this.listeners[event] = [];
        }
        this.listeners[event].push(callback);
    }

    off(event, callback) {
        if (this.listeners[event]) {
            this.listeners[event] = this.listeners[event].filter(cb => cb !== callback);
        }
    }

    emit(event, data) {
        if (this.listeners[event]) {
            this.listeners[event].forEach(callback => {
                try {
                    callback(data);
                } catch (error) {
                    console.error(`事件监听器错误 [${event}]:`, error);
                }
            });
        }
    }

    startHeartbeat() {
        this.heartbeatInterval = setInterval(() => {
            if (this.ws && this.ws.readyState === WebSocket.OPEN) {
                this.send({ type: 'ping', timestamp: Date.now() });
            }
        }, 30000);
    }

    stopHeartbeat() {
        if (this.heartbeatInterval) {
            clearInterval(this.heartbeatInterval);
            this.heartbeatInterval = null;
        }
    }

    scheduleReconnect() {
        if (this.reconnectAttempts >= this.maxReconnectAttempts) {
            console.error('达到最大重连次数，停止重连');
            return;
        }

        this.reconnectAttempts++;
        const delay = this.reconnectDelay * Math.pow(2, this.reconnectAttempts - 1);

        console.log(`尝试重连 (${this.reconnectAttempts}/${this.maxReconnectAttempts})，${delay}ms 后...`);

        setTimeout(() => {
            this.connect();
        }, delay);
    }

    getConnectionStatus() {
        return {
            connected: this.isConnected,
            readyState: this.ws ? this.ws.readyState : WebSocket.CLOSED,
            reconnectAttempts: this.reconnectAttempts
        };
    }
}

const wsClient = new WebSocketClient();

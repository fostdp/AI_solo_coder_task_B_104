# 古代瓷器釉面裂纹监测与纳米修复材料评估系统

## 项目概述

本系统用于某瓷器博物馆200件宋元青花瓷的釉面裂纹监测与修复评估，通过激光共聚焦显微镜和微振动传感器实时采集数据，结合Paris公式裂纹扩展预测和DEM离散元法纳米修复模拟，实现文物保护的智能化管理。

## 系统架构

```
传感器层 → PROFINET → C++后端 → PostgreSQL → 前端展示
    ↓                          ↓
20台激光共聚焦显微镜      Paris公式裂纹扩展预测
40台微振动传感器         DEM纳米修复模拟
    ↓                          ↓
每3小时上报一次          阈值告警(深度>200μm/宽度>50μm)
```

## 技术栈

### 后端
- **语言**: C++17
- **网络框架**: Boost.Asio (TCP/UDP/WebSocket/HTTP)
- **数据库**: PostgreSQL + libpqxx
- **序列化**: nlohmann/json
- **线程模型**: 多线程IO上下文池

### 数据库
- **PostgreSQL**: 存储裂纹三维坐标、传感器数据、预测结果、修复模拟结果
- **空间索引**: GIN索引用于三维坐标数组查询
- **分区表**: 按时间分区存储历史数据

### 前端
- **3D渲染**: Three.js (瓷器三维模型展示)
- **2D绘图**: Canvas (粒子填充模拟、裂纹深度渐变)
- **图表**: Chart.js (数据可视化)
- **实时通信**: WebSocket
- **样式**: 原生CSS

## 项目结构

```
AI_solo_coder_task_A_053/
├── database/
│   └── init.sql              # 数据库初始化脚本
├── backend/
│   ├── include/
│   │   ├── common.h          # 全局数据结构
│   │   ├── config.h          # 系统配置
│   │   ├── profinet_parser.h # PROFINET协议解析
│   │   ├── tcp_server.h      # Boost.Asio服务器
│   │   ├── database.h        # PostgreSQL数据访问层
│   │   ├── alert_manager.h   # 告警管理
│   │   ├── websocket_server.h # WebSocket服务器
│   │   └── http_server.h     # HTTP REST API
│   ├── algorithms/
│   │   ├── crack_propagation.h  # Paris公式裂纹扩展预测
│   │   ├── dem_simulation.h     # DEM离散元法修复模拟
│   │   ├── crack_propagation.cpp
│   │   └── dem_simulation.cpp
│   └── src/
│       ├── main.cpp          # 主程序入口
│       ├── profinet_parser.cpp
│       ├── tcp_server.cpp
│       ├── database.cpp
│       ├── alert_manager.cpp
│       ├── websocket_server.cpp
│       └── http_server.cpp
├── simulator/
│   └── main.cpp              # PROFINET传感器模拟器
├── frontend/
│   ├── index.html
│   ├── css/
│   │   └── style.css
│   └── js/
│       ├── api-client.js     # REST API客户端
│       ├── websocket-client.js # WebSocket客户端
│       ├── porcelain-viewer.js # Three.js 3D查看器
│       └── app.js            # 主应用逻辑
└── CMakeLists.txt            # CMake构建配置
```

## 核心功能

### 1. PROFINET协议通信
- 支持TCP和UDP两种传输方式
- 自定义帧格式：16字节头部 + 可变负载
- 数据包类型：激光扫描数据(0x8001)、振动数据(0x8002)、确认帧(0xFFFF)
- 自动重连和心跳机制

### 2. 裂纹三维坐标存储
- 使用PostgreSQL NUMERIC[3]数组存储三维坐标
- 点云模型存储裂纹轮廓，支持空间查询
- 每个裂纹包含深度、宽度、法向量等属性

### 3. Paris公式裂纹扩展预测
- **核心公式**: da/dN = C·(ΔK)^m
- **应力强度因子**: ΔK = Y·Δσ·√(πa)
- **数值方法**: 四阶Runge-Kutta积分
- **输出**: 裂纹扩展预测曲线、风险等级评估、剩余寿命估算

### 4. DEM离散元法纳米修复模拟
- **力模型**: Hertz接触力、阻尼力、范德华力
- **粒子系统**: 纳米氧化锆(ZrO2)、纳米二氧化硅(SiO2)、复合材料
- **评估指标**: 填充率、堆积密度、结合强度、表面光滑度、耐久性

### 5. 告警系统
- **阈值检测**: 深度>200μm 或 宽度>50μm
- **通知方式**: 短信模拟、WebSocket实时推送
- **多线程处理**: 生产者-消费者模式，异步处理告警

### 6. 前端三维展示
- Three.js渲染瓷器三维模型（青花瓷瓶造型）
- 裂纹用红色曲线高亮，深度用颜色渐变表示（蓝→绿→黄→橙→红）
- 支持实体、线框、透明三种显示模式
- 鼠标交互：旋转、缩放、平移

## 数据库表结构

| 表名 | 说明 | 核心字段 |
|------|------|----------|
| porcelains | 瓷器信息 | id, name, dynasty, origin, year |
| sensors | 传感器配置 | id, type, porcelain_id, status |
| cracks | 裂纹主表 | id, porcelain_id, max_depth, max_width |
| crack_points | 裂纹点云 | crack_id, point_index, coordinate[3], depth |
| laser_microscope_data | 激光扫描数据 | sensor_id, scan_area, resolution |
| vibration_data | 振动监测数据 | sensor_id, rms_amplitude, peak_amplitude |
| crack_propagation_predictions | 预测结果 | crack_id, horizon_hours, predicted_depth |
| repair_materials | 修复材料 | id, name, particle_radius, youngs_modulus |
| repair_simulations | 修复模拟结果 | crack_id, material_id, filling_rate |
| alerts | 告警记录 | id, alert_type, severity, status |
| profinet_packets | 通信日志 | frame_id, timestamp, payload_size |

## 部署指南

### 1. 数据库初始化

```bash
# 创建数据库
createdb porcelain_monitor

# 执行初始化脚本
psql -d porcelain_monitor -f database/init.sql

# 验证数据
psql -d porcelain_monitor -c "SELECT COUNT(*) FROM porcelains;"
```

### 2. 后端编译

```bash
# 安装依赖
# Ubuntu/Debian
sudo apt-get install libboost-all-dev libpqxx-dev postgresql-server-dev-all

# Windows (vcpkg)
vcpkg install boost:x64-windows libpqxx:x64-windows

# 构建
mkdir build && cd build
cmake ..
cmake --build . --config Release

# 运行
./bin/porcelain_monitor
```

### 3. PROFINET模拟器

```bash
# 运行模拟器（默认连接本地34964端口，3小时上报一次）
./bin/profinet_simulator

# 自定义参数：服务器IP、端口、上报间隔(ms)
./bin/profinet_simulator 192.168.1.100 34964 10800000
```

### 4. 前端部署

```bash
# 直接用浏览器打开
open frontend/index.html

# 或启动本地服务器
cd frontend
python3 -m http.server 8000
# 访问 http://localhost:8000
```

## API接口

### REST API (端口: 8080)

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | /api/porcelains | 获取瓷器列表 |
| GET | /api/porcelains/{id} | 获取瓷器详情 |
| GET | /api/porcelains/{id}/cracks | 获取瓷器裂纹列表 |
| GET | /api/cracks/{id} | 获取裂纹详情 |
| GET | /api/cracks/{id}/points | 获取裂纹三维点云 |
| POST | /api/cracks/{id}/predict?horizon_hours=720 | 裂纹扩展预测 |
| POST | /api/cracks/{id}/simulate/{material_id} | 修复模拟 |
| GET | /api/alerts | 获取告警列表 |
| PUT | /api/alerts/{id}/status | 更新告警状态 |
| GET | /api/repair-materials | 获取修复材料列表 |
| GET | /api/system/stats | 获取系统统计 |

### WebSocket (端口: 8080, 路径: /ws)

消息类型：
- `laser_data`: 激光扫描数据实时推送
- `vibration_data`: 振动监测数据实时推送
- `alert`: 告警通知
- `heartbeat`: 心跳包

## 核心算法参数

### Paris公式参数
```cpp
C = 1.5e-10   // Paris公式C参数
m = 3.0       // Paris公式m参数
Y = 1.12      // 几何修正因子
Δσ = 20.0     // 应力幅范围(MPa)
```

### DEM模拟参数
| 材料 | 粒子半径(nm) | 杨氏模量(GPa) | 泊松比 | 密度(g/cm³) |
|------|-------------|--------------|--------|------------|
| ZrO2 | 50 | 200 | 0.31 | 5.89 |
| SiO2 | 30 | 70 | 0.17 | 2.65 |
| 复合 | 40 | 150 | 0.25 | 3.8 |

## 告警阈值

```cpp
constexpr double ALERT_DEPTH_THRESHOLD = 200.0;  // μm
constexpr double ALERT_WIDTH_THRESHOLD = 50.0;   // μm
constexpr double ALERT_VIBRATION_RMS = 5.0e-7;   // m/s²
```

## 端口配置

| 端口 | 协议 | 用途 |
|------|------|------|
| 34964 | TCP/UDP | PROFINET数据采集 |
| 8080 | TCP | HTTP REST API + WebSocket |
| 5432 | TCP | PostgreSQL数据库 |

## 性能指标

- **数据处理能力**: >1000包/秒
- **数据库写入**: >5000点/秒（批量插入）
- **预测计算**: <500ms/次（720小时预测）
- **DEM模拟**: ~10秒/1000粒子/1000步
- **WebSocket延迟**: <100ms

## 注意事项

1. PROFINET协议使用小端字节序，需注意跨平台兼容性
2. 三维坐标点云数据量大，建议定期归档历史数据
3. DEM模拟为计算密集型任务，建议在独立线程执行
4. 前端需要加载Three.js CDN，确保网络连接正常
5. PostgreSQL需启用数组类型和GIN索引支持

## 故障排查

### 数据库连接失败
检查`postgresql.conf`中的`listen_addresses`配置和`pg_hba.conf`的访问权限。

### PROFINET连接超时
检查防火墙设置，确保34964端口开放。可使用Wireshark抓包分析。

### 前端3D渲染卡顿
尝试减少粒子数量，或使用WebGL 2.0渲染。可在Chrome中开启`chrome://flags/#webgl2`。

### 告警不触发
检查`alert_manager.cpp`中的阈值配置，确认数据库中数据超过阈值。

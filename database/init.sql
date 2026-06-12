-- =============================================
-- 古代瓷器釉面裂纹监测与纳米修复材料评估系统
-- PostgreSQL 数据库初始化脚本
-- =============================================

-- 创建数据库
-- CREATE DATABASE porcelain_monitor;
-- \c porcelain_monitor;

-- 启用PostGIS扩展
CREATE EXTENSION IF NOT EXISTS postgis;
CREATE EXTENSION IF NOT EXISTS postgis_topology;
CREATE EXTENSION IF NOT EXISTS fuzzystrmatch;

-- =============================================
-- 枚举类型定义
-- =============================================

CREATE TYPE sensor_type AS ENUM (
    'LASER_CONFOCAL_MICROSCOPE',
    'MICRO_VIBRATION_SENSOR'
);

CREATE TYPE alert_type AS ENUM (
    'CRACK_DEPTH_EXCEEDED',
    'CRACK_WIDTH_EXCEEDED',
    'CRACK_PROPAGATION_RISK',
    'VIBRATION_ANOMALY'
);

CREATE TYPE alert_status AS ENUM (
    'PENDING',
    'ACKNOWLEDGED',
    'RESOLVED',
    'IGNORED'
);

CREATE TYPE repair_material_type AS ENUM (
    'ZIRCONIA',
    'SILICA',
    'COMPOSITE'
);

CREATE TYPE dynasty_type AS ENUM (
    'SONG',
    'YUAN'
);

-- =============================================
-- 瓷器表 - 存储200件宋元青花瓷基本信息
-- =============================================

CREATE TABLE porcelains (
    id SERIAL PRIMARY KEY,
    museum_id VARCHAR(50) UNIQUE NOT NULL,
    name VARCHAR(200) NOT NULL,
    dynasty dynasty_type NOT NULL,
    production_year INTEGER,
    description TEXT,
    origin_location VARCHAR(200),
    acquisition_date DATE,
    dimensions JSONB,
    model_path VARCHAR(500),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- =============================================
-- 传感器表 - 60台传感器（20台激光共聚焦显微镜 + 40台微振动传感器）
-- =============================================

CREATE TABLE sensors (
    id SERIAL PRIMARY KEY,
    sensor_code VARCHAR(50) UNIQUE NOT NULL,
    type sensor_type NOT NULL,
    name VARCHAR(200) NOT NULL,
    porcelain_id INTEGER REFERENCES porcelains(id),
    installation_position NUMERIC[3],
    ip_address INET,
    mac_address MACADDR,
    sampling_interval INTEGER DEFAULT 10800,
    calibration_date DATE,
    status VARCHAR(50) DEFAULT 'ACTIVE',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- =============================================
-- 裂纹表 - 存储裂纹基本信息
-- =============================================

CREATE TABLE cracks (
    id SERIAL PRIMARY KEY,
    porcelain_id INTEGER REFERENCES porcelains(id) NOT NULL,
    crack_code VARCHAR(50) UNIQUE NOT NULL,
    detected_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    detection_method VARCHAR(100),
    status VARCHAR(50) DEFAULT 'ACTIVE',
    max_depth NUMERIC(10,3),
    max_width NUMERIC(10,3),
    total_length NUMERIC(10,3),
    description TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- =============================================
-- 裂纹三维坐标点表 - 存储裂纹的三维坐标点云
-- =============================================

CREATE TABLE crack_points (
    id BIGSERIAL PRIMARY KEY,
    crack_id INTEGER REFERENCES cracks(id) NOT NULL,
    measurement_id BIGINT,
    point_3d geometry(PointZ, 4326) NOT NULL,
    normal_vector NUMERIC[3],
    depth NUMERIC(10,3) NOT NULL,
    width NUMERIC(10,3) NOT NULL,
    curvature NUMERIC(10,6),
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- =============================================
-- 激光共聚焦显微镜数据表
-- =============================================

CREATE TABLE laser_microscope_data (
    id BIGSERIAL PRIMARY KEY,
    sensor_id INTEGER REFERENCES sensors(id) NOT NULL,
    porcelain_id INTEGER REFERENCES porcelains(id),
    measurement_time TIMESTAMP NOT NULL,
    scan_area NUMERIC[4],
    resolution NUMERIC(10,6),
    crack_detected BOOLEAN DEFAULT false,
    crack_count INTEGER DEFAULT 0,
    raw_data_path VARCHAR(500),
    processed_data JSONB,
    received_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- =============================================
-- 微振动传感器数据表
-- =============================================

CREATE TABLE vibration_data (
    id BIGSERIAL PRIMARY KEY,
    sensor_id INTEGER REFERENCES sensors(id) NOT NULL,
    porcelain_id INTEGER REFERENCES porcelains(id),
    measurement_time TIMESTAMP NOT NULL,
    frequency_spectrum JSONB,
    amplitude NUMERIC(10,6)[] NOT NULL,
    rms_value NUMERIC(10,6),
    peak_value NUMERIC(10,6),
    dominant_frequency NUMERIC(10,3),
    temperature NUMERIC(5,2),
    humidity NUMERIC(5,2),
    received_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- =============================================
-- 裂纹扩展预测结果表
-- =============================================

CREATE TABLE crack_propagation_predictions (
    id BIGSERIAL PRIMARY KEY,
    crack_id INTEGER REFERENCES cracks(id) NOT NULL,
    prediction_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    model_type VARCHAR(50) DEFAULT 'PARIS_LAW',
    parameters JSONB NOT NULL,
    time_horizon_hours INTEGER NOT NULL,
    predicted_depth NUMERIC(10,3),
    predicted_width NUMERIC(10,3),
    predicted_length NUMERIC(10,3),
    confidence NUMERIC(5,4),
    risk_level VARCHAR(20),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- =============================================
-- 修复材料表
-- =============================================

CREATE TABLE repair_materials (
    id SERIAL PRIMARY KEY,
    material_type repair_material_type NOT NULL,
    name VARCHAR(200) NOT NULL,
    manufacturer VARCHAR(200),
    particle_size_nm NUMERIC(10,3),
    purity NUMERIC(5,4),
    viscosity NUMERIC(10,4),
    refractive_index NUMERIC(10,6),
    thermal_expansion_coeff NUMERIC(10,8),
    properties JSONB,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- =============================================
-- 修复模拟结果表
-- =============================================

CREATE TABLE repair_simulations (
    id BIGSERIAL PRIMARY KEY,
    crack_id INTEGER REFERENCES cracks(id) NOT NULL,
    material_id INTEGER REFERENCES repair_materials(id) NOT NULL,
    simulation_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    method VARCHAR(50) DEFAULT 'DEM',
    parameters JSONB NOT NULL,
    particle_count INTEGER,
    filling_rate NUMERIC(5,4),
    bonding_strength NUMERIC(10,4),
    surface_smoothness NUMERIC(10,4),
    durability_score NUMERIC(5,4),
    simulation_result JSONB,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- =============================================
-- 告警表
-- =============================================

CREATE TABLE alerts (
    id BIGSERIAL PRIMARY KEY,
    alert_type alert_type NOT NULL,
    porcelain_id INTEGER REFERENCES porcelains(id),
    crack_id INTEGER REFERENCES cracks(id),
    sensor_id INTEGER REFERENCES sensors(id),
    threshold_value NUMERIC(10,3),
    actual_value NUMERIC(10,3),
    unit VARCHAR(20),
    message TEXT,
    status alert_status DEFAULT 'PENDING',
    sms_sent BOOLEAN DEFAULT false,
    websocket_sent BOOLEAN DEFAULT false,
    acknowledged_at TIMESTAMP,
    resolved_at TIMESTAMP,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- =============================================
-- PROFINET数据包日志表
-- =============================================

CREATE TABLE profinet_packets (
    id BIGSERIAL PRIMARY KEY,
    source_ip INET NOT NULL,
    destination_ip INET NOT NULL,
    packet_type VARCHAR(50),
    frame_id INTEGER,
    payload_length INTEGER,
    payload JSONB,
    received_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- =============================================
-- 索引创建
-- =============================================

-- 瓷器表索引
CREATE INDEX idx_porcelains_dynasty ON porcelains(dynasty);
CREATE INDEX idx_porcelains_museum_id ON porcelains(museum_id);

-- 传感器表索引
CREATE INDEX idx_sensors_type ON sensors(type);
CREATE INDEX idx_sensors_porcelain_id ON sensors(porcelain_id);
CREATE INDEX idx_sensors_status ON sensors(status);

-- 裂纹表索引
CREATE INDEX idx_cracks_porcelain_id ON cracks(porcelain_id);
CREATE INDEX idx_cracks_status ON cracks(status);

-- 裂纹点表索引（GiST空间索引用于PostGIS几何查询）
CREATE INDEX idx_crack_points_crack_id ON crack_points(crack_id);
CREATE INDEX idx_crack_points_timestamp ON crack_points(timestamp);
CREATE INDEX idx_crack_points_3d ON crack_points USING GIST(point_3d);

-- 激光显微镜数据表索引
CREATE INDEX idx_laser_data_sensor_id ON laser_microscope_data(sensor_id);
CREATE INDEX idx_laser_data_porcelain_id ON laser_microscope_data(porcelain_id);
CREATE INDEX idx_laser_data_measurement_time ON laser_microscope_data(measurement_time DESC);

-- 振动数据表索引
CREATE INDEX idx_vibration_data_sensor_id ON vibration_data(sensor_id);
CREATE INDEX idx_vibration_data_porcelain_id ON vibration_data(porcelain_id);
CREATE INDEX idx_vibration_data_measurement_time ON vibration_data(measurement_time DESC);

-- 预测结果表索引
CREATE INDEX idx_predictions_crack_id ON crack_propagation_predictions(crack_id);
CREATE INDEX idx_predictions_created_at ON crack_propagation_predictions(created_at DESC);

-- 修复模拟表索引
CREATE INDEX idx_simulations_crack_id ON repair_simulations(crack_id);
CREATE INDEX idx_simulations_material_id ON repair_simulations(material_id);

-- 告警表索引
CREATE INDEX idx_alerts_type ON alerts(alert_type);
CREATE INDEX idx_alerts_status ON alerts(status);
CREATE INDEX idx_alerts_created_at ON alerts(created_at DESC);
CREATE INDEX idx_alerts_porcelain_id ON alerts(porcelain_id);

-- PROFINET数据包索引
CREATE INDEX idx_profinet_source_ip ON profinet_packets(source_ip);
CREATE INDEX idx_profinet_received_at ON profinet_packets(received_at DESC);

-- =============================================
-- 触发器函数：自动更新updated_at
-- =============================================

CREATE OR REPLACE FUNCTION update_updated_at_column()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = CURRENT_TIMESTAMP;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER update_porcelains_updated_at
    BEFORE UPDATE ON porcelains
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

CREATE TRIGGER update_sensors_updated_at
    BEFORE UPDATE ON sensors
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

CREATE TRIGGER update_cracks_updated_at
    BEFORE UPDATE ON cracks
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

CREATE TRIGGER update_alerts_updated_at
    BEFORE UPDATE ON alerts
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

-- =============================================
-- 视图：裂纹统计视图
-- =============================================

CREATE OR REPLACE VIEW crack_statistics AS
SELECT
    p.id AS porcelain_id,
    p.museum_id,
    p.name,
    p.dynasty,
    COUNT(c.id) AS crack_count,
    MAX(c.max_depth) AS max_depth,
    MAX(c.max_width) AS max_width,
    SUM(c.total_length) AS total_crack_length
FROM porcelains p
LEFT JOIN cracks c ON p.id = c.porcelain_id
GROUP BY p.id, p.museum_id, p.name, p.dynasty;

-- =============================================
-- 视图：活跃告警视图
-- =============================================

CREATE OR REPLACE VIEW active_alerts AS
SELECT
    a.*,
    p.name AS porcelain_name,
    p.museum_id,
    c.crack_code
FROM alerts a
LEFT JOIN porcelains p ON a.porcelain_id = p.id
LEFT JOIN cracks c ON a.crack_id = c.id
WHERE a.status IN ('PENDING', 'ACKNOWLEDGED')
ORDER BY a.created_at DESC;

-- =============================================
-- 初始化数据
-- =============================================

-- 插入200件宋元青花瓷模拟数据
INSERT INTO porcelains (museum_id, name, dynasty, production_year, description, origin_location, dimensions)
SELECT
    'PM-' || LPAD(i::TEXT, 6, '0'),
    CASE
        WHEN i % 4 = 0 THEN '青花缠枝莲纹梅瓶'
        WHEN i % 4 = 1 THEN '青花云龙纹玉壶春瓶'
        WHEN i % 4 = 2 THEN '青花鸳鸯戏水纹大盘'
        ELSE '青花人物故事纹罐'
    END AS name,
    CASE WHEN i <= 100 THEN 'SONG'::dynasty_type ELSE 'YUAN'::dynasty_type END AS dynasty,
    CASE WHEN i <= 100 THEN 1127 + (i % 150) ELSE 1271 + (i % 98) END AS production_year,
    '珍贵的' || CASE WHEN i <= 100 THEN '宋代' ELSE '元代' END || '青花瓷，保存完好，具有重要历史价值。' AS description,
    CASE WHEN i % 3 = 0 THEN '景德镇' WHEN i % 3 = 1 THEN '龙泉' ELSE '钧窑' END AS origin_location,
    jsonb_build_object(
        'height', 20.0 + (i % 30)::numeric,
        'diameter', 10.0 + (i % 20)::numeric,
        'weight', 1.5 + (i % 5)::numeric
    ) AS dimensions
FROM generate_series(1, 200) AS i;

-- 插入20台激光共聚焦显微镜
INSERT INTO sensors (sensor_code, type, name, porcelain_id, installation_position, ip_address, mac_address)
SELECT
    'LCM-' || LPAD(i::TEXT, 3, '0'),
    'LASER_CONFOCAL_MICROSCOPE'::sensor_type,
    '激光共聚焦显微镜 #' || i,
    i,
    ARRAY[0.0, 0.0, 0.5 + (i % 10)::numeric / 10.0]::numeric[],
    ('192.168.1.' || (100 + i))::inet,
    ('00:1A:2B:3C:4D:' || LPAD((i + 10)::TEXT, 2, '0'))::macaddr
FROM generate_series(1, 20) AS i;

-- 插入40台微振动传感器
INSERT INTO sensors (sensor_code, type, name, porcelain_id, installation_position, ip_address, mac_address)
SELECT
    'MVS-' || LPAD(i::TEXT, 3, '0'),
    'MICRO_VIBRATION_SENSOR'::sensor_type,
    '微振动传感器 #' || i,
    ((i - 1) % 200) + 1,
    ARRAY[(i % 5)::numeric / 5.0, (i % 3)::numeric / 3.0, (i % 7)::numeric / 7.0]::numeric[],
    ('192.168.2.' || (100 + i))::inet,
    ('00:1A:2B:3C:4E:' || LPAD((i + 10)::TEXT, 2, '0'))::macaddr
FROM generate_series(1, 40) AS i;

-- 插入修复材料
INSERT INTO repair_materials (material_type, name, manufacturer, particle_size_nm, purity, viscosity, refractive_index, thermal_expansion_coeff, properties)
VALUES
('ZIRCONIA', '纳米氧化锆修复材料 ZrO2-50', 'AdvancedCeramics Inc.', 50.0, 0.9995, 1200.0, 2.17, 10.5e-6,
 jsonb_build_object('hardness', '1200 HV', 'fracture_toughness', '10 MPa·m^0.5', 'color', '半透明白色')),
('SILICA', '纳米二氧化硅修复材料 SiO2-30', 'NanoMaterials Ltd.', 30.0, 0.9999, 800.0, 1.458, 0.5e-6,
 jsonb_build_object('hardness', '1100 HV', 'fracture_toughness', '0.7 MPa·m^0.5', 'color', '无色透明')),
('COMPOSITE', 'ZrO2-SiO2复合修复材料', 'AdvancedCeramics Inc.', 40.0, 0.9990, 1000.0, 1.85, 5.5e-6,
 jsonb_build_object('hardness', '1150 HV', 'fracture_toughness', '8.5 MPa·m^0.5', 'color', '半透明乳白色'));

-- =============================================
-- 函数：获取瓷器最近的裂纹数据
-- =============================================

CREATE OR REPLACE FUNCTION get_recent_crack_data(p_porcelain_id INTEGER, p_hours INTEGER DEFAULT 24)
RETURNS TABLE (
    crack_id INTEGER,
    crack_code VARCHAR,
    max_depth NUMERIC,
    max_width NUMERIC,
    total_length NUMERIC,
    point_count BIGINT,
    last_measurement TIMESTAMP
) AS $$
BEGIN
    RETURN QUERY
    SELECT
        c.id,
        c.crack_code,
        c.max_depth,
        c.max_width,
        c.total_length,
        COUNT(cp.id) AS point_count,
        MAX(cp.timestamp) AS last_measurement
    FROM cracks c
    LEFT JOIN crack_points cp ON c.id = cp.crack_id
    WHERE c.porcelain_id = p_porcelain_id
      AND cp.timestamp >= NOW() - (p_hours || ' hours')::INTERVAL
    GROUP BY c.id, c.crack_code, c.max_depth, c.max_width, c.total_length;
END;
$$ LANGUAGE plpgsql;

-- =============================================
-- 函数：插入裂纹检测结果
-- =============================================

CREATE OR REPLACE FUNCTION insert_crack_detection(
    p_porcelain_id INTEGER,
    p_points JSONB,
    p_depth NUMERIC,
    p_width NUMERIC
) RETURNS INTEGER AS $$
DECLARE
    v_crack_id INTEGER;
    v_crack_code VARCHAR;
BEGIN
    v_crack_code := 'CRK-' || p_porcelain_id || '-' || TO_CHAR(NOW(), 'YYYYMMDDHH24MISS');

    INSERT INTO cracks (porcelain_id, crack_code, max_depth, max_width, total_length)
    VALUES (p_porcelain_id, v_crack_code, p_depth, p_width,
            SQRT(
                POWER((p_points->0->'x')::NUMERIC - (p_points->(jsonb_array_length(p_points)-1)->'x')::NUMERIC, 2) +
                POWER((p_points->0->'y')::NUMERIC - (p_points->(jsonb_array_length(p_points)-1)->'y')::NUMERIC, 2)
            )
           )
    RETURNING id INTO v_crack_id;

    INSERT INTO crack_points (crack_id, point_3d, depth, width, timestamp)
    SELECT
        v_crack_id,
        ARRAY[(pt->>'x')::NUMERIC, (pt->>'y')::NUMERIC, (pt->>'z')::NUMERIC]::NUMERIC[],
        (pt->>'depth')::NUMERIC,
        (pt->>'width')::NUMERIC,
        NOW()
    FROM jsonb_array_elements(p_points) AS pt;

    RETURN v_crack_id;
END;
$$ LANGUAGE plpgsql;

-- =============================================
-- 函数：PostGIS空间查询 - 查找指定范围内的裂纹点
-- =============================================

CREATE OR REPLACE FUNCTION find_crack_points_within_radius(
    p_center_x DOUBLE PRECISION,
    p_center_y DOUBLE PRECISION,
    p_center_z DOUBLE PRECISION,
    p_radius DOUBLE PRECISION
) RETURNS TABLE (
    crack_id INTEGER,
    point_id BIGINT,
    depth NUMERIC,
    width NUMERIC,
    distance DOUBLE PRECISION
) AS $$
BEGIN
    RETURN QUERY
    SELECT
        cp.crack_id,
        cp.id,
        cp.depth,
        cp.width,
        ST_3DDistance(
            cp.point_3d,
            ST_MakePoint(p_center_x, p_center_y, p_center_z)::geometry(PointZ, 4326)
        ) AS distance
    FROM crack_points cp
    WHERE ST_3DDWithin(
        cp.point_3d,
        ST_MakePoint(p_center_x, p_center_y, p_center_z)::geometry(PointZ, 4326),
        p_radius
    )
    ORDER BY distance;
END;
$$ LANGUAGE plpgsql;

-- =============================================
-- 函数：获取裂纹空间范围边界框
-- =============================================

CREATE OR REPLACE FUNCTION get_crack_bbox(p_crack_id INTEGER)
RETURNS TABLE (
    min_x DOUBLE PRECISION, min_y DOUBLE PRECISION, min_z DOUBLE PRECISION,
    max_x DOUBLE PRECISION, max_y DOUBLE PRECISION, max_z DOUBLE PRECISION
) AS $$
BEGIN
    RETURN QUERY
    SELECT
        ST_XMin(bbox) AS min_x, ST_YMin(bbox) AS min_y, ST_ZMin(bbox) AS min_z,
        ST_XMax(bbox) AS max_x, ST_YMax(bbox) AS max_y, ST_ZMax(bbox) AS max_z
    FROM (
        SELECT ST_3DExtent(cp.point_3d) AS bbox
        FROM crack_points cp
        WHERE cp.crack_id = p_crack_id
    ) sub;
END;
$$ LANGUAGE plpgsql;

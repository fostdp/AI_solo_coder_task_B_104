-- =============================================
-- 新Feature扩展：应力热力图、渗透预测、强度评估、虚拟修复
-- =============================================

-- =============================================
-- 1. 应力场分析结果表
-- =============================================

CREATE TABLE IF NOT EXISTS stress_analysis_results (
    id BIGSERIAL PRIMARY KEY,
    porcelain_id INTEGER REFERENCES porcelains(id) NOT NULL,
    analysis_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    method VARCHAR(50) DEFAULT 'FEM_CRACK_DENSITY',
    parameters JSONB NOT NULL,
    grid_resolution INTEGER DEFAULT 50,
    max_von_mises NUMERIC(12,4),
    avg_von_mises NUMERIC(12,4),
    high_stress_area_ratio NUMERIC(5,4),
    stress_field JSONB,
    result JSONB,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS stress_grid_points (
    id BIGSERIAL PRIMARY KEY,
    analysis_id BIGINT REFERENCES stress_analysis_results(id) ON DELETE CASCADE,
    x NUMERIC(10,4) NOT NULL,
    y NUMERIC(10,4) NOT NULL,
    z NUMERIC(10,4) NOT NULL,
    sigma_xx NUMERIC(12,4),
    sigma_yy NUMERIC(12,4),
    sigma_zz NUMERIC(12,4),
    tau_xy NUMERIC(12,4),
    tau_yz NUMERIC(12,4),
    tau_zx NUMERIC(12,4),
    von_mises NUMERIC(12,4),
    crack_density NUMERIC(8,4),
    principal_direction NUMERIC(10,4),
    point_3d geometry(PointZ, 4326)
);

CREATE INDEX IF NOT EXISTS idx_stress_results_porcelain ON stress_analysis_results(porcelain_id);
CREATE INDEX IF NOT EXISTS idx_stress_results_created ON stress_analysis_results(created_at DESC);
CREATE INDEX IF NOT EXISTS idx_stress_grid_analysis ON stress_grid_points(analysis_id);
CREATE INDEX IF NOT EXISTS idx_stress_grid_3d ON stress_grid_points USING GIST(point_3d);

-- =============================================
-- 2. 纳米材料渗透预测表（Washburn方程）
-- =============================================

CREATE TABLE IF NOT EXISTS penetration_predictions (
    id BIGSERIAL PRIMARY KEY,
    crack_id INTEGER REFERENCES cracks(id) NOT NULL,
    material_id INTEGER REFERENCES repair_materials(id) NOT NULL,
    prediction_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    method VARCHAR(50) DEFAULT 'WASHBURN_EQUATION',
    parameters JSONB NOT NULL,
    target_depth_um NUMERIC(10,3),
    viscosity_pa_s NUMERIC(10,4),
    surface_tension_n_m NUMERIC(10,4),
    contact_angle_deg NUMERIC(6,2),
    crack_width_um NUMERIC(10,3),
    predicted_time_s NUMERIC(12,3),
    penetration_rate_um_s NUMERIC(10,4),
    time_series NUMERIC[],
    depth_series NUMERIC[],
    result JSONB,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_penetration_crack ON penetration_predictions(crack_id);
CREATE INDEX IF NOT EXISTS idx_penetration_material ON penetration_predictions(material_id);
CREATE INDEX IF NOT EXISTS idx_penetration_created ON penetration_predictions(created_at DESC);

-- =============================================
-- 3. 四点弯曲试验结果表（强度恢复率）
-- =============================================

CREATE TABLE IF NOT EXISTS bending_test_results (
    id BIGSERIAL PRIMARY KEY,
    porcelain_id INTEGER REFERENCES porcelains(id),
    crack_id INTEGER REFERENCES cracks(id),
    material_id INTEGER REFERENCES repair_materials(id),
    test_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    method VARCHAR(50) DEFAULT 'FEM_FOUR_POINT_BENDING',
    parameters JSONB NOT NULL,
    original_strength_mpa NUMERIC(10,4),
    unrepaired_strength_mpa NUMERIC(10,4),
    repaired_strength_mpa NUMERIC(10,4),
    strength_recovery_ratio NUMERIC(6,4),
    youngs_modulus_gpa NUMERIC(10,4),
    fracture_toughness_mpa_m05 NUMERIC(10,4),
    load_series NUMERIC[],
    displacement_series NUMERIC[],
    stress_distribution JSONB,
    result JSONB,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_bending_porcelain ON bending_test_results(porcelain_id);
CREATE INDEX IF NOT EXISTS idx_bending_crack ON bending_test_results(crack_id);
CREATE INDEX IF NOT EXISTS idx_bending_material ON bending_test_results(material_id);
CREATE INDEX IF NOT EXISTS idx_bending_created ON bending_test_results(created_at DESC);

-- =============================================
-- 4. 虚拟修复记录表
-- =============================================

CREATE TABLE IF NOT EXISTS virtual_repair_records (
    id BIGSERIAL PRIMARY KEY,
    porcelain_id INTEGER REFERENCES porcelains(id) NOT NULL,
    crack_id INTEGER REFERENCES cracks(id),
    user_id VARCHAR(100),
    repair_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    repaired_region JSONB,
    repair_radius NUMERIC(10,3),
    estimated_closure_ratio NUMERIC(5,4),
    animation_data JSONB,
    notes TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_virtual_repair_porcelain ON virtual_repair_records(porcelain_id);
CREATE INDEX IF NOT EXISTS idx_virtual_repair_crack ON virtual_repair_records(crack_id);
CREATE INDEX IF NOT EXISTS idx_virtual_repair_created ON virtual_repair_records(created_at DESC);

-- =============================================
-- 视图：应力分析统计视图
-- =============================================

CREATE OR REPLACE VIEW stress_statistics AS
SELECT
    p.id AS porcelain_id,
    p.museum_id,
    p.name,
    COUNT(s.id) AS analysis_count,
    MAX(s.max_von_mises) AS max_stress_recorded,
    AVG(s.avg_von_mises) AS avg_stress,
    MAX(s.created_at) AS last_analysis
FROM porcelains p
LEFT JOIN stress_analysis_results s ON p.id = s.porcelain_id
GROUP BY p.id, p.museum_id, p.name;

-- =============================================
-- 视图：材料渗透对比视图
-- =============================================

CREATE OR REPLACE VIEW material_penetration_comparison AS
SELECT
    c.id AS crack_id,
    c.crack_code,
    m.name AS material_name,
    m.material_type,
    p.viscosity_pa_s,
    p.predicted_time_s,
    p.penetration_rate_um_s,
    p.target_depth_um,
    p.created_at
FROM penetration_predictions p
JOIN cracks c ON p.crack_id = c.id
JOIN repair_materials m ON p.material_id = m.id
ORDER BY p.crack_id, p.predicted_time_s;

-- =============================================
-- 函数：获取瓷器最新应力分析
-- =============================================

CREATE OR REPLACE FUNCTION get_latest_stress_analysis(p_porcelain_id INTEGER)
RETURNS TABLE (
    analysis_id BIGINT,
    grid_points JSONB
) AS $$
BEGIN
    RETURN QUERY
    SELECT
        s.id,
        jsonb_agg(jsonb_build_object(
            'x', gp.x, 'y', gp.y, 'z', gp.z,
            'von_mises', gp.von_mises,
            'sigma_xx', gp.sigma_xx,
            'sigma_yy', gp.sigma_yy,
            'sigma_zz', gp.sigma_zz,
            'crack_density', gp.crack_density,
            'principal_direction', gp.principal_direction
        ) ORDER BY gp.x, gp.y, gp.z)
    FROM stress_analysis_results s
    JOIN stress_grid_points gp ON s.id = gp.analysis_id
    WHERE s.porcelain_id = p_porcelain_id
    GROUP BY s.id
    ORDER BY s.created_at DESC
    LIMIT 1;
END;
$$ LANGUAGE plpgsql;

-- =============================================
-- 函数：计算材料渗透效率评分
-- =============================================

CREATE OR REPLACE FUNCTION calculate_penetration_efficiency(
    p_viscosity NUMERIC,
    p_surface_tension NUMERIC,
    p_contact_angle NUMERIC,
    p_crack_width NUMERIC
) RETURNS NUMERIC AS $$
DECLARE
    v_eff NUMERIC;
BEGIN
    v_eff := (p_surface_tension * COS(p_contact_angle * PI() / 180.0)) 
             / (p_viscosity * SQRT(p_crack_width));
    RETURN ROUND(GREATEST(0, LEAST(100, v_eff * 1000)), 2);
END;
$$ LANGUAGE plpgsql;

-- =============================================
-- 函数：计算强度恢复等级
-- =============================================

CREATE OR REPLACE FUNCTION get_strength_recovery_level(p_ratio NUMERIC)
RETURNS VARCHAR AS $$
BEGIN
    IF p_ratio >= 0.95 THEN RETURN 'EXCELLENT';
    ELSIF p_ratio >= 0.85 THEN RETURN 'GOOD';
    ELSIF p_ratio >= 0.70 THEN RETURN 'FAIR';
    ELSIF p_ratio >= 0.50 THEN RETURN 'POOR';
    ELSE RETURN 'CRITICAL';
    END IF;
END;
$$ LANGUAGE plpgsql;

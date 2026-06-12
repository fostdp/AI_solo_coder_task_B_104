cmake_minimum_required(VERSION 3.15)
project(PorcelainMonitor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0A00)
    add_definitions(-D_USE_MATH_DEFINES)
endif()

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()

include_directories(
    ${CMAKE_SOURCE_DIR}/backend/include
    ${CMAKE_SOURCE_DIR}/backend/include/modules
    ${CMAKE_SOURCE_DIR}/backend/algorithms
    ${CMAKE_SOURCE_DIR}/backend/modules
    ${CMAKE_SOURCE_DIR}/tests
)

# ============================================================
# 公共核心库 (common_utils) - 所有模块共享
# ============================================================
set(COMMON_UTILS_SOURCES
    backend/algorithms/stress_analysis.cpp
    backend/algorithms/washburn_model.cpp
    backend/algorithms/four_point_bending.cpp
    backend/algorithms/crack_propagation.cpp
    backend/algorithms/dem_simulation.cpp
)

add_library(common_utils STATIC ${COMMON_UTILS_SOURCES})
target_include_directories(common_utils PUBLIC
    ${CMAKE_SOURCE_DIR}/backend/include
    ${CMAKE_SOURCE_DIR}/backend/include/modules
    ${CMAKE_SOURCE_DIR}/backend/algorithms
)

# ============================================================
# 模块公共基础库 (modules_common) - 线程池 / IPC / 工厂
# ============================================================
set(MODULES_COMMON_SOURCES
    backend/include/modules/common/thread_pool.cpp
    backend/modules/common/ipc_channel.cpp
    backend/modules/common/module_factory.cpp
)

add_library(modules_common STATIC ${MODULES_COMMON_SOURCES})
target_link_libraries(modules_common PUBLIC common_utils)

# ============================================================
# 独立模块 1: fem_stress (有限元应力分析 - 集成 deal.II adapter)
# ============================================================
set(FEM_STRESS_SOURCES
    backend/modules/fem_stress/fem_stress_module.cpp
    backend/modules/fem_stress/dealii_adapter.cpp
)

add_library(fem_stress_module STATIC ${FEM_STRESS_SOURCES})
target_link_libraries(fem_stress_module PUBLIC modules_common common_utils)
target_compile_definitions(fem_stress_module PUBLIC PM_MODULE_FEM_STRESS)

# ============================================================
# 独立模块 2: washburn_penetration (渗透计算 - 线程池并行)
# ============================================================
set(WASHBURN_SOURCES
    backend/modules/washburn_penetration/washburn_module.cpp
)

add_library(washburn_penetration_module STATIC ${WASHBURN_SOURCES})
target_link_libraries(washburn_penetration_module PUBLIC modules_common common_utils)
target_compile_definitions(washburn_penetration_module PUBLIC PM_MODULE_WASHBURN)

# ============================================================
# 独立模块 3: strength_fem (强度有限元 - 集成 deal.II adapter)
# ============================================================
set(STRENGTH_FEM_SOURCES
    backend/modules/strength_fem/strength_module.cpp
    backend/modules/strength_fem/dealii_adapter.cpp
)

add_library(strength_fem_module STATIC ${STRENGTH_FEM_SOURCES})
target_link_libraries(strength_fem_module PUBLIC modules_common common_utils)
target_compile_definitions(strength_fem_module PUBLIC PM_MODULE_STRENGTH)

# ============================================================
# 独立模块 4: vr_preview (虚拟修复预览)
# ============================================================
set(VR_PREVIEW_SOURCES
    backend/modules/vr_preview/vr_module.cpp
)

add_library(vr_preview_module STATIC ${VR_PREVIEW_SOURCES})
target_link_libraries(vr_preview_module PUBLIC modules_common common_utils)
target_compile_definitions(vr_preview_module PUBLIC PM_MODULE_VR)

# 所有模块集合成一个便捷 target
add_library(all_modules INTERFACE)
target_link_libraries(all_modules INTERFACE
    fem_stress_module
    washburn_penetration_module
    strength_fem_module
    vr_preview_module
    modules_common
    common_utils
)

# ============================================================
# 可选: 外部依赖 (Boost / PostgreSQL / spdlog)
# ============================================================
find_package(Boost 1.70 COMPONENTS system thread chrono regex filesystem)
find_package(PostgreSQL)
find_package(spdlog)

set(HAS_BOOST ${Boost_FOUND})
set(HAS_POSTGRESQL ${PostgreSQL_FOUND})
set(HAS_SPDLOG ${spdlog_FOUND})

if(HAS_BOOST)
    add_definitions(-DHAS_BOOST)
    include_directories(${Boost_INCLUDE_DIRS})
endif()

if(HAS_POSTGRESQL)
    add_definitions(-DHAS_POSTGRESQL)
    include_directories(${PostgreSQL_INCLUDE_DIRS})
endif()

if(HAS_SPDLOG)
    add_definitions(-DHAS_SPDLOG)
endif()

# ============================================================
# 主程序 (仅当依赖齐全时编译)
# ============================================================
if(HAS_BOOST AND HAS_POSTGRESQL AND HAS_SPDLOG)
    set(BACKEND_SOURCES
        backend/src/main.cpp
        backend/src/profinet_parser.cpp
        backend/src/tcp_server.cpp
        backend/src/database.cpp
        backend/src/database_extensions.cpp
        backend/src/alert_manager.cpp
        backend/src/websocket_server.cpp
        backend/src/http_server.cpp
    )

    add_executable(porcelain_monitor ${BACKEND_SOURCES})
    target_link_libraries(porcelain_monitor
        all_modules
        ${Boost_LIBRARIES}
        ${PostgreSQL_LIBRARIES}
        spdlog::spdlog
    )
    if(WIN32)
        target_link_libraries(porcelain_monitor ws2_32 mswsock)
    endif()

    set(SIMULATOR_SOURCES
        simulator/main.cpp
        backend/src/profinet_parser.cpp
    )
    add_executable(profinet_simulator ${SIMULATOR_SOURCES})
    target_link_libraries(profinet_simulator ${Boost_LIBRARIES})
    if(WIN32)
        target_link_libraries(profinet_simulator ws2_32 mswsock)
    endif()

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    install(TARGETS porcelain_monitor profinet_simulator RUNTIME DESTINATION bin)
    install(DIRECTORY frontend/ DESTINATION share/porcelain_monitor/frontend)
    install(FILES database/init.sql DESTINATION share/porcelain_monitor/database)
endif()

# ============================================================
# FEM Stress 独立进程 (基于 deal.II，独立进程)
# ============================================================
add_executable(fem_stress_process
    backend/modules/fem_stress/process_main.cpp
    backend/modules/fem_stress/fem_stress_module.cpp
    backend/modules/fem_stress/dealii_adapter.cpp
    backend/algorithms/stress_analysis.cpp
    backend/modules/common/ipc_channel.cpp
    backend/include/modules/common/thread_pool.cpp
    backend/modules/common/module_factory.cpp
)
target_include_directories(fem_stress_process PRIVATE
    ${CMAKE_SOURCE_DIR}/backend/include
    ${CMAKE_SOURCE_DIR}/backend/include/modules
    ${CMAKE_SOURCE_DIR}/backend/algorithms
)
target_compile_definitions(fem_stress_process PRIVATE PM_FEM_STANDALONE_PROCESS)
if(HAS_SPDLOG)
    target_link_libraries(fem_stress_process spdlog::spdlog)
endif()
if(WIN32)
    target_link_libraries(fem_stress_process ws2_32 mswsock)
endif()

# ============================================================
# Strength FEM 独立进程 (基于 deal.II，独立进程)
# ============================================================
add_executable(strength_fem_process
    backend/modules/strength_fem/process_main.cpp
    backend/modules/strength_fem/strength_module.cpp
    backend/modules/strength_fem/dealii_adapter.cpp
    backend/modules/fem_stress/fem_stress_module.cpp
    backend/modules/fem_stress/dealii_adapter.cpp
    backend/algorithms/four_point_bending.cpp
    backend/algorithms/stress_analysis.cpp
    backend/algorithms/washburn_model.cpp
    backend/modules/common/ipc_channel.cpp
    backend/include/modules/common/thread_pool.cpp
    backend/modules/common/module_factory.cpp
)
target_include_directories(strength_fem_process PRIVATE
    ${CMAKE_SOURCE_DIR}/backend/include
    ${CMAKE_SOURCE_DIR}/backend/include/modules
    ${CMAKE_SOURCE_DIR}/backend/algorithms
)
target_compile_definitions(strength_fem_process PRIVATE PM_STRENGTH_STANDALONE_PROCESS)
if(HAS_SPDLOG)
    target_link_libraries(strength_fem_process spdlog::spdlog)
endif()
if(WIN32)
    target_link_libraries(strength_fem_process ws2_32 mswsock)
endif()

# ============================================================
# 测试目标 (始终编译，无需外部依赖)
# ============================================================

# 单测: stress_analysis
set(TEST_STRESS_SOURCES
    tests/test_stress_analysis.cpp
    backend/algorithms/stress_analysis.cpp
)
add_executable(test_stress_analysis ${TEST_STRESS_SOURCES})
target_include_directories(test_stress_analysis PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/backend/include
    ${CMAKE_SOURCE_DIR}/backend/algorithms
)
if(WIN32)
    target_link_libraries(test_stress_analysis ws2_32 mswsock)
endif()

# 单测: washburn_model
set(TEST_WASHBURN_SOURCES
    tests/test_washburn_model.cpp
    backend/algorithms/washburn_model.cpp
)
add_executable(test_washburn_model ${TEST_WASHBURN_SOURCES})
target_include_directories(test_washburn_model PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/backend/include
    ${CMAKE_SOURCE_DIR}/backend/algorithms
)
if(WIN32)
    target_link_libraries(test_washburn_model ws2_32 mswsock)
endif()

# 单测: four_point_bending
set(TEST_BENDING_SOURCES
    tests/test_four_point_bending.cpp
    backend/algorithms/four_point_bending.cpp
)
add_executable(test_four_point_bending ${TEST_BENDING_SOURCES})
target_include_directories(test_four_point_bending PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/backend/include
    ${CMAKE_SOURCE_DIR}/backend/algorithms
)
if(WIN32)
    target_link_libraries(test_four_point_bending ws2_32 mswsock)
endif()

# 完整回归测试 (包含 4 个模块)
set(ALL_TEST_SOURCES
    tests/test_main.cpp
    tests/test_stress_analysis.cpp
    tests/test_washburn_model.cpp
    tests/test_four_point_bending.cpp
    tests/test_modules_regression.cpp
)
add_executable(porcelain_tests ${ALL_TEST_SOURCES})
target_compile_definitions(porcelain_tests PRIVATE PORCELAIN_TESTS_COMBINED)
target_link_libraries(porcelain_tests all_modules)
if(WIN32)
    target_link_libraries(porcelain_tests ws2_32 mswsock)
endif()

enable_testing()
add_test(NAME AllTests COMMAND porcelain_tests)
add_test(NAME StressAnalysisTests COMMAND porcelain_tests --suite StressFEMTests)
add_test(NAME WashburnModelTests COMMAND porcelain_tests --suite WashburnModelTests)
add_test(NAME FourPointBendingTests COMMAND porcelain_tests --suite BendingTestTests)
add_test(NAME ModuleRegressionTests COMMAND porcelain_tests --suite ModuleRegressionTests)
add_test(NAME StressAcceptance COMMAND porcelain_tests --suite StressFEMTests --test Acceptance)
add_test(NAME WashburnAcceptance COMMAND porcelain_tests --suite WashburnModelTests --test Acceptance)
add_test(NAME BendingAcceptance COMMAND porcelain_tests --suite BendingTestTests --test Acceptance)
add_test(NAME ModuleAcceptance COMMAND porcelain_tests --suite ModuleRegressionTests --test Acceptance)

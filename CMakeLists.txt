cmake_minimum_required(VERSION 3.16)

project(M5CardputerZeroCompass VERSION 0.2.4 LANGUAGES C CXX ASM)

option(COMPASS_USE_SDL "Use LVGL's SDL backend for desktop testing" ON)
option(COMPASS_CONVERT_ASSETS "Convert image assets during CMake configure" ON)
option(BUILD_TESTING "Build Compass tests" OFF)
set(COMPASS_BIN_NAME "M5CardputerZero-Compass" CACHE STRING "Output executable name")

include(CTest)

if(APPLE AND NOT COMPASS_USE_SDL)
    message(FATAL_ERROR "macOS builds only support COMPASS_USE_SDL=ON.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(COMPASS_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(COMPASS_SRC_DIR "${COMPASS_ROOT}/src")
set(COMPASS_DEPS_DIR "${COMPASS_ROOT}/dependencies")
set(COMPASS_LVGL_DIR "${COMPASS_DEPS_DIR}/lvgl")
set(COMPASS_SPDLOG_DIR "${COMPASS_DEPS_DIR}/spdlog")
set(COMPASS_SMOOTH_UI_DIR "${COMPASS_DEPS_DIR}/smooth_ui_toolkit")
set(COMPASS_SYSTEM_LIBS ${CMAKE_DL_LIBS})

if(UNIX AND NOT APPLE)
    list(APPEND COMPASS_SYSTEM_LIBS m)
endif()

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
endif()

if(NOT DEFINED Python3_EXECUTABLE)
    foreach(candidate IN ITEMS
        "${COMPASS_ROOT}/.venv/bin/python"
        "${COMPASS_ROOT}/../.venv/bin/python"
        "${COMPASS_ROOT}/../../.venv/bin/python"
    )
        if(EXISTS "${candidate}")
            execute_process(
                COMMAND "${candidate}" -m pip --version
                RESULT_VARIABLE COMPASS_PYTHON_PIP_RESULT
                OUTPUT_QUIET
                ERROR_QUIET
            )
            if(COMPASS_PYTHON_PIP_RESULT EQUAL 0)
                set(Python3_EXECUTABLE "${candidate}")
                break()
            endif()
        endif()
    endforeach()
endif()

foreach(required_dir IN LISTS COMPASS_LVGL_DIR COMPASS_SPDLOG_DIR COMPASS_SMOOTH_UI_DIR)
    if(NOT EXISTS "${required_dir}/CMakeLists.txt")
        message(FATAL_ERROR "Missing dependency: ${required_dir}. Run ./bootstrap.sh first.")
    endif()
endforeach()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(Threads REQUIRED)

if(COMPASS_CONVERT_ASSETS)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${COMPASS_SRC_DIR}/assets/convert_images.py"
        WORKING_DIRECTORY "${COMPASS_ROOT}"
        RESULT_VARIABLE COMPASS_ASSET_RESULT
    )
    if(NOT COMPASS_ASSET_RESULT EQUAL 0)
        message(FATAL_ERROR "Image asset conversion failed")
    endif()
endif()

set(LV_BUILD_CONF_PATH "${COMPASS_ROOT}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)

set(SMOOTH_UI_TOOLKIT_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SMOOTH_UI_TOOLKIT_BUILD_TEST OFF CACHE BOOL "" FORCE)

add_subdirectory("${COMPASS_LVGL_DIR}" EXCLUDE_FROM_ALL)
add_subdirectory("${COMPASS_SPDLOG_DIR}" EXCLUDE_FROM_ALL)
add_subdirectory("${COMPASS_SMOOTH_UI_DIR}" EXCLUDE_FROM_ALL)

function(compass_add_sdl2_parent_include target imported_target)
    if(NOT TARGET "${imported_target}")
        return()
    endif()

    get_target_property(sdl2_include_dirs "${imported_target}" INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT sdl2_include_dirs)
        return()
    endif()

    foreach(sdl2_include_dir IN LISTS sdl2_include_dirs)
        if(sdl2_include_dir MATCHES "/SDL2$")
            get_filename_component(sdl2_parent_include_dir "${sdl2_include_dir}" DIRECTORY)
            target_include_directories("${target}" SYSTEM PRIVATE "${sdl2_parent_include_dir}")
        endif()
    endforeach()
endfunction()

set(COMPASS_LVGL_POSIX_PATH "./")
if(NOT COMPASS_USE_SDL)
    set(COMPASS_LVGL_POSIX_PATH "/usr/share/Compass/")
endif()

target_compile_definitions(lvgl PUBLIC
    COMPASS_LVGL_USE_SDL=$<IF:$<BOOL:${COMPASS_USE_SDL}>,1,0>
    COMPASS_LVGL_FS_POSIX_PATH="${COMPASS_LVGL_POSIX_PATH}"
)

if(COMPASS_USE_SDL)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
    endif()

    if(SDL2_FOUND)
        target_link_libraries(lvgl PRIVATE PkgConfig::SDL2)
        compass_add_sdl2_parent_include(lvgl PkgConfig::SDL2)
        set(COMPASS_SDL_LIBS PkgConfig::SDL2)
    else()
        find_package(SDL2 CONFIG QUIET)
        if(TARGET SDL2::SDL2)
            target_link_libraries(lvgl PRIVATE SDL2::SDL2)
            compass_add_sdl2_parent_include(lvgl SDL2::SDL2)
            set(COMPASS_SDL_LIBS SDL2::SDL2)
        else()
            find_package(SDL2 REQUIRED)
            target_include_directories(lvgl SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
            foreach(sdl2_include_dir IN LISTS SDL2_INCLUDE_DIRS)
                if(sdl2_include_dir MATCHES "/SDL2$")
                    get_filename_component(sdl2_parent_include_dir "${sdl2_include_dir}" DIRECTORY)
                    target_include_directories(lvgl SYSTEM PRIVATE "${sdl2_parent_include_dir}")
                endif()
            endforeach()
            set(COMPASS_SDL_LIBS ${SDL2_LIBRARIES})
        endif()
    endif()
endif()

file(GLOB COMPASS_CPP CONFIGURE_DEPENDS
    "${COMPASS_SRC_DIR}/main.cpp"
    "${COMPASS_SRC_DIR}/core/*.cpp"
    "${COMPASS_SRC_DIR}/hal/*.cpp"
    "${COMPASS_SRC_DIR}/input/*.cpp"
    "${COMPASS_SRC_DIR}/models/*.cpp"
    "${COMPASS_SRC_DIR}/view_models/*.cpp"
    "${COMPASS_SRC_DIR}/views/*.cpp"
)
file(GLOB_RECURSE COMPASS_ASSET_C CONFIGURE_DEPENDS
    "${COMPASS_SRC_DIR}/assets/res_c/*.c"
)

add_executable(${COMPASS_BIN_NAME}
    ${COMPASS_CPP}
    ${COMPASS_ASSET_C}
)

target_include_directories(${COMPASS_BIN_NAME} PRIVATE
    "${COMPASS_SRC_DIR}"
    "${COMPASS_SRC_DIR}/assets"
)

target_compile_definitions(${COMPASS_BIN_NAME} PRIVATE
    $<$<BOOL:${COMPASS_USE_SDL}>:COMPASS_USE_MOCK_IMU=1>
    $<$<NOT:$<BOOL:${COMPASS_USE_SDL}>>:COMPASS_USE_IIO_IMU=1>
)

target_link_libraries(${COMPASS_BIN_NAME} PRIVATE
    lvgl::lvgl
    smooth_ui_toolkit
    spdlog::spdlog
    Threads::Threads
    ${COMPASS_SDL_LIBS}
    ${COMPASS_SYSTEM_LIBS}
)

set_target_properties(${COMPASS_BIN_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${COMPASS_ROOT}/dist"
)

if(BUILD_TESTING)
    set(COMPASS_TEST_APP_CPP ${COMPASS_CPP})
    list(REMOVE_ITEM COMPASS_TEST_APP_CPP "${COMPASS_SRC_DIR}/main.cpp")
    add_executable(compass_calibration_flow_tests
        tests/calibration_flow_test.cpp
        ${COMPASS_TEST_APP_CPP}
        ${COMPASS_ASSET_C}
    )
    target_include_directories(compass_calibration_flow_tests PRIVATE "${COMPASS_SRC_DIR}")
    target_compile_definitions(compass_calibration_flow_tests PRIVATE COMPASS_USE_MOCK_IMU=1)
    target_link_libraries(compass_calibration_flow_tests PRIVATE
        lvgl::lvgl smooth_ui_toolkit spdlog::spdlog Threads::Threads
        ${COMPASS_SDL_LIBS} ${COMPASS_SYSTEM_LIBS}
    )
    add_test(NAME compass_calibration_flow COMMAND compass_calibration_flow_tests)

    add_executable(compass_pose_tests
        tests/compass_pose_test.cpp
        src/models/compass_pose.cpp
    )
    target_include_directories(compass_pose_tests PRIVATE
        "${COMPASS_SRC_DIR}"
    )
    add_test(NAME compass_pose COMMAND compass_pose_tests)

    add_executable(compass_calibration_tests
        tests/calibration_model_test.cpp
        src/models/calibration_model.cpp
        src/models/compass_pose.cpp
    )
    target_include_directories(compass_calibration_tests PRIVATE
        "${COMPASS_SRC_DIR}"
    )
    target_compile_definitions(compass_calibration_tests PRIVATE
        COMPASS_USE_MOCK_IMU=1
    )
    target_link_libraries(compass_calibration_tests PRIVATE
        smooth_ui_toolkit
        spdlog::spdlog
    )
    add_test(NAME compass_calibration COMMAND compass_calibration_tests)

    add_executable(compass_mock_calibration_tests
        tests/compass_mock_calibration_test.cpp
        src/models/compass_model.cpp
        src/models/calibration_model.cpp
        src/models/compass_pose.cpp
    )
    target_include_directories(compass_mock_calibration_tests PRIVATE
        "${COMPASS_SRC_DIR}"
    )
    target_compile_definitions(compass_mock_calibration_tests PRIVATE
        COMPASS_USE_MOCK_IMU=1
    )
    target_link_libraries(compass_mock_calibration_tests PRIVATE
        smooth_ui_toolkit
        spdlog::spdlog
    )
    add_test(NAME compass_mock_calibration COMMAND compass_mock_calibration_tests)

    add_executable(compass_model_unavailable_tests
        tests/compass_model_unavailable_test.cpp
        src/core/compass_router.cpp
        src/models/compass_model.cpp
        src/models/calibration_model.cpp
        src/models/compass_pose.cpp
        src/view_models/compass_view_model.cpp
    )
    target_include_directories(compass_model_unavailable_tests PRIVATE
        "${COMPASS_SRC_DIR}"
    )
    target_compile_definitions(compass_model_unavailable_tests PRIVATE
        COMPASS_USE_IIO_IMU=1
        COMPASS_USE_MOCK_IMU=0
    )
    target_link_libraries(compass_model_unavailable_tests PRIVATE
        smooth_ui_toolkit
        spdlog::spdlog
    )
    add_test(NAME compass_model_unavailable COMMAND compass_model_unavailable_tests)
endif()

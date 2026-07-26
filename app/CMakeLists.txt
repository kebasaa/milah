cmake_minimum_required(VERSION 3.24)

project(Milah VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

option(MILAH_BUILD_TESTS "Build Milah's minimal native test" OFF)
option(MILAH_DEPLOY_QT "Run windeployqt after a Windows build" OFF)

find_package(Qt6 6.6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    WebChannel
    WebEngineWidgets
)
find_package(QuaZip-Qt6 CONFIG REQUIRED)
find_program(NPM_EXECUTABLE npm REQUIRED)

set(FRONTEND_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/frontend")
set(FRONTEND_DIST_DIR "${FRONTEND_SOURCE_DIR}/dist")
set(FRONTEND_OUTPUTS
    "${FRONTEND_DIST_DIR}/index.html"
    "${FRONTEND_DIST_DIR}/assets/app.js"
    "${FRONTEND_DIST_DIR}/assets/app.css"
)
file(GLOB_RECURSE FRONTEND_SOURCES CONFIGURE_DEPENDS
    "${FRONTEND_SOURCE_DIR}/src/*"
    "${FRONTEND_SOURCE_DIR}/index.html"
    "${FRONTEND_SOURCE_DIR}/package.json"
    "${FRONTEND_SOURCE_DIR}/tsconfig.json"
    "${FRONTEND_SOURCE_DIR}/vite.config.ts"
)

add_custom_command(
    OUTPUT ${FRONTEND_OUTPUTS}
    COMMAND "${NPM_EXECUTABLE}" run build
    WORKING_DIRECTORY "${FRONTEND_SOURCE_DIR}"
    DEPENDS ${FRONTEND_SOURCES}
    COMMENT "Building the Milah web interface"
    VERBATIM
)
add_custom_target(MilahFrontend DEPENDS ${FRONTEND_OUTPUTS})
set_source_files_properties(${FRONTEND_OUTPUTS} PROPERTIES GENERATED TRUE)

qt_add_executable(Milah WIN32
    src/main.cpp
    src/main_window.cpp
    src/main_window.h
    src/project_storage.cpp
    src/project_storage.h
    src/web_bridge.cpp
    src/web_bridge.h
)

qt_add_resources(Milah "milah_frontend"
    PREFIX "/"
    BASE "${FRONTEND_DIST_DIR}"
    FILES ${FRONTEND_OUTPUTS}
)

add_dependencies(Milah MilahFrontend)

target_link_libraries(Milah PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::WebChannel
    Qt6::WebEngineWidgets
    QuaZip::QuaZip
)

target_compile_definitions(Milah PRIVATE
    MILAH_VERSION="${PROJECT_VERSION}"
)

if(WIN32 AND MILAH_DEPLOY_QT)
    add_custom_command(TARGET Milah POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -DMILAH_EXECUTABLE=$<TARGET_FILE:Milah>
            -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/deploy_windows.cmake"
        VERBATIM
    )
endif()

if(MILAH_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.6 REQUIRED COMPONENTS Test)
    qt_add_executable(MilahProjectStorageTest
        tests/project_storage_test.cpp
        src/project_storage.cpp
        src/project_storage.h
    )
    target_include_directories(MilahProjectStorageTest PRIVATE src)
    target_link_libraries(MilahProjectStorageTest PRIVATE
        Qt6::Core
        Qt6::Test
        QuaZip::QuaZip
    )
    add_test(NAME MilahProjectStorageTest COMMAND MilahProjectStorageTest)
endif()

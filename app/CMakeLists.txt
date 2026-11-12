cmake_minimum_required(VERSION 3.25)

# The one place the application version is written. It reaches the binary as
# MILAH_VERSION (below), the About dialog, `Milah.exe --version`, and the
# Windows VERSIONINFO resource. It is unrelated to the `.milah` project file
# format version, which is pinned to 1 in src/core/project.cpp and gates
# whether a saved project can be opened at all.
project(Milah VERSION 1.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MILAH_BUILD_TESTS "Build Milah's test suite" OFF)

# Used by the corpus tests to find the published OSIS files, which live in the
# sibling hebrew_manuscripts repository rather than in this one: they carry
# their own licences. The tests skip themselves when no corpus is found, so the
# suite stays runnable from a standalone copy of app/.
set(MILAH_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH
    "Repository root; the corpus tests look for ../hebrew_manuscripts beside it")

# A Windows install is a portable folder: the executable, Qt's DLLs and data/
# all side by side, which is what dataSearchPaths() looks for first. Set in the
# cache and before find_package(Qt6), because Qt writes its deployment script
# from it there: set later, Milah.exe landed in one folder and its DLLs in
# another.
if(WIN32)
    set(CMAKE_INSTALL_BINDIR "." CACHE PATH "Executables (portable layout)" FORCE)
endif()

# Network is used by exactly one thing: fetching published manuscripts into the
# local library. MilahCore never touches it.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Network Widgets)

# Sets AUTOMOC/AUTOUIC, GNUInstallDirs and the Qt policies for every version
# up to the one named, as Qt's own CMake documentation recommends.
qt_standard_project_setup(REQUIRES 6.6)

# --------------------------------------------------------------------------
# Third-party: QuaZip (and the zlib it needs)
#
# Built from pinned sources as a static library, so every platform links the
# same QuaZip and no extra DLL has to be found at run time or deployed. An
# installed QuaZip is used instead when CMake can find one, which is what a
# distribution packager wants (FIND_PACKAGE_ARGS below).
# --------------------------------------------------------------------------

include(FetchContent)

# QuaZip looks for zlib with find_package(ZLIB REQUIRED). Linux and macOS have a
# system copy; a Windows toolchain usually does not, and Qt keeps its own zlib
# inside Qt6Core where nothing else can link it. Build one there instead.
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    FetchContent_Declare(ZLIB
        URL https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz
        URL_HASH SHA256=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
        OVERRIDE_FIND_PACKAGE
        EXCLUDE_FROM_ALL
    )
    block()
        set(ZLIB_BUILD_SHARED OFF)
        set(ZLIB_BUILD_STATIC ON)
        set(ZLIB_BUILD_TESTING OFF)
        set(ZLIB_INSTALL OFF)
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
        FetchContent_MakeAvailable(ZLIB)
    endblock()
    # zlib's own CMake names only its static target ZLIB::ZLIBSTATIC; QuaZip
    # links ZLIB::ZLIB.
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

FetchContent_Declare(QuaZip-Qt6
    URL https://github.com/stachenov/quazip/archive/refs/tags/v1.7.1.zip
    URL_HASH SHA256=244c9971544cac9b9132f239c2f10bc336cc94c00e313b7da5bc03e1fe5cba0f
    EXCLUDE_FROM_ALL
    FIND_PACKAGE_ARGS CONFIG
)
block()
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(QUAZIP_QT_MAJOR_VERSION 6)
    # QTextCodec would need Qt6::Core5Compat; QuaZip falls back to
    # QStringConverter without it, which is all Milah needs.
    set(QUAZIP_ENABLE_QTEXTCODEC OFF)
    set(QUAZIP_BZIP2 OFF)
    set(QUAZIP_FETCH_LIBS OFF)
    set(QUAZIP_INSTALL OFF)
    set(QUAZIP_ENABLE_TESTS OFF)
    FetchContent_MakeAvailable(QuaZip-Qt6)
endblock()

# --------------------------------------------------------------------------
# Warnings and deprecations, for Milah's own targets only
# --------------------------------------------------------------------------

add_library(milah_warnings INTERFACE)
if(MSVC)
    target_compile_options(milah_warnings INTERFACE /W4 /permissive-)
else()
    # Designated initialisers that leave defaulted members out are the idiom
    # here (DocxRun{.text = ...}), not an oversight.
    target_compile_options(milah_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()
# Anything Qt deprecated up to the oldest version Milah supports is an error
# at compile time rather than a surprise at the next Qt upgrade.
target_compile_definitions(milah_warnings INTERFACE
    QT_DISABLE_DEPRECATED_UP_TO=0x060600
)

# --------------------------------------------------------------------------
# MilahCore: OSIS parsing, alignment, consensus, serialisation and storage.
# No widgets, so the tests can link it directly.
# --------------------------------------------------------------------------

qt_add_library(MilahCore STATIC
    src/core/types.cpp
    src/core/types.h
    src/core/books.cpp
    src/core/books.h
    src/core/tokenize.cpp
    src/core/tokenize.h
    src/core/diff.cpp
    src/core/diff.h
    src/core/reading_marks.cpp
    src/core/reading_marks.h
    src/core/name_forms.cpp
    src/core/name_forms.h
    src/core/bands.cpp
    src/core/bands.h
    src/core/hebrew_forms.cpp
    src/core/hebrew_forms.h
    src/core/align_score.cpp
    src/core/align_score.h
    src/core/data_paths.cpp
    src/core/data_paths.h
    src/core/lexicon.cpp
    src/core/lexicon.h
    src/core/suggestions.cpp
    src/core/suggestions.h
    src/core/word_marker.cpp
    src/core/word_marker.h
    src/core/osis.cpp
    src/core/osis.h
    src/core/page_layout.cpp
    src/core/page_layout.h
    src/core/alignment.cpp
    src/core/alignment.h
    src/core/coverage.cpp
    src/core/coverage.h
    src/core/manuscript_catalogue.cpp
    src/core/manuscript_catalogue.h
    src/core/scan_catalogue.cpp
    src/core/scan_catalogue.h
    src/core/serialize.cpp
    src/core/serialize.h
    src/core/project.cpp
    src/core/project.h
    src/core/line_fill.cpp
    src/core/line_fill.h
    src/core/osis_fill.cpp
    src/core/osis_fill.h
    src/core/training_export.cpp
    src/core/training_export.h
    src/core/transcription.cpp
    src/core/transcription.h
    src/core/recent_files.cpp
    src/core/recent_files.h
    src/core/docx.cpp
    src/core/docx.h
    src/core/transcription_docx.cpp
    src/core/transcription_docx.h
    src/core/collation_docx.cpp
    src/core/collation_docx.h
    src/project_storage.cpp
    src/project_storage.h
)

target_include_directories(MilahCore PUBLIC src)

target_link_libraries(MilahCore
    PUBLIC
        Qt6::Core
        QuaZip::QuaZip
    PRIVATE
        milah_warnings
)

# --------------------------------------------------------------------------
# Data files
#
# Read from disk rather than compiled in, so a regenerated lexicon or an edited
# rule table can replace the shipped one without rebuilding Milah. They are
# staged beside the binaries here so the application and the tests both find
# them the same way they would in an installed copy, and installed below for
# each platform's layout. See data/README.md for sources, licences and the
# search order.
# --------------------------------------------------------------------------

set(MILAH_DATA_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/data/hebrew_abbreviations.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/hebrew_lexicon.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/hebrew_names.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/hebrew_phrase_rules.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/hebrew_roots.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/rabbinic.words.txt"
)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/data/.staged"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/data"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        ${MILAH_DATA_FILES} "${CMAKE_CURRENT_BINARY_DIR}/data"
    COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_CURRENT_BINARY_DIR}/data/.staged"
    DEPENDS ${MILAH_DATA_FILES}
    COMMENT "Staging the Hebrew data files beside the executables"
    VERBATIM
)
add_custom_target(MilahData ALL
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/data/.staged"
)

# --------------------------------------------------------------------------
# Milah: the Qt Widgets application.
# --------------------------------------------------------------------------

qt_add_executable(Milah
    src/main.cpp
    src/app_controller.cpp
    src/app_controller.h
    src/main_window.cpp
    src/main_window.h
    src/transcription_controller.cpp
    src/transcription_controller.h
    src/ui/about_dialog.cpp
    src/ui/about_dialog.h
    src/ui/band_grid.cpp
    src/ui/band_grid.h
    src/ui/dictionary_entry_dialog.cpp
    src/ui/dictionary_entry_dialog.h
    src/ui/download_manuscripts_dialog.cpp
    src/ui/download_manuscripts_dialog.h
    src/ui/htr_last_run_dialog.cpp
    src/ui/htr_last_run_dialog.h
    src/ui/htr_strips_dialog.cpp
    src/ui/htr_strips_dialog.h
    src/ui/htr_models_dialog.cpp
    src/ui/htr_models_dialog.h
    src/ui/htr_progress.cpp
    src/ui/htr_progress.h
    src/ui/htr_repository_dialog.cpp
    src/ui/htr_repository_dialog.h
    src/ui/htr_setup_dialog.cpp
    src/ui/htr_setup_dialog.h
    src/ui/htr_training_dialog.cpp
    src/ui/htr_training_dialog.h
    src/ui/iiif_image.cpp
    src/ui/iiif_image.h
    src/ui/osis_fill_dialog.cpp
    src/ui/osis_fill_dialog.h
    src/ui/training_set.cpp
    src/ui/training_set.h
    src/ui/training_set_dialog.cpp
    src/ui/training_set_dialog.h
    src/ui/kraken_environment.cpp
    src/ui/kraken_environment.h
    src/ui/manuscript_image_view.cpp
    src/ui/manuscript_image_view.h
    src/ui/network_fetch.cpp
    src/ui/network_fetch.h
    src/ui/online_scan_dialog.cpp
    src/ui/online_scan_dialog.h
    src/ui/scan_metadata_dialog.cpp
    src/ui/scan_metadata_dialog.h
    src/ui/manuscript_library_dialog.cpp
    src/ui/manuscript_library_dialog.h
    src/ui/transcription_grid_widget.cpp
    src/ui/transcription_grid_widget.h
    src/ui/transcription_meta_widget.cpp
    src/ui/transcription_meta_widget.h
    src/ui/transcription_notes_widget.cpp
    src/ui/transcription_notes_widget.h
    src/ui/transcription_training_widget.cpp
    src/ui/transcription_training_widget.h
    src/ui/transcription_widget.cpp
    src/ui/transcription_widget.h
    src/ui/icons.cpp
    src/ui/icons.h
    src/ui/notes_widget.cpp
    src/ui/notes_widget.h
    src/ui/source_settings_widget.cpp
    src/ui/source_settings_widget.h
    src/ui/verse_grid_widget.cpp
    src/ui/verse_grid_widget.h
)

set_target_properties(Milah PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.milah.Milah"
    MACOSX_BUNDLE_BUNDLE_NAME "Milah"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

target_link_libraries(Milah PRIVATE
    MilahCore
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Widgets
    milah_warnings
)

add_dependencies(Milah MilahData)

# Runtime window / taskbar icon plus the toolbar artwork, embedded so they ship
# inside the binary and are reachable as ":/img/milah.png" (see main.cpp) and
# ":/img/icons/<name>.svg" (see ui/icons.cpp). The SVGs are single-colour and get
# recoloured to the palette on load, so one file serves light and dark.
qt_add_resources(Milah "milah_icons"
    PREFIX "/"
    FILES
        img/milah.png
        img/icons/boxes-line.svg
        img/icons/boxes-word.svg
        img/icons/document-open.svg
        img/icons/document-save.svg
        img/icons/document-save-as.svg
        img/icons/edit-redo.svg
        img/icons/edit-undo.svg
        img/icons/eye.svg
        img/icons/eye-off.svg
        img/icons/go-next.svg
        img/icons/go-previous.svg
        img/icons/help-about.svg
        img/icons/load-manuscript.svg
        img/icons/note-marker.svg
        img/icons/load-translation.svg
        img/icons/text-recognise.svg
        img/icons/view-refresh.svg
        img/icons/zoom-in.svg
)

# Windows executable file icon and version metadata (what Explorer shows under
# Properties). Generated from img/milah.rc.in so the version comes from the
# project() declaration above rather than being written out a second time. The
# RC toolchain only exists on Windows builds.
if(WIN32)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/img/milah.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/milah.rc"
        @ONLY
    )
    target_sources(Milah PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/milah.rc")
endif()

# Stamped at configure time rather than left to __DATE__, which records when a
# translation unit was last compiled and so goes stale on an incremental build.
# build_milah_windows.ps1 reconfigures on every run, so this is refreshed with it.
string(TIMESTAMP MILAH_BUILD_DATE "%Y-%m-%d" UTC)

target_compile_definitions(Milah PRIVATE
    MILAH_VERSION="${PROJECT_VERSION}"
    MILAH_BUILD_DATE="${MILAH_BUILD_DATE}"
)

# --------------------------------------------------------------------------
# Install and deploy
#
# `cmake --install <build> --prefix <dir>` produces a runnable copy: Qt's
# deployment script brings in Qt's libraries and plugins (windeployqt and
# macdeployqt underneath), and the data files go wherever dataSearchPaths()
# looks on that platform:
#   Windows  <prefix>/Milah.exe, <prefix>/data/
#   macOS    <prefix>/Milah.app, Milah.app/Contents/Resources/data/
#   Linux    <prefix>/bin/Milah, <prefix>/share/milah/data/
# --------------------------------------------------------------------------

install(TARGETS Milah
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(APPLE)
    set(MILAH_DATA_INSTALL_DIR "Milah.app/Contents/Resources/data")
elseif(WIN32)
    set(MILAH_DATA_INSTALL_DIR "data")
else()
    set(MILAH_DATA_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/milah/data")
endif()
install(FILES ${MILAH_DATA_FILES} DESTINATION "${MILAH_DATA_INSTALL_DIR}")

qt_generate_deploy_app_script(
    TARGET Milah
    OUTPUT_SCRIPT MILAH_DEPLOY_SCRIPT
    NO_TRANSLATIONS
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT "${MILAH_DEPLOY_SCRIPT}")

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------

if(MILAH_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

    # One test executable. Plain add_executable, not qt_add_executable: the
    # latter finalises the target as a GUI application on Windows, and a
    # GUI-subsystem binary discards the stdout QTest reports through, leaving a
    # failing test with nothing to show for it.
    #
    #   milah_add_test(<name> SOURCES <extra sources...> LIBRARIES <libs...>)
    function(milah_add_test name)
        cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LIBRARIES")
        set(target Milah_${name}_test)
        add_executable(${target} tests/${name}_test.cpp ${arg_SOURCES})
        target_include_directories(${target} PRIVATE src tests)
        target_link_libraries(${target} PRIVATE
            Qt6::Core Qt6::Test milah_warnings ${arg_LIBRARIES})
        target_compile_definitions(${target} PRIVATE
            MILAH_REPO_ROOT="${MILAH_REPO_ROOT}")
        # The data files are read from beside the executable, so the tests
        # exercise exactly the path an installed copy uses.
        add_dependencies(${target} MilahData)
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES ENVIRONMENT
            # No display on a CI runner; nothing here draws to a screen.
            "QT_QPA_PLATFORM=offscreen;QT_ASSUME_STDERR_HAS_CONSOLE=1")
    endfunction()

    foreach(name
            project_storage osis consensus apparatus diff acronym lexicon suggestions
            coverage alignment catalogue word_marker transcription books
            scan_catalogue recent_files docx transcription_docx page_layout
            reading_marks collation_docx name_forms training_export line_fill
            osis_fill data_paths)
        milah_add_test(${name}
            SOURCES tests/test_data.cpp tests/test_data.h
            LIBRARIES MilahCore)
    endforeach()

    # The training set lives in the application target rather than in
    # MilahCore, because where it puts its files is an application question.
    # It is still worth testing, so this target takes the source in directly
    # and links the Gui module it needs to read a picture's size.
    milah_add_test(training_set
        SOURCES src/ui/training_set.cpp src/ui/training_set.h
        LIBRARIES MilahCore Qt6::Gui)

    # Pure string and arithmetic work over what a IIIF service says about
    # itself, so it takes no network and no widgets.
    milah_add_test(iiif_image
        SOURCES src/ui/iiif_image.cpp src/ui/iiif_image.h
        LIBRARIES Qt6::Gui)
endif()

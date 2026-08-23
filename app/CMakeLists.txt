cmake_minimum_required(VERSION 3.24)

# The one place the application version is written. It reaches the binary as
# MILAH_VERSION (below), the About dialog, `Milah.exe --version`, and the
# Windows VERSIONINFO resource. It is unrelated to the `.milah` project file
# format version, which is pinned to 1 in src/core/project.cpp and gates
# whether a saved project can be opened at all.
project(Milah VERSION 1.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

option(MILAH_BUILD_TESTS "Build Milah's test suite" OFF)
option(MILAH_DEPLOY_QT "Run windeployqt after a Windows build" OFF)

# Used by the apparatus test to find the repository's OSIS corpus. The test
# skips itself when this points nowhere useful, so the suite stays runnable
# from a standalone copy of app/.
set(MILAH_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH
    "Repository root containing the data/ corpus")

# Network is used by exactly one thing: fetching published manuscripts into the
# local library. MilahCore never touches it.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Network Widgets)
find_package(QuaZip-Qt6 CONFIG REQUIRED)

qt_standard_project_setup()

# --------------------------------------------------------------------------
# MilahCore: OSIS parsing, alignment, consensus, serialisation and storage.
# No widgets, so the tests can link it directly.
# --------------------------------------------------------------------------

add_library(MilahCore STATIC
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

# --------------------------------------------------------------------------
# Data files
#
# Read from disk rather than compiled in, so a regenerated lexicon or an edited
# rule table can replace the shipped one without rebuilding Milah. They are
# staged beside the binaries here so the application and the tests both find
# them the same way they would in an installed copy; packaging has to carry the
# directory too. See data/README.md for sources, licences and the search order.
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

target_link_libraries(MilahCore PUBLIC
    Qt6::Core
    QuaZip::QuaZip
)

# --------------------------------------------------------------------------
# Milah: the Qt Widgets application.
# --------------------------------------------------------------------------

qt_add_executable(Milah WIN32
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

target_link_libraries(Milah PRIVATE
    MilahCore
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Widgets
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

if(WIN32 AND MILAH_DEPLOY_QT)
    add_custom_command(TARGET Milah POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -DMILAH_EXECUTABLE=$<TARGET_FILE:Milah>
            -P "${CMAKE_CURRENT_SOURCE_DIR}/packaging/deploy_windows.cmake"
        VERBATIM
    )
endif()

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------

if(MILAH_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

    foreach(test_name
            project_storage osis consensus apparatus diff acronym lexicon suggestions
            coverage alignment catalogue word_marker transcription books
            scan_catalogue recent_files docx transcription_docx page_layout
            reading_marks collation_docx name_forms training_export line_fill
            osis_fill)
        # Plain add_executable, not qt_add_executable: the latter finalises the
        # target as a Windows GUI application, and a GUI-subsystem binary
        # discards the stdout QTest reports through, leaving a failing test
        # with nothing to show for it.
        add_executable(Milah_${test_name}_test
            tests/${test_name}_test.cpp
            tests/test_data.cpp
            tests/test_data.h
        )
        target_include_directories(Milah_${test_name}_test PRIVATE tests)
        # The data files are read from beside the executable, so the tests
        # exercise exactly the path an installed copy uses.
        add_dependencies(Milah_${test_name}_test MilahData)
        target_link_libraries(Milah_${test_name}_test PRIVATE
            MilahCore
            Qt6::Core
            Qt6::Test
        )
        target_compile_definitions(Milah_${test_name}_test PRIVATE
            MILAH_REPO_ROOT="${MILAH_REPO_ROOT}"
        )
        add_test(NAME Milah_${test_name}_test COMMAND Milah_${test_name}_test)
    endforeach()

    # The training set lives in the application target rather than in MilahCore,
    # because where it puts its files is an application question. It is still
    # worth testing, so this one target takes the source in directly and links
    # the Gui module it needs to read a picture's size.
    add_executable(Milah_training_set_test
        tests/training_set_test.cpp
        src/ui/training_set.cpp
        src/ui/training_set.h
    )
    target_include_directories(Milah_training_set_test PRIVATE src tests)
    target_link_libraries(Milah_training_set_test PRIVATE
        MilahCore
        Qt6::Core
        Qt6::Gui
        Qt6::Test
    )
    add_test(NAME Milah_training_set_test COMMAND Milah_training_set_test)

    # Pure string and arithmetic work over what a IIIF service says about
    # itself, so it takes no network and no widgets.
    add_executable(Milah_iiif_image_test
        tests/iiif_image_test.cpp
        src/ui/iiif_image.cpp
        src/ui/iiif_image.h
    )
    target_include_directories(Milah_iiif_image_test PRIVATE src tests)
    target_link_libraries(Milah_iiif_image_test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME Milah_iiif_image_test COMMAND Milah_iiif_image_test)

    # Which line a point on the folio falls on used to be tested here, against a
    # build of the whole image widget. lineAtPoint() has moved into MilahCore —
    # the fill dialog needs it too, to turn a click on an unread folio into a
    # line once it has read one — so its cases moved into line_fill_test above,
    # where they cost no window and no Widgets link.
endif()

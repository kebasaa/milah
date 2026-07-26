if(NOT DEFINED MILAH_EXECUTABLE)
    message(FATAL_ERROR "MILAH_EXECUTABLE was not provided")
endif()

find_program(WINDEPLOYQT_EXECUTABLE windeployqt REQUIRED)
execute_process(
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
        --no-translations
        --compiler-runtime
        --release
        "${MILAH_EXECUTABLE}"
    COMMAND_ERROR_IS_FATAL ANY
)

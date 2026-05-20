cmake_minimum_required(VERSION 3.19)
project(dump_parsing LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets PrintSupport)

if(WIN32)
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt.exe windeployqt
        HINTS "${QT_BIN_DIR}"
        REQUIRED
    )
endif()

qt_standard_project_setup()

qt_add_executable(dump_parsing
    WIN32 MACOSX_BUNDLE
    app_icon.rc
    app_resources.qrc
    resource.h
    analyzer/analyzer_backend.h
    analyzer/crash_analyzer.cpp
    analyzer/crash_analyzer.h
    analyzer/crash_analyzer_router.cpp
    analyzer/dbg_analyzer.cpp
    analyzer/dbg_analyzer.h
    analyzer/i_analyzer.h
    analyzer/pdb_analyzer.cpp
    analyzer/pdb_analyzer.h
    common/analyzer_common.cpp
    common/analyzer_common.h
    common/report_exporter.cpp
    common/report_exporter.h
    common/report_renderer.cpp
    common/report_renderer.h
    ui/analysis_controller.cpp
    ui/analysis_controller.h
    ui/busy_spinner.cpp
    ui/busy_spinner.h
    ui/main_window.cpp
    ui/main_window.h
    ui/mainwindow.ui
    main.cpp
)

# 输出到源码目录同级的 bin（用于直接拿到可运行程序和依赖）
set(APP_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bin")
file(MAKE_DIRECTORY "${APP_OUTPUT_DIR}")
set_target_properties(dump_parsing PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${APP_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${APP_OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${APP_OUTPUT_DIR}"
)
if(CMAKE_CONFIGURATION_TYPES)
    foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER "${cfg}" cfg_upper)
        set_target_properties(dump_parsing PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${cfg_upper}" "${APP_OUTPUT_DIR}"
            "LIBRARY_OUTPUT_DIRECTORY_${cfg_upper}" "${APP_OUTPUT_DIR}"
            "ARCHIVE_OUTPUT_DIRECTORY_${cfg_upper}" "${APP_OUTPUT_DIR}"
        )
    endforeach()
endif()

target_compile_features(dump_parsing
    PRIVATE
        cxx_std_17
)

target_include_directories(dump_parsing
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/analyzer
        ${CMAKE_CURRENT_SOURCE_DIR}/common
        ${CMAKE_CURRENT_SOURCE_DIR}/ui
)

target_link_libraries(dump_parsing
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::PrintSupport
)

if(WIN32)
    target_compile_definitions(dump_parsing
        PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
    )

    target_link_libraries(dump_parsing
        PRIVATE
            dbghelp
            shell32
    )

    add_custom_command(TARGET dump_parsing POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --compiler-runtime
                --no-translations
                "$<TARGET_FILE:dump_parsing>"
        COMMENT "Deploying Qt runtime dependencies beside dump_parsing executable"
        VERBATIM
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools")
    set(CRASH_ANALYSIS_TOOL_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_windbg_dump_report.cmd"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_windbg_dump_report.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/mingw_crash_tools.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/resolve_mingw_crash_address.cmd"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/resolve_mingw_crash_address.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/resolve_mingw_windbg_stack.cmd"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/resolve_mingw_windbg_stack.ps1"
    )

    add_custom_command(TARGET dump_parsing POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:dump_parsing>/tools"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CRASH_ANALYSIS_TOOL_FILES}
                "$<TARGET_FILE_DIR:dump_parsing>/tools"
        COMMENT "Copying crash analysis scripts beside dump_parsing executable"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endif()

install(TARGETS dump_parsing
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET dump_parsing
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

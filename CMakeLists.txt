cmake_minimum_required(VERSION 3.19)
project(XYOscilloscope  LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()

if(WIN32)
    set(EXTRA_LIBS ${EXTRA_LIBS} ole32)
endif()

qt_add_executable(XYOscilloscope
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
)

target_link_libraries(XYOscilloscope
    PRIVATE
        Qt::Core
        Qt::Widgets
        ole32
        windowsapp
)

include(GNUInstallDirs)

install(TARGETS XYOscilloscope
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET XYOscilloscope
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

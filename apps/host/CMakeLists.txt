add_executable(ofx_host
  src/main.cpp
  src/MainWindow.cpp
  src/MainWindow.h
  src/OfxSdkProbe.cpp
  src/OfxSdkProbe.h
)

target_link_libraries(ofx_host
  PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    QtNodes::QtNodes
    OpenFX::Headers
)

target_compile_definitions(ofx_host
  PRIVATE
    QT_NO_KEYWORDS
)

if(MINGW)
  target_link_options(ofx_host PRIVATE -mwindows)
endif()

if(WIN32)
  get_target_property(_qt6_qmake Qt6::qmake IMPORTED_LOCATION)
  cmake_path(GET _qt6_qmake PARENT_PATH _qt6_bin_dir)
  find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt6_bin_dir}" REQUIRED)
  add_custom_command(TARGET ofx_host POST_BUILD
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --no-translations
            "$<TARGET_FILE:ofx_host>"
    COMMENT "Deploy Qt runtime next to ofx_host"
  )
endif()

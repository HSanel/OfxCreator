add_executable(ofx_host
  src/main.cpp
  src/HostGraphicsView.cpp
  src/HostGraphicsView.h
  src/MainWindow.cpp
  src/MainWindow.h
  src/OfxSdkProbe.cpp
  src/OfxSdkProbe.h
  src/PlaybackState.h
  src/PipelineCodec.cpp
  src/PipelineCodec.h
  src/ViewerPanel.cpp
  src/ViewerPanel.h
  src/nodes/ImageData.h
  src/nodes/InputNodeModel.cpp
  src/nodes/InputNodeModel.h
  src/nodes/KernelNodeModel.cpp
  src/nodes/KernelNodeModel.h
  src/nodes/OutputNodeModel.cpp
  src/nodes/OutputNodeModel.h
  ${CMAKE_SOURCE_DIR}/src/ir/Pipeline.cpp
  ${CMAKE_SOURCE_DIR}/src/io/ImageSequence.cpp
  ${CMAKE_SOURCE_DIR}/src/io/VideoIngest.cpp
  ${CMAKE_SOURCE_DIR}/src/runtime/CpuRuntime.cpp
  ${CMAKE_SOURCE_DIR}/src/runtime/CudaRuntime.cpp
  ${CMAKE_SOURCE_DIR}/src/runtime/OpenClRuntime.cpp
  ${CMAKE_SOURCE_DIR}/src/runtime/PortInference.cpp
  ${CMAKE_SOURCE_DIR}/src/runtime/PythonRuntime.cpp
)

target_include_directories(ofx_host
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src"
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
  set(_ffmpeg_src "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/bin/ffmpeg.exe")
  if(EXISTS "${_ffmpeg_src}")
    add_custom_command(TARGET ofx_host POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "${_ffmpeg_src}"
              "$<TARGET_FILE_DIR:ofx_host>/ffmpeg.exe"
      COMMENT "Copy bundled ffmpeg next to ofx_host"
    )
  endif()
endif()

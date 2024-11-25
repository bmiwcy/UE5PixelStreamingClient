cmake_minimum_required(VERSION 3.14.0)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

project(CppUE5PixelStreamingClient)

set(LIBRARY_OUTPUT_PATH bin/${CMAKE_BUILD_TYPE})

include_directories(${OpenCV_INCLUDE_DIRS})
include_directories(BEFORE SYSTEM ${webrtc_native_INCLUDE})
include_directories(../../opentera-webrtc-native-client/3rdParty/json/include)
include_directories(../../opentera-webrtc-native-client/3rdParty/IXWebSocket)
include_directories(../../opentera-webrtc-native-client/3rdParty/cpp-httplib)
include_directories(../../opentera-webrtc-native-client/OpenteraWebrtcNativeClient/include)

find_package(X11 REQUIRED)


add_executable(CppUE5PixelStreamingClient 
    main.cpp
    mainwindow.cpp
    mainwindow.h
    monitors.cpp
    monitors.h
    videowidget.cpp    
    videowidget.h      
    )

target_link_libraries(CppUE5PixelStreamingClient
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    OpenteraWebrtcNativeClient
    opencv_videoio
    opencv_highgui
    oscpack
    ${X11_LIBRARIES}
)

if (NOT WIN32)
    target_link_libraries(CppUE5PixelStreamingClient
        pthread
    )
endif()

if (NOT OPENTERA_WEBRTC_USE_SYSTEM_OPENCV)
    add_dependencies(CppUE5PixelStreamingClient opencv_highgui opencv_videoio)
endif()

set_property(TARGET CppUE5PixelStreamingClient PROPERTY CXX_STANDARD 17)

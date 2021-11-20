# The MIT License (MIT)

# Copyright (c) 2015-2021 OpenImageDebugger contributors
# (https://github.com/OpenImageDebugger/OpenImageDebugger)

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to
# deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

cmake_minimum_required(VERSION 3.10.0)

project(oidwindow)

find_package(OpenGL REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 COMPONENTS Core Gui OpenGL Widgets REQUIRED)

file(GLOB_RECURSE SOURCES
    oid_window.cpp
    io/*.cpp
    ipc/*.cpp
    math/*.cpp
    ui/*.cpp
    visualization/*cpp
    logger/*.cpp
)

set(QT_FORMS ui/main_window/main_window.ui)
set(QT_RESOURCES resources/resources.qrc)

add_executable(${PROJECT_NAME} ${SOURCES} ${QT_FORMS} ${QT_RESOURCES})

target_include_directories(${PROJECT_NAME}
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Include external dependencies as "system dependencies" so compilation errors from them are ignored
target_include_directories(${PROJECT_NAME} SYSTEM
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/Eigen
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/Khronos)

target_compile_options(${PROJECT_NAME} PRIVATE "$<$<PLATFORM_ID:UNIX>:-Wl,--exclude-libs,ALL>")

target_link_libraries(${PROJECT_NAME} PRIVATE
                      Qt5::Core
                      Qt5::Gui
                      Qt5::OpenGL
                      Qt5::Network
                      Qt5::Widgets
                      Threads::Threads
                      ${OPENGL_gl_LIBRARY}
                      spdlog::spdlog)

install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION OpenImageDebugger)


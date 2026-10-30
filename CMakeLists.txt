cmake_minimum_required(VERSION 3.20)
project(ImpulsiveShading LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
if(UNIX AND NOT APPLE)
  set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
FetchContent_Declare(glad GIT_REPOSITORY https://github.com/Dav1dde/glad.git GIT_TAG v0.1.36 GIT_SHALLOW TRUE)
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_Declare(stb GIT_REPOSITORY https://github.com/nothings/stb.git GIT_TAG master GIT_SHALLOW TRUE)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.9b GIT_SHALLOW TRUE)
FetchContent_Declare(miniaudio GIT_REPOSITORY https://github.com/mackron/miniaudio.git GIT_TAG 0.11.25 GIT_SHALLOW TRUE SOURCE_SUBDIR no_cmake_subdirectory)
FetchContent_MakeAvailable(glfw glad json)
FetchContent_MakeAvailable(stb)
FetchContent_MakeAvailable(imgui)
FetchContent_MakeAvailable(miniaudio)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(EMBEDDED_SHADERS_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_shaders.h)
add_custom_command(OUTPUT ${EMBEDDED_SHADERS_HEADER}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cpp/embed_json.py
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/data/shaders.json ${EMBEDDED_SHADERS_HEADER}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cpp/embed_json.py ${CMAKE_CURRENT_SOURCE_DIR}/cpp/data/shaders.json
  COMMENT "Embedding ShaderToy shaders")
file(GLOB PREVIEW_IMAGES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cpp/data/previews/*.png)
set(EMBEDDED_PREVIEWS_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_previews.h)
add_custom_command(OUTPUT ${EMBEDDED_PREVIEWS_HEADER}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cpp/embed_previews.py
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/data/previews ${EMBEDDED_PREVIEWS_HEADER}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cpp/embed_previews.py ${PREVIEW_IMAGES}
  COMMENT "Embedding shader previews")

add_executable(ImpulsiveShading cpp/main.cpp cpp/audio.cpp ${EMBEDDED_SHADERS_HEADER} ${EMBEDDED_PREVIEWS_HEADER}
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
if(WIN32)
  enable_language(RC)
  configure_file(cpp/media.rc.in ${CMAKE_CURRENT_BINARY_DIR}/generated/media.rc @ONLY)
  target_sources(ImpulsiveShading PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated/media.rc)
endif()
set_target_properties(ImpulsiveShading PROPERTIES OUTPUT_NAME "Impulsive Shading")
target_include_directories(ImpulsiveShading PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated ${stb_SOURCE_DIR} ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${miniaudio_SOURCE_DIR})
target_link_libraries(ImpulsiveShading PRIVATE glfw glad nlohmann_json::nlohmann_json)
if(UNIX AND NOT APPLE)
  target_link_options(ImpulsiveShading PRIVATE -static-libstdc++ -static-libgcc)
endif()
if(MSVC)
  target_compile_definitions(ImpulsiveShading PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
  target_compile_options(ImpulsiveShading PRIVATE /W4 /permissive-)
else()
  target_compile_options(ImpulsiveShading PRIVATE -Wall -Wextra -Wpedantic)
endif()

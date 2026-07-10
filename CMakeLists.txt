cmake_minimum_required(VERSION 3.22)
project(VegetationTool VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# vcpkg toolchain（用户需在cmake命令行指定或设置环境变量VCPKG_ROOT）
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

# ---- 第三方库（vcpkg） ----
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# ---- imgui-node-editor（手动集成） ----
set(NE_DIR ${CMAKE_SOURCE_DIR}/external/imgui-node-editor)
add_library(imgui_node_editor STATIC
    ${NE_DIR}/imgui_node_editor.cpp
    ${NE_DIR}/imgui_node_editor_api.cpp
    ${NE_DIR}/imgui_canvas.cpp
    ${NE_DIR}/crude_json.cpp
)
target_include_directories(imgui_node_editor PUBLIC ${NE_DIR})
target_link_libraries(imgui_node_editor PUBLIC imgui::imgui)
target_compile_definitions(imgui_node_editor PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

# ---- 源文件 ----
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Windows: 附带资源文件(应用图标 + 版本信息)
if(WIN32)
    list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/assets/app.rc)
endif()

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE src/ ${Stb_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        glfw
        glad::glad
        glm::glm
        imgui::imgui
        imgui_node_editor
        httplib::httplib
        nlohmann_json::nlohmann_json
)
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE opengl32 comdlg32)
endif()

# 编译选项
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W3 /utf-8)
    target_compile_options(imgui_node_editor PRIVATE /utf-8)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()

# 复制 shaders 到构建目录
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/src/shaders
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders
    COMMENT "Copying shaders..."
)

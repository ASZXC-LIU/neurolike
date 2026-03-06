cmake_minimum_required(VERSION 3.20)

# ------------------------------------------------------------------------------
# 1. vcpkg 集成 (必须在 project() 之前设置)
# ------------------------------------------------------------------------------
if(DEFINED ENV{VCPKG_ROOT})
  set(VCPKG_ROOT "$ENV{VCPKG_ROOT}" CACHE PATH "")
else()
  # 如果没有环境变量，尝试使用默认路径或报错
  message(WARNING "VCPKG_ROOT environment variable not set. Please set it to your vcpkg installation path.")
endif()

set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")

project(NeuralLink_Native)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# 1.5 全局定义 (Unicode & Windows)
# ------------------------------------------------------------------------------
add_compile_definitions(UNICODE _UNICODE)
add_compile_definitions(CSM_TARGET_WIN_GL) # Enable Live2D OpenGL for Windows

if(MSVC)
    add_compile_options(/utf-8)
endif()

# ------------------------------------------------------------------------------
# 2. Live2D Cubism SDK 集成 (外部 SDK)
# ------------------------------------------------------------------------------
if(DEFINED ENV{CUBISM_SDK_ROOT})
    set(CUBISM_SDK_DIR "$ENV{CUBISM_SDK_ROOT}")
    message(STATUS "Found Cubism SDK at: ${CUBISM_SDK_DIR}")
    
    include_directories(${CUBISM_SDK_DIR}/Core/include)
    # Framework headers are in src
    include_directories(${CUBISM_SDK_DIR}/Framework/src)
    
    # STB Image (in Samples/Metal/thirdParty/stb)
    include_directories(${CUBISM_SDK_DIR}/Samples/Metal/thirdParty/stb)

    # Core Library (Windows x64) - Using v142 MD as a safe default for VS2019/2022
    set(CUBISM_CORE_LIB "${CUBISM_SDK_DIR}/Core/lib/windows/x86_64/142/Live2DCubismCore_MD.lib")
    
    # Framework Sources
    file(GLOB_RECURSE FRAMEWORK_SOURCES "${CUBISM_SDK_DIR}/Framework/src/*.cpp")
    
    # Filter out non-OpenGL renderers to avoid missing header errors
    list(FILTER FRAMEWORK_SOURCES EXCLUDE REGEX ".*Rendering/D3D.*")
    list(FILTER FRAMEWORK_SOURCES EXCLUDE REGEX ".*Rendering/Metal.*")
    list(FILTER FRAMEWORK_SOURCES EXCLUDE REGEX ".*Rendering/Vulkan.*")
    # Exclude Cocos2d specific if any (not present in this SDK version usually)

    message(STATUS "Added Live2D Framework sources: ${FRAMEWORK_SOURCES}")
else()
    message(WARNING "CUBISM_SDK_ROOT environment variable not set. Live2D features will not be available.")
endif()

# ------------------------------------------------------------------------------
# 3. 查找依赖包
# ------------------------------------------------------------------------------
find_package(Boost CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# 包含目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
# 显式包含 GLEW 的头文件目录 (vcpkg 通常会自动处理，但有时需要显式指定)
include_directories(${GLEW_INCLUDE_DIRS})

# 源文件
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.h")

# 合并 Framework 源码
if(DEFINED FRAMEWORK_SOURCES)
    list(APPEND SOURCES ${FRAMEWORK_SOURCES})
endif()

# 可执行文件
add_executable(NeuralLink_Native ${SOURCES})

# 链接库
target_link_libraries(NeuralLink_Native PRIVATE
    nlohmann_json::nlohmann_json
    Boost::headers
    Boost::boost
    OpenSSL::SSL OpenSSL::Crypto
    GLEW::GLEW
    glfw
    dwmapi
    opengl32
)

if(DEFINED CUBISM_CORE_LIB)
    target_link_libraries(NeuralLink_Native PRIVATE ${CUBISM_CORE_LIB})
    
    # Copy DLL to output directory
    add_custom_command(TARGET NeuralLink_Native POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CUBISM_SDK_DIR}/Core/dll/windows/x86_64/Live2DCubismCore.dll"
        $<TARGET_FILE_DIR:NeuralLink_Native>)
endif()

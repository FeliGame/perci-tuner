# 设置 CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 设置项目名称
project(dd_real)

# 设置 C 标准
set(CMAKE_C_STANDARD 11)

# 设置编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -DGL")

# 查找当前目录下的所有 C 文件
file(GLOB_RECURSE DD_REAL_SOURCES "*.c")

# 查找当前目录下的所有头文件
file(GLOB_RECURSE DD_REAL_HEADERS "*.h")

# 添加库，同时支持静态库和共享库
add_library(dd_real SHARED ${DD_REAL_SOURCES})
add_library(dd_real_static STATIC ${DD_REAL_SOURCES})

# 设置库的输出名称
set_target_properties(dd_real_static PROPERTIES OUTPUT_NAME dd_real)

# 设置库的输出目录
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR})

# 将当前目录添加为头文件包含路径
target_include_directories(dd_real PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(dd_real_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 安装库文件和头文件
install(TARGETS dd_real dd_real_static
        ARCHIVE DESTINATION .
        LIBRARY DESTINATION .)

install(FILES ${DD_REAL_HEADERS} DESTINATION include/dd_real)
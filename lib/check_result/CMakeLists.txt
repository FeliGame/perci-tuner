# 设置 CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 设置项目名称
project(check_result)

# 设置 C 标准
set(CMAKE_C_STANDARD 11)

# 设置编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -DGL -DDD")

# 查找当前目录下的 C 文件
file(GLOB CHECK_RESULT_SOURCES "*.c")

# 查找当前目录下的头文件
file(GLOB CHECK_RESULT_HEADERS "*.h")

# 添加库，同时支持静态库和共享库
add_library(check_result SHARED ${CHECK_RESULT_SOURCES})
add_library(check_result_static STATIC ${CHECK_RESULT_SOURCES})

# 设置库的输出名称
set_target_properties(check_result_static PROPERTIES OUTPUT_NAME check_result)

# 将当前目录添加为头文件包含路径
target_include_directories(check_result PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(check_result_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 设置库的输出目录
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR})

# 安装库文件和头文件
install(TARGETS check_result check_result_static
        ARCHIVE DESTINATION .
        LIBRARY DESTINATION .)

install(FILES ${CHECK_RESULT_HEADERS} DESTINATION include/check_result)

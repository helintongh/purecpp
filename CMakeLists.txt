cmake_minimum_required(VERSION 3.15.0)
project(purecpp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)

include_directories(../cinatra/include)
include_directories(../ormpp)
include_directories(../iguana)

set(ENABLE_MYSQL ON)
add_definitions(-DORMPP_ENABLE_MYSQL)
add_library(ormpp INTERFACE)
include(../ormpp/cmake/mysql.cmake)

find_package(OpenSSL REQUIRED)

target_link_libraries(ormpp INTERFACE ${MYSQL_LIBRARY})
target_include_directories(ormpp INTERFACE ormpp ormpp/ormpp ${MYSQL_INCLUDE_DIR})

add_executable(purecpp feather.cpp)
target_compile_options(purecpp PRIVATE -DCINATRA_ENABLE_SSL)
target_link_libraries(purecpp ormpp OpenSSL::SSL OpenSSL::Crypto)

# 复制 HTML 资源的函数
function(copy_html_resources target_name)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Linux: 复制到 build 目录
        message(STATUS "Linux system detected: HTML resources will be copied to ${CMAKE_BINARY_DIR}/html")
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/html
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/html
            ${CMAKE_BINARY_DIR}/html
            COMMAND ${CMAKE_COMMAND} -E echo "HTML resources copied to: ${CMAKE_BINARY_DIR}/html"
            COMMENT "Copying HTML resources to build directory"
        )
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        # Windows: 根据配置复制到相应的输出目录
        message(STATUS "Windows system detected: HTML resources will be copied to output directory based on configuration")
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${target_name}>/html
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/html
            $<TARGET_FILE_DIR:${target_name}>/html
            COMMAND ${CMAKE_COMMAND} -E echo "HTML resources copied to: $<TARGET_FILE_DIR:${target_name}>/html"
            COMMENT "Copying HTML resources to output directory"
        )
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        # macOS: 复制到 build 目录
        message(STATUS "macOS system detected: HTML resources will be copied to ${CMAKE_BINARY_DIR}/html")
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/html
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/html
            ${CMAKE_BINARY_DIR}/html
            COMMAND ${CMAKE_COMMAND} -E echo "HTML resources copied to: ${CMAKE_BINARY_DIR}/html"
            COMMENT "Copying HTML resources to build directory"
        )
    else()
        message(WARNING "Unsupported system: ${CMAKE_SYSTEM_NAME}, HTML resources will not be copied")
    endif()
endfunction()

# 为 purecpp 目标调用复制函数
copy_html_resources(purecpp)

# 在配置阶段显示详细信息
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Binary directory: ${CMAKE_BINARY_DIR}")
message(STATUS "Source directory: ${CMAKE_SOURCE_DIR}")
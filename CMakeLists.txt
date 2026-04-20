cmake_minimum_required(VERSION 3.15.0)
project(purecpp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PURECPP_CINATRA_DIR ../cinatra/include)
set(PURECPP_ORMPP_DIR ../ormpp)
set(PURECPP_IGUANA_DIR ../iguana)
set(PURECPP_SQLITE_DIR ../ormpp/thirdparty/sqlite3)
set(PURECPP_DB_BACKEND "mysql" CACHE STRING "Database backend for purecpp")
set_property(CACHE PURECPP_DB_BACKEND PROPERTY STRINGS sqlite mysql postgresql)
string(TOLOWER "${PURECPP_DB_BACKEND}" PURECPP_DB_BACKEND_NORMALIZED)

find_package(OpenSSL REQUIRED)

set(PURECPP_TARGET_INCLUDE_DIRS
    ${PURECPP_CINATRA_DIR}
    ${PURECPP_ORMPP_DIR}
    ${PURECPP_IGUANA_DIR}
)
set(PURECPP_TARGET_LIBRARIES
    OpenSSL::SSL
    OpenSSL::Crypto
)
set(PURECPP_TARGET_DEFINITIONS
    CINATRA_ENABLE_SSL
)

if(WIN32)
    list(APPEND PURECPP_TARGET_DEFINITIONS WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if(PURECPP_DB_BACKEND_NORMALIZED STREQUAL "sqlite")
    add_library(ormpp_sqlite3 STATIC ${PURECPP_SQLITE_DIR}/sqlite3.c)
    target_include_directories(ormpp_sqlite3 PUBLIC ${PURECPP_SQLITE_DIR})
    list(APPEND PURECPP_TARGET_INCLUDE_DIRS ${PURECPP_SQLITE_DIR})
    list(APPEND PURECPP_TARGET_LIBRARIES ormpp_sqlite3)
    list(APPEND PURECPP_TARGET_DEFINITIONS PURECPP_DB_SQLITE)
elseif(PURECPP_DB_BACKEND_NORMALIZED STREQUAL "mysql")
    set(MYSQL_FIND_REQUIRED TRUE)
    include(${PURECPP_ORMPP_DIR}/cmake/mysql.cmake)
    list(APPEND PURECPP_TARGET_INCLUDE_DIRS ${MYSQL_INCLUDE_DIR})
    list(APPEND PURECPP_TARGET_LIBRARIES ${MYSQL_LIBRARIES})
    list(APPEND PURECPP_TARGET_DEFINITIONS PURECPP_DB_MYSQL ORMPP_ENABLE_MYSQL)
elseif(PURECPP_DB_BACKEND_NORMALIZED STREQUAL "postgresql")
    set(PGSQL_FIND_REQUIRED TRUE)
    include(${PURECPP_ORMPP_DIR}/cmake/pgsql.cmake)
    list(APPEND PURECPP_TARGET_INCLUDE_DIRS ${PGSQL_INCLUDE_DIR})
    list(APPEND PURECPP_TARGET_LIBRARIES ${PGSQL_LIBRARIES})
    list(APPEND PURECPP_TARGET_DEFINITIONS PURECPP_DB_POSTGRESQL ORMPP_ENABLE_PG)
else()
    message(FATAL_ERROR "Unsupported PURECPP_DB_BACKEND: ${PURECPP_DB_BACKEND}")
endif()

add_executable(purecpp feather.cpp)
target_compile_definitions(purecpp PRIVATE ${PURECPP_TARGET_DEFINITIONS})
if(MSVC)
    target_compile_options(purecpp PRIVATE /bigobj)
endif()
target_include_directories(purecpp PRIVATE ${PURECPP_TARGET_INCLUDE_DIRS})
target_link_libraries(purecpp PRIVATE ${PURECPP_TARGET_LIBRARIES})

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
message(STATUS "Database backend: ${PURECPP_DB_BACKEND_NORMALIZED}")

cmake_minimum_required(VERSION 3.16)

project(SimpleModbusTcpClient LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(SIMPLEMODBUSTCPCLIENT_IS_TOP_LEVEL ON)
else()
    set(SIMPLEMODBUSTCPCLIENT_IS_TOP_LEVEL OFF)
endif()

option(SIMPLEMODBUSTCPCLIENT_BUILD_TESTS "Build the SimpleModbusTcpClient unit tests" ${SIMPLEMODBUSTCPCLIENT_IS_TOP_LEVEL})

set(SIMPLEMODBUSTCPCLIENT_QT_VERSION "" CACHE STRING "Qt major version to use (5 or 6), empty for automatic")

# Automatic: the Qt version already used by the parent project, otherwise
# Qt 6 when available, otherwise Qt 5.
if(NOT SIMPLEMODBUSTCPCLIENT_QT_VERSION)
    if(TARGET Qt6::Network)
        set(SIMPLEMODBUSTCPCLIENT_QT_VERSION 6)
    elseif(TARGET Qt5::Network)
        set(SIMPLEMODBUSTCPCLIENT_QT_VERSION 5)
    else()
        find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Network)
        set(SIMPLEMODBUSTCPCLIENT_QT_VERSION ${QT_VERSION_MAJOR})
    endif()
endif()
find_package(Qt${SIMPLEMODBUSTCPCLIENT_QT_VERSION} REQUIRED COMPONENTS Network)

add_library(SimpleModbusTcpClient STATIC
    simplemodbustcpclient.cpp
    simplemodbustcpclient.h
)
set_target_properties(SimpleModbusTcpClient PROPERTIES AUTOMOC ON)
target_compile_features(SimpleModbusTcpClient PUBLIC cxx_std_11)
target_include_directories(SimpleModbusTcpClient PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SimpleModbusTcpClient PUBLIC Qt${SIMPLEMODBUSTCPCLIENT_QT_VERSION}::Network)

if(SIMPLEMODBUSTCPCLIENT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

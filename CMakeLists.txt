cmake_minimum_required(VERSION 3.10)
project(Arm2x86 VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 构建选项
option(ARM2X86_BUILD_TESTS "Build test cases" ON)
option(ARM2X86_BUILD_TOOLS "Build tools" ON)
option(ARM2X86_ENABLE_AVX "Enable AVX optimization" OFF)
option(ARM2X86_ENABLE_NEON "Enable NEON translation" ON)
option(ARM2X86_ENABLE_PERF "Enable performance monitoring" ON)

# 编译选项
add_compile_options(-Wall -Wextra -O2)

if(ARM2X86_ENABLE_AVX)
    add_compile_options(-mavx2)
endif()

# 调试版本
set(CMAKE_C_FLAGS_DEBUG "-g -DARM2X86_DEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# 源文件
set(ARM2X86_SOURCES
    modules/arm2x86_core.c
    modules/arm2x86_decode.c
    modules/arm2x86_encode.c
    modules/arm2x86_regs.c
    modules/arm2x86_tcache.c
    modules/arm2x86_error.c
    modules/arm2x86_easy.c
    modules/arm2x86_trace.c
    modules/arm2x86_perf.c
)

# 头文件
set(ARM2X86_HEADERS
    include/arm2x86.h
    include/arm2x86_arch.h
    include/arm2x86_error.h
    include/arm2x86_easy.h
    modules/arm2x86_tcache.h
    modules/arm2x86_perf.h
    modules/arm2x86_trace.h
)

# 创建共享库
add_library(arm2x86 SHARED ${ARM2X86_SOURCES})
target_include_directories(arm2x86 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# 线程库
find_package(Threads REQUIRED)
target_link_libraries(arm2x86 PRIVATE Threads::Threads pthread)

# 版本信息
set_target_properties(arm2x86 PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER "${ARM2X86_HEADERS}"
)

# 安装规则
include(GNUInstallDirs)

install(TARGETS arm2x86
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# pkg-config 文件
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/arm2x86.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/arm2x86.pc
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/arm2x86.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# 测试
if(ARM2X86_BUILD_TESTS)
    enable_testing()
    
    file(GLOB TEST_SOURCES "tests/test_*.c")
    foreach(TEST_SRC ${TEST_SOURCES})
        get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
        add_executable(${TEST_NAME} ${TEST_SRC})
        target_link_libraries(${TEST_NAME} arm2x86)
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endforeach()
endif()

# 工具
if(ARM2X86_BUILD_TOOLS)
    add_executable(arm2x86_fuzz tools/arm2x86_fuzz.c)
    target_link_libraries(arm2x86_fuzz arm2x86)
    
    add_executable(arm2x86_prof tools/arm2x86_prof.c)
    target_link_libraries(arm2x86_prof arm2x86)
endif()

# 导出配置
export(TARGETS arm2x86 FILE Arm2x86Config.cmake)

message(STATUS "Arm2x86 ${PROJECT_VERSION} configuration complete")
message(STATUS "  Build tests: ${ARM2X86_BUILD_TESTS}")
message(STATUS "  Build tools: ${ARM2X86_BUILD_TOOLS}")
message(STATUS "  AVX optimization: ${ARM2X86_ENABLE_AVX}")
message(STATUS "  NEON translation: ${ARM2X86_ENABLE_NEON}")

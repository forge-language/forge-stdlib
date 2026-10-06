cmake_minimum_required(VERSION 3.16)
project(ForgeStd VERSION 0.3.0 LANGUAGES C)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
find_package(Threads REQUIRED)

include(cmake/dependencies.cmake)
if(NOT TARGET ForgeRuntime::runtime)
    if(FORGE_RUNTIME_SOURCE_DIR)
        add_subdirectory("${FORGE_RUNTIME_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/runtime")
    else()
        find_package(ForgeRuntime CONFIG QUIET)
        if(NOT ForgeRuntime_FOUND)
            include(FetchContent)
            FetchContent_Declare(forge_runtime
                GIT_REPOSITORY https://github.com/forge-language/forge-runtime.git
                GIT_TAG "${FORGE_RUNTIME_REVISION}")
            FetchContent_MakeAvailable(forge_runtime)
        endif()
    endif()
endif()
option(FORGE_ENABLE_GPU "Enable OpenCL GPU support" ON)
option(FORGE_ENABLE_IO_URING "Enable Linux io_uring HTTP support" ON)
option(FORGE_ENABLE_TLS "Enable OpenSSL TLS HTTP support" ON)
add_library(forge_std STATIC
    src/docstore.c
    src/fs.c
    src/gpu.c
    src/http.c
    src/http_routing.c
    src/http_tls.c
    src/http_uring.c
    src/io.c
    src/json.c
    src/lsprpc.c
    src/math.c
    src/os.c
    src/process.c
    src/string.c
    src/tcp.c
    src/threading.c
    src/time.c
    src/udp.c
)
add_library(ForgeStd::stdlib ALIAS forge_std)
set_target_properties(forge_std PROPERTIES EXPORT_NAME stdlib)
target_include_directories(forge_std PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(forge_std PUBLIC ForgeRuntime::runtime)
if(NOT WIN32)
    target_link_libraries(forge_std PUBLIC m)
    target_compile_definitions(forge_std PRIVATE _GNU_SOURCE)
endif()
if(MSVC)
    target_compile_options(forge_std PRIVATE /W4)
else()
    target_compile_options(forge_std PRIVATE -Wall -Wextra)
endif()
set(FORGE_LINK_EXTRAS "")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
if(FORGE_ENABLE_GPU)
    find_package(OpenCL)
    if(NOT OpenCL_FOUND)
        find_path(OpenCL_INCLUDE_DIR NAMES CL/cl.h OpenCL/cl.h
            PATHS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencl")
        find_library(OpenCL_LIBRARY NAMES OpenCL OpenCL.so.1)
        if(OpenCL_INCLUDE_DIR AND OpenCL_LIBRARY)
            set(OpenCL_FOUND TRUE)
            set(OpenCL_INCLUDE_DIRS "${OpenCL_INCLUDE_DIR}")
            set(OpenCL_LIBRARIES "${OpenCL_LIBRARY}")
        endif()
    endif()
    if(OpenCL_FOUND)
        message(STATUS "Forge GPU: OpenCL enabled (${OpenCL_LIBRARIES})")
        target_compile_definitions(forge_std PRIVATE FORGE_HAS_OPENCL=1)
        target_include_directories(forge_std PRIVATE ${OpenCL_INCLUDE_DIRS})
        target_link_libraries(forge_std PUBLIC ${OpenCL_LIBRARIES})
        list(APPEND FORGE_LINK_EXTRAS "${OpenCL_LIBRARIES}")
    else()
        message(STATUS "Forge GPU: OpenCL not found — gpu module will report unavailable at runtime")
    endif()
endif()

if(FORGE_ENABLE_IO_URING AND NOT WIN32)
    find_path(LIBURING_INCLUDE_DIR NAMES liburing.h PATHS /usr/include /usr/local/include)
    find_library(LIBURING_LIBRARY NAMES uring)
    if(LIBURING_INCLUDE_DIR AND LIBURING_LIBRARY)
        message(STATUS "Forge HTTP: io_uring enabled (${LIBURING_LIBRARY})")
        target_compile_definitions(forge_std PRIVATE FORGE_HAS_IO_URING=1)
        target_include_directories(forge_std PRIVATE ${LIBURING_INCLUDE_DIR})
        target_link_libraries(forge_std PUBLIC ${LIBURING_LIBRARY})
        list(APPEND FORGE_LINK_EXTRAS "${LIBURING_LIBRARY}")
    else()
        message(STATUS "Forge HTTP: io_uring not found — http_serve_uring falls back to epoll")
    endif()
endif()

if(FORGE_ENABLE_TLS)
    find_package(OpenSSL)
    if(OpenSSL_FOUND)
        message(STATUS "Forge HTTP: TLS enabled (${OPENSSL_SSL_LIBRARY})")
        target_compile_definitions(forge_std PRIVATE FORGE_HAS_TLS=1)
        target_include_directories(forge_std PRIVATE ${OPENSSL_INCLUDE_DIR})
        target_link_libraries(forge_std PUBLIC OpenSSL::SSL OpenSSL::Crypto)
        list(APPEND FORGE_LINK_EXTRAS "${OPENSSL_SSL_LIBRARY}" "${OPENSSL_CRYPTO_LIBRARY}")
    else()
        message(STATUS "Forge HTTP: OpenSSL not found — TLS falls back to plain HTTP")
    endif()
endif()

if(FORGE_LINK_EXTRAS)
    string(REPLACE ";" "\n" FORGE_LINK_CONTENT "${FORGE_LINK_EXTRAS}")
    file(WRITE "${CMAKE_BINARY_DIR}/lib/forge.link" "${FORGE_LINK_CONTENT}\n")
else()
    file(WRITE "${CMAKE_BINARY_DIR}/lib/forge.link" "")
endif()


set_property(TARGET forge_std PROPERTY FORGE_LINK_EXTRAS "${FORGE_LINK_EXTRAS}")
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(FORGE_LINK_EXTRAS "${FORGE_LINK_EXTRAS}" PARENT_SCOPE)
endif()
install(TARGETS forge_std EXPORT ForgeStdTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES "${CMAKE_BINARY_DIR}/lib/forge.link" DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(EXPORT ForgeStdTargets NAMESPACE ForgeStd:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ForgeStd)
set(FORGE_STD_FIND_DEPENDENCIES "")
if(FORGE_ENABLE_TLS AND OpenSSL_FOUND)
    set(FORGE_STD_FIND_DEPENDENCIES "find_dependency(OpenSSL)")
endif()
configure_package_config_file(cmake/ForgeStdConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/ForgeStdConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ForgeStd)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/ForgeStdConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ForgeStdConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ForgeStdConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ForgeStd)
include(CTest)
if(BUILD_TESTING)
    foreach(name string os fs)
        add_executable(${name}_test "tests/${name}_test.c")
        target_link_libraries(${name}_test PRIVATE ForgeStd::stdlib)
        if(MSVC)
            target_compile_options(${name}_test PRIVATE /UNDEBUG)
        else()
            target_compile_options(${name}_test PRIVATE -UNDEBUG)
        endif()
        add_test(NAME ${name}_regressions COMMAND ${name}_test)
        set_tests_properties(${name}_regressions PROPERTIES TIMEOUT 30)
    endforeach()
endif()

install(FILES LICENSE NOTICE DESTINATION ${CMAKE_INSTALL_DATADIR}/forge-stdlib)
install(DIRECTORY third_party/opencl DESTINATION ${CMAKE_INSTALL_DATADIR}/forge-stdlib/third_party)

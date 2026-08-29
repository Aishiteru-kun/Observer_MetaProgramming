cmake_minimum_required(VERSION 3.25)

project(
    ObserverMeta
    VERSION 1.0.0
    DESCRIPTION "Portable, deterministic C++ observer and delegate primitives"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(ObserverMeta INTERFACE)
add_library(ObserverMeta::ObserverMeta ALIAS ObserverMeta)

target_compile_features(ObserverMeta INTERFACE cxx_std_23)
target_include_directories(
    ObserverMeta
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

option(OBSERVER_META_BUILD_TESTS "Build ObserverMeta standalone tests" ${PROJECT_IS_TOP_LEVEL})

if(OBSERVER_META_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS ObserverMeta EXPORT ObserverMetaTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
    EXPORT ObserverMetaTargets
    FILE ObserverMetaTargets.cmake
    NAMESPACE ObserverMeta::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ObserverMeta
)

configure_package_config_file(
    cmake/ObserverMetaConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ObserverMetaConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ObserverMeta
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ObserverMetaConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/ObserverMetaConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/ObserverMetaConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ObserverMeta
)

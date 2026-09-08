cmake_minimum_required(VERSION 3.14)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS deployment version")
project(AppleDepth)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64")
set(OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/plugin" CACHE PATH "Plugin bundle output directory")

find_library(VISION_FRAMEWORK Vision REQUIRED)
find_library(COREML_FRAMEWORK CoreML REQUIRED)
find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)

add_library(AppleDepth MODULE
    AppleDepth.mm
    AppleDepth.h
    TOP_CPlusPlusBase.h
    CPlusPlus_Common.h
)

target_compile_options(AppleDepth PRIVATE -fobjc-arc)

target_link_libraries(AppleDepth PRIVATE
    ${VISION_FRAMEWORK}
    ${COREML_FRAMEWORK}
    ${COREVIDEO_FRAMEWORK}
    ${FOUNDATION_FRAMEWORK}
    ${COREFOUNDATION_FRAMEWORK}
)

set_target_properties(AppleDepth PROPERTIES
    BUNDLE ON
    LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIRECTORY}"
    RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIRECTORY}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    MACOSX_BUNDLE_BUNDLE_NAME "AppleDepth"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.derivative.AppleDepth"
    BUNDLE_EXTENSION "plugin"
    OUTPUT_NAME "AppleDepth"
)

option(TD_BUILD_TESTS "Build the half-precision depth conversion regression test" OFF)
if(TD_BUILD_TESTS)
	enable_testing()
	add_executable(AppleDepthHalfFloatTest tests/half-float.mm AppleDepth.mm)
	target_compile_options(AppleDepthHalfFloatTest PRIVATE -fobjc-arc)
	get_target_property(DEPTH_FRAMEWORKS AppleDepth LINK_LIBRARIES)
	target_link_libraries(AppleDepthHalfFloatTest PRIVATE ${DEPTH_FRAMEWORKS})
	add_test(NAME AppleDepthHalfFloat COMMAND AppleDepthHalfFloatTest)
	set_tests_properties(AppleDepthHalfFloat PROPERTIES TIMEOUT 30)
endif()

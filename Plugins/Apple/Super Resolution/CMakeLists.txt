cmake_minimum_required(VERSION 3.14)
project(AppleSuperResolution)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64")

find_library(VIDEOTOOLBOX_FRAMEWORK VideoToolbox REQUIRED)
find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
find_library(COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)

add_library(AppleSuperResolution MODULE
    AppleSuperResolution.mm
    AppleSuperResolution.h
    TOP_CPlusPlusBase.h
    CPlusPlus_Common.h
)

target_compile_options(AppleSuperResolution PRIVATE -fobjc-arc)

target_link_libraries(AppleSuperResolution PRIVATE
    ${VIDEOTOOLBOX_FRAMEWORK}
    ${COREVIDEO_FRAMEWORK}
    ${COREMEDIA_FRAMEWORK}
    ${FOUNDATION_FRAMEWORK}
    ${COREFOUNDATION_FRAMEWORK}
)

set_target_properties(AppleSuperResolution PROPERTIES
    BUNDLE ON
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    MACOSX_BUNDLE_BUNDLE_NAME "AppleSuperResolution"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.derivative.AppleSuperResolution"
    BUNDLE_EXTENSION "plugin"
    OUTPUT_NAME "AppleSuperResolution"
)
cmake_minimum_required(VERSION 3.14)
project(AppleInstanceMask)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64")

find_library(VISION_FRAMEWORK Vision REQUIRED)
find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)

add_library(AppleInstanceMask MODULE
    AppleInstanceMask.mm
    AppleInstanceMask.h
    TOP_CPlusPlusBase.h
    CPlusPlus_Common.h
)

target_compile_options(AppleInstanceMask PRIVATE -fobjc-arc)

target_link_libraries(AppleInstanceMask PRIVATE
    ${VISION_FRAMEWORK}
    ${COREVIDEO_FRAMEWORK}
    ${FOUNDATION_FRAMEWORK}
    ${COREFOUNDATION_FRAMEWORK}
)

set_target_properties(AppleInstanceMask PROPERTIES
    BUNDLE ON
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    MACOSX_BUNDLE_BUNDLE_NAME "AppleInstanceMask"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.derivative.AppleInstanceMask"
    BUNDLE_EXTENSION "plugin"
    OUTPUT_NAME "AppleInstanceMask"
)
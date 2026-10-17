cmake_minimum_required(VERSION 3.20)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS deployment version")
set(CMAKE_OSX_ARCHITECTURES "arm64")
project(AppleDepthAnything LANGUAGES CXX OBJCXX)
if(NOT APPLE)
    message(FATAL_ERROR "Apple Depth Anything requires macOS")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/plugin" CACHE PATH "Plugin bundle output directory")
set(MODEL_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/models" CACHE PATH "Folder containing the two original model packages")
find_package(Python3 3.9 COMPONENTS Interpreter REQUIRED)

foreach(FRAMEWORK Vision CoreML CoreVideo Foundation CoreFoundation)
    find_library(${FRAMEWORK}_LIBRARY ${FRAMEWORK} REQUIRED)
    list(APPEND DEPTH_FRAMEWORKS "${${FRAMEWORK}_LIBRARY}")
endforeach()
set(TD_APPLICATION "/Applications/TouchDesigner.app" CACHE PATH "TouchDesigner application to build against")
set(TD_PYTHON_ROOT "${TD_APPLICATION}/Contents/Frameworks/Python.framework")
file(GLOB TD_PYTHON_HEADERS "${TD_PYTHON_ROOT}/Versions/Current/include/python*/Python.h")
list(LENGTH TD_PYTHON_HEADERS TD_PYTHON_HEADER_COUNT)
if(NOT TD_PYTHON_HEADER_COUNT EQUAL 1)
    message(FATAL_ERROR "Set TD_APPLICATION to TouchDesigner with its Python development headers")
endif()
list(GET TD_PYTHON_HEADERS 0 TD_PYTHON_HEADER)
get_filename_component(TD_PYTHON_INCLUDE "${TD_PYTHON_HEADER}" DIRECTORY)
find_library(TD_PYTHON_FRAMEWORK Python PATHS "${TD_APPLICATION}/Contents/Frameworks" NO_DEFAULT_PATH REQUIRED)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ModelHost.py models/catalog.json)
file(READ ModelHost.py DEPTH_MODEL_HOST_SCRIPT)
configure_file(ModelHostScript.h.in ModelHostScript.h @ONLY)

execute_process(COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/model_assets.py"
    --verify --directory "${MODEL_DIRECTORY}" RESULT_VARIABLE MODEL_RESULT OUTPUT_VARIABLE MODEL_LOG ERROR_VARIABLE MODEL_ERROR)
if(NOT MODEL_RESULT EQUAL 0)
    message(FATAL_ERROR "Model packages are missing or changed. Run python3 model_assets.py first.\n${MODEL_ERROR}")
endif()
execute_process(COMMAND xcode-select -p OUTPUT_VARIABLE DEVELOPER_ROOT OUTPUT_STRIP_TRAILING_WHITESPACE)
find_program(COREML_COMPILER coremlcompiler HINTS
    "$ENV{DEVELOPER_DIR}/Toolchains/XcodeDefault.xctoolchain/usr/bin"
    "${DEVELOPER_ROOT}/Toolchains/XcodeDefault.xctoolchain/usr/bin"
    "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin" REQUIRED)
set(MODEL_NAMES DepthAnythingV2SmallF16 DepthAnythingV2SmallF16P6)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tmp" "${CMAKE_CURRENT_BINARY_DIR}/compiled-models")
foreach(MODEL IN LISTS MODEL_NAMES)
    file(GLOB_RECURSE MODEL_FILES CONFIGURE_DEPENDS "${MODEL_DIRECTORY}/${MODEL}.mlpackage/*")
    set(STAMP "${CMAKE_CURRENT_BINARY_DIR}/compiled-models/${MODEL}.stamp")
    add_custom_command(OUTPUT "${STAMP}"
        COMMAND "${CMAKE_COMMAND}" -E env "TMPDIR=${CMAKE_CURRENT_BINARY_DIR}/tmp/"
            "${COREML_COMPILER}" compile "${MODEL_DIRECTORY}/${MODEL}.mlpackage"
            "${CMAKE_CURRENT_BINARY_DIR}/compiled-models"
        COMMAND "${CMAKE_COMMAND}" -E touch "${STAMP}"
        DEPENDS ${MODEL_FILES} VERBATIM)
    list(APPEND MODEL_STAMPS "${STAMP}")
endforeach()

add_library(DepthImplementation STATIC AppleDepth.mm ModelHost.cpp)
set_target_properties(DepthImplementation PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(DepthImplementation PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
target_include_directories(DepthImplementation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${TD_PYTHON_INCLUDE}" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(DepthImplementation PUBLIC ${DEPTH_FRAMEWORKS} "${TD_PYTHON_FRAMEWORK}")
# The implementation exports TD's entry points. Force loading it into the module.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Module.cpp" "// Entry points are in DepthImplementation.\n")
add_library(AppleDepthAnything MODULE "${CMAKE_CURRENT_BINARY_DIR}/Module.cpp")
target_link_libraries(AppleDepthAnything PRIVATE DepthImplementation)
target_link_options(AppleDepthAnything PRIVATE "-Wl,-force_load,$<TARGET_FILE:DepthImplementation>")
set_target_properties(AppleDepthAnything PROPERTIES PREFIX "" BUNDLE ON BUNDLE_EXTENSION "plugin"
    LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIRECTORY}" RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIRECTORY}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "@executable_path/../Frameworks")

set(RESOURCES "$<TARGET_BUNDLE_DIR:AppleDepthAnything>/Contents/Resources")
add_custom_target(AppleDepthAnythingResources ALL DEPENDS AppleDepthAnything ${MODEL_STAMPS}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${RESOURCES}/Models"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/licenses" "${RESOURCES}/Licenses"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/models/catalog.json" "${RESOURCES}/Models/catalog.json")
foreach(MODEL IN LISTS MODEL_NAMES)
    add_custom_command(TARGET AppleDepthAnythingResources POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${MODEL_DIRECTORY}/${MODEL}.mlpackage" "${RESOURCES}/Models/${MODEL}.mlpackage"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/compiled-models/${MODEL}.mlmodelc" "${RESOURCES}/Models/${MODEL}.mlmodelc"
        VERBATIM)
endforeach()
option(TD_ADHOC_SIGN "Ad-hoc sign the complete development bundle" ON)
if(TD_ADHOC_SIGN)
    add_custom_command(TARGET AppleDepthAnythingResources POST_BUILD
        COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:AppleDepthAnything>" VERBATIM)
endif()

option(TD_BUILD_TESTS "Build half-float, model loading, and project-copy tests" OFF)
if(TD_BUILD_TESTS)
    enable_testing()
    add_executable(AppleDepthHalfFloatTest tests/half-float.mm)
    target_compile_options(AppleDepthHalfFloatTest PRIVATE -fobjc-arc)
    target_link_libraries(AppleDepthHalfFloatTest PRIVATE DepthImplementation)
    add_test(NAME AppleDepthHalfFloat COMMAND AppleDepthHalfFloatTest)
    add_test(NAME DepthProjectCopies COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/model_host.py" "${CMAKE_CURRENT_BINARY_DIR}/copy-tests")
    add_executable(DepthModelSmoke tests/models.mm)
    target_compile_options(DepthModelSmoke PRIVATE -fobjc-arc)
    target_link_libraries(DepthModelSmoke PRIVATE DepthImplementation)
    add_test(NAME DepthBundledInference COMMAND DepthModelSmoke "$<TARGET_BUNDLE_DIR:AppleDepthAnything>")
    set_tests_properties(DepthBundledInference PROPERTIES TIMEOUT 180 ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_BINARY_DIR}/tmp/")
    set_tests_properties(AppleDepthHalfFloat DepthProjectCopies PROPERTIES TIMEOUT 30)
endif()

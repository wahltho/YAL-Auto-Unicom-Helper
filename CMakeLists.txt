cmake_minimum_required(VERSION 3.15)
project(YAL_autounicomhelper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(XPLANE_SDK_PATH "" CACHE PATH "Path to the X-Plane SDK root")
if (NOT XPLANE_SDK_PATH OR NOT EXISTS "${XPLANE_SDK_PATH}/CHeaders/XPLM/XPLMPlugin.h")
    message(FATAL_ERROR "Set XPLANE_SDK_PATH to the X-Plane SDK root containing CHeaders.")
endif()

set(IS_WINDOWS_BUILD OFF)
if (WIN32 OR MINGW OR CMAKE_SYSTEM_NAME STREQUAL "Windows" OR
    CMAKE_CXX_COMPILER MATCHES "mingw")
    set(IS_WINDOWS_BUILD ON)
endif()

add_library(AutoUnicomHelper MODULE
    src/YAL_autounicomhelper.cpp
    src/auto_unicom.cpp
    src/auto_unicom_voice.cpp
    src/helper_config.cpp
    src/pcm_sound.cpp
    src/pilotui_message_sender.cpp
)

target_include_directories(AutoUnicomHelper PRIVATE
    src
    "${XPLANE_SDK_PATH}/CHeaders"
    "${XPLANE_SDK_PATH}/CHeaders/XPLM"
)

if (APPLE)
    target_compile_definitions(AutoUnicomHelper PRIVATE APL=1 IBM=0 LIN=0)
    target_link_options(AutoUnicomHelper PRIVATE "-F${XPLANE_SDK_PATH}/Libraries/Mac")
    target_link_libraries(AutoUnicomHelper PRIVATE "-framework XPLM")
elseif (IS_WINDOWS_BUILD)
    target_compile_definitions(AutoUnicomHelper PRIVATE APL=0 IBM=1 LIN=0)
    target_link_libraries(AutoUnicomHelper PRIVATE
        "${XPLANE_SDK_PATH}/Libraries/Win/XPLM_64.lib"
        user32
        uiautomationcore
        ole32
        oleaut32
        propsys
        sapi
        uuid
    )
    if (MINGW)
        target_compile_options(AutoUnicomHelper PRIVATE -mthreads)
        target_link_libraries(AutoUnicomHelper PRIVATE winpthread)
        target_link_options(AutoUnicomHelper PRIVATE
            -mthreads -static -static-libgcc -static-libstdc++)
    endif()
else()
    target_compile_definitions(AutoUnicomHelper PRIVATE APL=0 IBM=0 LIN=1)
    target_link_libraries(AutoUnicomHelper PRIVATE
        "${XPLANE_SDK_PATH}/Libraries/Lin/XPLM_64.so")
endif()

target_compile_definitions(AutoUnicomHelper PRIVATE
    XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM303=1 XPLM400=1)

set_target_properties(AutoUnicomHelper PROPERTIES PREFIX "" SUFFIX ".xpl")
if (IS_WINDOWS_BUILD)
    set_target_properties(AutoUnicomHelper PROPERTIES OUTPUT_NAME "win")
elseif (APPLE)
    set_target_properties(AutoUnicomHelper PROPERTIES OUTPUT_NAME "mac")
else()
    set_target_properties(AutoUnicomHelper PROPERTIES OUTPUT_NAME "lin")
endif()

include(CTest)
if (BUILD_TESTING)
    add_executable(auto_unicom_tests
        src/auto_unicom.cpp
        src/auto_unicom_voice.cpp
        src/helper_config.cpp
        src/pilotui_message_sender.cpp
        tests/auto_unicom_tests.cpp
    )
    target_include_directories(auto_unicom_tests PRIVATE src)
    if (IS_WINDOWS_BUILD)
        target_compile_definitions(auto_unicom_tests PRIVATE APL=0 IBM=1 LIN=0)
        target_link_libraries(auto_unicom_tests PRIVATE
            user32 uiautomationcore ole32 oleaut32 propsys sapi uuid)
        if (MINGW)
            target_compile_options(auto_unicom_tests PRIVATE -mthreads)
            target_link_libraries(auto_unicom_tests PRIVATE winpthread)
            target_link_options(auto_unicom_tests PRIVATE
                -mthreads -static -static-libgcc -static-libstdc++)
        endif()
    elseif (APPLE)
        target_compile_definitions(auto_unicom_tests PRIVATE APL=1 IBM=0 LIN=0)
    else()
        target_compile_definitions(auto_unicom_tests PRIVATE APL=0 IBM=0 LIN=1)
    endif()
    add_test(NAME auto_unicom_tests COMMAND auto_unicom_tests)
endif()

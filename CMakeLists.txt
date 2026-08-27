cmake_minimum_required(VERSION 3.20)
project(CopyPointerNotifier LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(CopyPointerNotifier WIN32
    src/application.cpp
    src/clipboard_classifier.cpp
    src/clipboard_rules.cpp
    src/main.cpp
    src/overlay_window.cpp
    src/startup_registration.cpp
    src/tray_icon.cpp
    resources.rc
)

if(MSVC)
    set_property(TARGET CopyPointerNotifier PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_options(CopyPointerNotifier PRIVATE /MANIFEST:NO)
else()
    target_link_options(CopyPointerNotifier PRIVATE -municode -static)
endif()

target_include_directories(CopyPointerNotifier PRIVATE src)
target_compile_definitions(CopyPointerNotifier PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)
target_compile_options(CopyPointerNotifier PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /EHsc>
)
target_link_libraries(
    CopyPointerNotifier
    PRIVATE advapi32 dwmapi gdi32 gdiplus shell32 user32 windowsapp)

find_program(MSBUILD_EXECUTABLE MSBuild.exe
    HINTS "C:/BuildTools2026/MSBuild/Current/Bin"
          "C:/Program Files/Microsoft Visual Studio/2022/BuildTools/MSBuild/Current/Bin")
if(MSBUILD_EXECUTABLE)
    set(COPY_POINTER_NOTIFIER_SETTINGS_PLATFORM "" CACHE STRING
        "WinUI settings platform (x64 or ARM64)")
    if(NOT COPY_POINTER_NOTIFIER_SETTINGS_PLATFORM)
        string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" system_processor)
        string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" generator_platform)
        if(generator_platform STREQUAL "arm64" OR
           system_processor MATCHES "^(arm64|aarch64)$")
            set(COPY_POINTER_NOTIFIER_SETTINGS_PLATFORM "ARM64")
        elseif(generator_platform STREQUAL "x64" OR
               system_processor MATCHES "^(amd64|x86_64|x64)$")
            set(COPY_POINTER_NOTIFIER_SETTINGS_PLATFORM "x64")
        else()
            message(FATAL_ERROR
                "Unsupported Windows architecture '${CMAKE_SYSTEM_PROCESSOR}'. "
                "Copy Pointer Notifier supports x64 and ARM64.")
        endif()
    endif()
    if(NOT COPY_POINTER_NOTIFIER_SETTINGS_PLATFORM MATCHES "^(x64|ARM64)$")
        message(FATAL_ERROR
            "COPY_POINTER_NOTIFIER_SETTINGS_PLATFORM must be x64 or ARM64.")
    endif()
    message(STATUS
        "WinUI settings platform: ${COPY_POINTER_NOTIFIER_SETTINGS_PLATFORM}")

    add_custom_target(CopyPointerNotifierSettings ALL
        COMMAND "${MSBUILD_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/settings/CopyPointerNotifier.Settings.vcxproj"
            /restore
            /p:RestorePackagesConfig=true
            /p:Configuration=Release
            /p:Platform=${COPY_POINTER_NOTIFIER_SETTINGS_PLATFORM}
            "/p:SolutionDir=${CMAKE_CURRENT_SOURCE_DIR}/"
            "/p:OutDir=$<TARGET_FILE_DIR:CopyPointerNotifier>/settings/"
            "/p:IntDir=${CMAKE_BINARY_DIR}/settings-obj/${COPY_POINTER_NOTIFIER_SETTINGS_PLATFORM}/Release/"
            /verbosity:minimal
            /nologo
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Building native WinUI 3 settings app"
        VERBATIM
    )
    add_dependencies(CopyPointerNotifierSettings CopyPointerNotifier)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(clipboard_classifier_tests
        tests/clipboard_classifier_tests.cpp
        src/clipboard_classifier.cpp
        src/clipboard_rules.cpp
    )
    target_include_directories(clipboard_classifier_tests PRIVATE src)
    target_compile_definitions(clipboard_classifier_tests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
    )
    target_compile_options(clipboard_classifier_tests PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /EHsc>
    )
    target_link_libraries(clipboard_classifier_tests PRIVATE user32)
    add_test(NAME clipboard_classifier_tests COMMAND clipboard_classifier_tests)
endif()

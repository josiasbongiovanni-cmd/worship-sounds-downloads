cmake_minimum_required(VERSION 3.22)
project(WorshipDAW VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Windows: runtime de C++ enlazado dentro del .exe (no hace falta instalar el "Visual C++ Redistributable")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS mínimo")
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal (Apple Silicon + Intel)")
endif()

# JUCE: usa una copia local (-DJUCE_DIR=...) o la descarga fijada a una versión.
set(JUCE_DIR "" CACHE PATH "Carpeta de JUCE (opcional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.8 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(WDAW_MODEL_SOURCES
    Source/Model/Model.cpp
    Source/Model/Editing.cpp
    Source/Model/Serialization.cpp)

file(GLOB WDAW_ENGINE_SOURCES CONFIGURE_DEPENDS Source/Engine/*.cpp)
file(GLOB WDAW_UI_SOURCES CONFIGURE_DEPENDS Source/UI/*.cpp)

set(WDAW_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

# ASIO (Windows): necesita el ASIO SDK de Steinberg, que no se incluye en el repositorio (licencia propia).
# Descárgalo con scripts/get-asio-sdk.ps1 (o pásalo con -DWDAW_ASIO_SDK=<carpeta>). Sin SDK: WASAPI y DirectSound.
set(WDAW_ASIO_SDK "" CACHE PATH "Carpeta del ASIO SDK de Steinberg (Windows)")
if(WIN32 AND NOT WDAW_ASIO_SDK AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/asiosdk/common/asio.h")
    set(WDAW_ASIO_SDK "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/asiosdk")
endif()
if(WIN32 AND WDAW_ASIO_SDK)
    if(NOT EXISTS "${WDAW_ASIO_SDK}/common/asio.h")
        message(FATAL_ERROR "WDAW_ASIO_SDK no contiene common/asio.h: ${WDAW_ASIO_SDK}")
    endif()
    message(STATUS "ASIO activado (SDK en ${WDAW_ASIO_SDK})")
    list(APPEND WDAW_DEFINITIONS JUCE_ASIO=1)
    include_directories("${WDAW_ASIO_SDK}/common")
else()
    if(WIN32)
        message(STATUS "ASIO desactivado: falta el ASIO SDK (ver scripts/get-asio-sdk.ps1). Se usa WASAPI / DirectSound.")
    endif()
    list(APPEND WDAW_DEFINITIONS JUCE_ASIO=0)
endif()

set(WDAW_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_audio_devices
    juce::juce_dsp
    juce::juce_gui_extra)

# ---------------------------------------------------------------------------
# Aplicación (Mac y Windows; también compila en Linux para pruebas)
juce_add_gui_app(WorshipDAW
    PRODUCT_NAME "Worship DAW"
    COMPANY_NAME "Worship Sounds"
    BUNDLE_ID "com.worshipsounds.daw"
    VERSION ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Worship DAW usa tu micrófono o interfaz de audio para grabar."
    HARDENED_RUNTIME_ENABLED TRUE
    HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input com.apple.security.cs.disable-library-validation com.apple.security.cs.allow-jit com.apple.security.cs.allow-unsigned-executable-memory
    DOCUMENT_EXTENSIONS worship
    ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png)

# Tipografía Inter incluida en la app (mismo aspecto en Mac y Windows). Licencia SIL OFL en Resources/Fonts.
juce_add_binary_data(WorshipDAWData SOURCES Resources/Fonts/Inter-Regular.otf Resources/Fonts/Inter-SemiBold.otf)

target_sources(WorshipDAW PRIVATE Source/Main.cpp ${WDAW_MODEL_SOURCES} ${WDAW_ENGINE_SOURCES} ${WDAW_UI_SOURCES})
target_include_directories(WorshipDAW PRIVATE Source)
target_compile_definitions(WorshipDAW PRIVATE ${WDAW_DEFINITIONS}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:WorshipDAW,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:WorshipDAW,JUCE_VERSION>")
target_link_libraries(WorshipDAW PRIVATE ${WDAW_MODULES} WorshipDAWData
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Pruebas (modelo + motor de audio sin interfaz). ctest o ./WorshipDAWTests
juce_add_console_app(WorshipDAWTests PRODUCT_NAME "WorshipDAWTests")
file(GLOB WDAW_TEST_SOURCES CONFIGURE_DEPENDS Tests/*.cpp)
target_sources(WorshipDAWTests PRIVATE ${WDAW_TEST_SOURCES} ${WDAW_MODEL_SOURCES} ${WDAW_ENGINE_SOURCES}
    Source/UI/Session.cpp Source/UI/MackieControl.cpp)
target_include_directories(WorshipDAWTests PRIVATE Source)
target_compile_definitions(WorshipDAWTests PRIVATE ${WDAW_DEFINITIONS} JUCE_UNIT_TESTS=1)
target_link_libraries(WorshipDAWTests PRIVATE ${WDAW_MODULES} juce::juce_recommended_config_flags)

enable_testing()
add_test(NAME WorshipDAWTests COMMAND WorshipDAWTests)

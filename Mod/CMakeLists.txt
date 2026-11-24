cmake_minimum_required(VERSION 3.15)

# Living World – KI-NPCs & Realismus fuer HITMAN World of Assassination.
# Wird vom ZHMModSDK-Baum als Mods/LivingWorld eingebunden (siehe tools/build.ps1).

# Abhaengigkeiten direkt aus den Quellarchiven (CPM kommt aus dem SDK-Root). Bewusst nicht ueber vcpkg,
# dessen msys2/pkg-config-Werkzeuge auf diesem Rechner nicht entpackt werden konnten.
CPMAddPackage(
        NAME nlohmann_json
        URL https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz
        OPTIONS
            "JSON_BuildTests OFF"
            "JSON_Install OFF"
)

CPMAddPackage(
        NAME whisper
        URL https://github.com/ggml-org/whisper.cpp/archive/refs/tags/v1.8.6.tar.gz
        OPTIONS
            "BUILD_SHARED_LIBS OFF"
            "WHISPER_BUILD_EXAMPLES OFF"
            "WHISPER_BUILD_TESTS OFF"
            "WHISPER_BUILD_SERVER OFF"
            "WHISPER_SDL2 OFF"
            "WHISPER_CURL OFF"
            "WHISPER_ALL_WARNINGS OFF"
            "GGML_NATIVE OFF"
            "GGML_AVX ON"
            "GGML_AVX2 ON"
            "GGML_FMA ON"
            "GGML_F16C ON"
            "GGML_OPENMP OFF"
            "GGML_BACKEND_DL OFF"
            "GGML_CPU_ALL_VARIANTS OFF"
            "GGML_CUDA OFF"
            "GGML_VULKAN OFF"
            "GGML_BLAS OFF"
            "GGML_BUILD_TESTS OFF"
            "GGML_BUILD_EXAMPLES OFF"
)

# ---------------------------------------------------------------------------------------------------------------------
# Kern: alles ohne Spiel-Abhaengigkeiten (KI, Sprache, Audio, Logik) – wird auch von den Tests genutzt.
# ---------------------------------------------------------------------------------------------------------------------
file(GLOB_RECURSE LW_CORE_SOURCES CONFIGURE_DEPENDS
        Src/Core/*.cpp
        Src/Core/*.h
)

add_library(LivingWorldCore STATIC ${LW_CORE_SOURCES})

target_include_directories(LivingWorldCore PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Src/Core
)

target_link_libraries(LivingWorldCore PUBLIC
        nlohmann_json::nlohmann_json
        whisper
        winhttp
        winmm
        ole32
        oleaut32
        sapi
        xaudio2
        shlwapi
)

target_compile_options(LivingWorldCore PRIVATE /utf-8)

# ---------------------------------------------------------------------------------------------------------------------
# Die eigentliche Mod (DLL fuer Retail/mods).
# ---------------------------------------------------------------------------------------------------------------------
file(GLOB_RECURSE LW_PLUGIN_SOURCES CONFIGURE_DEPENDS
        Src/Plugin/*.cpp
        Src/Plugin/*.h
)

add_library(LivingWorld SHARED ${LW_PLUGIN_SOURCES})

target_include_directories(LivingWorld PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Src/Plugin
)

target_link_libraries(LivingWorld PRIVATE
        ZHMModSDK
        LivingWorldCore
)

target_compile_options(LivingWorld PRIVATE /utf-8)

# ---------------------------------------------------------------------------------------------------------------------
# Tests fuer den Kern (laufen ohne Spiel).
# ---------------------------------------------------------------------------------------------------------------------
add_executable(LivingWorldTests Tests/CoreTests.cpp)
target_link_libraries(LivingWorldTests PRIVATE LivingWorldCore)
target_compile_options(LivingWorldTests PRIVATE /utf-8)

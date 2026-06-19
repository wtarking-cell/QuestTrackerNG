cmake_minimum_required(VERSION 3.24)

project(
	QuestTrackerNG
	VERSION 1.1.0
	LANGUAGES CXX
)

if(NOT WIN32 OR NOT MSVC)
	message(FATAL_ERROR
		"QuestTrackerNG is an SKSE plugin and must be built with MSVC on Windows.\n"
		"The engine-independent logic tests in tests/ build on any platform:\n"
		"  cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Link-time optimization for every optimized config. Previously this only
# covered Release, so the RelWithDebInfo binaries the CI/release pipeline
# actually shipped were built without LTO.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE        ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)

# ---------------------------------------------------------------------------
# CommonLibSSE-NG (pinned commit on main, fetched at configure time).
# Its own dependencies (spdlog, rapidcsv) come from this project's vcpkg
# manifest, so no third-party vcpkg registry is required.
# ---------------------------------------------------------------------------
include(FetchContent)

set(ENABLE_SKYRIM_SE ON  CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON  CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS      OFF CACHE BOOL "" FORCE)  # CommonLibSSE's own unit tests
set(SKSE_SUPPORT_XBYAK OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
	CommonLibSSE
	GIT_REPOSITORY https://github.com/CharmedBaryon/CommonLibSSE-NG.git
	GIT_TAG        b93280e832f263dbef44e44cbe2936622a02f91a # main, verified 2026-06-09
)
FetchContent_MakeAvailable(CommonLibSSE)

find_package(imgui CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
set(SOURCES
	src/main.cpp
	src/Settings.cpp
	src/Hooks/Hooks.cpp
	src/Input/InputTranslator.cpp
	src/UI/QuestTrackerUI.cpp
)

set(HEADERS
	src/PCH.h
	src/Plugin.h
	src/Settings.h
	src/Core/StageLogic.h
	src/Hooks/Hooks.h
	src/Input/InputTranslator.h
	src/UI/QuestTrackerUI.h
)

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

target_compile_options(${PROJECT_NAME} PRIVATE
	/permissive-
	/Zc:preprocessor
	/EHsc
	/MP
	/W4
	/external:W0
	/external:anglebrackets
	# Optimized builds: aggressive inlining (/Ob3), function-level linking
	# (/Gy) and COMDAT folding (/Gw) so the linker can drop everything the
	# plugin never calls. /Zi still emits a PDB for crash-log symbolication;
	# package.py keeps the PDB out of the player archive.
	$<$<CONFIG:Release,RelWithDebInfo>:/Ob3;/Gy;/Gw;/Zi>
)

# /OPT:REF drops unreferenced functions/data, /OPT:ICF folds identical ones.
# RelWithDebInfo links with /DEBUG, which silently turns these OFF unless we
# re-enable them explicitly -- that omission is what tripled the DLL size by
# keeping all the unused CommonLibSSE-NG + ImGui code in the binary.
target_link_options(${PROJECT_NAME} PRIVATE
	$<$<CONFIG:Release,RelWithDebInfo>:/DEBUG:FULL;/OPT:REF;/OPT:ICF;/INCREMENTAL:NO>
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME}
	PRIVATE
		CommonLibSSE::CommonLibSSE
		imgui::imgui
)

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

# ---------------------------------------------------------------------------
# Package layout: build/dist/Data/SKSE/Plugins/{dll,ini} — zip this folder
# (or point MO2/Vortex at it) to install the mod.
# ----------------------------
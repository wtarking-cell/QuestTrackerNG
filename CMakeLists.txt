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
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

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
# ---------------------------------------------------------------------------
set(DIST_DIR "${CMAKE_BINARY_DIR}/dist/Data/SKSE/Plugins")

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<TARGET_FILE:${PROJECT_NAME}>" "${DIST_DIR}/"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DIST_DIR}/"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_SOURCE_DIR}/contrib/QuestTrackerNG.ini" "${DIST_DIR}/"
	COMMENT "Staging mod package in ${CMAKE_BINARY_DIR}/dist"
)

# Optional: copy straight into a mod manager / game folder after each build.
# Set the env var QUEST_TRACKER_OUTPUT_DIR to e.g. an MO2 mods directory.
if(DEFINED ENV{QUEST_TRACKER_OUTPUT_DIR})
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_BINARY_DIR}/dist" "$ENV{QUEST_TRACKER_OUTPUT_DIR}"
		COMMENT "Deploying to $ENV{QUEST_TRACKER_OUTPUT_DIR}"
	)
endif()

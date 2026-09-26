cmake_minimum_required(VERSION 3.29)

project(StashRightClickToCube LANGUAGES CXX RC)

# Path to the official D2RLoader PluginSDK checkout.
set(D2RL_PLUGIN_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../D2RLoader-PluginSDK" CACHE PATH "D2RLoader PluginSDK directory")

add_subdirectory("${D2RL_PLUGIN_SDK_DIR}" D2RLoader-PluginSDK)

add_library(StashRightClickToCube SHARED
	stash_right_click_to_cube.cpp
	stash_right_click_to_cube.rc
)

target_link_libraries(StashRightClickToCube PRIVATE D2RLPlugin::D2RLPlugin)

set_target_properties(StashRightClickToCube PROPERTIES
	OUTPUT_NAME "d2rl-stash-rightclick-cube"
)

if(MSVC)
	target_compile_options(StashRightClickToCube PRIVATE /W3 /permissive- /EHsc)
endif()

# Deploy straight into the game's global plugin folder after building.
set(GAME_PLUGIN_DIR "" CACHE PATH "Optional game d2rloader/plugins directory; if set the DLL is copied there on build")
if(GAME_PLUGIN_DIR)
	add_custom_command(TARGET StashRightClickToCube POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:StashRightClickToCube>" "${GAME_PLUGIN_DIR}/$<TARGET_FILE_NAME:StashRightClickToCube>"
	)
endif()

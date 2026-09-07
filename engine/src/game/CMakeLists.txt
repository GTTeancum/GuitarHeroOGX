# engine/src/game/CMakeLists.txt
#
# ghogx_game: song gameplay system (highway renderer, audio player, state machine).
# Windows-only (D3D9 + XAudio2).

add_library(ghogx_gameplay_rules STATIC
    gameplay_rules.cpp gameplay_rules.h
    gameplay_session.cpp gameplay_session.h
)
target_include_directories(ghogx_gameplay_rules PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(ghogx_source_anim_task_test source_anim_task_test.cpp)
target_include_directories(ghogx_source_anim_task_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_test(NAME ghogx_source_anim_task_test COMMAND ghogx_source_anim_task_test)
target_link_libraries(ghogx_gameplay_rules PUBLIC ghogx_chart)

if(MSVC)
    target_compile_options(ghogx_gameplay_rules PRIVATE /W4)
else()
    target_compile_options(ghogx_gameplay_rules PRIVATE -Wall -Wextra)
endif()

add_library(ghogx_game STATIC
    highway_renderer.cpp highway_renderer.h
    audio_player.cpp     audio_player.h
    gameplay.cpp         gameplay.h
    ../catalog.cpp       ../catalog.h
)

target_include_directories(ghogx_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ghogx_chart for the MIDI/chart types.
# ghogx_render for Window + D3D9.
# ark_v3 + vgs for asset loading inside gameplay/audio_player.
target_link_libraries(ghogx_game PUBLIC
    ghogx_gameplay_rules
    ghogx_chart
    ghogx_render
    ghogx_asset
    ghogx_milo_scene
    ghogx_character
    ghogx_script
    ark_v3
    dtb
    milo
    vgs
)

# XAudio2 is available on Windows 8+ SDK as a system DLL; link against the
# import library. Older SDK may need xaudio2.lib separately.
target_link_libraries(ghogx_game PRIVATE xaudio2)

if(MSVC)
    target_compile_options(ghogx_game PRIVATE /W4 /bigobj)
else()
    target_compile_options(ghogx_game PRIVATE -Wall -Wextra)
endif()

# Manual end-to-end smoke test for the streaming audio player (not a CTest;
# needs real ARK assets). Links only ghogx_game, so it does not pull in the
# character module.
add_executable(audio_smoke audio_smoke.cpp)
target_link_libraries(audio_smoke PRIVATE ghogx_game)
ghogx_add_windows_import_guard(audio_smoke)

add_executable(ghogx_gameplay_venue_band_contract_test
    gameplay_venue_band_contract_test.cpp)
target_compile_definitions(ghogx_gameplay_venue_band_contract_test PRIVATE
    GHOGX_GAME_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    GHOGX_ASSET_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../asset"
    GHOGX_CHART_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../chart"
    GHOGX_CHARACTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../character"
    GHOGX_HUD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../hud"
    GHOGX_MILO_SCENE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../milo_scene"
    GHOGX_RENDER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../render")
add_test(NAME ghogx_gameplay_venue_band_contract_test
         COMMAND ghogx_gameplay_venue_band_contract_test)

if(WIN32)
    find_program(GHOGX_POWERSHELL powershell.exe)
    if(GHOGX_POWERSHELL)
        add_test(NAME ghogx_gameplay_venue_band_contract_imports
                 COMMAND ${GHOGX_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
                         -File "${CMAKE_SOURCE_DIR}/../tools/check_windows_imports.ps1"
                         $<TARGET_FILE:ghogx_gameplay_venue_band_contract_test>)
    endif()
endif()

add_executable(ghogx_gameplay_rules_test gameplay_rules_test.cpp)
target_link_libraries(ghogx_gameplay_rules_test PRIVATE ghogx_gameplay_rules)
add_test(NAME ghogx_gameplay_rules_test COMMAND ghogx_gameplay_rules_test)

add_executable(ghogx_gameplay_session_test gameplay_session_test.cpp)
target_link_libraries(ghogx_gameplay_session_test PRIVATE ghogx_gameplay_rules)
add_test(NAME ghogx_gameplay_session_test COMMAND ghogx_gameplay_session_test)

  add_executable(ghogx_camera_filter_test camera_filter_test.cpp)
  add_test(NAME ghogx_camera_filter_test COMMAND ghogx_camera_filter_test)
  ghogx_add_windows_import_guard(ghogx_camera_filter_test)

  add_executable(ghogx_camera_projection_test camera_projection_test.cpp)
  add_test(NAME ghogx_camera_projection_test COMMAND ghogx_camera_projection_test)
  ghogx_add_windows_import_guard(ghogx_camera_projection_test)

  add_executable(ghogx_camera_intro_timing_test camera_intro_timing_test.cpp)
  add_test(NAME ghogx_camera_intro_timing_test
           COMMAND ghogx_camera_intro_timing_test)
  ghogx_add_windows_import_guard(ghogx_camera_intro_timing_test)
ghogx_add_windows_import_guard(ghogx_gameplay_venue_band_contract_test)
ghogx_add_windows_import_guard(ghogx_gameplay_rules_test)
ghogx_add_windows_import_guard(ghogx_gameplay_session_test)

if(MSVC)
    target_compile_options(ghogx_gameplay_venue_band_contract_test PRIVATE /W4)
    target_compile_options(ghogx_gameplay_rules_test PRIVATE /W4)
    target_compile_options(ghogx_gameplay_session_test PRIVATE /W4)
else()
    target_compile_options(ghogx_gameplay_venue_band_contract_test PRIVATE
        -Wall -Wextra)
    target_compile_options(ghogx_gameplay_rules_test PRIVATE -Wall -Wextra)
    target_compile_options(ghogx_gameplay_session_test PRIVATE -Wall -Wextra)
endif()

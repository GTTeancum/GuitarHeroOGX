# engine/src/ui/CMakeLists.txt
#
# ghogx_ui_core: the menu UI runtime -- the GH2 UI object classes (GHPanel/
# GHScreen/UIComponent/...), the ScreenManager (the `ui` object + interpreter
# Host + screen-transition state machine), and the {new ...} DTB screen loader.
# Portable (core + script + dtb_bridge + ark/dtb; no Windows dependency). The
# D3D9 renderer (ghogx_ui_render) is a separate WIN32 target added in Phase 5.

add_library(ghogx_ui_core STATIC
    ui_classes.cpp      ui_classes.h
    screen_manager.cpp  screen_manager.h
    screen_loader.cpp   screen_loader.h
    config_db.cpp       config_db.h
    meta_objects.cpp    meta_objects.h
    manage_band_panel.cpp manage_band_panel.h
    soundcheck_panel.cpp soundcheck_panel.h
)
target_include_directories(ghogx_ui_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(ghogx_ui_core PUBLIC
    ghogx_core ghogx_script ghogx_dtb_bridge ghogx_chart ghogx_milo_scene
    ark_v3 dtb milo)

if(MSVC)
    target_compile_options(ghogx_ui_core PRIVATE /W4)
else()
    target_compile_options(ghogx_ui_core PRIVATE -Wall -Wextra)
endif()

# The menu bitmap font ("impact"): parse impact.font byte-exact (charset +
# kerning) and derive glyph rects from the decoded atlas. Portable (asset decode
# is CPU-side; no D3D9), so it can be unit-tested headless.
add_library(ghogx_menu_font STATIC menu_font.cpp menu_font.h menu_labels.cpp menu_labels.h)
target_include_directories(ghogx_menu_font PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(ghogx_menu_font PUBLIC ghogx_asset ark_v3 milo dtb
                                               ghogx_milo_scene)
# The stock panel loader seeds UITrigger/AnimFilter/TransAnim runtime state from
# the same byte-exact decoders used by the renderer and corpus tests.
target_link_libraries(ghogx_ui_core PUBLIC ghogx_menu_font)
if(MSVC)
    target_compile_options(ghogx_menu_font PRIVATE /W4)
else()
    target_compile_options(ghogx_menu_font PRIVATE -Wall -Wextra)
endif()

# Windowed menu presentation. GH2 menus are 3-D MILO scenes, so this reuses the
# MiloSceneRenderer; WIN32/D3D9-only (its deps are the WIN32 render targets).
add_library(ghogx_ui_render STATIC
    menu_app.cpp menu_app.h
    song_intro_overlay.cpp song_intro_overlay.h
    pss_video_player_win32.cpp pss_video_player_win32.h)
target_include_directories(ghogx_ui_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(ghogx_ui_render PUBLIC
    ghogx_ui_core ghogx_render ghogx_milo_scene ghogx_asset ghogx_menu_font
    ghogx_game ghogx_hud ghogx_character ark_v3 milo)
if(MSVC)
    target_compile_options(ghogx_ui_render PRIVATE /W4)
endif()

# Headless font test against the real stock ARK (skips if the ARK is absent).
add_executable(ghogx_menu_font_test menu_font_test.cpp)
target_link_libraries(ghogx_menu_font_test PRIVATE ghogx_menu_font)
if(MSVC)
    target_compile_options(ghogx_menu_font_test PRIVATE /W4)
endif()
add_test(NAME ghogx_menu_font_test COMMAND ghogx_menu_font_test)

add_executable(ghogx_menu_labels_test menu_labels_test.cpp)
target_link_libraries(ghogx_menu_labels_test PRIVATE ghogx_menu_font ghogx_milo_scene)
if(MSVC)
    target_compile_options(ghogx_menu_labels_test PRIVATE /W4)
endif()
add_test(NAME ghogx_menu_labels_test COMMAND ghogx_menu_labels_test)

add_executable(ghogx_song_intro_overlay_format_test
    song_intro_overlay_format_test.cpp)
target_link_libraries(ghogx_song_intro_overlay_format_test PRIVATE
    ghogx_menu_font ghogx_milo_scene)
if(MSVC)
    target_compile_options(ghogx_song_intro_overlay_format_test PRIVATE /W4)
endif()
add_test(NAME ghogx_song_intro_overlay_format_test
         COMMAND ghogx_song_intro_overlay_format_test)

# Headless boot test against the real stock ARK (skips if the ARK is absent).
add_executable(ghogx_ui_test ui_test.cpp)
target_link_libraries(ghogx_ui_test PRIVATE ghogx_ui_core)
if(MSVC)
    target_compile_options(ghogx_ui_test PRIVATE /W4)
else()
    target_compile_options(ghogx_ui_test PRIVATE -Wall -Wextra)
endif()
add_test(NAME ghogx_ui_test COMMAND ghogx_ui_test)

add_executable(ghogx_soundcheck_calibration_test
    soundcheck_calibration_test.cpp)
target_link_libraries(ghogx_soundcheck_calibration_test PRIVATE ghogx_ui_core)
if(MSVC)
    target_compile_options(ghogx_soundcheck_calibration_test PRIVATE /W4)
else()
    target_compile_options(ghogx_soundcheck_calibration_test PRIVATE
        -Wall -Wextra)
endif()
add_test(NAME ghogx_soundcheck_calibration_test
         COMMAND ghogx_soundcheck_calibration_test)

add_executable(ghogx_soundcheck_profile_migration_test
    soundcheck_profile_migration_test.cpp)
target_link_libraries(ghogx_soundcheck_profile_migration_test PRIVATE
    ghogx_ui_core)
if(MSVC)
    target_compile_options(ghogx_soundcheck_profile_migration_test PRIVATE /W4)
else()
    target_compile_options(ghogx_soundcheck_profile_migration_test PRIVATE
        -Wall -Wextra)
endif()
add_test(NAME ghogx_soundcheck_profile_migration_test
         COMMAND ghogx_soundcheck_profile_migration_test)

add_executable(ghogx_panel_external_dependency_test
    panel_external_dependency_test.cpp)
target_include_directories(ghogx_panel_external_dependency_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
if(MSVC)
    target_compile_options(ghogx_panel_external_dependency_test PRIVATE /W4)
else()
    target_compile_options(ghogx_panel_external_dependency_test PRIVATE
        -Wall -Wextra)
endif()
add_test(NAME ghogx_panel_external_dependency_test
         COMMAND ghogx_panel_external_dependency_test)

add_executable(ghogx_character_variant_catalog_test
    character_variant_catalog_test.cpp)
target_link_libraries(ghogx_character_variant_catalog_test PRIVATE
    ghogx_ui_core ghogx_character)
target_compile_definitions(ghogx_character_variant_catalog_test PRIVATE
    GHOGX_SOURCE_ROOT="${CMAKE_SOURCE_DIR}/..")
if(MSVC)
    target_compile_options(ghogx_character_variant_catalog_test PRIVATE /W4)
else()
    target_compile_options(ghogx_character_variant_catalog_test PRIVATE
        -Wall -Wextra)
endif()
add_test(NAME ghogx_character_variant_catalog_test
         COMMAND ghogx_character_variant_catalog_test)

add_executable(ghogx_addon_preferred_guitar_test
    addon_preferred_guitar_test.cpp)
target_link_libraries(ghogx_addon_preferred_guitar_test PRIVATE ghogx_ui_core)
if(MSVC)
    target_compile_options(ghogx_addon_preferred_guitar_test PRIVATE /W4)
else()
    target_compile_options(ghogx_addon_preferred_guitar_test PRIVATE
        -Wall -Wextra)
endif()
add_test(NAME ghogx_addon_preferred_guitar_test
         COMMAND ghogx_addon_preferred_guitar_test)

add_executable(ghogx_dlc_import_catalog_test dlc_import_catalog_test.cpp)
target_link_libraries(ghogx_dlc_import_catalog_test PRIVATE ghogx_ui_core)
if(MSVC)
    target_compile_options(ghogx_dlc_import_catalog_test PRIVATE /W4)
else()
    target_compile_options(ghogx_dlc_import_catalog_test PRIVATE -Wall -Wextra)
endif()
add_test(NAME ghogx_dlc_import_catalog_test
         COMMAND ghogx_dlc_import_catalog_test)

add_executable(ghogx_band_text_entry_test band_text_entry_test.cpp)
target_link_libraries(ghogx_band_text_entry_test PRIVATE ghogx_ui_core)
if(MSVC)
    target_compile_options(ghogx_band_text_entry_test PRIVATE /W4)
else()
    target_compile_options(ghogx_band_text_entry_test PRIVATE -Wall -Wextra)
endif()

# Deliberately not registered as a passing CTest gate until the release
# persistence findings it reports are closed. The release audit runs its
# write/read modes as separate processes against an isolated profile path.
add_executable(ghogx_release_progression_audit
    release_progression_audit.cpp)
target_link_libraries(ghogx_release_progression_audit PRIVATE ghogx_ui_core)
if(MSVC)
    target_compile_options(ghogx_release_progression_audit PRIVATE /W4)
else()
    target_compile_options(ghogx_release_progression_audit PRIVATE
        -Wall -Wextra)
endif()

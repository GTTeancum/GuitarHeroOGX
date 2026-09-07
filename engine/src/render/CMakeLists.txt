# engine/src/render/CMakeLists.txt
#
# ghogx_render: the PC (D3D9) windowing + render-device layer. Windows-only;
# the OG-Xbox build supplies a D3D8/NV2A sibling later. Header is PIMPL so the
# engine can include it without dragging in windows.h.

add_library(ghogx_render STATIC
    window_d3d9.cpp window_d3d9.h
    scene_d3d9.cpp  scene_d3d9.h
    milo_scene_renderer.cpp milo_scene_renderer.h
    gh1_crowd_regions.cpp gh1_crowd_regions.h
)
target_include_directories(ghogx_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# milo_scene = the Trans/Mat/Mesh decoder; asset = RGBA texture loader (Image).
target_link_libraries(ghogx_render
    PUBLIC d3d9 user32 gdi32 xinput ghogx_milo_scene ghogx_asset)

if(MSVC)
    target_compile_options(ghogx_render PRIVATE /W4)
else()
    target_compile_options(ghogx_render PRIVATE -Wall -Wextra)
endif()

add_executable(ghogx_render_material_uv_test material_uv_sampler_test.cpp)
target_link_libraries(ghogx_render_material_uv_test PRIVATE ghogx_render)
add_test(NAME ghogx_render_material_uv_test
         COMMAND ghogx_render_material_uv_test)

add_executable(ghogx_render_camera_matrix_test camera_matrix_test.cpp)
target_link_libraries(ghogx_render_camera_matrix_test PRIVATE ghogx_render)
add_test(NAME ghogx_render_camera_matrix_test
         COMMAND ghogx_render_camera_matrix_test)

add_executable(ghogx_render_trans_anim_state_test trans_anim_state_test.cpp)
target_link_libraries(ghogx_render_trans_anim_state_test PRIVATE ghogx_render)
add_test(NAME ghogx_render_trans_anim_state_test
         COMMAND ghogx_render_trans_anim_state_test)

add_executable(ghogx_gh1_crowd_regions_test gh1_crowd_regions_test.cpp)
target_link_libraries(ghogx_gh1_crowd_regions_test PRIVATE ghogx_render)
add_test(NAME ghogx_gh1_crowd_regions_test COMMAND ghogx_gh1_crowd_regions_test)

# Read-only numerical oracle: no D3D renderer/window dependency.
add_executable(ghogx_gh1_crowd_region_audit gh1_crowd_region_audit.cpp gh1_crowd_regions.cpp)
target_link_libraries(ghogx_gh1_crowd_region_audit PRIVATE ghogx_milo_scene milo milo_object)

add_executable(ghogx_render_cull_contract_test cull_contract_test.cpp)
target_link_libraries(ghogx_render_cull_contract_test PRIVATE ghogx_render)
add_test(NAME ghogx_render_cull_contract_test
         COMMAND ghogx_render_cull_contract_test)

add_executable(ghogx_render_material_lighting_contract_test
               material_lighting_contract_test.cpp)
target_link_libraries(ghogx_render_material_lighting_contract_test
                      PRIVATE ghogx_render)
add_test(NAME ghogx_render_material_lighting_contract_test
         COMMAND ghogx_render_material_lighting_contract_test)

if(MSVC)
    target_compile_options(ghogx_render_material_uv_test PRIVATE /W4)
    target_compile_options(ghogx_render_camera_matrix_test PRIVATE /W4)
    target_compile_options(ghogx_render_cull_contract_test PRIVATE /W4)
    target_compile_options(ghogx_render_material_lighting_contract_test
                           PRIVATE /W4)
else()
    target_compile_options(ghogx_render_material_uv_test PRIVATE -Wall -Wextra)
    target_compile_options(ghogx_render_camera_matrix_test PRIVATE -Wall -Wextra)
    target_compile_options(ghogx_render_cull_contract_test PRIVATE -Wall -Wextra)
    target_compile_options(ghogx_render_material_lighting_contract_test
                           PRIVATE -Wall -Wextra)
endif()

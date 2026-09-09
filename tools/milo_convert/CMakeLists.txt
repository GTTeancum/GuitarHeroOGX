cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
project(milo_convert LANGUAGES C CXX)
enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/windows_toolchain_guard.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/windows_toolchain_guard.cmake")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_subdirectory(../milo ${CMAKE_CURRENT_BINARY_DIR}/milo)
add_subdirectory(../milo_object ${CMAKE_CURRENT_BINARY_DIR}/milo_object)
add_subdirectory(../ark ${CMAKE_CURRENT_BINARY_DIR}/ark)
add_subdirectory(../acp ${CMAKE_CURRENT_BINARY_DIR}/acp)
add_subdirectory(../acg ${CMAKE_CURRENT_BINARY_DIR}/acg)
add_subdirectory(../acs ${CMAKE_CURRENT_BINARY_DIR}/acs)
add_subdirectory(../dtb ${CMAKE_CURRENT_BINARY_DIR}/dtb)
add_subdirectory(../texture_ps2 ${CMAKE_CURRENT_BINARY_DIR}/texture_ps2)

add_library(milo_convert STATIC
    milo_convert.cpp milo_convert.h
    semantic_field_contract.cpp
    gh1_animation_manifest.cpp gh1_animation_manifest.h
    gh1_character_manifest.cpp gh1_character_manifest.h
    gh1_character_package.cpp gh1_character_package.h
    gh1_character_model_package.cpp gh1_character_model_package.h
    gh1_venue_camera_conversion.cpp gh1_venue_camera_conversion.h
    gh1_venue_placement_conversion.cpp gh1_venue_placement_conversion.h
    gh1_venue_script_conversion.cpp gh1_venue_script_conversion.h
    gh2_face_config_patch.cpp gh2_face_config_patch.h
    singer_face_track.cpp singer_face_track.h
)
target_include_directories(milo_convert PUBLIC .)
target_link_libraries(
    milo_convert PUBLIC
    milo milo_object acp acg acs dtb ps2_texture
)

add_executable(milo_convert_tool milo_convert_tool.cpp)
target_link_libraries(milo_convert_tool PRIVATE milo_convert)
if(COMMAND ghogx_add_windows_import_guard)
    ghogx_add_windows_import_guard(milo_convert_tool)
endif()

add_executable(milo_convert_audit milo_convert_audit.cpp)
target_link_libraries(milo_convert_audit PRIVATE milo_convert ark_v3)
if(COMMAND ghogx_add_windows_import_guard)
    ghogx_add_windows_import_guard(milo_convert_audit)
endif()

add_executable(milo_convert_test milo_convert_test.cpp)
target_link_libraries(milo_convert_test PRIVATE milo_convert)
add_test(NAME milo_convert_test COMMAND milo_convert_test)

add_executable(skin_bind_retarget_test skin_bind_retarget_test.cpp)
target_compile_features(skin_bind_retarget_test PRIVATE cxx_std_17)
add_test(NAME skin_bind_retarget_test COMMAND skin_bind_retarget_test)
if(COMMAND ghogx_add_windows_import_guard)
    ghogx_add_windows_import_guard(milo_convert_test)
endif()

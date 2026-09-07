// Revision-aware semantic MILO object-body readers/writers.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gh::milo_object {

struct LegacyAnimOperation {
    uint32_t type = 0;
    float first = 0.0f;
    float second = 0.0f;
    bool loop = false;
    std::array<int32_t, 3> integers{};
};

struct LegacyAnimatable {
    uint32_t revision = 0;
    std::vector<LegacyAnimOperation> operations;
    std::vector<std::string> objects;
};

struct LegacyAnimSettings {
    float scale = 1.0f;
    float offset = 0.0f;
    float minimum = 0.0f;
    float maximum = 0.0f;
    bool loop = false;

    bool requires_filter() const {
        return scale != 1.0f || offset != 0.0f || minimum != maximum;
    }
};

struct MorphKey {
    float value = 0.0f;
    float frame = 0.0f;
};

struct MorphPose {
    std::string mesh;
    std::vector<MorphKey> keys;
};

struct Morph {
    uint32_t revision = 3;
    LegacyAnimatable animatable;
    std::vector<MorphPose> poses;
    std::string target;
    bool normals = false;
    bool spline = false;
    float intensity = 1.0f;
};

struct LegacyDrawable {
    uint32_t revision = 0;
    bool showing = true;
    std::vector<std::string> objects;
    std::array<float, 4> sphere{};
    float draw_order = 0.0f;
    std::string legacy_target;
};

struct LegacyTransformable {
    uint32_t revision = 8;
    std::array<float, 12> local{};
    std::array<float, 12> world{};
    std::vector<std::string> children;
    uint32_t constraint = 0;
    std::string target;
    bool preserve_scale = false;
    std::string parent;
};

// GH2 revision 9 remaps the legacy revision 8 constraint enum while loading.
uint32_t convert_transformable_constraint8_to_9(uint32_t constraint);

struct Vec3Key {
    std::array<float, 3> value{};
    float frame = 0.0f;
};

struct QuatKey {
    std::array<float, 4> value{};
    float frame = 0.0f;
};

struct Vec2Key {
    std::array<float, 2> value{};
    float frame = 0.0f;
};

struct ColorKey {
    std::array<float, 4> value{};
    float frame = 0.0f;
};

struct TransAnim {
    uint32_t revision = 4;
    LegacyAnimatable animatable;
    LegacyDrawable drawable;
    std::string target;
    std::vector<QuatKey> rotation_keys;
    std::vector<Vec3Key> translation_keys;
    std::string keys_owner;
    bool translation_spline = false;
    bool repeat_translation = false;
    std::vector<Vec3Key> scale_keys;
    bool scale_spline = false;
    bool follow_path = false;
    bool rotation_slerp = false;
};

struct MultiMesh {
    uint32_t revision = 0;
    LegacyDrawable drawable;
    std::string mesh;
    std::vector<std::array<float, 12>> transforms;
};

template <typename T>
struct VectorKey {
    std::vector<T> values;
    float frame = 0.0f;
};

struct MeshAnim {
    uint32_t revision = 0;
    LegacyAnimatable animatable;
    std::string mesh;
    std::vector<VectorKey<std::array<float, 3>>> point_keys;
    std::vector<VectorKey<std::array<float, 2>>> texcoord_keys;
    std::vector<VectorKey<std::array<float, 4>>> color_keys;
    std::string keys_owner;
};

struct CamAnim {
    uint32_t revision = 0;
    LegacyAnimatable animatable;
    std::string camera;
    std::vector<MorphKey> fov_keys;
    std::string keys_owner;
};

struct EnvAnim {
    uint32_t revision = 3;
    LegacyAnimatable animatable;
    std::string environment;
    std::vector<ColorKey> ambient_color_keys;
    std::string keys_owner;
    std::vector<ColorKey> fog_color_keys;
    std::vector<Vec2Key> fog_range_keys;
};

struct LightAnim {
    uint32_t revision = 1;
    LegacyAnimatable animatable;
    std::string light;
    std::vector<ColorKey> color_keys;
    std::string keys_owner;
};

struct ParticleSysAnim {
    uint32_t revision = 2;
    LegacyAnimatable animatable;
    std::string particle_system;
    std::vector<ColorKey> start_color_keys;
    std::vector<ColorKey> end_color_keys;
    std::vector<Vec2Key> emit_rate_keys;
    std::string keys_owner;
    std::vector<Vec2Key> speed_keys;
    std::vector<Vec2Key> life_keys;
    std::vector<Vec2Key> start_size_keys;
};

struct ObjectKey {
    std::string object;
    float frame = 0.0f;
};

struct MatAnimStage {
    std::vector<Vec3Key> translation_keys;
    std::vector<Vec3Key> scale_keys;
    std::vector<Vec3Key> rotation_keys;
    std::vector<ObjectKey> texture_keys;
};

struct MatAnim {
    uint32_t revision = 5;
    LegacyAnimatable animatable;
    std::string material;
    std::vector<MatAnimStage> stages;
    std::string keys_owner;
    std::vector<ColorKey> color_keys;
    std::vector<MorphKey> alpha_keys;
};

struct Text {
    uint32_t revision = 15;
    LegacyDrawable drawable;
    LegacyTransformable transformable;
    std::string font;
    int32_t alignment = 0;
    std::string text;
    std::array<float, 4> color = {1, 1, 1, 1};
    float wrap_width = 0.0f;
    float leading = 1.0f;
    int32_t fixed_length = 0;
    float italics = 0.0f;
    float size = 1.0f;
    bool markup = false;
    int32_t caps_mode = 0;
};

struct Movie {
    uint32_t revision = 6;
    LegacyAnimatable animatable;
    std::string file;
    std::string texture;
    bool stream = false;
    bool loop = true;
};

struct FontKerning {
    uint32_t packed_char_pair = 0;
    float kerning = 0.0f;
};

struct Font {
    uint32_t revision = 7;
    std::string material;
    std::array<float, 2> cell_size{};
    float deprecated_size = 0.0f;
    float base_kerning = 0.0f;
    std::string characters;
    bool has_kerning_table = false;
    std::vector<FontKerning> kerning;
};

struct HmxBitmap {
    uint8_t header_kind = 1;
    uint8_t bits_per_pixel = 0;
    int32_t encoding = 0;
    uint8_t mipmap_count = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t bytes_per_line = 0;
    uint16_t wii_alpha = 0;
    std::array<uint8_t, 17> reserved{};
    std::vector<uint8_t> data;
};

struct Tex {
    uint32_t revision = 8;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bits_per_pixel = 0;
    std::string external_path;
    float mipmap_bias = 0.0f;
    int32_t type = 0;
    bool use_external = false;
    bool has_bitmap = false;
    HmxBitmap bitmap;
};

struct View {
    uint32_t revision = 7;
    LegacyAnimatable animatable;
    LegacyTransformable transformable;
    LegacyDrawable drawable;
    std::string children_owner;
    std::array<float, 2> showing_range{};
};

struct Cam {
    uint32_t revision = 9;
    LegacyTransformable transformable;
    LegacyDrawable drawable;
    float near_plane = 1.0f;
    float far_plane = 1000.0f;
    float fov = 0.5f;
    std::array<float, 4> screen_rect = {0, 0, 1, 1};
    std::array<float, 2> z_range = {0, 1};
    std::string target_texture;
};

struct Flare {
    uint32_t revision = 3;
    LegacyTransformable transformable;
    LegacyDrawable drawable;
    std::string material;
    std::array<float, 2> sizes{};
    std::array<float, 2> range{};
    int32_t steps = 1;
};

struct Light {
    uint32_t revision = 3;
    LegacyTransformable transformable;
    std::array<float, 4> color = {1, 1, 1, 1};
    float range = 0.0f;
    int32_t serialized_type = 0;
};

struct Environ {
    uint32_t revision = 1;
    LegacyDrawable legacy_drawable;
    std::vector<std::string> lights;
    std::array<float, 4> ambient_color = {1, 1, 1, 1};
    std::array<float, 2> fog_range{};
    std::array<float, 4> fog_color = {1, 1, 1, 1};
    bool fog_enabled = false;
};

struct MatTexture {
    uint32_t stage_blend = 0;
    uint32_t tex_gen = 0;
    std::array<float, 12> transform{};
    uint32_t wrap = 0;
    std::string texture;
};

struct Mat {
    uint32_t revision = 21;
    std::vector<MatTexture> textures;
    uint32_t blend = 0;
    std::array<float, 4> color = {1, 1, 1, 1};
    bool use_environment = false;
    bool vertex_ambient = false;
    bool vertex_dynamic = false;
    bool cull = false;
    int32_t multipass = 0;
    bool normalize = false;
    uint32_t z_mode = 0;
    bool alpha_cut = false;
    bool alpha_write = false;
};

struct Particle {
    // GH1 revision 22 uses the pre-RB3 32-byte row: Vector3 position,
    // Color, size. The later row expands position to Vector4.
    std::array<float, 3> position{};
    std::array<float, 4> color{};
    float size = 0.0f;
};

struct ParticleSys {
    uint32_t revision = 22;
    LegacyAnimatable animatable;
    LegacyTransformable transformable;
    LegacyDrawable drawable;
    std::array<float, 2> life{};
    std::array<float, 3> box_extent_1{};
    std::array<float, 3> box_extent_2{};
    std::array<float, 2> speed{};
    std::array<float, 2> pitch{};
    std::array<float, 2> yaw{};
    std::array<float, 2> emit_rate{};
    std::array<float, 2> start_size{};
    std::array<float, 2> delta_size{};
    std::array<float, 4> start_color_low{};
    std::array<float, 4> start_color_high{};
    std::array<float, 4> end_color_low{};
    std::array<float, 4> end_color_high{};
    bool bounce_enabled = false;
    std::array<float, 4> bounce_plane{};
    std::array<float, 3> force_direction{};
    std::string material;
    uint32_t type = 0;
    float grow_ratio = 0.0f;
    float shrink_ratio = 0.0f;
    float mid_color_ratio = 0.0f;
    std::array<float, 4> mid_color_low{};
    std::array<float, 4> mid_color_high{};
    uint32_t max_particles = 0;
    std::array<float, 2> bubble_period{};
    std::array<float, 2> bubble_size{};
    bool bubble = false;
    float relative_motion = 0.0f;
    std::string emitter_mesh;
    bool preserve_particles = false;
    std::vector<Particle> particles;
};

struct MeshVertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
    // Revision 25 serializes Hmx::Color32 through Hmx::Color as four floats.
    // For skinned meshes PostLoad transfers these channels to bone weights.
    std::array<float, 4> color_or_weights{};
    std::array<float, 2> uv{};
};

struct MeshBoneSlot {
    std::string bone;
    std::array<float, 12> offset{};
};

struct MeshStripResult {
    std::vector<uint32_t> cumulative_strip_lengths;
    std::vector<uint16_t> strip_runs;
};

struct Mesh {
    uint32_t revision = 25;
    LegacyTransformable transformable;
    LegacyDrawable drawable;
    std::string material;
    std::string geometry_owner;
    uint32_t mutable_flags = 0;
    uint32_t volume = 0;
    bool has_bsp_tree = false;
    std::vector<MeshVertex> vertices;
    std::vector<std::array<uint16_t, 3>> faces;
    std::vector<uint8_t> patches;
    bool has_bones = false;
    std::array<MeshBoneSlot, 4> bone_slots;
    // GH1 PS2 revision 25 stores one platform strip cache per patch.
    std::vector<MeshStripResult> strip_results;
};

struct TypePropertyNode {
    uint32_t type = 0;
    uint32_t integer = 0;
    float floating = 0.0f;
    std::string symbol;
    uint32_t array_id = 0;
    std::vector<TypePropertyNode> children;
};

struct ObjectFields0 {
    uint32_t revision = 0;
    std::string type;
    bool has_type_properties = false;
    uint32_t type_property_id = 0;
    std::vector<TypePropertyNode> type_properties;
};

struct Tex10 {
    uint32_t revision = 10;
    ObjectFields0 object_fields;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bits_per_pixel = 0;
    std::string external_path;
    float mipmap_bias = 0.0f;
    int32_t type = 0;
    bool use_external = false;
    bool has_bitmap = false;
    HmxBitmap bitmap;
};

struct Mat27 {
    uint32_t revision = 27;
    ObjectFields0 object_fields;
    int32_t blend = 1;
    std::array<float, 4> color = {1, 1, 1, 1};
    bool use_environment = true;
    bool prelit = false;
    int32_t z_mode = 1;
    bool alpha_cut = false;
    bool alpha_write = false;
    int32_t tex_gen = 0;
    int32_t tex_wrap = 1;
    std::array<float, 12> texture_transform =
        {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    std::string diffuse_texture;
    std::string next_pass;
    bool intensify = false;
    bool cull = true;
    float emissive_multiplier = 1.0f;
    std::array<float, 3> specular_rgb{};
    float specular_power = 0.0f;
    std::string normal_map;
    std::string emissive_map;
    std::string specular_map;
    std::string legacy_unknown_map;
    std::string environment_map;
    bool per_pixel_lit = false;
    bool legacy_unknown_bool = false;
    bool has_fur_field = false;
    std::string fur;
};

struct ConvertedMatPass {
    std::string name;
    Mat27 material;
};

struct Transformable9 {
    uint32_t revision = 9;
    std::array<float, 12> local{};
    std::array<float, 12> world{};
    uint32_t constraint = 0;
    std::string target;
    bool preserve_scale = false;
    std::string parent;
};

struct Trans9 {
    uint32_t revision = 9;
    ObjectFields0 object_fields;
    std::array<float, 12> local =
        {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    std::array<float, 12> world =
        {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    uint32_t constraint = 0;
    std::string target;
    bool preserve_scale = false;
    std::string parent;
};

struct Animatable4 {
    uint32_t revision = 4;
    float frame = 0.0f;
    int32_t rate = 0;
};

struct Movie8 {
    uint32_t revision = 8;
    ObjectFields0 object_fields;
    Animatable4 animatable;
    std::string file;
    std::string texture;
    bool stream = false;
    bool loop = true;
};

struct Morph4 {
    uint32_t revision = 4;
    ObjectFields0 object_fields;
    Animatable4 animatable;
    std::vector<MorphPose> poses;
    std::string target;
    bool normals = false;
    bool spline = false;
    float intensity = 1.0f;
};

struct AnimFilter1 {
    uint32_t revision = 1;
    ObjectFields0 object_fields;
    Animatable4 animatable;
    std::string anim;
    float scale = 1.0f;
    float offset = 0.0f;
    float start = 0.0f;
    float end = 0.0f;
    int32_t type = 0;
    float period = 0.0f;
};

struct Drawable3 {
    uint32_t revision = 3;
    bool showing = true;
    std::array<float, 4> sphere{};
    float draw_order = 0.0f;
};

struct Waypoint3 {
    uint32_t revision = 3;
    ObjectFields0 object_fields;
    Drawable3 legacy_drawable;
    Transformable9 transformable;
    uint32_t flags = 0;
    std::vector<std::string> connections;
    float radius = 12.0f;
    float y_radius = 0.0f;
    float angle_radius = 0.0f;
};

struct MultiMesh1 {
    uint32_t revision = 1;
    ObjectFields0 object_fields;
    Drawable3 drawable;
    std::string mesh;
    std::vector<std::array<float, 12>> transforms;
};

struct Group12 {
    uint32_t revision = 12;
    ObjectFields0 object_fields;
    Animatable4 animatable;
    Transformable9 transformable;
    Drawable3 drawable;
    std::vector<std::string> objects;
    std::string environment;
    std::string draw_only;
    std::string lod;
    float lod_screen_size = 0.0f;
};

struct ResolvedObjectReference {
    std::string name;
    std::string type;
};

struct ResolvedViewGraph {
    std::vector<ResolvedObjectReference> animation_objects;
    std::vector<ResolvedObjectReference> drawable_objects;
};

struct ResolvedViewEnvironmentSegment {
    std::string environment;
    std::vector<ResolvedObjectReference> drawable_objects;
};

struct ObjectDirViewport16 {
    std::array<float, 12> transform{};
    int32_t legacy_value = 0;
};

struct ObjectDir16 {
    uint32_t revision = 16;
    ObjectFields0 object_fields;
    std::vector<ObjectDirViewport16> viewports;
    uint32_t current_viewport = 0;
    std::string proxy_path;
    std::vector<std::string> subdirectories;
    std::string legacy_string_5;
    std::string legacy_string;
    std::string legacy_camera;
};

struct RndDir8 {
    uint32_t revision = 8;
    ObjectDir16 object_directory;
    Animatable4 animatable;
    Drawable3 drawable;
    Transformable9 transformable;
    std::string environment;
    std::string test_event;
    std::string legacy_symbol_1;
    std::string legacy_symbol_2;
};

struct PanelDir2 {
    uint32_t revision = 2;
    RndDir8 render_directory;
    std::string camera;
    std::string test_event;
};

struct WorldDir11 {
    uint32_t revision = 11;
    uint32_t legacy_value = 0;
    float legacy_float = 0.0f;
    std::string fake_hud_filename;
    PanelDir2 panel_directory;
    std::array<float, 12> legacy_transform{};
};

struct CharacterLod9 {
    float screen_size = 0.0f;
    std::string group;
};

struct Character9 {
    uint32_t revision = 9;
    RndDir8 render_directory;
    std::vector<CharacterLod9> lods;
    std::string shadow;
    bool self_shadow = false;
    std::string sphere_base;
};

struct BandCharacter1 {
    uint32_t revision = 1;
    Character9 character;
};

struct CharWeightable2 {
    uint32_t revision = 2;
    float weight = 1.0f;
    std::string weight_owner;
};

struct CharDriver3 {
    uint32_t revision = 3;
    ObjectFields0 object_fields;
    CharWeightable2 weightable;
    std::string bones;
    std::string clips;
    bool realign = false;
};

struct CharDriverMidi3 {
    uint32_t revision = 3;
    CharDriver3 driver;
    std::string default_clip;
};

struct CharWeightSetter2 {
    uint32_t revision = 2;
    ObjectFields0 object_fields;
    CharWeightable2 weightable;
    std::string driver;
    uint32_t flags = 0;
};

struct CharIKHand2 {
    uint32_t revision = 2;
    ObjectFields0 object_fields;
    CharWeightable2 weightable;
    std::string hand;
    std::string target;
    bool orientation = false;
    bool stretch = false;
    bool scalable = false;
};

struct CharIKMidi4 {
    uint32_t revision = 4;
    ObjectFields0 object_fields;
    std::string bone;
};

struct CharIKRod2 {
    uint32_t revision = 2;
    ObjectFields0 object_fields;
    std::string left_end;
    std::string right_end;
    float dest_pos = 0.5f;
    std::string side_axis;
    bool vertical = false;
    std::string dest;
    std::array<float, 12> transform{};
};

struct CharHairPoint2 {
    std::array<float, 3> position{};
    std::string bone;
    float length = 0.0f;
    int32_t legacy_value = 0;
    std::string legacy_name;
    float radius = 0.0f;
    float outer_radius = 0.0f;
};

struct CharHairStrand2 {
    std::string root;
    float angle = 0.0f;
    std::vector<CharHairPoint2> points;
    std::array<float, 9> base_matrix{};
    std::array<float, 9> root_matrix{};
};

struct CharHair2 {
    uint32_t revision = 2;
    ObjectFields0 object_fields;
    float stiffness = 0.0f;
    float torsion = 0.0f;
    float inertia = 0.0f;
    float gravity = 0.0f;
    float weight = 0.0f;
    float friction = 0.0f;
    std::vector<CharHairStrand2> strands;
    bool simulate = false;
};

struct FaceFxLipSyncServoTarget5 {
    std::string object;
    int32_t property_type = 0;
    std::string property;
};

struct FaceFxLipSyncServo5 {
    uint32_t revision = 5;
    ObjectFields0 object_fields;
    CharWeightable2 weightable;
    std::string facefx_path;
    std::string viseme_milo;
    std::vector<FaceFxLipSyncServoTarget5> targets;
};

struct EventTriggerAnim8 {
    std::string animation;
    float blend = 0.0f;
    bool wait = false;
    float delay = 0.0f;
};

struct EventTriggerProxyCall8 {
    std::string proxy;
    std::string call;
};

struct EventTrigger8 {
    uint32_t revision = 8;
    ObjectFields0 object_fields;
    std::string trigger_event;
    std::vector<EventTriggerAnim8> animations;
    std::vector<std::string> sounds;
    std::vector<std::string> shows;
    std::vector<std::string> legacy_hides;
    std::vector<std::string> enable_events;
    std::vector<std::string> disable_events;
    std::vector<std::string> wait_for_events;
    std::string next_link;
    std::vector<EventTriggerProxyCall8> proxy_calls;
};

struct WorldFx1 {
    uint32_t revision = 1;
    RndDir8 render_directory;
};

struct OutfitLoaderOutfit1 {
    uint8_t hide = 0;
    uint8_t desire = 0;
    uint8_t exclude = 0;
};

struct OutfitLoaderCategory1 {
    uint8_t selected = 0;
    uint8_t shown = 0;
    std::vector<OutfitLoaderOutfit1> outfits;
};

struct OutfitLoader1 {
    uint32_t revision = 1;
    ObjectFields0 object_fields;
    std::string directory;
    std::vector<OutfitLoaderCategory1> categories;
};

struct CharLookAt2 {
    uint32_t revision = 2;
    ObjectFields0 object_fields;
    CharWeightable2 weightable;
    std::string source;
    std::string pivot;
    std::string target;
    float half_time = 0.0f;
    float min_yaw = 0.0f;
    float max_yaw = 0.0f;
    float min_pitch = 0.0f;
    float max_pitch = 0.0f;
    float min_weight_yaw = -1.0f;
    float max_weight_yaw = -1.0f;
    float weight_yaw_speed = 10000.0f;
};

struct CharEyes3 {
    uint32_t revision = 3;
    ObjectFields0 object_fields;
    std::vector<std::string> eyes;
    std::string legacy_transform;
};

struct CharWalk1 {
    uint32_t revision = 1;
    ObjectFields0 object_fields;
};

struct CharServoBone2 {
    uint32_t revision = 1;
    ObjectFields0 object_fields;
    std::string clip_type;
};

struct CharUpperTwist1 {
    uint32_t revision = 1;
    ObjectFields0 object_fields;
    std::string upper_arm;
    std::string twist1;
    std::string twist2;
};

struct CharForeTwist4 {
    uint32_t revision = 1;
    ObjectFields0 object_fields;
    float offset = 0.0f;
    std::string hand;
    std::string twist2;
    int32_t legacy_revision2_value = 0;
    float bias = 0.0f;
};

struct CharPosConstraint2 {
    uint32_t revision = 2;
    ObjectFields0 object_fields;
    std::vector<std::string> targets;
    std::string source;
    std::array<float, 3> box_min = {1, 1, 1};
    std::array<float, 3> box_max = {-1, -1, -1};
};

struct CharClipPointer14 {
    std::string clip;
    uint32_t flags = 0;
    uint32_t size_bytes = 0;
};

struct CharClipSet14 {
    uint32_t revision = 14;
    ObjectDir16 object_directory;
    float blend_width = 0.0f;
    uint32_t play_flags = 0;
    std::vector<CharClipPointer14> clips;
    bool move_self = false;
    std::vector<std::string> recenter_targets;
    std::vector<std::string> recenter_average;
    bool recenter_slide = false;
    std::string legacy_type;
    int32_t legacy_type_version = -1;
};

struct CharClipTransitionNode5 {
    float current_beat = 0.0f;
    float next_beat = 0.0f;
};

struct CharClipTransition5 {
    std::string clip;
    std::vector<CharClipTransitionNode5> nodes;
};

struct CharClipFrameEvent5 {
    float frame = 0.0f;
    std::string script;
};

struct CharBonesSamples10 {
    std::vector<std::string> channels;
    std::array<uint32_t, 10> counts{};
    uint32_t compression = 0;
    uint32_t sample_count = 0;
    std::vector<uint8_t> sample_bytes;
};

struct CharClipSamples10 {
    uint32_t revision = 10;
    uint32_t char_clip_revision = 5;
    ObjectFields0 object_fields;
    float start_beat = 0.0f;
    float end_beat = 0.0f;
    float beats_per_second = 0.0f;
    uint32_t flags = 0;
    uint32_t play_flags = 0;
    float blend_width = 0.0f;
    float range = 0.0f;
    bool legacy_flag = false;
    std::vector<CharClipTransition5> transitions;
    std::string legacy_enter_event;
    std::string legacy_exit_event;
    std::vector<CharClipFrameEvent5> events;
    CharBonesSamples10 full;
    CharBonesSamples10 one;
    // Revisions 8-12 serialize this legacy header but load no corresponding
    // data block. GH2's loader discards it; retail headers have no channels.
    CharBonesSamples10 duplicate;
};

struct CharBone2 {
    uint32_t revision = 2;
    ObjectFields0 object_fields;
    Transformable9 legacy_transform;
    bool position_context = false;
    bool scale_context = false;
    int32_t rotation = 9;
    int32_t legacy_rotation = 9;
};

struct CharClipFilter0 {
    ObjectFields0 object_fields;
};

struct CharClipGroup1 {
    uint32_t revision = 1;
    ObjectFields0 object_fields;
    std::vector<std::string> clips;
    int32_t which = 0;
};

struct Cam12 {
    uint32_t revision = 12;
    ObjectFields0 object_fields;
    Transformable9 transformable;
    float near_plane = 1.0f;
    float far_plane = 1000.0f;
    float y_fov = 0.5f;
    std::array<float, 4> screen_rect = {0, 0, 1, 1};
    std::array<float, 2> z_range = {0, 1};
    std::string target_texture;
};

struct Flare4 {
    uint32_t revision = 4;
    ObjectFields0 object_fields;
    Transformable9 transformable;
    Drawable3 drawable;
    std::string material;
    std::array<float, 2> sizes{};
    std::array<float, 2> range{};
    int32_t steps = 1;
};

struct Light6 {
    uint32_t revision = 6;
    ObjectFields0 object_fields;
    Transformable9 transformable;
    std::array<float, 4> color = {1, 1, 1, 1};
    float range = 1000.0f;
    int32_t serialized_type = 0;
    bool animate_color_from_preset = true;
    bool animate_position_from_preset = true;
};

struct Environ5 {
    uint32_t revision = 5;
    ObjectFields0 object_fields;
    std::vector<std::string> lights;
    std::array<float, 4> ambient_color = {1, 1, 1, 1};
    std::array<float, 2> fog_range = {0, 1};
    std::array<float, 4> fog_color = {1, 1, 1, 1};
    bool fog_enabled = false;
    bool animate_from_preset = true;
    bool fade_out = false;
    float fade_start = 0.0f;
    float fade_end = 1000.0f;
};

struct CamAnim2 {
    uint32_t revision = 2;
    ObjectFields0 object_fields;
    Animatable4 animatable;
    std::string camera;
    std::vector<MorphKey> fov_keys;
    std::string keys_owner;
};

struct EnvAnim4 {
    uint32_t revision = 4;
    ObjectFields0 object_fields;
    Animatable4 animatable;
    std::string environment;
    std::vector<ColorKey> ambient_color_keys;
    std::string keys_owner;
    std::vector<ColorKey> fog_color_keys;
    std::vector<Vec2Key> fog_range_keys;
};

struct LightAnim2 {
    uint32_t revision = 2;
    ObjectFields0 object_fields;
    Animatable4 animatable;
    std::string light;
    std::vector<ColorKey> color_keys;
    std::string keys_owner;
};

struct ParticleSysAnim3 {
    uint32_t revision = 3;
    ObjectFields0 object_fields;
    Animatable4 animatable;
    std::string particle_system;
    std::vector<ColorKey> start_color_keys;
    std::vector<ColorKey> end_color_keys;
    std::vector<Vec2Key> emit_rate_keys;
    std::string keys_owner;
    std::vector<Vec2Key> speed_keys;
    std::vector<Vec2Key> life_keys;
    std::vector<Vec2Key> start_size_keys;
};

struct MeshAnim1 {
    uint32_t revision = 1;
    ObjectFields0 object_fields;
    Animatable4 animatable;
    std::string mesh;
    std::vector<VectorKey<std::array<float, 3>>> point_keys;
    std::vector<VectorKey<std::array<float, 2>>> texcoord_keys;
    std::vector<VectorKey<std::array<float, 4>>> color_keys;
    std::string keys_owner;
};

struct MatAnim7 {
    uint32_t revision = 7;
    ObjectFields0 object_fields;
    Animatable4 animatable;
    std::string material;
    std::string keys_owner;
    std::vector<ColorKey> color_keys;
    std::vector<MorphKey> alpha_keys;
    std::vector<Vec3Key> translation_keys;
    std::vector<Vec3Key> scale_keys;
    std::vector<Vec3Key> rotation_keys;
    std::vector<ObjectKey> texture_keys;
};

struct ConvertedMatAnimPass {
    std::string name;
    MatAnim7 animation;
};

struct TransAnim6 {
    uint32_t revision = 6;
    ObjectFields0 object_fields;
    Animatable4 animatable;
    std::string target;
    std::vector<QuatKey> rotation_keys;
    std::vector<Vec3Key> translation_keys;
    std::string keys_owner;
    bool translation_spline = false;
    bool repeat_translation = false;
    std::vector<Vec3Key> scale_keys;
    bool scale_spline = false;
    bool follow_path = false;
    bool rotation_slerp = false;
};

struct CamShotSubPart20 {
    int32_t legacy_unknown = 0;
    std::string object;
    std::string part;
};

struct CamShotFrame20 {
    float duration = 0.0f;
    float blend = 0.0f;
    float blend_ease = 0.0f;
    float field_of_view = 1.22173048f;
    std::array<float, 12> world_offset{};
    std::array<float, 2> screen_offset{};
    float blur_depth = 0.0f;
    int32_t legacy_blur = 0;
    int32_t legacy_focus = 0;
    std::vector<CamShotSubPart20> targets;
    CamShotSubPart20 parent;
    bool use_parent_rotation = false;
    float shake_noise_amplitude = 0.0f;
    float shake_noise_frequency = 0.0f;
    std::array<float, 2> maximum_angular_offset{};
};

struct CamShot20 {
    uint32_t revision = 20;
    ObjectFields0 object_fields;
    Animatable4 animatable;
    std::vector<CamShotFrame20> keyframes;
    bool looping = false;
    // Historical API name: GH2 path_ease (+54), Save2643E4 / property26873C.
    float legacy_loop_frame = 0.0f;
    float near_plane = 10.0f;
    float far_plane = 10000.0f;
    bool use_depth_of_field = false;
    float filter = 0.0f;
    float clamp_height = -1.0f;
    std::string path;
    // Historical API name: GH2 fade_time (+3C), property26850C; not path position.
    float legacy_path_frame = 0.0f;
    std::string category;
    // Historical API name. GH2 uses this as CamShot selection weight (+44).
    float legacy_category_frame = 0.0f;
    std::vector<std::array<int32_t, 2>> legacy_crowd_pairs;
    int32_t legacy_crowd_modify_stamp = -1;
    std::vector<std::string> hide_list;
    std::string legacy_crowd;
    std::string glow_spot;
};

struct Text17 {
    uint32_t revision = 17;
    ObjectFields0 object_fields;
    Drawable3 drawable;
    Transformable9 transformable;
    std::string font;
    int32_t alignment = 0;
    std::string text;
    std::array<float, 4> color = {1, 1, 1, 1};
    float wrap_width = 0.0f;
    float leading = 1.0f;
    int32_t fixed_length = 0;
    float italics = 0.0f;
    float size = 1.0f;
    bool markup = false;
    int32_t caps_mode = 0;
};

struct Particle27 {
    std::array<float, 3> position{};
    std::array<float, 4> color{};
    float size = 0.0f;
};

struct ParticleSys27 {
    uint32_t revision = 27;
    ObjectFields0 object_fields;
    Animatable4 animatable;
    Transformable9 transformable;
    Drawable3 drawable;
    std::array<float, 2> life{};
    std::array<float, 3> box_extent_1{};
    std::array<float, 3> box_extent_2{};
    std::array<float, 2> speed{};
    std::array<float, 2> pitch{};
    std::array<float, 2> yaw{};
    std::array<float, 2> emit_rate{};
    std::array<float, 2> start_size{};
    std::array<float, 2> delta_size{};
    std::array<float, 4> start_color_low{};
    std::array<float, 4> start_color_high{};
    std::array<float, 4> end_color_low{};
    std::array<float, 4> end_color_high{};
    std::string bounce;
    std::array<float, 3> force_direction{};
    std::string material;
    uint32_t type = 0;
    float grow_ratio = 0.0f;
    float shrink_ratio = 0.0f;
    float mid_color_ratio = 0.0f;
    std::array<float, 4> mid_color_low{};
    std::array<float, 4> mid_color_high{};
    uint32_t max_particles = 0;
    std::array<float, 2> bubble_period{};
    std::array<float, 2> bubble_size{};
    bool bubble = false;
    float relative_motion = 0.0f;
    std::string relative_parent;
    std::string emitter_mesh;
    bool preserve_particles = false;
    std::vector<Particle27> particles;
};

struct FontCharInfo15 {
    float texture_u = 0.0f;
    float texture_v = 0.0f;
    float character_width = 0.0f;
    float character_advance = 0.0f;
};

struct Font15 {
    uint32_t revision = 15;
    ObjectFields0 object_fields;
    std::string material;
    std::array<float, 2> cell_size{};
    float deprecated_size = 0.0f;
    float base_kerning = 0.0f;
    std::string characters;
    bool has_kerning_table = false;
    std::vector<FontKerning> kerning;
    std::string texture_owner;
    bool monospace = false;
    bool packed = false;
    int32_t bitmap_width = 0;
    int32_t bitmap_height = 0;
    std::array<float, 2> texture_cell_size{};
    std::array<FontCharInfo15, 256> character_info{};
};

struct MeshBspNode {
    bool has_value = false;
    std::array<float, 4> plane{};
    uint32_t left = UINT32_MAX;
    uint32_t right = UINT32_MAX;
};

struct Mesh28 {
    uint32_t revision = 28;
    ObjectFields0 object_fields;
    Transformable9 transformable;
    Drawable3 drawable;
    std::string material;
    std::string geometry_owner;
    uint32_t mutable_flags = 0;
    uint32_t volume = 0;
    std::vector<MeshBspNode> bsp_nodes;
    std::vector<MeshVertex> vertices;
    std::vector<std::array<uint16_t, 3>> faces;
    std::vector<uint8_t> group_sizes;
    bool has_bones = false;
    std::array<MeshBoneSlot, 4> bone_slots;
    std::vector<MeshStripResult> group_sections;
};

Morph parse_morph(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_morph(const Morph& morph);
TransAnim parse_trans_anim(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_trans_anim(const TransAnim& anim);
MultiMesh parse_multi_mesh(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_multi_mesh(const MultiMesh& multi_mesh);
MeshAnim parse_mesh_anim(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_mesh_anim(const MeshAnim& anim);
CamAnim parse_cam_anim(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_cam_anim(const CamAnim& anim);
EnvAnim parse_env_anim(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_env_anim(const EnvAnim& anim);
LightAnim parse_light_anim(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_light_anim(const LightAnim& anim);
ParticleSysAnim parse_particle_sys_anim(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_particle_sys_anim(
    const ParticleSysAnim& anim);
MatAnim parse_mat_anim(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_mat_anim(const MatAnim& anim);
Text parse_text(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_text(const Text& text);
Movie parse_movie(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_movie(const Movie& movie);
Font parse_font(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_font(const Font& font);
Tex parse_tex(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_tex(const Tex& tex);
View parse_view(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_view(const View& view);
Cam parse_cam(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_cam(const Cam& cam);
Flare parse_flare(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_flare(const Flare& flare);
Light parse_light(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_light(const Light& light);
Environ parse_environ(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_environ(const Environ& environment);
Mat parse_mat(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_mat(const Mat& mat);
ParticleSys parse_particle_sys(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_particle_sys(const ParticleSys& particles);
Mesh parse_mesh(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_mesh(const Mesh& mesh);
Mesh28 parse_mesh28(const std::vector<uint8_t>& bytes,
                    uint32_t parent_directory_revision = 24);
std::vector<uint8_t> serialize_mesh28(
    const Mesh28& mesh, uint32_t parent_directory_revision = 24);
Mesh28 convert_mesh25_to_mesh28(const Mesh& source);
Tex10 parse_tex10(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_tex10(const Tex10& tex);
Tex10 convert_tex8_to_tex10(const Tex& source);
Mat27 parse_mat27(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_mat27(const Mat27& mat);
std::vector<ConvertedMatPass> convert_mat21_to_mat27_passes(
    const Mat& source, const std::string& source_name);
Cam12 parse_cam12(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_cam12(const Cam12& cam);
Cam12 convert_cam9_to_cam12(const Cam& source);
Flare4 parse_flare4(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_flare4(const Flare4& flare);
Flare4 convert_flare3_to_flare4(const Flare& source);
Light6 parse_light6(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_light6(const Light6& light);
Light6 convert_light3_to_light6(const Light& source);
Environ5 parse_environ5(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_environ5(const Environ5& environment);
Environ5 convert_environ1_to_environ5(const Environ& source);
CamAnim2 parse_cam_anim2(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_cam_anim2(const CamAnim2& anim);
CamAnim2 convert_cam_anim0_to_cam_anim2(const CamAnim& source);
EnvAnim4 parse_env_anim4(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_env_anim4(const EnvAnim4& anim);
EnvAnim4 convert_env_anim3_to_env_anim4(const EnvAnim& source);
LightAnim2 parse_light_anim2(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_light_anim2(const LightAnim2& anim);
LightAnim2 convert_light_anim1_to_light_anim2(const LightAnim& source);
ParticleSysAnim3 parse_particle_sys_anim3(
    const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_particle_sys_anim3(
    const ParticleSysAnim3& anim);
ParticleSysAnim3 convert_particle_sys_anim2_to_particle_sys_anim3(
    const ParticleSysAnim& source);
MeshAnim1 parse_mesh_anim1(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_mesh_anim1(const MeshAnim1& anim);
MeshAnim1 convert_mesh_anim0_to_mesh_anim1(const MeshAnim& source);
MatAnim7 parse_mat_anim7(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_mat_anim7(const MatAnim7& anim);
MatAnim7 convert_mat_anim5_to_mat_anim7(const MatAnim& source);
std::vector<ConvertedMatAnimPass> convert_mat_anim5_to_mat_anim7_passes(
    const MatAnim& source, const std::string& source_name);
TransAnim6 parse_trans_anim6(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_trans_anim6(const TransAnim6& anim);
TransAnim6 convert_trans_anim4_to_trans_anim6(const TransAnim& source);
CamShot20 parse_cam_shot20(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_cam_shot20(const CamShot20& shot);
MultiMesh1 parse_multi_mesh1(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_multi_mesh1(const MultiMesh1& multi_mesh);
MultiMesh1 convert_multi_mesh0_to_multi_mesh1(const MultiMesh& source);
Movie8 parse_movie8(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_movie8(const Movie8& movie);
Movie8 convert_movie6_to_movie8(const Movie& source);
Morph4 parse_morph4(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_morph4(const Morph4& morph);
Morph4 convert_morph3_to_morph4(const Morph& source);
Text17 parse_text17(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_text17(const Text17& text);
Text17 convert_text15_to_text17(const Text& source);
ParticleSys27 parse_particle_sys27(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_particle_sys27(
    const ParticleSys27& particles);
ParticleSys27 convert_particle_sys22_to_particle_sys27(
    const ParticleSys& source, const std::string& bounce_name = {});
Trans9 parse_trans9(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_trans9(const Trans9& trans);
Waypoint3 parse_waypoint3(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_waypoint3(const Waypoint3& waypoint);
Trans9 convert_bounce_plane_to_trans9(
    const std::array<float, 4>& plane);
Font15 parse_font15(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_font15(const Font15& font);
Font15 convert_font7_to_font15(
    const Font& source, const std::string& source_name,
    uint32_t bitmap_width, uint32_t bitmap_height,
    const std::vector<uint8_t>& bitmap_rgba);
LegacyAnimSettings reduce_legacy_animatable(
    const LegacyAnimatable& source);
AnimFilter1 parse_anim_filter1(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_anim_filter1(const AnimFilter1& filter);
AnimFilter1 convert_legacy_animatable_to_anim_filter1(
    const LegacyAnimatable& source, const std::string& anim);
Group12 parse_group12(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_group12(const Group12& group);
std::vector<ResolvedViewEnvironmentSegment>
resolve_view_environment_segments(
    const std::vector<ResolvedObjectReference>& drawable_objects);
Group12 convert_view7_to_group12(
    const View& source, const ResolvedViewGraph& effective_graph);
ObjectDir16 parse_object_dir16(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_object_dir16(
    const ObjectDir16& directory);
RndDir8 parse_rnd_dir8(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_rnd_dir8(const RndDir8& directory);
PanelDir2 parse_panel_dir2(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_panel_dir2(
    const PanelDir2& directory);
WorldDir11 parse_world_dir11(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_world_dir11(
    const WorldDir11& directory);
Character9 parse_character9(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_character9(
    const Character9& character);
BandCharacter1 parse_band_character1(
    const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_band_character1(
    const BandCharacter1& character);
CharDriver3 parse_char_driver3(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_char_driver3(
    const CharDriver3& driver);
CharDriverMidi3 parse_char_driver_midi3(
    const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_char_driver_midi3(
    const CharDriverMidi3& driver);
CharWeightSetter2 parse_char_weight_setter2(
    const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_char_weight_setter2(
    const CharWeightSetter2& setter);
CharIKHand2 parse_char_ik_hand2(
    const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_char_ik_hand2(
    const CharIKHand2& hand);
CharIKMidi4 parse_char_ik_midi4(
    const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_char_ik_midi4(
    const CharIKMidi4& midi);
CharIKRod2 parse_char_ik_rod2(
    const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_char_ik_rod2(
    const CharIKRod2& rod);
CharHair2 parse_char_hair2(
    const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_char_hair2(
    const CharHair2& hair);
FaceFxLipSyncServo5 parse_facefx_lip_sync_servo5(
    const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_facefx_lip_sync_servo5(
    const FaceFxLipSyncServo5& servo);
EventTrigger8 parse_event_trigger8(
    const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_event_trigger8(
    const EventTrigger8& trigger);
WorldFx1 parse_world_fx1(
    const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_world_fx1(
    const WorldFx1& world_fx);
OutfitLoader1 parse_outfit_loader1(
    const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_outfit_loader1(
    const OutfitLoader1& loader);
CharLookAt2 parse_char_look_at2(
    const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_char_look_at2(
    const CharLookAt2& look_at);
CharEyes3 parse_char_eyes3(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_char_eyes3(
    const CharEyes3& eyes);
CharWalk1 parse_char_walk1(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_char_walk1(
    const CharWalk1& walk);
CharServoBone2 parse_char_servo_bone2(
    const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_char_servo_bone2(
    const CharServoBone2& servo);
CharUpperTwist1 parse_char_upper_twist1(
    const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_char_upper_twist1(
    const CharUpperTwist1& twist);
CharForeTwist4 parse_char_fore_twist4(
    const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_char_fore_twist4(
    const CharForeTwist4& twist);
CharPosConstraint2 parse_char_pos_constraint2(
    const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_char_pos_constraint2(
    const CharPosConstraint2& constraint);
CharClipSet14 parse_char_clip_set14(
    const std::vector<uint8_t>& bytes, uint32_t clip_count);
std::vector<uint8_t> serialize_char_clip_set14(
    const CharClipSet14& clips);
CharClipSamples10 parse_char_clip_samples10(
    const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_char_clip_samples10(
    const CharClipSamples10& clip);
size_t char_bones_samples10_ps2_allocate_size(
    const CharBonesSamples10& samples);
size_t char_clip_samples10_ps2_allocate_size(
    const CharClipSamples10& clip);
CharBone2 parse_char_bone2(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_char_bone2(const CharBone2& bone);
CharClipFilter0 parse_char_clip_filter0(
    const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_char_clip_filter0(
    const CharClipFilter0& filter);
CharClipGroup1 parse_char_clip_group1(
    const std::vector<uint8_t>& bytes);
std::vector<uint8_t> serialize_char_clip_group1(
    const CharClipGroup1& group);
std::vector<uint8_t> round_trip_gh2_object_body(
    const std::string& type, const std::vector<uint8_t>& bytes,
    uint32_t parent_directory_revision = 24);

}  // namespace gh::milo_object

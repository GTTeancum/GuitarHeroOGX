#include "character/char_bones_output.h"
#include "character/char_servo.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

using namespace ghogx::character;
using ghogx::milo_scene::Xfm;
using Type = Gh2BoneChannelType;
namespace {
void check(bool condition, const char* message) {
  if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void near(float a, float b, const char* message) {
  check(std::isfinite(a) && std::abs(a-b) < 0.0003f, message);
}
void basis(const Xfm& a, const Xfm& b, const char* message) {
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) near(a.rot[i][j], b.rot[i][j], message);
}
Xfm scaled() {
  Xfm x;
  x.rot[0][0] = 2; x.rot[1][1] = 3; x.rot[2][2] = 4;
  x.pos[0] = 8; x.pos[1] = 9; x.pos[2] = 10;
  return x;
}
float length(const float* v) { return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
void allocation() {
  Gh2ClipSetBinding main, hands;
  main.channels = {{"bone_b.pos", Type::Pos}, {"bone_a.pos", Type::Pos},
                   {"bone_a.scale", Type::Scale}, {"bone_a.quat", Type::Quat},
                   {"bone_a.rotz", Type::RotZ}};
  hands.channels = {{"bone_b.pos", Type::Pos}, {"bone_facing.pos", Type::Pos},
                    {"bone_facing_delta.pos", Type::Pos}, {"bone_facing.rotz", Type::RotZ},
                    {"bone_facing_delta.rotz", Type::RotZ}};
  Xfm a = scaled(), ignored, b, dummy;
  a.rot[0][0] = -2;
  dummy.pos[0] = 7;
  std::unordered_map<std::string, Xfm*> directory = {
      {"bone_a.trans", &a}, {"bone_a.mesh", &ignored}, {"bone_b.mesh", &b}};
  std::vector<std::string> lookups;
  auto find = [&](std::string_view name) -> Xfm* {
    lookups.emplace_back(name);
    const auto found = directory.find(std::string(name));
    return found == directory.end() ? nullptr : found->second;
  };
  SourceCharBonesMeshesOutput out;
  out.reallocate({&main, &hands}, find, &dummy);
  check(out.rows().size() == 9, "driver union deduplicates channels");
  check(out.counts() == std::array<std::size_t, 10>{0,4,5,6,6,6,9,9,9,9}, "cumulative counts");
  check(out.offsets() == std::array<std::size_t, 10>{0,64,80,96,96,96,108,108,108,108}, "typed byte offsets");
  check(out.allocated_bytes() == 112, "allocation rounded to 16");
  check(reinterpret_cast<std::uintptr_t>(out.data()) % 16 == 0, "aligned output storage");
  check(out.rows()[0].channel.name == "bone_a.pos" && out.rows()[0].local == &a,
        "lexical type order and trans priority");
  check(std::find(lookups.begin(), lookups.end(), "bone_a.mesh") == lookups.end(),
        "mesh not searched when transform exists");
  int fallback_count = 0;
  for (const auto& row : out.rows()) if (row.fallback) {
    ++fallback_count; check(row.local == &dummy, "one shared servo dummy");
  }
  check(fallback_count == 4, "all four facing rows use actual fallback");
  near(out.channel("bone_facing.pos")[0], 7, "fallback current-local seed");
  near(out.channel("bone_a.scale")[0], 2, "signed scale X magnitude");
  near(out.channel("bone_a.scale")[2], -4, "reflection carried by Z");
  near(out.channel("bone_a.quat")[3], std::sqrt(6.0f)*0.5f, "quaternion seed uses raw scaled basis");
  check(out.channel("absent.quat") == nullptr && out.delta_scalar_count() == 0, "missing channel and empty tail");

  // Reallocation seeds CURRENT locals, not an old bind pose or last buffer.
  a.pos[0] = 42;
  out.reallocate({&main, &hands}, find, &dummy);
  near(out.channel("bone_a.pos")[0], 42, "current-local seed on reallocate");
  const auto before = out.allocated_bytes();
  try { out.reallocate({&main, &hands}, find, nullptr); check(false, "missing fallback rejected"); }
  catch (const std::runtime_error& e) {
    check(std::string(e.what()).find("bone_facing.pos") != std::string::npos, "missing target named");
  }
  check(out.allocated_bytes() == before, "failed rebind leaves allocation valid");

  Gh2ClipSetBinding first_dot;
  first_dot.channels = {{"bone_a.extra.pos", Type::Pos}};
  out.reallocate({&first_dot}, find, nullptr);
  check(out.rows()[0].local == &a, "target suffix uses first dot");
  out.channel("bone_a.extra.pos")[0] = 12;
  out.pose_meshes();
  near(a.pos[0], 12, "position publisher");
  a.pos[0] = 99;
  out.pose_meshes();
  near(a.pos[0], 12, "persistent unwritten channel is not reset");
  out.reallocate({}, find, nullptr);
  check(out.rows().empty() && !out.data() && !out.delta_scalars(), "empty allocation");
  out.pose_meshes();
}
void quaternion() {
  Gh2ClipSetBinding bank;
  bank.channels = {{"joint.quat", Type::Quat}};
  for (int axis = 0; axis < 3; ++axis) {
    Xfm joint;
    for (int i = 0; i < 3; ++i) joint.rot[i][i] = i == axis ? 1.0f : -1.0f;
    SourceCharBonesMeshesOutput out;
    out.reallocate({&bank}, [&](std::string_view) { return &joint; }, nullptr);
    for (int i = 0; i < 4; ++i) near(out.channel("joint.quat")[i], i == axis ? 1.0f : 0.0f,
                                     "matrix quaternion largest-diagonal branches");
    const Xfm before = joint;
    out.pose_meshes();
    basis(joint, before, "quaternion roundtrip");
  }
  Xfm joint;
  joint.pos[2] = 13;
  SourceCharBonesMeshesOutput out;
  out.reallocate({&bank}, [&](std::string_view) { return &joint; }, nullptr);
  float* q = out.channel("joint.quat");
  q[0] = q[1] = 0; q[2] = q[3] = 2;
  int dirties = 0;
  out.pose_meshes([&](Xfm& x) { check(&x == &joint, "dirty actual target"); ++dirties; });
  near(q[2], std::sqrt(0.5f), "quaternion normalized in persistent buffer");
  Xfm expected; expected.rot[0][0] = expected.rot[1][1] = 0;
  expected.rot[0][1] = 1; expected.rot[1][0] = -1;
  basis(joint, expected, "row-vector quaternion rotation");
  near(joint.pos[2], 13, "quaternion does not orbit target position");
  check(dirties == 1, "dirty on publish");
  std::fill_n(q, 4, 0.0f);
  out.pose_meshes();
  for (int i = 0; i < 4; ++i) near(q[i], 0, "VU zero-quaternion normalization stays zero");
  basis(joint, Xfm{}, "zero quaternion matrix formula yields identity");
}
void absolute_axes() {
  const char* names[] = {"joint.rotx", "joint.roty", "joint.rotz"};
  const float half_pi = std::acos(-1.0f)*0.5f;
  for (int axis = 0; axis < 3; ++axis) {
    Xfm joint;
    float next = 2;
    for (auto& row : joint.rot) for (float& cell : row) cell = next++;
    joint.pos[0] = 17;
    Gh2ClipSetBinding bank;
    bank.channels = {{names[axis], static_cast<Type>(3+axis)}};
    SourceCharBonesMeshesOutput out;
    out.reallocate({&bank}, [&](std::string_view) { return &joint; }, nullptr);
    const float seeds[] = {std::atan2(7.0f,6.0f), std::atan2(-4.0f,10.0f), -std::atan2(5.0f,6.0f)};
    near(*out.channel(names[axis]), seeds[axis], "original single-axis extraction");
    *out.channel(names[axis]) = half_pi;
    out.pose_meshes();
    Xfm expected;
    if (axis == 0) { expected.rot[1][1]=0; expected.rot[1][2]=1; expected.rot[2][1]=-1; expected.rot[2][2]=0; }
    if (axis == 1) { expected.rot[0][0]=0; expected.rot[0][2]=-1; expected.rot[2][0]=1; expected.rot[2][2]=0; }
    if (axis == 2) { expected.rot[0][0]=0; expected.rot[0][1]=1; expected.rot[1][0]=-1; expected.rot[1][1]=0; }
    basis(joint, expected, "absolute axis replaces entire basis");
    near(joint.pos[0], 17, "absolute axis preserves translation");
  }
}
void deltas() {
  Xfm x = scaled(), y = scaled(), z = scaled();
  Gh2ClipSetBinding bank;
  bank.channels = {{"x.drotx", Type::DeltaRotX}, {"y.droty", Type::DeltaRotY}, {"z.drotz", Type::DeltaRotZ}};
  SourceCharBonesMeshesOutput out;
  const auto find = [&](std::string_view name) { return name[0] == 'x' ? &x : name[0] == 'y' ? &y : &z; };
  out.reallocate({&bank}, find, nullptr);
  check(out.delta_scalar_count() == 3, "contiguous scalar delta tail");
  check(out.delta_scalars() == out.channel("x.drotx"), "tail points at delta X start");
  for (int i = 0; i < 3; ++i) near(out.delta_scalars()[i], 0, "delta seed zero");
  out.delta_scalars()[0] = out.delta_scalars()[2] = std::acos(-1.0f)*0.5f;
  out.delta_scalars()[1] = 0.73f;
  out.pose_meshes();
  check(out.normalization_cursor() == 1, "cursor advances before normalization");
  basis(y, Xfm{}, "delta Y is no-op but selected normalization executes");
  near(x.rot[1][2], 3, "delta X postrotation keeps unselected scale");
  near(x.rot[2][1], -4, "delta X sign");
  near(z.rot[0][1], 2, "delta Z postrotation");
  near(z.rot[1][0], -3, "delta Z sign");
  out.pose_meshes();
  check(out.normalization_cursor() == 2, "second selected delta");
  near(length(z.rot[1]), 1, "normalize Z target in second update");
  near(length(x.rot[1]), 3, "X still not normalized");
  out.pose_meshes();
  check(out.normalization_cursor() == 0, "normalization wraps");
  near(length(x.rot[1]), 1, "normalize X target on third update");
  near(*out.channel("y.droty"), 0.73f, "pose does not clear delta channels");
  near(x.pos[0], 8, "delta rotation never orbits position");
  out.pose_meshes();
  out.reallocate({&bank}, find, nullptr);
  check(out.normalization_cursor() == 1, "reallocation preserves normalization cursor");

  // Only the selected row orthonormalizes, using row Y then Y cross Z,
  // not independent normalization (which would retain this shear).
  y.rot[0][0]=9; y.rot[0][1]=4; y.rot[0][2]=5;
  y.rot[1][0]=0; y.rot[1][1]=2; y.rot[1][2]=0;
  y.rot[2][0]=0; y.rot[2][1]=3; y.rot[2][2]=4;
  Gh2ClipSetBinding only_y;
  only_y.channels = {{"y.droty", Type::DeltaRotY}};
  out.reallocate({&only_y}, find, nullptr);
  *out.channel("y.droty") = 1;
  out.pose_meshes();
  basis(y, Xfm{}, "source row-preserving orthonormalization removes shear");
}
void publication_order() {
  Xfm joint = scaled();
  Gh2ClipSetBinding bank;
  bank.channels = {{"joint.pos", Type::Pos}, {"joint.scale", Type::Scale},
                   {"joint.quat", Type::Quat}, {"joint.rotx", Type::RotX}, {"joint.drotz", Type::DeltaRotZ}};
  SourceCharBonesMeshesOutput out;
  out.reallocate({&bank}, [&](std::string_view) { return &joint; }, nullptr);
  float* q = out.channel("joint.quat"); q[0]=q[1]=0; q[2]=q[3]=2;
  *out.channel("joint.rotx") = *out.channel("joint.drotz") = std::acos(-1.0f)*0.5f;
  auto* scale = out.channel("joint.scale"); scale[0]=2; scale[1]=3; scale[2]=-4;
  out.pose_meshes();
  Xfm expected;
  for (auto& row : expected.rot) for (float& value : row) value = 0;
  expected.rot[0][1]=2; expected.rot[1][2]=3; expected.rot[2][0]=-4;
  basis(joint, expected, "pos quat absolute delta normalize scale publication order");
  near(joint.pos[0], 8, "preserve published position through every rotation/scale pass");
}
void servo_output_bridge() {
  Xfm pelvis, joint, dummy, owner;
  owner.pos[0]=10; owner.pos[1]=20; owner.pos[2]=30;
  Gh2ClipSetBinding bank;
  bank.channels = {{"bone_pelvis.pos", Type::Pos}, {"bone_pelvis.quat", Type::Quat},
                   {"bone_facing.pos", Type::Pos}, {"bone_facing.rotz", Type::RotZ},
                   {"bone_facing_delta.pos", Type::Pos}, {"bone_facing_delta.rotz", Type::RotZ},
                   {"joint.drotx", Type::DeltaRotX}};
  const auto find = [&](std::string_view name) -> Xfm* {
    if (name == "bone_pelvis.mesh") return &pelvis;
    return name == "joint.mesh" ? &joint : nullptr;
  };
  SourceCharBonesMeshesOutput output;
  output.reallocate({&bank}, find, &dummy);
  SourceCharServoRuntime servo;
  *output.channel("bone_facing_delta.pos") = 3;
  *output.channel("joint.drotx") = 0.7f;
  bool cleared = false;
  servo.enter(output, [&] {
    near(*output.channel("bone_facing_delta.pos"), 0, "Enter clears actual facing before regulation");
    near(*output.channel("joint.drotx"), 0, "Enter clears actual scalar tail");
    cleared = true;
  });
  check(cleared && servo.has_typed_outputs && servo.move_self, "Enter uses allocated output inventory");
  for (int i = 0; i < 3; ++i) {
    output.channel("bone_pelvis.pos")[i] = float(4+i);
    output.channel("bone_facing.pos")[i] = float(7+i);
    output.channel("bone_facing_delta.pos")[i] = float(1+i);
  }
  *output.channel("bone_facing.rotz") = 0.2f;
  *output.channel("joint.drotx") = 0.4f;
  int regulated = 0;
  servo.poll(output, owner, pelvis, [&](Xfm& live_owner) {
    ++regulated;
    check(&live_owner == &owner, "regulate receives real owner local");
    near(pelvis.pos[0], 4, "typed PoseMeshes precedes movement and regulation");
    near(owner.pos[0], 11, "delta movement before regulation");
    near(*output.channel("bone_facing_delta.pos"), 1, "delta remains available during regulation");
  });
  check(regulated == 1, "one regulation callback");
  near(owner.pos[1], 22, "actual allocated delta Y consumed");
  near(owner.pos[2], 33, "actual allocated delta Z consumed");
  for (int i = 0; i < 3; ++i) near(output.channel("bone_facing_delta.pos")[i], 0, "position deltas zeroed in same buffer");
  near(*output.channel("bone_facing_delta.rotz"), 0, "rotation delta zeroed in buffer");
  near(*output.channel("joint.drotx"), 0, "scalar tail zeroed after pose");
  near(*output.channel("bone_facing.pos"), 7, "absolute facing remains persistent");
  near(*output.channel("bone_facing.rotz"), 0.2f, "absolute rotation remains persistent");
  servo.poll(output, owner, pelvis, [](Xfm&) {});
  near(owner.pos[0], 11, "no double application on unwritten next frame");

  // Rebinding before next Poll must not leave raw pointers into freed storage.
  output.reallocate({&bank}, find, &dummy);
  *output.channel("bone_facing_delta.pos") = 2;
  *output.channel("bone_facing_delta.rotz") = 0;
  output.channel("bone_facing_delta.pos")[1] = output.channel("bone_facing_delta.pos")[2] = 0;
  servo.poll(output, owner, pelvis, [](Xfm&) {});
  near(owner.pos[0], 13, "buffer rebinding uses new allocation");
  near(*output.channel("bone_facing_delta.pos"), 0, "new buffer cleared");
  Gh2ClipSetBinding broken;
  broken.channels = {{"bone_facing_delta.pos", Type::Pos}};
  output.reallocate({&broken}, find, &dummy);
  try { servo.poll(output, owner, pelvis, [](Xfm&) {}); check(false, "incomplete facing rejected"); }
  catch (const std::runtime_error&) {}
}
template<class T> void append(Gh2BoneSamplesPage& page, T value) {
  const auto before = page.disk_samples.size();
  page.disk_samples.resize(before + sizeof(value));
  std::memcpy(page.disk_samples.data() + before, &value, sizeof(value));
}
void driver_buffer_math() {
  Xfm dummy;
  Gh2ClipSetBinding bank;
  bank.channels = {{"p.pos",Type::Pos}, {"s.scale",Type::Scale}, {"q.quat",Type::Quat},
                   {"rx.rotx",Type::RotX}, {"ry.roty",Type::RotY}, {"rz.rotz",Type::RotZ},
                   {"dx.drotx",Type::DeltaRotX}, {"dy.droty",Type::DeltaRotY}, {"dz.drotz",Type::DeltaRotZ},
                   {"untouched.pos",Type::Pos}};
  SourceCharBonesMeshesOutput out;
  out.reallocate({&bank}, {}, &dummy);
  out.channel("untouched.pos")[0]=123;
  Gh2BoneSamplesPage page;
  page.channels.assign(bank.channels.begin(), bank.channels.end()-1);
  page.sample_count=1;
  for (int vector=0; vector<2; ++vector)
    for (int component=0; component<3; ++component) append(page, float(1+vector*3+component));
  for (float v : {1.0f,2.0f,3.0f,4.0f}) append(page,v);
  for (int i=0; i<6; ++i) append(page,float(i+1));
  page.validate();
  out.scale_down(page.channels,0);
  near(out.channel("p.pos")[3],1,"zero ScaleDown vector fourth word is VU constant one");
  for(int i=0;i<4;++i) near(out.channel("q.quat")[i],0,"zero ScaleDown quaternion is all zero");
  near(out.channel("untouched.pos")[0],123,"ScaleDown is per-clip membership, not global reset");
  out.scale_add(page,0,-0.5f);
  near(out.channel("p.pos")[2],-1.5f,"signed vector ScaleAdd");
  near(out.channel("s.scale")[2],-3,"signed scale ScaleAdd");
  for (int i=0;i<3;++i) near(out.channel("q.quat")[i],float(i+1)*0.5f,"quat XYZ uses absolute weight");
  near(out.channel("q.quat")[3],-2,"quat W uses signed weight");
  near(*out.channel("dz.drotz"),-3,"all six scalar buckets accumulate signed weight");
  out.scale_down(page.channels,0.25f);
  near(out.channel("q.quat")[3],-0.5f,"nonzero ScaleDown multiplies quaternion without normalization");
  near(out.channel("p.pos")[3],1,"nonzero ScaleDown preserves vector padding");

  Gh2BoneSamplesPage packed;
  packed.channels = {{"q.quat",Type::Quat},{"rz.rotz",Type::RotZ}};
  packed.compression=1; packed.sample_count=1;
  for (std::int16_t v : {std::int16_t(-32768),std::int16_t(32767),std::int16_t(0),std::int16_t(16384),std::int16_t(-32768)}) append(packed,v);
  out.scale_down(packed.channels,0);
  out.scale_add(packed,0,0.5f);
  near(out.channel("q.quat")[0],-0.5000152587890625f,"packed quaternion minimum is not clamped to -1 before weighting");
  near(*out.channel("rz.rotz"),-10,"packed scalar uses 0x3A200000, not pi or snorm");

  Gh2BoneSamplesPage blend;
  blend.channels = {{"q.quat",Type::Quat},{"rz.rotz",Type::RotZ}}; blend.sample_count=2;
  for(float v:{0.0f,0.0f,0.0f,2.0f,3.0f, 0.0f,0.0f,-2.0f,0.0f,-3.0f}) append(blend,v);
  out.scale_down(blend.channels,0);
  out.scale_add_at_phase(blend,0.5f,1);
  near(out.channel("q.quat")[2],-1,"fractional quaternion addition is not normalized mid-blend");
  near(out.channel("q.quat")[3],1,"first then second sample quaternion accumulation");
  near(*out.channel("rz.rotz"),0,"scalar interpolation is linear, not angle wrapping");
  // Same orientation with opposite quaternion signs must not cancel.
  out.channel("q.quat")[2]=2; out.channel("q.quat")[3]=0;
  out.scale_add(blend,1,0.5f);
  near(out.channel("q.quat")[2],3,"hemisphere chosen against current accumulator");
  blend.interpolate=false;
  out.scale_down(blend.channels,0);
  out.scale_add_at_phase(blend,0.6f,1);
  near(*out.channel("rz.rotz"),-3,"noninterpolated nearest sample");

  Gh2BoneSamplesPage delta;
  delta.channels={{"dz.drotz",Type::DeltaRotZ}}; delta.sample_count=2;
  append(delta,0.0f); append(delta,10.0f);
  for(const auto phases : {std::array<float,2>{-0.1f,0.1f}, std::array<float,2>{0.9f,1.1f}}) {
    out.scale_down(delta.channels,0);
    out.scale_add_delta(delta,phases[0],phases[1],1);
    near(*out.channel("dz.drotz"),2,"delta boundary preserves interval by shifting other endpoint");
  }
  out.scale_down(delta.channels,0);
  out.scale_add_delta(delta,0.3f,0.7f,-0.5f);
  near(*out.channel("dz.drotz"),-2,"delta adds current first and previous with negative weight");
  delta.interpolate=false;
  out.scale_add_delta(delta,0,1,4);
  near(*out.channel("dz.drotz"),-2,"disabled interpolation suppresses delta page");
  delta.disk_samples.pop_back();
  try { delta.validate(); check(false,"truncated raw page rejected"); } catch(const std::runtime_error&) {}
  try { out.scale_down({{"missing.quat",Type::Quat}},0); check(false,"unallocated source channel rejected"); }
  catch(const std::runtime_error&) {}
}
void clip_driver_transport() {
  Xfm dummy;
  Gh2ClipSetBinding bank;
  bank.channels={{"joint.pos",Type::Pos},{"bone_facing.pos",Type::Pos},{"bone_facing.rotz",Type::RotZ},
                 {"bone_facing_delta.pos",Type::Pos},{"bone_facing_delta.rotz",Type::RotZ}};
  SourceCharBonesMeshesOutput out;
  out.reallocate({&bank},{},&dummy);
  const auto make_clip = [&](const char* name, float x) {
    CharClip clip; clip.name=name; clip.loaded=true; clip.frames.resize(1);
    clip.start_beat=0; clip.end_beat=8; clip.beats_per_second=2;
    auto samples=std::make_shared<Gh2ClipPoseSamples>();
    auto& one=samples->pages[Gh2ClipPoseSamples::One];
    one.channels={{"joint.pos",Type::Pos}}; one.sample_count=1;
    append(one,x); append(one,0.0f); append(one,0.0f);
    clip.gh2_pose_samples=samples; return clip;
  };
  const auto a=make_clip("oldest",1000), b=make_clip("middle",100), c=make_clip("newest",10);
  CharClipPlayer player;
  player.play(a);
  player.play_source(b,kCharPlayLoop,0,0,2);
  player.advance_source(0.5f,0.5f,0.25f);
  player.play_source(c,kCharPlayLoop,0,0,2);
  player.advance_source(1,0.5f,0.25f);
  check(player.source_stack_depth()==3,"three active source contribution nodes");
  player.accumulate_source_pose(out);
  const float newest=0.5f-0.5f*std::cos(std::acos(-1.0f)*0.25f);
  const float older=(1-newest)*0.5f;
  near(out.channel("joint.pos")[0],10*newest+100*older+1000*older,"source newest-to-oldest three-node accumulation");
  out.channel("joint.pos")[0]=200;
  player.accumulate_source_pose(out,0.5f);
  near(out.channel("joint.pos")[0],100+0.5f*(10*newest+100*older+1000*older),"coverage scales existing accumulator, preserving partial driver influence");

  Gh2ClipPoseSamples samples;
  auto& full=samples.pages[Gh2ClipPoseSamples::Full];
  full.channels={{"bone_facing.pos",Type::Pos},{"bone_facing.rotz",Type::RotZ}};
  full.sample_count=2;
  const float half_pi=std::acos(-1.0f)*0.5f;
  for(float v:{0.0f,0.0f,0.0f,half_pi, 2.0f,4.0f,6.0f,half_pi+0.4f}) append(full,v);
  out.scale_down_clip(samples,0);
  out.scale_add_clip(samples,0,1,0.5f);
  near(out.channel("bone_facing_delta.pos")[0],2,"facing delta rotated into previous heading X");
  near(out.channel("bone_facing_delta.pos")[1],-1,"facing delta rotated into previous heading Y");
  near(out.channel("bone_facing_delta.pos")[2],3,"facing delta vertical difference");
  near(*out.channel("bone_facing_delta.rotz"),0.2f,"facing angular delta weighted once");
  out.scale_down_clip(samples,0);
  out.scale_add_clip(samples,-0.1f,0.1f,1);
  near(out.channel("bone_facing_delta.pos")[2],0.6f,"FacingBones clamps endpoints, unlike generic delta interval shift");
  // FacingBones only binds full-page position: constant one-page facing
  // channels must not manufacture locomotion outputs.
  samples.pages[Gh2ClipPoseSamples::One]=std::move(full);
  full={};
  *out.channel("bone_facing_delta.pos")=17;
  out.scale_down_clip(samples,0);
  out.scale_add_clip(samples,0,1,1);
  near(*out.channel("bone_facing_delta.pos"),17,"one-only facing is not a generated FacingBones source");
}
void retail_bindings() {
  // Bounded, pipe-fed actual saved EE tables. No desktop or emulator inputs.
  int cases = 0; std::cin >> cases;
  check(cases > 0 && cases <= 64, "saved binding case count");
  for (int n = 0; n < cases; ++n) {
    std::string owner;
    std::size_t target_count = 0, fallback_index = 0, row_count = 0, allocated = 0;
    std::cin >> std::quoted(owner) >> target_count >> fallback_index;
    check(target_count > 0 && target_count < 4096 && fallback_index < target_count, "saved target bounds");
    std::vector<Xfm> locals(target_count);
    std::unordered_map<std::string, Xfm*> directory;
    for (std::size_t i = 0; i < target_count; ++i) {
      std::string name; std::cin >> std::quoted(name);
      for (auto& row : locals[i].rot) for (float& cell : row) std::cin >> cell;
      for (float& cell : locals[i].pos) std::cin >> cell;
      if (!name.empty()) check(directory.emplace(name, &locals[i]).second, "saved target names unambiguous");
    }
    std::array<std::size_t, 10> counts{}, offsets{};
    for (auto& value : counts) std::cin >> value;
    for (auto& value : offsets) std::cin >> value;
    std::cin >> allocated >> row_count;
    check(row_count > 0 && row_count < 4096, "saved row bounds");
    Gh2ClipSetBinding bank;
    std::vector<std::size_t> expected_targets;
    for (std::size_t i = 0; i < row_count; ++i) {
      std::string name; int type; std::size_t target;
      std::cin >> std::quoted(name) >> type >> target;
      check(type >= 0 && type < 9 && target < target_count, "saved typed target");
      bank.channels.push_back({name, static_cast<Type>(type)});
      expected_targets.push_back(target);
    }
    check(!std::cin.fail(), "complete saved fixture");
    SourceCharBonesMeshesOutput output;
    output.reallocate({&bank}, [&](std::string_view name) -> Xfm* {
      const auto it = directory.find(std::string(name));
      return it == directory.end() ? nullptr : it->second;
    }, &locals[fallback_index]);
    check(output.counts() == counts && output.offsets() == offsets && output.allocated_bytes() == allocated,
          "production layout equals actual EE allocation");
    check(output.rows().size() == row_count, "production row count equals actual EE table");
    std::size_t fallbacks = 0;
    for (std::size_t i = 0; i < row_count; ++i) {
      const auto& row = output.rows()[i];
      check(row.channel.name == bank.channels[i].name, "production order equals actual EE table");
      check(row.local == &locals[expected_targets[i]], "production target equals actual EE ObjPtr");
      check(row.fallback == (expected_targets[i] == fallback_index), "production dummy ownership equals actual EE table");
      if (row.fallback) ++fallbacks;
    }
    std::cout << "RETAIL_BINDING " << std::quoted(owner) << ' ' << row_count << ' '
              << fallbacks << ' ' << allocated << '\n';
  }
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--retail-bindings") {
    retail_bindings(); return 0;
  }
  allocation(); quaternion(); absolute_axes(); deltas(); publication_order(); servo_output_bridge(); driver_buffer_math(); clip_driver_transport();
  std::puts("PASS: GH2 persistent typed output, allocation, all 9 publishers, normalization and ownership");
  std::puts("PASS: allocated buffer -> PoseMeshes -> Servo movement/regulation -> buffer ZeroDeltas, no double movement");
  std::puts("PASS: GH2 ScaleDown/ScaleAdd, raw packed codes, signed quaternion weights, ordered samples and delta boundaries");
  std::puts("PASS: full/one/delta/facing clip publication and three-node CharDriver buffer contributions");
}

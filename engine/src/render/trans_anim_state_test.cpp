#include "render/milo_scene_renderer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
using Renderer = ghogx::render::MiloSceneRenderer;
using Sample = Renderer::MeshTransformSample;
using Matrix = std::array<float, 16>;
constexpr float pi = 3.14159265358979323846f;
const Matrix identity = {1, 0, 0, 0, 0, 1, 0, 0,
                         0, 0, 1, 0, 0, 0, 0, 1};

void check(bool condition, const char* label) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", label);
    std::exit(1);
  }
}
void near(float got, float expected, const char* label) {
  if (!std::isfinite(got) || std::fabs(got - expected) > 0.0001f) {
    std::fprintf(stderr, "FAIL: %s: %.8f != %.8f\n", label, got, expected);
    std::exit(1);
  }
}
Sample rotate(float degrees, float blend = 1, bool slerp = false) {
  Sample sample;
  sample.has_rotation = sample.rotation_is_absolute = true;
  sample.rotation_xyzw = {0, std::sin(degrees * pi / 360), 0,
                             std::cos(degrees * pi / 360)};
  sample.blend = blend;
  sample.rotation_slerp = slerp;
  return sample;
}
void y_rotation(const Sample& sample, float degrees, const char* label) {
  const float angle = degrees * pi / 180;
  near(sample.local_transform[0], std::cos(angle), label);
  near(sample.local_transform[2], -std::sin(angle), label);
  near(sample.local_transform[8], std::sin(angle), label);
  near(sample.local_transform[10], std::cos(angle), label);
}
}  // namespace

int main() {
  // GH2 SetFrame 0x1E0FA0 / MakeTransform rotation branch 0x1E0C8C:
  // interpolation is from current LocalXfm, never from the initial scene pose.
  auto old = Renderer::compose_transform_animation_sample(identity, nullptr, rotate(90));
  y_rotation(old, 90, "initial absolute rotation");
  auto zero = Renderer::compose_transform_animation_sample(identity, &old, rotate(180, 0));
  check(zero.local_transform == old.local_transform, "zero blend preserves exact current pose");
  auto half = Renderer::compose_transform_animation_sample(identity, &zero, rotate(180, .5f));
  y_rotation(half, 135, "blend starts at current pose");
  auto next = Renderer::compose_transform_animation_sample(identity, &half, rotate(180, .5f));
  y_rotation(next, 157.5f, "successive SetFrame uses last publication");

  // Reusing the resolved snapshot during any number of rendering passes is
  // idempotent. Only another raw SetFrame sample advances the blend.
  auto publication = Renderer::compose_transform_animation_sample(identity, nullptr, next);
  for (int draw = 0; draw < 8; ++draw)
    publication = Renderer::compose_transform_animation_sample(identity, &publication, next);
  check(publication.local_transform == next.local_transform, "draw publication is idempotent");

  auto translated = rotate(45);
  translated.has_translation = translated.translation_is_absolute = true;
  translated.translation = {42, -7, 3};
  auto full = Renderer::compose_transform_animation_sample(identity, nullptr, translated);
  auto rotation_only = Renderer::compose_transform_animation_sample(identity, &full, rotate(90));
  near(rotation_only.local_transform[12], 42, "unkeyed translation x retained");
  near(rotation_only.local_transform[13], -7, "unkeyed translation y retained");
  Sample translation_only;
  translation_only.has_translation = translation_only.translation_is_absolute = true;
  translation_only.translation = {10, 12, 14};
  translation_only.blend = .5f;
  auto moved = Renderer::compose_transform_animation_sample(identity, &rotation_only, translation_only);
  y_rotation(moved, 90, "unkeyed rotation retained");
  near(moved.local_transform[12], 26, "translation uses current position");

  auto spherical = Renderer::compose_transform_animation_sample(identity, nullptr, rotate(90, .25f, true));
  y_rotation(spherical, 22.5f, "source slerp flag used for blend");
  auto linear = Renderer::compose_transform_animation_sample(identity, nullptr, rotate(90, .25f));
  const float linear_degrees = 2 * std::atan2(.25f * std::sin(pi / 4),
                                             .75f + .25f * std::cos(pi / 4)) * 180 / pi;
  y_rotation(linear, linear_degrees, "source normalized linear blend");

  Matrix reflected = {2, 0, 0, 0, 0, -3, 0, 0,
                       0, 0, 4, 0, -30, 8, 15, 1};
  auto other_scene = Renderer::compose_transform_animation_sample(reflected, nullptr, rotate(20, 0));
  check(other_scene.local_transform == reflected, "new scene uses own scale and handedness");
  auto reset = Renderer::compose_transform_animation_sample(identity, nullptr, rotate(20, 0));
  check(reset.local_transform == identity, "reset does not inherit another scene's pose");
  auto repeat_zero = Renderer::compose_transform_animation_sample(identity, &other_scene, rotate(20, 0));
  check(repeat_zero.local_transform == reflected, "zero blend preserves reflected current matrix");

  // The fan regression: first incoming music-start sample must not restore
  // the stored 15.55-degree local basis over the outgoing -79.95-degree pose.
  Matrix fan_base = {.963416f, 0, .268011f, 0, 0, 1, 0, 0,
                    -.268011f, 0, .963416f, 0, 0, 0, 0, 1};
  Sample fan_outgoing = rotate(0);
  fan_outgoing.rotation_xyzw = {0, -.642442f, 0, -.766334f};
  auto fan = Renderer::compose_transform_animation_sample(fan_base, nullptr, fan_outgoing);
  auto fan_start = Renderer::compose_transform_animation_sample(fan_base, &fan, rotate(0, 0));
  check(fan_start.local_transform == fan.local_transform, "fan takeover preserves preceding matrix");
  check(fan_start.local_transform != fan_base, "fan takeover never resets to stored base");
  std::puts("PASS: source current-pose blending, channel retention, slerp/nlerp, repeated draw publication, scene isolation/reset, fan handoff");
}

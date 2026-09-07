#include "character/char_bones_output.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace ghogx::character {
namespace {
using milo_scene::Xfm;
using Type = Gh2BoneChannelType;

std::size_t disk_type_bytes(Type type, bool packed) {
  if (type == Type::Pos || type == Type::Scale) return 12;
  if (type == Type::Quat) return packed ? 8 : 16;
  if (type >= Type::RotX && type < Type::End) return packed ? 2 : 4;
  throw std::runtime_error("invalid GH2 sample channel type");
}
float read_float(const std::uint8_t*& data) {
  float value; std::memcpy(&value, data, sizeof(value)); data += sizeof(value);
  return value;
}
float read_short(const std::uint8_t*& data) {
  std::int16_t value; std::memcpy(&value, data, sizeof(value)); data += sizeof(value);
  return static_cast<float>(value);
}
const Gh2BoneChannel* find_sample_channel(const Gh2BoneSamplesPage& page, std::string_view name) {
  for (const auto& channel : page.channels) if (channel.name == name) return &channel;
  return nullptr;
}
std::array<float, 3> sample_facing_channel(const Gh2BoneSamplesPage& page,
    std::string_view name, float phase) {
  const auto stride = page.checked_frame_bytes();
  const auto* channel = find_sample_channel(page,name);
  if (!channel || (channel->type != Type::Pos && channel->type != Type::RotZ))
    throw std::runtime_error("invalid GH2 facing sample channel");
  if (!std::isfinite(phase)) throw std::runtime_error("non-finite GH2 facing phase");
  std::size_t offset=0;
  for (const auto& row : page.channels) {
    if (&row == channel) break;
    offset += disk_type_bytes(row.type,page.compression != 0);
  }
  const float coordinate = std::clamp(phase,0.0f,1.0f)*static_cast<float>(page.sample_count-1);
  const auto first = static_cast<std::size_t>(coordinate + (page.interpolate ? 0.0f : 0.5f));
  const float fraction = page.interpolate ? coordinate-static_cast<float>(first) : 0.0f;
  const auto read_sample = [&](std::size_t index) {
    const auto* data = page.disk_samples.data()+index*stride+offset;
    std::array<float,3> value{};
    if (channel->type == Type::Pos) for (float& component : value) component=read_float(data);
    else value[0] = page.compression ? read_short(data)*0x1.4p-11f : read_float(data);
    return value;
  };
  auto a = read_sample(first);
  if (fraction > 0) {
    const auto b = read_sample(first+1);
    for (int i=0;i<3;++i) a[i] += (b[i]-a[i])*fraction;
  }
  return a;
}
const std::vector<Gh2BoneChannel>& facing_delta_channels(const Gh2BoneSamplesPage& full) {
  static const std::vector<Gh2BoneChannel> empty;
  static const std::vector<Gh2BoneChannel> pos{{"bone_facing_delta.pos",Type::Pos}};
  static const std::vector<Gh2BoneChannel> both{{"bone_facing_delta.pos",Type::Pos},
                                            {"bone_facing_delta.rotz",Type::RotZ}};
  // FacingBones::Set 16A9xx binds FULL samples, not full/one Channel fallback.
  // Missing full facing position returns before adding either virtual row.
  if (!find_sample_channel(full,"bone_facing.pos")) return empty;
  return find_sample_channel(full,"bone_facing.rotz") ? both : pos;
}

float dot(const float* a, const float* b) {
  return (a[0] * b[0] + a[1] * b[1]) + a[2] * b[2];
}
std::array<float, 3> cross(const float* a, const float* b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}
std::array<float, 3> get_scale(const Xfm& x) {
  // 0x2D9F58: only Z carries reflection; det == 0 takes the negative branch.
  const auto xy = cross(x.rot[0], x.rot[1]);
  const float z = std::sqrt(dot(x.rot[2], x.rot[2]));
  return {std::sqrt(dot(x.rot[0], x.rot[0])),
          std::sqrt(dot(x.rot[1], x.rot[1])), dot(xy.data(), x.rot[2]) > 0 ? z : -z};
}
float vu_reciprocal_sqrt(float sum) {
  // Original VRSQRT Q,vf0w,... saturates 1/sqrt(0), so a zero input
  // vector/quaternion remains zero, not NaN. PCSX2 _vuRSQRT confirms this.
  return sum == 0 ? std::numeric_limits<float>::max() : 1.0f / std::sqrt(sum);
}
void normalize(float* v, int count) {
  float sum = v[0] * v[0];
  for (int i = 1; i < count; ++i) sum += v[i] * v[i];
  const float scale = vu_reciprocal_sqrt(sum);
  for (int i = 0; i < count; ++i) v[i] *= scale;
}
void normalize_basis(Xfm& x) {
  // Inlined VU sequence at 0x193158, repeated for Y/Z delta buckets.
  normalize(x.rot[1], 3);
  auto first = cross(x.rot[1], x.rot[2]);
  normalize(first.data(), 3);
  std::copy(first.begin(), first.end(), x.rot[0]);
  const auto last = cross(x.rot[0], x.rot[1]);
  std::copy(last.begin(), last.end(), x.rot[2]);
}
void matrix_quat(const Xfm& x, float* q) {
  // 0x2DA318 uses the RAW basis, without extracting scale first.
  const auto& m = x.rot;
  const float trace = m[0][0] + m[1][1] + m[2][2];
  if (trace > 0) {
    const float s = std::sqrt(trace + 1.0f);
    q[3] = 0.5f * s;
    const float f = 0.5f / s;
    q[0] = (m[1][2] - m[2][1]) * f;
    q[1] = (m[2][0] - m[0][2]) * f;
    q[2] = (m[0][1] - m[1][0]) * f;
  } else {
    int i = 0;
    if (m[1][1] > m[0][0]) i = 1;
    if (m[2][2] > m[i][i]) i = 2;
    constexpr int next[] = {1, 2, 0}; // original 0x45AA00
    const int j = next[i], k = next[j];
    float s = std::sqrt((m[i][i] - m[j][j] - m[k][k]) + 1.0f);
    q[i] = 0.5f * s;
    if (s != 0) s = 0.5f / s;
    q[3] = (m[j][k] - m[k][j]) * s;
    q[j] = (m[i][j] + m[j][i]) * s;
    q[k] = (m[i][k] + m[k][i]) * s;
  }
}
void quat_matrix(const float* q, Xfm& x) {
  // 0x2DAA30, after in-place normalization at 0x2D9DC8.
  const float xx = 2*q[0]*q[0], yy = 2*q[1]*q[1], zz = 2*q[2]*q[2];
  const float xy = 2*q[0]*q[1], xz = 2*q[0]*q[2], yz = 2*q[1]*q[2];
  const float xw = 2*q[0]*q[3], yw = 2*q[1]*q[3], zw = 2*q[2]*q[3];
  x.rot[0][0] = 1-yy-zz; x.rot[0][1] = xy+zw; x.rot[0][2] = xz-yw;
  x.rot[1][0] = xy-zw; x.rot[1][1] = 1-zz-xx; x.rot[1][2] = yz+xw;
  x.rot[2][0] = xz+yw; x.rot[2][1] = yz-xw; x.rot[2][2] = 1-xx-yy;
}
void absolute_axis(Xfm& x, int axis, float angle) {
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) x.rot[i][j] = i == j ? 1.0f : 0.0f;
  const int a = (axis + 1) % 3, b = (axis + 2) % 3;
  const float c = std::sin(angle + 1.57079637050628662109375f), s = std::sin(angle);
  x.rot[a][a] = c; x.rot[a][b] = s;
  x.rot[b][a] = -s; x.rot[b][b] = c;
}
void delta_axis(Xfm& x, int axis, float angle) {
  // GH2 0x2DABC8 is literally jr ra; nop. Preserve the delta-Y no-op;
  // replacing it with a plausible RotateAboutY changes original behavior.
  if (axis == 1) return;
  const int a = (axis + 1) % 3, b = (axis + 2) % 3;
  const float c = std::sin(angle + 1.57079637050628662109375f), s = std::sin(angle);
  for (auto& row : x.rot) {
    const float va = row[a], vb = row[b];
    row[a] = va*c - vb*s;
    row[b] = va*s + vb*c;
  }
}
void seed(Type type, const Xfm& x, float* value) {
  switch (type) {
    case Type::Pos: std::copy(x.pos, x.pos + 3, value); break;
    case Type::Scale: {
      const auto scale = get_scale(x);
      std::copy(scale.begin(), scale.end(), value); break;
    }
    case Type::Quat: matrix_quat(x, value); break;
    case Type::RotX: *value = std::atan2(x.rot[1][2], x.rot[1][1]); break;
    case Type::RotY: *value = std::atan2(-x.rot[0][2], x.rot[2][2]); break;
    case Type::RotZ: *value = -std::atan2(x.rot[1][0], x.rot[1][1]); break;
    default: *value = 0; break;
  }
}
}  // namespace

std::size_t Gh2BoneSamplesPage::frame_bytes() const {
  std::size_t bytes = 0;
  for (const auto& channel : channels) bytes += disk_type_bytes(channel.type, compression != 0);
  return bytes;
}
std::size_t Gh2BoneSamplesPage::checked_frame_bytes() const {
  const auto stride = frame_bytes();
  if ((!channels.empty() && !sample_count) || sample_count > 100000 ||
      (stride && sample_count > std::numeric_limits<std::size_t>::max() / stride) ||
      disk_samples.size() != stride * sample_count)
    throw std::runtime_error("invalid GH2 sample page extent");
  return stride;
}
void Gh2BoneSamplesPage::validate() const {
  checked_frame_bytes();
  std::unordered_set<std::string> names;
  for (const auto& channel : channels)
    if (!names.insert(channel.name).second)
      throw std::runtime_error("duplicate GH2 sample channel: " + channel.name);
}

void SourceCharBonesMeshesOutput::AlignedDelete::operator()(float* data) const noexcept {
  ::operator delete[](data, std::align_val_t(16));
}
void SourceCharBonesMeshesOutput::reallocate(
    const std::vector<const Gh2ClipSetBinding*>& inventories,
    const FindTransform& find, Xfm* fallback) {
  std::vector<Row> rows;
  for (const auto* inventory : inventories) {
    if (!inventory) throw std::invalid_argument("null GH2 driver inventory");
    for (const auto& channel : inventory->channels) {
      if (channel.name.empty() || channel.type < Type::Pos || channel.type >= Type::End)
        throw std::invalid_argument("invalid GH2 output channel: " + channel.name);
      constexpr const char* suffixes[] = {".pos", ".scale", ".quat", ".rotx", ".roty",
                                         ".rotz", ".drotx", ".droty", ".drotz"};
      const std::string suffix = suffixes[static_cast<std::size_t>(channel.type)];
      if (channel.name.size() <= suffix.size() ||
          channel.name.compare(channel.name.size() - suffix.size(), suffix.size(), suffix) != 0)
        throw std::invalid_argument("GH2 output channel type/suffix mismatch: " + channel.name);
      rows.push_back({channel});
    }
  }
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
    return a.channel.type != b.channel.type ? a.channel.type < b.channel.type
                                          : a.channel.name < b.channel.name;
  });
  rows.erase(std::unique(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
    return a.channel.type == b.channel.type && a.channel.name == b.channel.name;
  }), rows.end());
  std::array<std::size_t, 10> counts{}, offsets{};
  for (auto& row : rows) {
    ++counts[static_cast<std::size_t>(row.channel.type) + 1];
    const std::string stem = row.channel.name.substr(0, row.channel.name.find('.'));
    if (find) {
      row.local = find(stem + ".trans");
      if (!row.local) row.local = find(stem + ".mesh");
    }
    if (!row.local) { row.local = fallback; row.fallback = true; }
    if (!row.local)
      throw std::runtime_error("unresolved GH2 pose target (no servo fallback): " + row.channel.name);
  }
  for (std::size_t i = 0; i < 9; ++i) {
    offsets[i+1] = offsets[i] + counts[i+1] * (i < 3 ? 16 : 4);
    counts[i+1] += counts[i];
  }
  const std::size_t bytes = (offsets[9] + 15) & ~std::size_t(15);
  std::unique_ptr<float[], AlignedDelete> data;
  if (bytes) {
    data.reset(static_cast<float*>(::operator new[](bytes, std::align_val_t(16))));
    std::fill_n(data.get(), bytes / sizeof(float), 0.0f);
  }
  for (std::size_t i = 0; i < rows.size(); ++i) {
    auto& row = rows[i];
    const auto type = static_cast<std::size_t>(row.channel.type);
    row.byte_offset = offsets[type] + (i - counts[type]) * (type < 3 ? 16 : 4);
    seed(row.channel.type, *row.local, data.get() + row.byte_offset / sizeof(float));
  }
  rows_ = std::move(rows); data_ = std::move(data);
  counts_ = counts; offsets_ = offsets; allocated_bytes_ = bytes;
}

float* SourceCharBonesMeshesOutput::channel(std::string_view name) {
  for (const auto& row : rows_)
    if (row.channel.name == name) return data_.get() + row.byte_offset / sizeof(float);
  return nullptr;
}
const float* SourceCharBonesMeshesOutput::channel(std::string_view name) const {
  for (const auto& row : rows_)
    if (row.channel.name == name) return data_.get() + row.byte_offset / sizeof(float);
  return nullptr;
}
float* SourceCharBonesMeshesOutput::delta_scalars() {
  return data_ ? data_.get() + offsets_[6] / sizeof(float) : nullptr;
}
float* SourceCharBonesMeshesOutput::require_channel(const Gh2BoneChannel& channel) {
  for (const auto& row : rows_)
    if (row.channel.name == channel.name && row.channel.type == channel.type)
      return data_.get() + row.byte_offset / sizeof(float);
  throw std::runtime_error("GH2 source channel missing from persistent output: " + channel.name);
}
void SourceCharBonesMeshesOutput::scale_down(const std::vector<Gh2BoneChannel>& channels, float weight) {
  // 167FD8: source supplies channel membership, destination supplies values.
  // Separate zero path stores vf0=(0,0,0,1) for vectors, all-zero quaternions.
  for (const auto& source : channels) {
    float* destination = require_channel(source);
    const int count = source.type <= Type::Scale ? 3 : source.type == Type::Quat ? 4 : 1;
    if (weight == 0) {
      std::fill_n(destination, count, 0.0f);
      if (source.type <= Type::Scale) destination[3] = 1.0f;
    } else {
      for (int i = 0; i < count; ++i) destination[i] *= weight;
    }
  }
}
void SourceCharBonesMeshesOutput::scale_add(const Gh2BoneSamplesPage& page, std::size_t sample, float weight) {
  const auto stride = page.checked_frame_bytes();
  if (page.channels.empty()) return;
  if (sample >= page.sample_count) throw std::out_of_range("GH2 source sample index");
  const auto* source = page.disk_samples.data() + sample * stride;
  const bool packed = page.compression != 0;
  // Original packed ScaleAdd multiplies the weight by these bit-exact
  // constants BEFORE multiplying raw signed codes. Do not clamp -32768.
  constexpr float short_quat_scale = 0x1.0002p-15f; // 0x38000100
  constexpr float short_angle_scale = 0x1.4p-11f;   // 0x3A200000
  for (const auto& channel : page.channels) {
    float* destination = require_channel(channel);
    if (channel.type <= Type::Scale) {
      for (int i = 0; i < 3; ++i) destination[i] += read_float(source) * weight;
    } else if (channel.type == Type::Quat) {
      const float xyz_weight = std::abs(weight) * (packed ? short_quat_scale : 1.0f);
      const float w_weight = weight * (packed ? short_quat_scale : 1.0f);
      float q[4];
      for (int i = 0; i < 4; ++i)
        q[i] = (packed ? read_short(source) : read_float(source)) * (i == 3 ? w_weight : xyz_weight);
      float product = q[0]*destination[0];
      for (int i = 1; i < 4; ++i) product += q[i]*destination[i];
      // Hemisphere choice is made against the CURRENT accumulator, after
      // weighting. This is component addition, not normalized nlerp/slerp.
      for (int i = 0; i < 4; ++i)
        destination[i] = product < 0 ? destination[i]-q[i] : destination[i]+q[i];
    } else {
      const float scaled_weight = weight * (packed ? short_angle_scale : 1.0f);
      destination[0] += (packed ? read_short(source) : read_float(source)) * scaled_weight;
    }
  }
}
void SourceCharBonesMeshesOutput::scale_add_at_phase(const Gh2BoneSamplesPage& page, float phase, float weight) {
  page.checked_frame_bytes();
  if (page.channels.empty()) return;
  if (!std::isfinite(phase)) throw std::runtime_error("non-finite GH2 sample phase");
  const float position = std::clamp(phase, 0.0f, 1.0f) * static_cast<float>(page.sample_count-1);
  const auto index = static_cast<std::size_t>(position + (page.interpolate ? 0.0f : 0.5f));
  const float fraction = page.interpolate ? position - static_cast<float>(index) : 0.0f;
  // 193D78: sample0 contribution FIRST, then sample1 only if fraction > 0.
  scale_add(page, index, (1.0f-fraction)*weight);
  if (fraction > 0) scale_add(page, index+1, fraction*weight);
}
void SourceCharBonesMeshesOutput::scale_add_delta(const Gh2BoneSamplesPage& page,
    float previous_phase, float current_phase, float weight) {
  page.checked_frame_bytes();
  if (page.channels.empty() || !page.interpolate) return; // 193E48/193E54
  // 193E18 shifts the opposite endpoint at boundaries to preserve the
  // interval, then FracToSample clamps. It does not just clamp both ends.
  if (current_phase > 1.0f) previous_phase -= current_phase - 1.0f;
  else if (previous_phase < 0.0f) current_phase -= previous_phase;
  scale_add_at_phase(page, current_phase, weight);
  scale_add_at_phase(page, previous_phase, -weight);
}
void SourceCharBonesMeshesOutput::scale_down_clip(const Gh2ClipPoseSamples& clip, float weight) {
  // 16B1D0: full, one, delta, then generated FacingBones inventory. Preserve
  // repeated membership across pages: this is four calls, not a set union.
  for (const auto& page : clip.pages) scale_down(page.channels,weight);
  scale_down(facing_delta_channels(clip.pages[Gh2ClipPoseSamples::Full]),weight);
}
void SourceCharBonesMeshesOutput::scale_add_clip(const Gh2ClipPoseSamples& clip,
    float previous_phase, float current_phase, float weight) {
  // Original 16B2F0 publication order: one sample 0, full, delta, facing.
  scale_add(clip.pages[Gh2ClipPoseSamples::One],0,weight);
  const auto& full = clip.pages[Gh2ClipPoseSamples::Full];
  scale_add_at_phase(full,current_phase,weight);
  scale_add_delta(clip.pages[Gh2ClipPoseSamples::Delta],previous_phase,current_phase,weight);
  const auto& generated = facing_delta_channels(full);
  if (generated.empty()) return;
  const auto previous = sample_facing_channel(full,"bone_facing.pos",previous_phase);
  auto delta = sample_facing_channel(full,"bone_facing.pos",current_phase);
  for (int i=0;i<3;++i) delta[i] -= previous[i];
  if (generated.size() == 2) {
    const float old_angle = sample_facing_channel(full,"bone_facing.rotz",previous_phase)[0];
    const float new_angle = sample_facing_channel(full,"bone_facing.rotz",current_phase)[0];
    constexpr float pi=3.1415927410125732421875f;
    float angle = std::fmod(new_angle-old_angle+pi,2*pi);
    if (angle < 0) angle += 2*pi;
    angle -= pi;
    const float c=std::sin(-old_angle+1.57079637050628662109375f), s=std::sin(-old_angle);
    const float x=delta[0], y=delta[1];
    delta[0]=x*c-y*s; delta[1]=x*s+y*c;
    *require_channel(generated[1]) += angle*weight;
  }
  float* destination=require_channel(generated[0]);
  for (int i=0;i<3;++i) destination[i] += delta[i]*weight;
}
void SourceCharBonesMeshesOutput::pose_meshes(const DirtyTransform& dirty) {
  const auto publish = [&](std::size_t type, auto&& apply) {
    for (std::size_t i = counts_[type]; i < counts_[type+1]; ++i) {
      auto& row = rows_[i];
      apply(*row.local, data_.get() + row.byte_offset / sizeof(float), i);
    }
  };
  publish(0, [&](Xfm& x, float* v, std::size_t) {
    std::copy_n(v, 3, x.pos);
    if (dirty) dirty(x); // Position store is in the original call's delay slot.
  });
  publish(2, [&](Xfm& x, float* v, std::size_t) {
    normalize(v, 4);
    if (dirty) dirty(x);
    quat_matrix(v, x);
  });
  for (int axis = 0; axis < 3; ++axis)
    publish(3 + axis, [&](Xfm& x, float* v, std::size_t) {
      if (dirty) dirty(x);
      absolute_axis(x, axis, *v);
    });
  const auto delta_count = delta_scalar_count();
  if (delta_count) {
    if (++normalization_cursor_ >= delta_count) normalization_cursor_ = 0;
    for (int axis = 0; axis < 3; ++axis)
      publish(6 + axis, [&](Xfm& x, float* v, std::size_t i) {
        if (dirty) dirty(x);
        delta_axis(x, axis, *v);
        if (i - counts_[6] == normalization_cursor_) {
          if (dirty) dirty(x);
          normalize_basis(x);
        }
      });
  }
  publish(1, [&](Xfm& x, float* v, std::size_t) {
    if (dirty) dirty(x);
    const auto current = get_scale(x);
    for (int i = 0; i < 3; ++i) {
      // Do not silently replace a malformed/zero-length source basis with
      // identity. Full EE exceptional floating-point emulation is not here.
      if (current[i] == 0)
        throw std::runtime_error("degenerate GH2 scale target basis");
      const float ratio = v[i] / current[i];
      for (float& component : x.rot[i]) component *= ratio;
    }
  });
}

}  // namespace ghogx::character

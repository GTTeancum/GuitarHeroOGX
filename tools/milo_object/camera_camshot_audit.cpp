// Read-only, typed inventory of authored GH2 CamShot driver fields.
// No scene loading, GPU work, extraction, or mutation. Unsupported revisions
// fail so the report can never silently classify an unknown object as empty.
#include "milo.h"
#include "milo_object.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using SubPart = gh::milo_object::CamShotSubPart20;

bool same_subpart(const SubPart& a, const SubPart& b) {
    return a.object == b.object && a.part == b.part;
}

// CamShotFrame::SameTargets compares count, then unordered object/subpart
// membership. Authored order is not significant.
bool same_targets(const std::vector<SubPart>& a,
                  const std::vector<SubPart>& b) {
    if (a.size() != b.size()) return false;
    for (const auto& target : a) {
        if (std::none_of(b.begin(), b.end(), [&](const SubPart& other) {
                return same_subpart(target, other);
            })) return false;
    }
    return true;
}

void require_finite(float value, const char* field) {
    if (!std::isfinite(value))
        throw std::runtime_error(std::string("non-finite ") + field);
}

std::string clean_field(std::string value) {
    std::replace(value.begin(), value.end(), '\t', ' ');
    std::replace(value.begin(), value.end(), '\r', ' ');
    std::replace(value.begin(), value.end(), '\n', ' ');
    return value.empty() ? "-" : value;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: milo_camera_camshot_audit <milo_ps2>\n");
        return 2;
    }
    try {
        const auto bytes = gh::milo::read_file(argv[1]);
        const auto payload =
            gh::milo::inflate_payload(bytes, gh::milo::parse_header(bytes));
        const auto dir = gh::milo::parse_directory(payload);
        size_t count = 0;
        for (const auto& entry : dir.entries) {
            if (entry.type != "CamShot") continue;
            try {
                const auto shot =
                    gh::milo_object::parse_cam_shot20(entry.body_bytes);
                require_finite(shot.legacy_loop_frame, "path_ease");
                require_finite(shot.legacy_path_frame, "fade_time");
                require_finite(shot.legacy_category_frame, "selection_weight");

                size_t target_frames = 0;
                size_t parent_frames = 0;
                size_t parent_rotation_frames = 0;
                size_t same_target_pairs = 0;
                size_t different_target_pairs = 0;
                size_t target_appears_pairs = 0;
                size_t target_disappears_pairs = 0;
                size_t no_target_pairs = 0;
                for (size_t i = 0; i < shot.keyframes.size(); ++i) {
                    const auto& frame = shot.keyframes[i];
                    const auto has_target = [](const auto& targets) {
                        return std::any_of(targets.begin(), targets.end(),
                            [](const SubPart& ref) { return !ref.object.empty(); });
                    };
                    if (has_target(frame.targets)) ++target_frames;
                    if (!frame.parent.object.empty()) ++parent_frames;
                    if (frame.use_parent_rotation) ++parent_rotation_frames;
                    if (i == 0) continue;
                    const auto& prev = shot.keyframes[i - 1];
                    const bool prev_has = has_target(prev.targets);
                    const bool next_has = has_target(frame.targets);
                    if (!prev_has && !next_has) {
                        ++no_target_pairs;
                    } else if (!prev_has && next_has) {
                        ++target_appears_pairs;
                    } else if (prev_has && !next_has) {
                        ++target_disappears_pairs;
                    } else if (same_targets(prev.targets, frame.targets)) {
                        ++same_target_pairs;
                    } else {
                        ++different_target_pairs;
                    }
                }

                std::printf(
                    "SHOT\t%s\t%d\t%.9g\t%.9g\t%.9g\t%s\t%s\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%d\n",
                    clean_field(entry.name).c_str(), shot.animatable.rate,
                    shot.legacy_loop_frame, shot.legacy_path_frame,
                    shot.legacy_category_frame, clean_field(shot.path).c_str(),
                    clean_field(shot.category).c_str(), shot.keyframes.size(),
                    target_frames, parent_frames, parent_rotation_frames,
                    same_target_pairs, different_target_pairs,
                    target_appears_pairs, target_disappears_pairs,
                    no_target_pairs, shot.looping);
                // Every CamShot frame is source evidence. Limiting FRAME rows
                // to path-backed shots concealed static parent/target records
                // such as Theatre band_POV02x2w from the audit output.
                {
                    for (size_t i = 0; i < shot.keyframes.size(); ++i) {
                        const auto& f = shot.keyframes[i];
                        std::printf("FRAME\t%s\t%zu\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g",
                                    clean_field(entry.name).c_str(), i,
                                    f.duration, f.blend, f.blend_ease,
                                    f.field_of_view, f.screen_offset[0], f.screen_offset[1]);
                        for (float x : f.world_offset) std::printf("\t%.9g", x);
                        std::printf("\t%s\t%s\t%d", clean_field(f.parent.object).c_str(),
                                    clean_field(f.parent.part).c_str(), f.use_parent_rotation);
                        for (const auto& target : f.targets)
                            std::printf("\t%s:%s", clean_field(target.object).c_str(),
                                        clean_field(target.part).c_str());
                        std::printf("\n");
                    }
                }
                for (size_t i = 0; i < shot.hide_list.size(); ++i) {
                    std::printf("HIDE\t%s\t%zu\t%s\n",
                                clean_field(entry.name).c_str(), i,
                                clean_field(shot.hide_list[i]).c_str());
                }
                ++count;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "%s: %s\n", entry.name.c_str(),
                             error.what());
                return 1;
            }
        }
        std::printf("COUNT\t%zu\n", count);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

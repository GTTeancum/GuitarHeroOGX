// Read-only, typed inventory of authored CamShot fade_time. No scene loading,
// GPU, file extraction or mutation. Unsupported revisions fail, never count as 0.
#include "milo.h"
#include "milo_object.h"
#include <cmath>
#include <cstdio>
#include <exception>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: milo_camera_blur_audit <milo_ps2>\n");
        return 2;
    }
    try {
        const auto bytes = gh::milo::read_file(argv[1]);
        const auto payload = gh::milo::inflate_payload(bytes, gh::milo::parse_header(bytes));
        const auto dir = gh::milo::parse_directory(payload);
        size_t count = 0;
        for (const auto& entry : dir.entries) {
            if (entry.type != "CamShot") continue;
            try {
                const auto shot = gh::milo_object::parse_cam_shot20(entry.body_bytes);
                if (!std::isfinite(shot.legacy_path_frame))
                    throw std::runtime_error("non-finite fade_time");
                std::printf("SHOT\t%s\t%d\t%.9g\n", entry.name.c_str(),
                            shot.animatable.rate, shot.legacy_path_frame);
                ++count;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "%s: %s\n", entry.name.c_str(), error.what());
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

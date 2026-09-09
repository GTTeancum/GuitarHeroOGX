#include "skin_bind_retarget.h"
#include <cstdio>
#include <utility>

int main() {
    using Matrix = std::array<float, 12>;
    const Matrix identity = {1,0,0,0,1,0,0,0,1,0,0,0};
    const auto close = [](float a, float b) {
        if (std::abs(a-b) > 1.0e-5f) throw std::runtime_error("retarget mismatch");
    };
    try {
        // The same seam vertex may use different slot orders in adjacent meshes.
        // Both must land at the same point after translation and rotation.
        Matrix rotated = {0,1,0,-1,0,0,0,0,1,10,0,0};
        std::array<Matrix,4> deltas = {identity,rotated,identity,identity};
        std::array<float,3> a = {2,0,0}, an = {1,0,0};
        auto b = a, bn = an;
        gh::milo_convert::retarget_skin_vertex(a,an,{0.25f,0.75f,0,0},deltas);
        std::swap(deltas[0],deltas[1]);
        gh::milo_convert::retarget_skin_vertex(b,bn,{0.75f,0.25f,0,0},deltas);
        close(a[0],8); close(a[1],1.5f);
        for(size_t i=0;i<3;++i) {close(a[i],b[i]); close(an[i],bn[i]);}

        // Nonuniform body scaling must leave a transformed tangent perpendicular
        // to its transformed normal, unlike multiplying both by the same matrix.
        Matrix scale = {2,0,0,0,1,0,0,0,1,0,0,0};
        deltas[0]=scale; a={1,2,3}; an={1,1,0};
        gh::milo_convert::retarget_skin_vertex(a,an,{1,0,0,0},deltas);
        close(a[0],2); close(2*an[0]-an[1],0);

        bool rejected=false;
        try {gh::milo_convert::retarget_skin_vertex(a,an,{0,0,0,0},deltas);}
        catch(const std::runtime_error&) {rejected=true;}
        if(!rejected) throw std::runtime_error("empty weights accepted");
        deltas[0]={}; rejected=false;
        try {gh::milo_convert::retarget_skin_vertex(a,an,{1,0,0,0},deltas);}
        catch(const std::runtime_error&) {rejected=true;}
        if(!rejected) throw std::runtime_error("singular blend accepted");
    } catch(const std::exception& e) {
        std::fprintf(stderr,"skin_bind_retarget_test: %s\n",e.what()); return 1;
    }
    std::puts("skin_bind_retarget_test: all checks passed");
    return 0;
}

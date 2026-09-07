// Apply an audited native-space finger plan. Retain untouched object bodies,
// copy source vertex attributes, partition palettes, and remove the mic subtree.
#include "milo.h"
#include "milo_object.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

using namespace gh;
namespace {
struct Reader {
    std::ifstream f;
    explicit Reader(const char* p) : f(p, std::ios::binary) {}
    template<class T> T read() {
        T x{}; f.read(reinterpret_cast<char*>(&x), sizeof(x));
        if (!f) throw std::runtime_error("truncated finger plan");
        return x;
    }
    uint32_t count() {
        auto x = read<uint32_t>();
        if (x > 1000000) throw std::runtime_error("invalid plan count");
        return x;
    }
    std::string string() {
        auto n = count(); std::string x(n, '\0'); f.read(x.data(), n);
        if (!f) throw std::runtime_error("truncated string");
        return x;
    }
    std::array<float,12> xfm() {
        std::array<float,12> x{};
        for(auto& v : x) { v=read<float>(); if(!std::isfinite(v)) throw std::runtime_error("nonfinite xfm"); }
        return x;
    }
};
bool finger(const std::string& name) {
    return (name.rfind("bone_L-",0)==0 || name.rfind("bone_R-",0)==0) &&
        (name.find("thumb")!=std::string::npos || name.find("index")!=std::string::npos ||
         name.find("middlefinger")!=std::string::npos || name.find("ringfinger")!=std::string::npos ||
         name.find("pinky")!=std::string::npos);
}
void groups(milo_object::Mesh28& m) {
    m.group_sizes.clear(); m.group_sections.clear();
    for(size_t begin=0; begin<m.faces.size(); begin+=48) {
        size_t n=std::min(size_t(48),m.faces.size()-begin);
        m.group_sizes.push_back(static_cast<uint8_t>(n));
        milo_object::MeshStripResult g;
        for(size_t i=0;i<n;++i) {
            auto& face=m.faces[begin+i];
            g.strip_runs.insert(g.strip_runs.end(),face.begin(),face.end());
            g.cumulative_strip_lengths.push_back(static_cast<uint32_t>(g.strip_runs.size()));
        }
        m.group_sections.push_back(std::move(g));
    }
}
}
int main(int argc,char** argv) {
    if(argc!=4) {std::cerr<<"milo_finger_patch source.milo_ps2 plan.bin output.milo_ps2\n";return 2;}
    try {
        if(std::string(argv[1])==argv[3]) throw std::runtime_error("source overwrite refused");
        auto bytes=milo::read_file(argv[1]);
        auto d=milo::parse_directory(milo::inflate_payload(bytes,milo::parse_header(bytes)));
        if(!d.boundaries_exact || d.dir_version!=24) throw std::runtime_error("requires exact GH2 directory");
        Reader r(argv[2]);
        if(r.read<uint64_t>()!=0x0031544641524746ULL) throw std::runtime_error("bad finger plan magic");
        std::map<std::string,size_t> indices;
        for(size_t i=0;i<d.entries.size();++i) indices[d.entries[i].name]=i;
        const auto node_count=r.count();
        size_t new_nodes=0;
        for(uint32_t i=0;i<node_count;++i) {
            auto name=r.string(), parent=r.string(); auto local=r.xfm();
            if(!finger(name)) throw std::runtime_error("non-finger node edit refused: "+name);
            if(auto found=indices.find(name);found!=indices.end()) {
                auto& e=d.entries[found->second];
                if(e.type=="Trans") {
                    auto t=milo_object::parse_trans9(e.body_bytes);
                    t.parent=parent;t.local=local;e.body_bytes=milo_object::serialize_trans9(t);
                } else throw std::runtime_error("existing finger must be transform-only: "+name);
            } else {
                milo_object::Trans9 t;t.parent=parent;t.local=local;
                milo::Entry e;e.name=name;e.type="Trans";e.terminator_value=0xDEADDEAD;
                e.body_bytes=milo_object::serialize_trans9(t);
                indices[name]=d.entries.size(); d.entries.push_back(std::move(e)); ++new_nodes;
            }
        }
        std::map<std::string,std::vector<std::string>> replacements;
        size_t modified=0, chunks=0, faces=0;
        const auto mesh_count=r.count();
        for(uint32_t mi=0;mi<mesh_count;++mi) {
            auto name=r.string();auto found=indices.find(name);
            if(found==indices.end())throw std::runtime_error("unknown mesh: "+name);
            const auto original_entry=d.entries[found->second];
            auto source=milo_object::parse_mesh28(original_entry.body_bytes,d.dir_version);
            if(!source.has_bones)throw std::runtime_error("unskinned source refused");
            // Source vertex IDs and face IDs establish that only weights and
            // palettes change; geometry, UVs, winding and normals are retained.
            std::set<uint32_t> face_ids;
            const auto count=r.count();
            for(uint32_t ci=0;ci<count;++ci) {
                auto cname=r.string();auto m=source;
                m.vertices.clear();m.faces.clear();m.geometry_owner=cname;
                for(auto& slot:m.bone_slots){slot.bone=r.string();slot.offset=r.xfm();}
                std::vector<uint32_t> vertex_ids;
                const auto nv=r.count();
                if(nv>65535)throw std::runtime_error("mesh index overflow");
                for(uint32_t vi=0;vi<nv;++vi) {
                    auto id=r.count();if(id>=source.vertices.size())throw std::runtime_error("vertex ID out of range");
                    auto v=source.vertices[id];
                    for(auto& w:v.color_or_weights){w=r.read<float>();if(!std::isfinite(w))throw std::runtime_error("nonfinite weight");}
                    vertex_ids.push_back(id);m.vertices.push_back(v);
                }
                const auto nf=r.count();
                for(uint32_t fi=0;fi<nf;++fi) {
                    auto id=r.count();
                    if(id>=source.faces.size() || !face_ids.insert(id).second)throw std::runtime_error("face missing/duplicate");
                    std::array<uint16_t,3> f{};
                    for(size_t k=0;k<3;++k){auto v=r.count();if(v>=nv || vertex_ids[v]!=source.faces[id][k])throw std::runtime_error("face geometry changed");f[k]=static_cast<uint16_t>(v);}
                    m.faces.push_back(f);
                }
                groups(m);auto e=original_entry;e.name=cname;e.body_bytes=milo_object::serialize_mesh28(m,d.dir_version);
                if(ci==0){if(cname!=name)throw std::runtime_error("first chunk must preserve source name");d.entries[found->second]=e;}
                else {if(indices.count(cname))throw std::runtime_error("chunk name collision");indices[cname]=d.entries.size();d.entries.push_back(e);}
                replacements[name].push_back(cname);++chunks;faces+=nf;
            }
            if(face_ids.size()!=source.faces.size())throw std::runtime_error("source faces omitted");
            ++modified;
        }
        if(r.f.peek()!=std::char_traits<char>::eof())throw std::runtime_error("trailing plan bytes");
        // Follow factual parent links for role-only microphone removal.
        std::map<std::string,std::string> parents;
        for(const auto& e:d.entries) {
            if(e.type=="Trans")parents[e.name]=milo_object::parse_trans9(e.body_bytes).parent;
            else if(e.type=="Mesh")parents[e.name]=milo_object::parse_mesh28(e.body_bytes,d.dir_version).transformable.parent;
            else if(e.type=="Group")parents[e.name]=milo_object::parse_group12(e.body_bytes).transformable.parent;
        }
        std::set<std::string> removed{"bone_pos_mic.mesh"};
        bool more=true;while(more){more=false;for(const auto& p:parents)if(removed.count(p.second))more|=removed.insert(p.first).second;}
        for(auto& e:d.entries)if(e.type=="Group") {
            auto g=milo_object::parse_group12(e.body_bytes);auto old=g.objects;
            std::vector<std::string> out;
            for(const auto& n:g.objects) {
                if(removed.count(n))continue;
                if(replacements.count(n))out.insert(out.end(),replacements[n].begin(),replacements[n].end());
                else out.push_back(n);
            }
            if(out!=old){g.objects=out;e.body_bytes=milo_object::serialize_group12(g);}
        }
        auto before=d.entries.size();
        d.entries.erase(std::remove_if(d.entries.begin(),d.entries.end(),[&](const auto& e){return removed.count(e.name);}),d.entries.end());
        const auto payload=milo::serialize_directory(d);
        auto result=milo::serialize_container(milo::make_object_aligned_container(payload));
        auto verified=milo::parse_directory(milo::inflate_payload(result,milo::parse_header(result)));
        if(!verified.boundaries_exact || milo::serialize_directory(verified)!=payload)throw std::runtime_error("roundtrip mismatch");
        std::ofstream out(argv[3],std::ios::binary);out.write(reinterpret_cast<const char*>(result.data()),result.size());
        if(!out)throw std::runtime_error("output write failed");
        std::cout<<"FINGER_PATCH_OK new_nodes="<<new_nodes<<" modified_meshes="<<modified<<" palette_chunks="<<chunks<<" source_faces="<<faces<<" removed_mic_nodes="<<(before-d.entries.size())<<" bytes="<<result.size()<<'\n';
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

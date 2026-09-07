// Add bend resolution without changing the source surface or proportions.
#include "milo.h"
#include "milo_object.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
using namespace gh;
using Face=std::array<uint16_t,3>;
using Edge=std::pair<uint16_t,uint16_t>;
static Edge edge(uint16_t a,uint16_t b){return std::minmax(a,b);}
static std::array<double,3> cross(const milo_object::MeshVertex& a,const milo_object::MeshVertex& b,const milo_object::MeshVertex& c){
    std::array<double,3> u{},v{},r{};
    for(int k=0;k<3;++k){u[k]=b.position[k]-a.position[k];v[k]=c.position[k]-a.position[k];}
    for(int k=0;k<3;++k)r[k]=u[(k+1)%3]*v[(k+2)%3]-u[(k+2)%3]*v[(k+1)%3];
    return r;
}
int main(int argc,char** argv){
 try{
    if(argc!=4)throw std::runtime_error("milo_hand_subdivide source output passes");
    if(std::string(argv[1])==argv[2])throw std::runtime_error("source overwrite refused");
    int passes=std::stoi(argv[3]);if(passes<1||passes>3)throw std::runtime_error("passes must be 1..3");
    auto b=milo::read_file(argv[1]);auto d=milo::parse_directory(milo::inflate_payload(b,milo::parse_header(b)));
    if(!d.boundaries_exact||d.dir_version!=24)throw std::runtime_error("exact GH2 directory required");
    size_t meshes=0,added=0,face_added=0;double max_area_error=0;
    for(auto& e:d.entries){
        if(e.type!="Mesh")continue;
        auto m=milo_object::parse_mesh28(e.body_bytes,d.dir_version);if(!m.has_bones)continue;
        std::array<bool,4> hand{};bool any=false;
        for(int i=0;i<4;++i){auto n=m.bone_slots[i].bone;hand[i]=n=="bone_L-hand.mesh"||n=="bone_R-hand.mesh"||n.rfind("bone_L-thumb",0)==0||n.rfind("bone_R-thumb",0)==0;any|=hand[i];}
        if(!any)continue;
        const auto original=m;
        std::vector<size_t> source_faces(m.faces.size());for(size_t i=0;i<source_faces.size();++i)source_faces[i]=i;
        for(int pass=0;pass<passes;++pass){
            std::map<Edge,uint16_t> mids;
            auto pure=[&](uint16_t vi){float sum=0;for(int k=0;k<4;++k){auto w=m.vertices[vi].color_or_weights[k];if(!hand[k]&&std::abs(w)>1e-6f)return false;if(hand[k])sum+=w;}return sum>.99f;};
            for(auto f:m.faces)if(pure(f[0])&&pure(f[1])&&pure(f[2]))for(int k=0;k<3;++k)mids.emplace(edge(f[k],f[(k+1)%3]),0);
            if(mids.empty())break;
            for(auto& [ab,vi]:mids){
                if(m.vertices.size()>=65535)throw std::runtime_error("vertex overflow");
                const auto a=m.vertices[ab.first],b=m.vertices[ab.second];auto v=a;
                for(int k=0;k<3;++k){v.position[k]=(a.position[k]+b.position[k])*.5f;v.normal[k]=(a.normal[k]+b.normal[k])*.5f;}
                for(int k=0;k<2;++k)v.uv[k]=(a.uv[k]+b.uv[k])*.5f;
                for(int k=0;k<4;++k)v.color_or_weights[k]=(a.color_or_weights[k]+b.color_or_weights[k])*.5f;
                vi=static_cast<uint16_t>(m.vertices.size());m.vertices.push_back(v);
            }
            std::vector<Face> faces;std::vector<size_t> parents;
            for(size_t fi=0;fi<m.faces.size();++fi){
                auto f=m.faces[fi];int count=0;for(int k=0;k<3;++k)count+=mids.count(edge(f[k],f[(k+1)%3]))!=0;
                auto add=[&](uint16_t a,uint16_t b,uint16_t c){faces.push_back({a,b,c});parents.push_back(source_faces[fi]);};
                if(!count){add(f[0],f[1],f[2]);continue;}
                if(count==3){auto a=mids.at(edge(f[0],f[1])),b=mids.at(edge(f[1],f[2])),c=mids.at(edge(f[2],f[0]));add(f[0],a,c);add(a,f[1],b);add(c,b,f[2]);add(a,b,c);continue;}
                // Rotate so split edges are AB (one) or AB, BC (two).
                while(!mids.count(edge(f[0],f[1]))||(count==2&&!mids.count(edge(f[1],f[2]))))std::rotate(f.begin(),f.begin()+1,f.end());
                auto a=mids.at(edge(f[0],f[1]));
                if(count==1){add(f[0],a,f[2]);add(a,f[1],f[2]);}
                else{auto b=mids.at(edge(f[1],f[2]));add(a,f[1],b);add(f[0],a,f[2]);add(a,b,f[2]);}
            }
            m.faces=std::move(faces);source_faces=std::move(parents);
        }
        if(m.vertices.size()==original.vertices.size())continue;
        // Every emitted triangle is a midpoint partition of its source face.
        // Verify signed area/normal sum; source positions/UVs/weights are never moved.
        std::vector<std::array<double,3>> sums(original.faces.size());
        for(size_t i=0;i<m.faces.size();++i){auto f=m.faces[i];auto c=cross(m.vertices[f[0]],m.vertices[f[1]],m.vertices[f[2]]);for(int k=0;k<3;++k)sums[source_faces[i]][k]+=c[k];}
        for(size_t i=0;i<original.faces.size();++i){auto f=original.faces[i];auto c=cross(original.vertices[f[0]],original.vertices[f[1]],original.vertices[f[2]]);for(int k=0;k<3;++k)max_area_error=std::max(max_area_error,std::abs(sums[i][k]-c[k]));}
        if(max_area_error>0.001)throw std::runtime_error("source surface conservation failed");
        m.group_sizes.clear();m.group_sections.clear();
        for(size_t begin=0;begin<m.faces.size();begin+=48){size_t n=std::min(size_t(48),m.faces.size()-begin);m.group_sizes.push_back(static_cast<uint8_t>(n));milo_object::MeshStripResult g;for(size_t i=0;i<n;++i){auto f=m.faces[begin+i];g.strip_runs.insert(g.strip_runs.end(),f.begin(),f.end());g.cumulative_strip_lengths.push_back(static_cast<uint32_t>(g.strip_runs.size()));}m.group_sections.push_back(std::move(g));}
        e.body_bytes=milo_object::serialize_mesh28(m,d.dir_version);++meshes;added+=m.vertices.size()-original.vertices.size();face_added+=m.faces.size()-original.faces.size();
    }
    auto payload=milo::serialize_directory(d);auto output=milo::serialize_container(milo::make_object_aligned_container(payload));
    auto check=milo::parse_directory(milo::inflate_payload(output,milo::parse_header(output)));if(!check.boundaries_exact||milo::serialize_directory(check)!=payload)throw std::runtime_error("roundtrip mismatch");
    std::ofstream out(argv[2],std::ios::binary);out.write(reinterpret_cast<const char*>(output.data()),output.size());if(!out)throw std::runtime_error("write failed");
    std::cout<<"HAND_SUBDIVIDE_OK meshes="<<meshes<<" added_vertices="<<added<<" added_faces="<<face_added<<" max_signed_area_error="<<max_area_error<<'\n';
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

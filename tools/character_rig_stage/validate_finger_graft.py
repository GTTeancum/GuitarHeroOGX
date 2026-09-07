"""Validate a patched native MILO export against source and transfer plan."""
import argparse
import json
import struct
from pathlib import Path
import numpy as np
from transfer_finger_weights import Rig, finger, hand_lobes, points

def read_plan(path):
    with Path(path).open('rb') as f:
        def u():return struct.unpack('<I',f.read(4))[0]
        def s():return f.read(u()).decode()
        assert f.read(8)==b'FGRAFT1\0'
        for _ in range(u()):s();s();f.read(48)
        result={}
        for _ in range(u()):
            name=s();chunks=[]
            for _ in range(u()):
                chunk=s()
                for _ in range(4):s();f.read(48)
                ids=[]
                for _ in range(u()):ids.append(u());f.read(16)
                faces=[]
                for _ in range(u()):faces.append((u(),[u(),u(),u()]))
                chunks.append((chunk,ids,faces))
            result[name]=chunks
        assert not f.read()
        return result

def validate(source,candidate,plan,anatomical_regions=False):
    originals=Rig(source);patched=Rig(candidate);changes=read_plan(plan)
    max_bind=0.;max_sum=0.;max_nonhand=0.;face_count=0;vertices=0
    def named(m):
        return {s['name']:np.asarray(m['weights'])[:,i] for i,s in enumerate(m['bone_slots']) if s['name']}
    for name,m in originals.meshes.items():
        if name not in patched.meshes:continue # explicitly removed mic subtree
        chunks=changes.get(name,[(name,list(range(len(m['positions']))),[(i,f) for i,f in enumerate(m['faces'])])])
        before=originals.skin_bind(m);old=named(m);seen=[]
        for cname,ids,faces in chunks:
            current=patched.meshes[cname];after=patched.skin_bind(current)
            ids=np.asarray(ids,int);new=named(current)
            assert np.array_equal(np.asarray(current['positions']),np.asarray(m['positions'])[ids]),cname+' moved raw vertices'
            if len(ids):max_bind=max(max_bind,float(np.linalg.norm(after-before[ids],axis=1).max()))
            if current['has_bones']:
                max_sum=max(max_sum,float(np.max(np.abs(np.asarray(current['weights']).sum(1)-np.asarray(m['weights'])[ids].sum(1)))))
            for n,w in old.items():
                if n in ('bone_L-hand.mesh','bone_R-hand.mesh') or finger(n,'L') or finger(n,'R'):continue
                max_nonhand=max(max_nonhand,float(np.max(np.abs(new.get(n,np.zeros(len(ids)))-w[ids]))))
            for fi,face in faces:
                assert list(ids[face])==m['faces'][fi],cname+' changed winding/topology'
                seen.append(fi)
            vertices+=len(ids)
        assert sorted(seen)==list(range(len(m['faces']))),name+' missing/duplicated faces'
        face_count+=len(seen)
    new_fingers=[n for n in patched.nodes if finger(n,'L') or finger(n,'R')]
    changed_nonfinger=[n for n in originals.nodes if n in patched.nodes and not (finger(n,'L') or finger(n,'R')) and originals.nodes[n]['local']!=patched.nodes[n]['local']]
    assert not changed_nonfinger,changed_nonfinger
    removed=sorted(set(originals.nodes)-set(patched.nodes))
    assert removed==['bone_pos_mic.mesh','mic_stand.mesh'],removed
    assert len(new_fingers)==30
    assert max_nonhand<2e-6
    assert max_sum<2e-6
    assert max_bind<.002,f'Bind shape changed by {max_bind}'
    digit_vertices={}
    for side in ('L','R'):
        for token in ('thumb','index','middlefinger','ringfinger','pinky'):
            count=0
            for m in patched.meshes.values():
                if not m['has_bones'] or not m['positions']:continue
                indices=[i for i,s in enumerate(m['bone_slots']) if finger(s['name'],side) and token in s['name']]
                if indices:count+=int(np.sum(np.asarray(m['weights'])[:,indices].sum(1)>.25))
            digit_vertices[f'{side}_{token}']=count
    assert min(digit_vertices.values())>=4,digit_vertices
    skin_checks={}
    if anatomical_regions:
        wrong_digit=[];seam_difference=0.;checked=0
        for side in ('L','R'):
            lobes=hand_lobes(originals,side)
            thumb=min(lobes,key=lambda i:lobes[i][:,0].max())
            order=sorted((i for i in lobes if i!=thumb),key=lambda i:abs(lobes[i][:,2].mean()-lobes[thumb][:,2].mean()))
            labels={tuple(np.round(p,5)):token for i,token in zip([thumb,*order],('thumb','index','middlefinger','ringfinger','pinky')) for p in lobes[i]}
            hand=f'bone_{side}-hand.mesh'
            for name,chunks in changes.items():
                original=originals.meshes[name]
                if not any(s['name']==hand for s in original['bone_slots']):continue
                p=points(np.linalg.inv(originals.world(hand)),originals.skin_bind(original));groups={}
                for cname,ids,faces in chunks:
                    m=patched.meshes[cname]
                    for vi,source_id in enumerate(ids):
                        point=tuple(np.round(p[source_id],5));token=labels.get(point)
                        row={s['name']:m['weights'][vi][i] for i,s in enumerate(m['bone_slots']) if s['name'] and m['weights'][vi][i]}
                        if token:
                            checked+=1
                            if any(finger(n,side) and token not in n and abs(w)>2e-6 for n,w in row.items()):wrong_digit.append((cname,vi,token,row))
                        external=tuple((n,round(w,6)) for n,w in sorted(row.items()) if n!=hand and not finger(n,side))
                        key=(point,external)
                        if key in groups:
                            other=groups[key]
                            seam_difference=max(seam_difference,max(abs(row.get(n,0)-other.get(n,0)) for n in set(row)|set(other)))
                        else:groups[key]=row
        assert not wrong_digit,f'{len(wrong_digit)} vertices driven by neighboring fingers; first={wrong_digit[:1]}'
        assert seam_difference<2e-6,f'Duplicate seam weight mismatch: {seam_difference}'
        skin_checks={'anatomical_vertices_checked':checked,'cross_digit_violations':0,'maximum_duplicate_seam_weight_difference':seam_difference}
    return {'status':'pass','finger_nodes':len(new_fingers),'vertices_checked':vertices,'faces_checked':face_count,'max_bind_vertex_error':max_bind,'max_weight_sum_error':max_sum,'max_nonhand_weight_error':max_nonhand,'nonfinger_transforms_changed':changed_nonfinger,'removed_nodes':removed,'digit_weighted_vertices':digit_vertices,**skin_checks}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source');p.add_argument('candidate');p.add_argument('plan');p.add_argument('--output',type=Path);p.add_argument('--anatomical-regions',action='store_true');a=p.parse_args()
    result=validate(a.source,a.candidate,a.plan,a.anatomical_regions)
    if a.output:a.output.write_text(json.dumps(result,indent=2))
    print(json.dumps(result))

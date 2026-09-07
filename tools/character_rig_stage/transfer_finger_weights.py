"""Transfer donor finger skinning in native MILO bind space (no Blender)."""
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path
import numpy as np


def matrix(values):
    x = np.eye(4)
    x[:3, :3] = np.asarray(values[:9]).reshape(3, 3).T
    x[:3, 3] = values[9:12]
    return x


def serialized(x):
    return [*x[:3, :3].T.flatten(), *x[:3, 3]]


def points(x, p):
    p = np.asarray(p, dtype=float)
    return p @ x[:3, :3].T + x[:3, 3]


def finger(name, side):
    return name.startswith(f"bone_{side}-") and any(
        token in name.lower() for token in ("thumb", "index", "middlefinger", "ringfinger", "pinky")
    )


class Rig:
    def __init__(self, path):
        self.path = Path(path)
        self.data = json.loads(self.path.read_text())
        self.nodes = {n['name']: n for n in self.data['nodes']}
        self.worlds = {}
        self.meshes = {m['name']: m for m in self.data['meshes']}
        self.visiting = set()

    def world(self, name):
        if not name or name not in self.nodes:
            return np.eye(4)
        if name not in self.worlds:
            if name in self.visiting:
                raise ValueError(f"Cyclic skeleton: {name}")
            self.visiting.add(name)
            n = self.nodes[name]
            self.worlds[name] = self.world(n['parent']) @ matrix(n['local'])
            self.visiting.remove(name)
        return self.worlds[name]

    def skin_bind(self, m):
        p = np.asarray(m['positions'])
        if not m['has_bones']:
            return points(self.world(m['name']), p)
        out = np.zeros_like(p)
        weights = np.asarray(m['weights'])
        for i, slot in enumerate(m['bone_slots']):
            if slot['name'] and np.any(weights[:, i]):
                out += points(self.world(slot['name']) @ matrix(slot['offset']), p) * weights[:, i, None]
        return out

    def hand_meshes(self, side, high_only=False):
        hand = f'bone_{side}-hand.mesh'
        for m in self.meshes.values():
            if high_only and ('lod1' in m['name'].lower() or 'shadow' in m['name'].lower()):
                continue
            if not m['positions'] or not m['has_bones']:
                continue
            weights = np.asarray(m['weights'])
            mask = np.array([s['name'] == hand or finger(s['name'], side) for s in m['bone_slots']])
            total = weights[:, mask].sum(axis=1)
            if not np.any(total > 0):
                continue
            p = points(np.linalg.inv(self.world(hand)), self.skin_bind(m))
            yield m, p, total


def inspect(rig, output):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.collections import PolyCollection
    fig, axes = plt.subplots(2, 3, figsize=(14, 8))
    for row, side in enumerate(('L', 'R')):
        used = []
        for m, p, total in rig.hand_meshes(side, True):
            faces = np.asarray(m['faces'], dtype=int)
            faces = faces[np.min(total[faces], axis=1) > 0.6]
            if not len(faces):
                continue
            used.extend(p[np.unique(faces)])
            for ax, (a, b) in zip(axes[row], ((0, 1), (0, 2), (1, 2))):
                ax.add_collection(PolyCollection(p[faces][:, :, (a,b)], facecolors='#cad9df', edgecolors='#243947', linewidths=.6))
        for ax, (a, b) in zip(axes[row], ((0, 1), (0, 2), (1, 2))):
            for n in rig.nodes.values():
                if finger(n['name'], side):
                    inv = np.linalg.inv(rig.world(f'bone_{side}-hand.mesh'))
                    h = (inv @ rig.world(n['name']))[:3, 3]
                    t = (inv @ rig.world(n['parent']))[:3, 3]
                    ax.plot([h[a], t[a]], [h[b], t[b]], '-o', c='crimson', lw=1.3, markersize=3)
            ax.autoscale_view()
            ax.set_aspect('equal')
            ax.set_title(f'{side} hand / axes {a},{b}')
            ax.grid(alpha=.2)
        if used:
            p = np.asarray(used)
            print(side, 'vertices', len(p), 'bounds', p.min(axis=0).round(3), p.max(axis=0).round(3))
    fig.suptitle(rig.path.stem + ' — raw MILO bind skin in hand space')
    fig.tight_layout()
    fig.savefig(output, dpi=130)
    plt.close(fig)


def closest_surface(p, triangles):
    """Exact nearest point/barycentrics over triangles, including their edges."""
    a, b, c = triangles[:, 0], triangles[:, 1], triangles[:, 2]
    ab, ac, ap = b-a, c-a, p-a
    d00 = np.einsum('ij,ij->i', ab, ab)
    d01 = np.einsum('ij,ij->i', ab, ac)
    d11 = np.einsum('ij,ij->i', ac, ac)
    d20 = np.einsum('ij,ij->i', ap, ab)
    d21 = np.einsum('ij,ij->i', ap, ac)
    det = d00*d11-d01*d01
    valid = np.abs(det)>1e-14
    v = np.divide(d11*d20-d01*d21, det, out=np.zeros_like(det), where=valid)
    w = np.divide(d00*d21-d01*d20, det, out=np.zeros_like(det), where=valid)
    bary = np.stack((1-v-w, v, w),axis=1)
    q = np.einsum('ij,ijk->ik',bary,triangles)
    dist = np.sum((q-p)**2,axis=1)
    dist[~valid | np.any(bary<0,axis=1)]=np.inf
    for i,j in ((0,1),(1,2),(2,0)):
        start,end = triangles[:,i],triangles[:,j]
        edge=end-start
        denom=np.einsum('ij,ij->i',edge,edge)
        t=np.clip(np.divide(np.einsum('ij,ij->i',p-start,edge),denom,out=np.zeros_like(denom),where=denom>1e-14),0,1)
        candidate=start+edge*t[:,None]
        d=np.sum((candidate-p)**2,axis=1)
        better=d<dist
        bb=np.zeros_like(bary);bb[:,i]=1-t;bb[:,j]=t
        dist[better]=d[better];bary[better]=bb[better]
    index=int(np.argmin(dist))
    return index,bary[index],float(np.sqrt(dist[index]))


def fit_hand(target, donor, side):
    from scipy.spatial import cKDTree
    names=[f'bone_{side}-hand.mesh']+sorted(n for n in donor.nodes if finger(n,side))
    cloud=[];triangles=[];weights=[]
    for m,p,total in donor.hand_meshes(side,True):
        fs=np.asarray(m['faces'],dtype=int)
        fs=fs[np.min(total[fs],axis=1)>.6]
        if not len(fs):continue
        ws=np.zeros((len(p),len(names)))
        for i,s in enumerate(m['bone_slots']):
            if s['name'] in names: ws[:,names.index(s['name'])]+=np.asarray(m['weights'])[:,i]
        ws=np.maximum(ws,0);ws/=np.maximum(ws.sum(axis=1)[:,None],1e-12)
        triangles.extend(p[fs]);weights.extend(ws[fs]);cloud.extend(p[np.unique(fs)])
    src=np.asarray(cloud);triangles=np.asarray(triangles);weights=np.asarray(weights)
    target_points=[]
    for m,p,total in target.hand_meshes(side,True):
        target_points.extend(p[total>.99])
    dst=np.asarray(target_points)
    lo,hi=src.min(axis=0),src.max(axis=0)
    tlo,thi=dst.min(axis=0),dst.max(axis=0)
    scales=(thi-tlo)/(hi-lo)
    affine=np.eye(4);affine[:3,:3]=np.diag(scales);affine[:3,3]=tlo-scales*lo
    # Longitudinal curl differs between singers and guitarists. Fit the hand's
    # height as an affine function of donor height and finger length, using
    # surface correspondence in longitudinal/lateral hand space. This moves
    # only the donor rig; no target vertex is moved.
    mapped=points(affine,src)
    tree=cKDTree(dst[:,(0,2)])
    _,ix=tree.query(mapped[:,(0,2)],k=min(8,len(dst)))
    target_height=np.median(dst[ix,1],axis=1)
    design=np.column_stack((src[:,1],src[:,0],np.ones(len(src))))
    coeff=np.linalg.lstsq(design,target_height,rcond=None)[0]
    # Retain actual palm thickness even where projection loses one surface.
    coeff[0]=max(float(coeff[0]),float(scales[1])*.25)
    affine[1]=[coeff[1],coeff[0],0,coeff[2]]
    # Fit each anatomical lobe independently. An overall box fit cannot map a
    # short thumb beside a long index, or the singers' different resting curl.
    # Surface topology (not the singer's misleading single "thumb" weights)
    # identifies these lobes. Only donor coordinates are fitted.
    dst_lobes=hand_lobes(target,side)
    transforms={}
    tokens=('thumb','index','middlefinger','ringfinger','pinky')
    # Donor labels are anatomical and trustworthy; use them rather than its
    # disconnected UV/PS2-palette islands when identifying the donor lobes.
    flat=triangles.reshape(-1,3);flat_weights=weights.reshape(-1,len(names))
    src_lobes={token:np.unique(np.round(flat[flat_weights[:,[i for i,n in enumerate(names) if token in n]].sum(axis=1)>.5],5),axis=0) for token in tokens}
    source_thumb='thumb'
    target_thumb=min(dst_lobes,key=lambda i:dst_lobes[i][:,0].max())
    ordered_src=sorted((i for i in src_lobes if i!=source_thumb),key=lambda i:src_lobes[i][:,2].mean())
    ordered_dst=sorted((i for i in dst_lobes if i!=target_thumb),key=lambda i:dst_lobes[i][:,2].mean())
    correspondence={source_thumb:target_thumb,**dict(zip(ordered_src,ordered_dst))}
    for token in tokens:
        transforms[token]=fit_lobe(src_lobes[token],dst_lobes[correspondence[token]])
    mapped_flat=points(affine,flat)*flat_weights[:,0,None]
    for token,tr in transforms.items():
        mass=flat_weights[:,[i for i,n in enumerate(names) if token in n]].sum(axis=1)
        mapped_flat+=points(tr,flat)*mass[:,None]
    mapped_triangles=mapped_flat.reshape(-1,3,3)
    hand=f'bone_{side}-hand.mesh'
    donor_hand_inverse=np.linalg.inv(donor.world(hand))
    relative={}
    for n in names[1:]:
        rel=donor_hand_inverse@donor.world(n)
        tr=transforms[next(token for token in tokens if token in n)]
        rel[:3,3]=points(tr,rel[None,:3,3])[0]
        # Apply only the rotation part of the affine map to joint axes.
        u,_,vt=np.linalg.svd(tr[:3,:3]@rel[:3,:3])
        rel[:3,:3]=u@vt
        relative[n]=rel
    nodes=[]
    for n in names[1:]:
        parent=donor.nodes[n]['parent']
        parent_rel=np.eye(4) if parent==hand else relative[parent]
        nodes.append({'name':n,'parent':parent,'local':serialized(np.linalg.inv(parent_rel)@relative[n])})
    regions={token:dst_lobes[correspondence[token]] for token in tokens}
    return names,relative,nodes,mapped_triangles,weights,affine,regions


def hand_lobes(rig,side):
    """Find the five free finger lobes by cutting back towards the palm.

    Each lobe grows until it touches another digit's seed. This is independent
    of UV seams and existing hand weights, and fails closed on mitten meshes.
    """
    from scipy.sparse import csr_matrix
    from scipy.sparse.csgraph import connected_components
    ps=[];fs=[]
    for m,p,total in rig.hand_meshes(side,True):
        f=np.asarray(m['faces'],int);f=f[np.min(total[f],axis=1)>.6]
        fs.extend(f+len(ps));ps.extend(p)
    p=np.asarray(ps);f=np.asarray(fs)
    p,ix=np.unique(np.round(p,5),axis=0,return_inverse=True);f=ix[f]
    used=np.unique(f);active=np.zeros(len(p),bool);active[used]=True
    edges=np.concatenate((f[:,(0,1)],f[:,(1,2)],f[:,(2,0)]))
    def components(cut):
        mask=active&(p[:,0]>cut)
        e=edges[np.all(mask[edges],axis=1)]
        graph=csr_matrix((np.ones(e.size),(e.flatten(),e[:,::-1].flatten())),shape=(len(p),)*2)
        _,label=connected_components(graph,directed=False)
        return mask,label
    seeds=None
    for ratio in np.arange(.30,.71,.01):
        mask,label=components(p[used,0].max()*ratio)
        groups=[np.flatnonzero(mask&(label==lab)) for lab in np.unique(label[mask])]
        groups=[g for g in groups if len(g)>=3]
        if len(groups)==5:
            seeds=[int(g[np.argmax(p[g,0])]) for g in groups];break
    if seeds is None:raise ValueError(f'{rig.path.stem}: {side} hand does not expose five separate finger lobes')
    result={i:p[[s]] for i,s in enumerate(seeds)}
    for cut in sorted(set(p[used,0]),reverse=True):
        mask,label=components(cut-1e-6)
        for i,s in enumerate(seeds):
            if not mask[s]:continue
            group=mask&(label==label[s])
            if sum(bool(group[t]) for t in seeds)==1 and group.sum()>len(result[i]):
                result[i]=p[group]
    return result


def fit_lobe(source,target):
    """Map a digit's length, thickness and resting slope, not target vertices."""
    tr=np.eye(4)
    span=source[:,0].max()-source[:,0].min()
    tr[0,0]=(target[:,0].max()-target[:,0].min())/span
    tr[0,3]=target[:,0].min()-source[:,0].min()*tr[0,0]
    for axis in (1,2):
        sm=np.column_stack((source[:,0],np.ones(len(source))))
        tm=np.column_stack((target[:,0],np.ones(len(target))))
        ss=np.linalg.lstsq(sm,source[:,axis],rcond=None)[0]
        ts=np.linalg.lstsq(tm,target[:,axis],rcond=None)[0]
        sr=source[:,axis]-sm@ss;tt=target[:,axis]-tm@ts
        scale=np.ptp(tt)/max(np.ptp(sr),1e-6)
        tr[axis,axis]=scale
        tr[axis,0]=ts[0]*tr[0,0]-scale*ss[0]
        tr[axis,3]=ts[0]*tr[0,3]+ts[1]-scale*ss[1]
    return tr


def anatomical_surface(target,side,names,triangles,donor_weights,regions):
    """Transfer inside corresponding digits, then use that target surface for LODs.

    Unconstrained proximity can jump between adjacent curled fingers. Native
    target topology determines the digit; donor labels determine its weights.
    Palm vertices remain attached to the hand, not a nearby distal phalanx.
    """
    point_regions={tuple(np.round(p,5)):token for token,ps in regions.items() for p in ps}
    allowed={token:np.array([n==names[0] or token in n for n in names]) for token in regions}
    masks={token:(donor_weights[:,:,mask].sum(2).min(1)>.98)&(donor_weights[:,:,np.array([token in n for n in names])].sum(2).max(1)>.05) for token,mask in allowed.items()}
    cache={};surface=[];surface_weights=[];edges=set()
    for m,p,total in target.hand_meshes(side,True):
        weights=np.zeros((len(p),len(names)))
        for i,v in enumerate(p):
            key=tuple(np.round(v,5))
            if key not in cache:
                w=np.zeros(len(names));token=point_regions.get(key)
                if token is None:w[0]=1.
                else:
                    selected=masks[token]
                    assert np.any(selected),f'No donor surface for {side} {token}'
                    ix,bary,_=closest_surface(v,triangles[selected])
                    w=bary@donor_weights[selected][ix]
                    w[~allowed[token]]=0;w/=w.sum()
                cache[key]=w
            weights[i]=cache[key]
        fs=np.asarray(m['faces'],int);fs=fs[np.min(total[fs],axis=1)>.6]
        surface.extend(p[fs]);surface_weights.extend(weights[fs])
        for face in fs:
            keys=[tuple(np.round(p[i],5)) for i in face]
            for i in range(3):edges.add(tuple(sorted((keys[i],keys[(i+1)%3]))))
    # The topology cut identifies digits, but is not a hard skinning seam.
    # Blend palm/proximal-joint mass across its boundary; retain donor distal
    # weights and forbid adjacent digits from leaking into a free finger.
    neighbours={k:set() for k in cache}
    for a,b in edges:neighbours[a].add(b);neighbours[b].add(a)
    roots=np.array([n==names[0] or n.endswith('01.mesh') for n in names])
    for _ in range(3):
        updated={}
        for key,w in cache.items():
            mass=w[roots].sum();ns=neighbours[key]
            if mass<1e-9 or not ns:updated[key]=w;continue
            average=.5*w+.5*np.mean([cache[k] for k in ns],axis=0)
            mask=roots.copy();token=point_regions.get(key)
            if token is not None:mask&=allowed[token]
            average[~mask]=0
            if average.sum()<1e-9:updated[key]=w;continue
            new=w.copy();new[roots]=0;new+=average/average.sum()*mass;updated[key]=new
        cache=updated
    surface=np.asarray(surface)
    surface_weights=np.array([[cache[tuple(np.round(p,5))] for p in face] for face in surface])
    return cache,surface,surface_weights,point_regions


def transfer(target,donor,anatomical_regions=False,fit_target=None):
    all_nodes=[];changes={};reports=[]
    for side in ('L','R'):
        names,relative,nodes,triangles,donor_weights,affine,regions=fit_hand(fit_target or target,donor,side)
        if fit_target is not None:
            # Subdivision must not change anatomical joint placement. Fit the
            # unchanged original surface, then label the denser surface's lobes.
            dense=hand_lobes(target,side)
            order=sorted(regions,key=lambda t:regions[t][:,2].mean())
            ids=sorted(dense,key=lambda i:dense[i][:,2].mean())
            regions={t:dense[i] for t,i in zip(order,ids)}
        if anatomical_regions:
            exact,transfer_triangles,transfer_weights,point_regions=anatomical_surface(target,side,names,triangles,donor_weights,regions)
        all_nodes.extend(nodes);hand=names[0];side_report={'side':side,'grafted_nodes':len(nodes),'fit_affine':affine.tolist(),'vertices':0,'maximum_surface_distance':0.0,'finger_mass':{n:0.0 for n in names[1:]}}
        for m,p,total in target.hand_meshes(side):
            # Each weight retains the original mesh-slot bind transform. New
            # offsets factor through that same hand bind, including GH1
            # source meshes which have a distinct mesh object transform.
            slots=m['bone_slots'];old=np.asarray(m['weights'])
            hand_slot=next((s for s in slots if s['name']==hand),None)
            if hand_slot is None:raise ValueError('finger-only target requires explicit hand bind')
            hand_offset=matrix(hand_slot['offset'])
            offsets={s['name']:matrix(s['offset']) for s in slots if s['name']}
            offsets.update({n:np.linalg.inv(relative[n])@hand_offset for n in names[1:]})
            new=[];changed=0
            for vi,v in enumerate(p):
                original={s['name']:float(old[vi,i]) for i,s in enumerate(slots) if s['name'] and old[vi,i]!=0}
                amount=sum(w for n,w in original.items() if n in names)
                if amount<=0:
                    new.append(original);continue
                if anatomical_regions:
                    key=tuple(np.round(v,5))
                    if key in exact:ws=exact[key].copy();distance=0.
                    else:
                        idx,bary,distance=closest_surface(v,transfer_triangles)
                        ws=bary@transfer_weights[idx]
                else:
                    idx,bary,distance=closest_surface(v,triangles)
                    ws=bary@donor_weights[idx]
                # Surface transfer only redistributes the existing hand mass.
                # Signed forearm/upperarm weights and their offsets are exact.
                ws[ws<.01]=0
                largest=np.argsort(ws)[-2:];bounded=np.zeros_like(ws);bounded[largest]=ws[largest]
                bounded/=bounded.sum()
                values={n:w for n,w in original.items() if n not in names}
                for n,w in zip(names,bounded):
                    if w>0:values[n]=float(w*amount)
                new.append(values)
                if any(finger(n,side) and w>0 for n,w in values.items()):
                    changed+=1;side_report['maximum_surface_distance']=max(side_report['maximum_surface_distance'],distance)
                for n,w in values.items():
                    if n in side_report['finger_mass']:side_report['finger_mass'][n]+=w
            side_report['vertices']+=changed
            changes[m['name']]={'source':m,'weights':new,'offsets':offsets,'side':side}
            if anatomical_regions:
                changes[m['name']]['weld_keys']=[(tuple(np.round(v,5)),tuple((n,round(w,6)) for n,w in sorted(row.items()) if n not in names)) for v,row in zip(p,new)]
        reports.append(side_report)
    return all_nodes,changes,reports


def partition(change):
    m=change['source'];weights=change['weights'];faces=m['faces'];side=change['side']
    # Mesh28 has four bones per entire mesh, not just per vertex. Simplify
    # tiny hand influences consistently per source vertex before grouping;
    # this avoids independently trimmed weights cracking split-mesh seams.
    drops=0
    groups={}
    for vi,key in enumerate(change.get('weld_keys',range(len(weights)))):groups.setdefault(key,[]).append(vi)
    twins={vi:group for group in groups.values() for vi in group}
    while True:
        bad=[]
        for fi,face in enumerate(faces):
            palette=set(n for vi in face for n,w in weights[vi].items() if w!=0)
            if len(palette)>4:bad.append((fi,face,palette))
        if not bad:break
        fi,face,palette=bad[0]
        candidates=[]
        for n in palette:
            if n!=f'bone_{side}-hand.mesh' and not finger(n,side):continue
            touched=sorted({twin for vi in set(face) if n in weights[vi] for twin in twins[vi] if n in weights[twin]})
            if all(sum(1 for name,w in weights[vi].items() if (finger(name,side) or name==f'bone_{side}-hand.mesh') and w>0)>1 for vi in touched):
                candidates.append((sum(abs(weights[vi][n]) for vi in touched),n,touched))
        if not candidates:raise ValueError(f'Cannot partition source triangle {m["name"]}:{fi} without changing non-hand skin')
        _,n,touched=min(candidates)
        for vi in touched:
            mass=weights[vi].pop(n)
            dest=max((name for name in weights[vi] if finger(name,side) or name==f'bone_{side}-hand.mesh'),key=lambda name:weights[vi][name])
            weights[vi][dest]+=mass;drops+=1
    chunks=[]
    for fi,face in enumerate(faces):
        palette={n for vi in face for n,w in weights[vi].items() if w!=0}
        choices=[(len(c['palette']|palette),i) for i,c in enumerate(chunks) if len(c['palette']|palette)<=4]
        if choices:i=min(choices)[1]
        else:i=len(chunks);chunks.append({'palette':set(),'faces':[]})
        chunks[i]['palette'].update(palette);chunks[i]['faces'].append(fi)
    for c in chunks:c['palette']=sorted(c['palette'])
    return chunks,drops


def write_plan(path,nodes,changes):
    with path.open('wb') as out:
        def uint(x):out.write(struct.pack('<I',x))
        def string(s):b=s.encode();uint(len(b));out.write(b)
        def xfm(x):out.write(struct.pack('<12f',*serialized(x)))
        out.write(b'FGRAFT1\0');uint(len(nodes))
        for n in nodes:string(n['name']);string(n['parent']);xfm(matrix(n['local']))
        uint(len(changes));summary=[]
        for name,change in changes.items():
            chunks,drops=partition(change);m=change['source'];string(name);uint(len(chunks))
            for ci,c in enumerate(chunks):
                string(name if ci==0 else name.removesuffix('.mesh')+f'.fingers{ci}.mesh')
                palette=c['palette']
                for i in range(4):
                    n=palette[i] if i<len(palette) else '';string(n);xfm(change['offsets'][n] if n else np.eye(4))
                vertices=sorted(set(v for fi in c['faces'] for v in m['faces'][fi]));indices={v:i for i,v in enumerate(vertices)}
                uint(len(vertices))
                for vi in vertices:
                    uint(vi);out.write(struct.pack('<4f',*[change['weights'][vi].get(palette[i],0) if i<len(palette) else 0 for i in range(4)]))
                uint(len(c['faces']))
                for fi in c['faces']:uint(fi);[uint(indices[v]) for v in m['faces'][fi]]
            summary.append({'mesh':name,'chunks':len(chunks),'palette_influence_merges':drops,'vertices':len(m['positions']),'faces':len(m['faces'])})
    return summary


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--inspect', type=Path)
    ap.add_argument('--target', type=Path)
    ap.add_argument('--donor', type=Path)
    ap.add_argument('--fit-target',type=Path,help='Original pre-subdivision surface used for invariant anatomical fitting')
    ap.add_argument('--anatomical-regions',action='store_true',help='Constrain transfer to corresponding digits and weld palette seam weights')
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if args.inspect:
        inspect(Rig(args.inspect), args.output)
    else:
        nodes,changes,report=transfer(Rig(args.target),Rig(args.donor),args.anatomical_regions,Rig(args.fit_target) if args.fit_target else None)
        chunks=write_plan(args.output,nodes,changes)
        args.output.with_suffix('.json').write_text(json.dumps({'hands':report,'meshes':chunks},indent=2))
        print(json.dumps({'nodes':len(nodes),'meshes':len(changes),'chunks':sum(c['chunks'] for c in chunks),'weighted_finger_vertices':sum(h['vertices'] for h in report)}))

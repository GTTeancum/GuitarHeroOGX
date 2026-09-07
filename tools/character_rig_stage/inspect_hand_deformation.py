"""Inspect skinned hand triangles using native post-controller bone traces."""
import argparse
import json
import re
from pathlib import Path
import numpy as np
from transfer_finger_weights import Rig,points,matrix,finger
from audit_singer_motion import HEADER,POSITION
from native_trace import lines

WORLD_ROW=re.compile(r'\[hwrow\] ph=postcontrollers b=(\S+) r([012])=\(([^)]+)\)')
LOCAL=re.compile(r'\[handpose\] phase=postcontrollers role=guitarist0 bone=\S+ exact=(\S+) localPos=\(([^)]+)\) world=\([^)]+\) r0=\(([^)]+)\) r1=\(([^)]+)\) r2=\(([^)]+)\)')

def native_frames(path):
    frames=[];current=None
    for line in lines(path):
        h=HEADER.search(line)
        if h:
            current={'time':float(h[1]),'bones':{},'locals':{}};frames.append(current)
        if current is None:continue
        local=LOCAL.search(line)
        if local:
            x=np.eye(4);x[:3,3]=np.fromstring(local[2],sep=' ')
            for k in range(3):x[:3,k]=np.fromstring(local[3+k],sep=' ')
            current['locals'][local[1]]=x
        for pattern in (POSITION,WORLD_ROW):
            m=pattern.search(line)
            if not m:continue
            bone=current['bones'].setdefault(m[1]+'.mesh',np.eye(4))
            if pattern is POSITION:bone[:3,3]=np.fromstring(m[2],sep=' ')
            else:bone[:3,int(m[2])]=np.fromstring(m[3],sep=' ')
    return frames

def posed_hand_meshes(rig,frame,side='L'):
    hand=f'bone_{side}-hand.mesh'
    # Only hand-relative transforms are needed. Reconstruct both hands from
    # the actual logged local poses; never substitute an identity world basis
    # for right-hand bones whose world rotation isn't in the older trace.
    relative={hand:np.eye(4)}
    def pose(n):
        if n not in relative:
            if n not in frame['locals']:raise ValueError('Missing native local pose: '+n)
            relative[n]=pose(rig.nodes[n]['parent'])@frame['locals'][n]
        return relative[n]
    for mesh in rig.meshes.values():
        if not mesh['has_bones'] or not mesh['positions'] or 'lod1' in mesh['name'].lower() or 'shadow' in mesh['name'].lower():continue
        w=np.asarray(mesh['weights']);fs=np.asarray(mesh['faces'],int)
        allowed=np.array([s['name']==hand or finger(s['name'],side) for s in mesh['bone_slots']])
        pure=np.all(np.abs(w[:,~allowed])<1e-6,axis=1)
        fs=fs[np.all(pure[fs],axis=1)]
        if not len(fs):continue
        posed=np.zeros((len(w),3))
        for i,s in enumerate(mesh['bone_slots']):
            if not allowed[i]:continue
            posed+=points(pose(s['name'])@matrix(s['offset']),mesh['positions'])*w[:,i,None]
        rest=points(np.linalg.inv(rig.world(hand)),rig.skin_bind(mesh))
        yield mesh,fs,rest,posed

def evaluate(rig,frame,side='L'):
    rows=[];polygons=[]
    for mesh,fs,rest,posed in posed_hand_meshes(rig,frame,side):
        w=np.asarray(mesh['weights'])
        for f in fs:
            edges=[(0,1),(1,2),(2,0)]
            ratio=max(np.linalg.norm(posed[f[a]]-posed[f[b]])/max(np.linalg.norm(rest[f[a]]-rest[f[b]]),1e-8) for a,b in edges)
            weights=[{s['name']:round(float(w[vi,i]),4) for i,s in enumerate(mesh['bone_slots']) if w[vi,i]} for vi in f]
            rows.append({'mesh':mesh['name'],'vertices':f.tolist(),'edge_stretch':float(ratio),'rest':rest[f].round(4).tolist(),'weights':weights})
            polygons.append(posed[f])
    return sorted(rows,key=lambda r:r['edge_stretch'],reverse=True),np.asarray(polygons)

def deformation_metrics(rig,frame,side):
    maximum=0.;seam=0.;seen={};triangles=0
    for mesh,fs,rest,posed in posed_hand_meshes(rig,frame,side):
        edges=np.concatenate((fs[:,[0,1]],fs[:,[1,2]],fs[:,[2,0]]))
        length=np.linalg.norm(rest[edges[:,0]]-rest[edges[:,1]],axis=1)
        now=np.linalg.norm(posed[edges[:,0]]-posed[edges[:,1]],axis=1)
        valid=length>1e-5
        if valid.any():maximum=max(maximum,float((now[valid]/length[valid]).max()))
        for vi in np.unique(fs):
            key=tuple(np.round(rest[vi],4))
            if key in seen:seam=max(seam,float(np.linalg.norm(posed[vi]-seen[key])))
            else:seen[key]=posed[vi]
        triangles+=len(fs)
    return {'max_edge_stretch':maximum,'max_duplicate_seam_gap':seam,'hand_triangles':triangles}

def main():
    p=argparse.ArgumentParser();p.add_argument('rig',type=Path);p.add_argument('log',type=Path);p.add_argument('--time',type=float,default=32);p.add_argument('--side',choices=('L','R'),default='L');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    rig=Rig(a.rig);frames=native_frames(a.log)
    selected=min(frames,key=lambda f:abs(f['time']-a.time));rows,tri=evaluate(rig,selected,a.side)
    report={'side':a.side,'time':selected['time'],'worst_faces':rows[:12],'timeline':[{'time':f['time'],'max_edge_stretch':evaluate(rig,f,a.side)[0][0]['edge_stretch']} for f in frames]}
    a.output.with_suffix('.json').write_text(json.dumps(report,indent=2))
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.collections import PolyCollection
    fig,axs=plt.subplots(1,3,figsize=(13,5))
    for ax,(x,y) in zip(axs,((0,1),(0,2),(1,2))):
        ax.add_collection(PolyCollection(tri[:,:,(x,y)],facecolors='#c7ab8d',edgecolors='#353535',linewidths=.65))
        ax.autoscale_view();ax.set_aspect('equal');ax.grid(alpha=.2);ax.set_title(f'{a.side}-hand axes {x},{y}')
    fig.suptitle(f'Native deformed hand, t={selected["time"]:.2f}s; worst edge stretch {rows[0]["edge_stretch"]:.2f}x')
    fig.tight_layout();fig.savefig(a.output,dpi=150);plt.close(fig)
    print(json.dumps({'time':selected['time'],'worst_edges':[(r['mesh'],round(r['edge_stretch'],2)) for r in rows[:4]]}))

if __name__=='__main__':main()

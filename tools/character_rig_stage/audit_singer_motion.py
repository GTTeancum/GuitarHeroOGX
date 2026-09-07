"""Audit actual post-controller finger shapes from the game's native log."""
import argparse
import json
from pathlib import Path
import re
import numpy as np
from native_trace import lines

HEADER=re.compile(r'\[handpose\] phase=postcontrollers role=guitarist0 t=([\d.]+) tick=(\d+) mask=(0x[\da-f]+) strum=(.*?) fret=(.*)')
POSITION=re.compile(r'\[hpos\] ph=postcontrollers b=(\S+) w=\(([^)]+)\)')

def audit(path):
    frames=[];current=None
    for line in lines(path):
        match=HEADER.search(line)
        if match:
            current={'t':float(match[1]),'tick':int(match[2]),'mask':int(match[3],16),'strum':match[4],'fret':match[5].strip(),'positions':{}}
            frames.append(current)
        match=POSITION.search(line)
        if current is not None and match:current['positions'][match[1]]=np.fromstring(match[2],sep=' ')
    assert frames,'No native post-controller hand samples'
    rows=[];lengths={}
    for frame in frames:
        p=frame.pop('positions');angles={}
        for side in ('L','R'):
            for digit in ('thumb','index','middlefinger','ringfinger','pinky'):
                names=[f'bone_{side}-{digit}{i:02}' for i in (1,2,3)]
                assert all(n in p for n in names),names
                a,b,c=(p[n] for n in names);u=b-a;v=c-b
                assert np.all(np.isfinite([*a,*b,*c]))
                assert min(np.linalg.norm(u),np.linalg.norm(v))>1e-5,(frame['t'],names)
                angle=float(np.degrees(np.arccos(np.clip(np.dot(u,v)/(np.linalg.norm(u)*np.linalg.norm(v)),-1,1))))
                assert np.isfinite(angle),(frame['t'],names)
                angles[f'{side}_{digit}']=round(angle,3)
                for n,d in zip(names[1:],(u,v)):lengths.setdefault(n,[]).append(float(np.linalg.norm(d)))
        frame['finger_bend_degrees']=angles;rows.append(frame)
    stats={}
    for digit in rows[0]['finger_bend_degrees']:
        values=[r['finger_bend_degrees'][digit] for r in rows]
        stats[digit]={'min':min(values),'max':max(values),'range':round(max(values)-min(values),3)}
    spans={n:round(max(v)-min(v),6) for n,v in lengths.items()}
    shapes={side:len({tuple(round(r['finger_bend_degrees'][f'{side}_{d}']/3) for d in ('index','middlefinger','ringfinger','pinky')) for r in rows}) for side in ('L','R')}
    assert all(v>=3 for v in shapes.values()),f'Not enough distinct playing finger shapes: {shapes}'
    assert max(spans.values())<.01,f'Animated finger length changed: {max(spans.values())}'
    assert any('strum_' in r['strum'] for r in rows),'No active strum clip'
    assert any('finger_' in r['fret'] for r in rows),'No authored fretting clip'
    return {'status':'pass','sample_count':len(rows),'distinct_shapes_3degree_bins':shapes,'finger_bend_degrees':stats,'maximum_segment_length_drift':max(spans.values()),'samples':rows}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('log',type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    result=audit(a.log);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('samples','finger_bend_degrees')}))

"""Acceptance evidence for the male correction; female is immutable."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from audit_singer_motion import audit
from inspect_hand_deformation import native_frames,deformation_metrics
from transfer_finger_weights import Rig
from native_trace import lines

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser();p.add_argument('--scratch',type=Path,required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    candidate=a.scratch/'core.singers';changes=[];preserved=[]
    allowed={f'content/char/gh{n}_male_singer/og/gen/metal_singer.milo_ps2' for n in (1,2)}|{'manifest.json','content-index.json'}
    for old in sorted(a.reference.rglob('*')):
        if not old.is_file():continue
        relative=old.relative_to(a.reference).as_posix();new=candidate/relative
        assert new.is_file(),f'Missing package file: {relative}'
        same=digest(old)==digest(new)
        if relative not in allowed:assert same,f'Protected asset changed: {relative}'
        (preserved if same else changes).append(relative)
    for item in json.loads((candidate/'content-index.json').read_text())['files']:
        f=candidate/'content'/item['path'];assert digest(f)==item['sha256'];assert f.stat().st_size==item['size']
    validation=json.loads((a.scratch/'build/validation.json').read_text())
    runs=[]
    for outfit in ('gh2','gh1'):
        rig=Rig(a.scratch/f'build/{outfit}_male.fingers.rig.json')
        for side,letter in (('left','L'),('right','R')):
            name=f'{outfit}-{side}';log=a.scratch/f'final-{name}.log'
            motion=audit(log);frames=native_frames(log)
            assert len(frames)>=60,'Insufficient active native samples'
            timeline=[{'time':f['time'],**deformation_metrics(rig,f,letter)} for f in frames]
            seam=max(f['max_duplicate_seam_gap'] for f in timeline)
            assert seam<.002,f'Posed seam split in {name}: {seam}'
            # This guard catches the previous 4.36x spike; it is NOT a substitute
            # for reviewing the footage, nor a universal anatomical threshold.
            stretch=max(f['max_edge_stretch'] for f in timeline)
            assert stretch<2.1,f'Male regression edge-stretch limit exceeded: {name}'
            summary=[s for s in lines(log) if 'final gameplay summary:' in s][-1]
            assert 'state=playing' in summary and int(re.search(r' hits=(\d+)',summary)[1])>=20
            runs.append({'name':name,'motion':motion,'deformation':timeline,'max_edge_stretch':stretch,'max_duplicate_seam_gap':seam,'gameplay':summary,'log_sha256':digest(log)})
    result={'status':'automated_checks_pass_visual_review_separate','changed_package_files':changes,'byte_identical_package_files':preserved,'source_and_bind_validation':validation,'native_runs':runs}
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'runs':len(runs),'protected_files_unchanged':len(preserved),'max_native_seam_gap':max(r['max_duplicate_seam_gap'] for r in runs),'max_native_edge_stretch':max(r['max_edge_stretch'] for r in runs)}))

if __name__=='__main__':main()

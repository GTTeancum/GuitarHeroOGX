"""Keep compact, reproducible native proof evidence before scratch cleanup."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile
from PIL import Image
from native_trace import lines

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

p=argparse.ArgumentParser();p.add_argument('scratch',type=Path);p.add_argument('proof',type=Path);p.add_argument('repository',type=Path);p.add_argument('stage',type=Path);a=p.parse_args()
files=[]
for f in sorted((a.scratch/'core.singers').rglob('*')):
    if not f.is_file():continue
    relative=f.relative_to(a.scratch/'core.singers');digest=sha(f)
    for root in (a.scratch/'rebuilt',a.repository/'DLC/core.singers',a.stage/'DLC/core.singers'):
        assert sha(root/relative)==digest,f'Package mismatch: {root/relative}'
    files.append({'path':relative.as_posix(),'sha256':digest,'bytes':f.stat().st_size})
logs=[a.scratch/f'final-{outfit}-{side}.log' for outfit in ('gh1','gh2') for side in ('left','right')]
logs.append(a.scratch/'deployed-gh2-context.log')
inputs=[a.scratch/f'build/{outfit}_male.fingers.rig.json' for outfit in ('gh1','gh2')]
with zipfile.ZipFile(a.proof/'native-audit-inputs.zip','w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    for f in logs+inputs:z.write(f,f.name)
for outfit in ('gh1','gh2'):
    for side in ('left','right'):
        with Image.open(a.scratch/f'final-{outfit}-{side}/frame_00240.bmp') as image:image.save(a.proof/f'{outfit}-{side}.png')
with Image.open(a.scratch/'deployed-gh2-context/frame_00240.bmp') as image:image.save(a.proof/'deployed-gh2-context.png')
result={'package_version':'1.1.1','fresh_rebuild_matches_repository_and_stage':True,'files':files,
        'staged_executable_sha256':sha(a.stage/'ghogx_app.exe'),
        'deployed_smoke_summary':[s for s in lines(logs[-1]) if 'final gameplay summary:' in s or 'loop performance:' in s],
        'visually_inspected_frames_per_clip':list(range(60,419,12)),
        'review_scope':'Both hands, both male outfits: chronological close native frames, plus earlier enlarged problem poses; no claim of user approval.',
        'proofs':[{'path':f.name,'sha256':sha(f),'bytes':f.stat().st_size} for f in sorted(a.proof.glob('*')) if f.suffix in ('.png','.mp4','.zip')]}
(a.proof/'deployment-manifest.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'matching_package_files':len(files),'staged_executable':result['staged_executable_sha256'],'proof_files':len(result['proofs'])}))

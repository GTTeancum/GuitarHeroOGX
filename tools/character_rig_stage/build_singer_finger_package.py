"""Reproducible four-outfit playable-only singer finger graft package."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]

def run(args):
    p=subprocess.run([str(a) for a in args],capture_output=True,text=True)
    if p.returncode:raise RuntimeError(p.stdout[-1500:]+p.stderr[-1500:])
    return p.stdout.strip()

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--gh2-female',type=Path,required=True)
    ap.add_argument('--gh2-male',type=Path,required=True)
    ap.add_argument('--gh1-converted',type=Path,default=ROOT/'tools/milo_convert/gh1-character-models')
    ap.add_argument('--template',type=Path,default=ROOT/'DLC/core.singers')
    ap.add_argument('--animation-content',type=Path,required=True,help='Content root of the preconverted GH1 package')
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--work',type=Path,required=True)
    ap.add_argument('--male-only',action='store_true',help='Copy approved package unchanged, regenerate only the two male outfits')
    a=ap.parse_args();a.work.mkdir(parents=True,exist_ok=True);a.output.mkdir(parents=True,exist_ok=True)
    if a.male_only:shutil.copytree(a.template,a.output,dirs_exist_ok=True)
    export=HERE/'build/Release/milo_rig_export.exe';patch=HERE/'build/Release/milo_finger_patch.exe'
    inputs={'gh1_female':a.gh1_converted/'female_singer.milo_ps2','gh1_male':a.gh1_converted/'metal_singer.milo_ps2','gh2_female':a.gh2_female,'gh2_male':a.gh2_male}
    if a.male_only:inputs={k:v for k,v in inputs.items() if k.endswith('_male')}
    for donor in ('alterna','classic'):
        run([export,a.gh1_converted/f'{donor}.milo_ps2',a.work/f'{donor}.rig.json'])
    records=[]
    for case,source in inputs.items():
        sex=case.split('_')[1];model='female_singer' if sex=='female' else 'metal_singer'
        selection=case+'_singer';relative=f'char/{selection}/og/gen/{model}.milo_ps2'
        dest=a.output/'content'/relative;dest.parent.mkdir(parents=True,exist_ok=True)
        if source.resolve()==dest.resolve():raise ValueError('Output must not overwrite its source')
        donor='alterna' if sex=='female' else 'classic'
        before=a.work/f'{case}.rig.json';after=a.work/f'{case}.fingers.rig.json';plan=a.work/f'{case}.plan.bin'
        skin_source=source;subdivision=None
        if sex=='male':
            skin_source=a.work/f'{case}.bend-resolution.milo_ps2'
            subdivision=run([HERE/'build/Release/milo_hand_subdivide.exe',source,skin_source,2])
            run([export,source,a.work/f'{case}.original.rig.json'])
        run([export,skin_source,before])
        flags=['--anatomical-regions'] if sex=='male' else []
        fit_flags=['--fit-target',a.work/f'{case}.original.rig.json'] if sex=='male' else []
        run([sys.executable,HERE/'transfer_finger_weights.py','--target',before,'--donor',a.work/f'{donor}.rig.json','--output',plan,*flags,*fit_flags])
        run([patch,skin_source,plan,dest]);run([export,dest,after])
        audit=a.work/f'{case}.validation.json'
        run([sys.executable,HERE/'validate_finger_graft.py',before,after,plan,'--output',audit,*flags])
        records.append({'outfit':selection,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'path':relative,'subdivision':subdivision,'validation':json.loads(audit.read_text())})
        print(case,': native graft + bind/weight/topology validation PASS',flush=True)
    manifest=json.loads((a.template/'manifest.json').read_text())
    manifest['version']='1.1.1'
    models={o['selection']:o['model'] for c in manifest['characters'] for o in c['outfits']} if a.male_only else {}
    models.update({r['outfit']:r['path'] for r in records})
    animation_files=[]
    for donor in ('alterna','classic'):
        directory=f'char/playable_singers/retarget/{donor}'
        payloads={f'{directory}/{donor}.milo_ps2':a.gh1_converted/f'{donor}.milo_ps2'}
        for bank in ('ui','main','strum','fret'):
            payloads[f'{directory}/{donor}_{bank}.milo_ps2']=a.animation_content/f'char/gh1_{donor}/anims/gen/{donor}_{bank}.milo_ps2'
        for relative,source in payloads.items():
            dest=a.output/'content'/relative;dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(source,dest);animation_files.append(relative)
    for ch in manifest['characters']:
        donor='alterna' if ch['id']=='female_singer' else 'classic'
        directory=f'char/playable_singers/retarget/{donor}'
        for outfit in ch['outfits']:
            outfit['model']=models[outfit['selection']];outfit['ui_model']=outfit['model']
            outfit['animation_source_model']=f'{directory}/{donor}.milo_ps2'
            for bank in ('ui','main','strum','fret'):outfit[f'{bank}_anim']=f'{directory}/{donor}_{bank}.milo_ps2'
            outfit.pop('guitarist_hidden_roots',None) # actual mic subtree removed
    portraits=[p for p in manifest['files'] if p.startswith('ui/')]
    for p in portraits:
        dest=a.output/'content'/p;dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(a.template/'content'/p,dest)
    manifest['files']=[*models.values(),*portraits,*animation_files]
    index={'schema_version':1,'files':[{'path':p,'size':(a.output/'content'/p).stat().st_size,'sha256':hashlib.sha256((a.output/'content'/p).read_bytes()).hexdigest()} for p in manifest['files']]}
    (a.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (a.output/'content-index.json').write_text(json.dumps(index,indent=2)+'\n')
    (a.work/'validation.json').write_text(json.dumps({'outfits':records},indent=2)+'\n')

if __name__=='__main__':main()

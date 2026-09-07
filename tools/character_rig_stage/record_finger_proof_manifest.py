"""Record the minimal evidence bundle for a finished four-outfit native run."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile

p=argparse.ArgumentParser();p.add_argument('work',type=Path);p.add_argument('proof',type=Path);p.add_argument('--package',type=Path,required=True);p.add_argument('--staged-package',type=Path,required=True);a=p.parse_args()
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
records=[]
with zipfile.ZipFile(a.proof/'native-logs.zip','w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
    for era,sex in [('gh2','female'),('gh1','female'),('gh2','male'),('gh1','male')]:
        stem=f'{sex}-{era}';log=a.work/f'{era}-{sex}-play.log'
        text=log.read_text(errors='replace');archive.write(log,f'{stem}.log')
        final=next(line for line in text.splitlines() if 'final gameplay summary:' in line)
        assert 'state=playing' in final and 'hits=22 ' in final and 'misses=0 ' in final
        records.append({'outfit':f'{era}_{sex}_singer','native_log_sha256':sha(log),'native_result':final,'video':stem+'-playing.mp4','video_sha256':sha(a.proof/(stem+'-playing.mp4')),'motion':stem+'-motion.json','bind':f'{era}_{sex}-bind-validation.json'})
    stage=a.work/'staged-smoke.log';archive.write(stage,'staged-smoke.log')
index=json.loads((a.package/'content-index.json').read_text())
for entry in index['files']:
    for root in (a.package,a.staged_package,a.work/'reproduced'):
        payload=root/'content'/entry['path']
        assert payload.stat().st_size==entry['size'] and sha(payload)==entry['sha256'],str(payload)
source=json.loads((a.work/'repro-work/validation.json').read_text())
result={'date':'2026-09-05','status':'pass','task':'Automated singer finger graft and weight transfer','source_provenance':source,'package_index':index,'byte_identical_rebuild_and_deployment':True,'outfits':records,'native_capture':{'song':'shoutatthedevil','difficulty':3,'start_seconds':30,'captured_frames':'60..418 step 2','fixed_dt':1/60,'video_fps':30,'video_duration':6,'autoplay':True,'hidden_process':True,'host_input_used':False,'diagnostic_camera':'guitarist0 front','diagnostic_lighting':True},'staged_game_result':next(line for line in stage.read_text(errors='replace').splitlines() if 'final gameplay summary:' in line)}
(a.proof/'manifest.json').write_text(json.dumps(result,indent=2)+'\n')
print(f'PASS: {len(records)} active-playing proofs, {len(index["files"])} identical payloads across rebuild/repo/deployment')

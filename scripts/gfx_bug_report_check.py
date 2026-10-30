#!/usr/bin/env python3
#ai: opt-in editor report writer fixtures, including no-write draft and failure paths.
import json
from pathlib import Path
import re
import numpy as np
import audit_common as audit

OUT=Path('/tmp/opencode/bug_report_check')


def require(condition,message):
    if not condition: raise RuntimeError(message)


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    directory=OUT/'reports'
    invalid=OUT/'invalid'
    blocked=OUT/'blocked'
    blocked.write_text('keep this file\n')
    before=set(directory.glob('*')) if directory.exists() else set()
    title='SSGI "stripes" — viewport'
    description='Horizontal bands\nwhen GI is enabled.\\'
    valid={'title':title,'description':description,'steps':'1. Open editor\n2. Enable SSGI',
        'when':'After enabling GI','affects':'Screen-space lighting','type':0,'severity':2,
        'directory':str(directory)}
    items=[dict(valid,title=' \t\n',directory=str(invalid)),dict(valid,severity=99,directory=str(invalid)),
           dict(valid,directory=str(blocked)),valid,dict(valid,diagnostics=True),dict(valid,title='Screenshot attachment',screenshot=True),
           dict(valid,open_only=True)]
    fixture=OUT/'fixture.json'
    fixture.write_text(json.dumps({'items':items},ensure_ascii=False)+'\n')
    legacy=audit.ROOT/'screenshot000.png'
    original=legacy.read_bytes() if legacy.exists() else None
    try:
        with audit.settings_preserved():
            env=audit.scene_env({'DISPLAY':':1','GX_GFX_BUG_REPORT_TEST':str(fixture),'GX_FRAMES':'8'},clean=True)
            run=audit.run_scene('--gfx',env,width=1024,height=768,fps=None,frames=8,
                extra_args=('--gfx-editor',),timeout=120)
            (OUT/'renderer.log').write_text(run.stdout or '')
            log=run.stdout or ''
            require(log.count('BUG_REPORT_TEST: saved=1')==3,'three explicit submissions must succeed')
            require(log.count('BUG_REPORT_TEST: saved=0')==3,'blank/options/path failures must be reported')
            require('BUG_REPORT_TEST: open only' in log,'form opening fixture missing')
            require(not invalid.exists(),'invalid fields must not create a directory')
            require(blocked.read_text()=='keep this file\n','failed save overwrote user file')
            created=set(directory.glob('*'))-before
            reports=sorted(p for p in created if p.suffix=='.json')
            shots=sorted(p for p in created if p.suffix=='.png')
            require(len(reports)==3 and len(shots)==1,'submissions must not overwrite or autosave')
            documents=[json.loads(p.read_text()) for p in reports]
            plain=[d for d in documents if d['title']==title]
            require(len(plain)==2,'repeat reports overwritten or Unicode lost')
            for d in plain:
                require(d['description']==description and d['steps']==valid['steps'],'escaped text changed')
                require(d['type']=='Rendering' and d['severity']=='High','option metadata changed')
                require(d['screenshot_status']=='not_requested' and d['screenshot']=='','unexpected capture')
            expected_fields={'status_code','title','description','type','severity','when','affects','steps',
                'utc','engine_version','preset','screenshot','screenshot_status'}
            for d in documents:
                require(type(d.get('status_code')) is int and d['status_code']==0,
                    'new reports must default to OPEN status code 0')
                expected=set(expected_fields)|({'diagnostics'} if 'diagnostics' in d else set())
                require(set(d)==expected,'unexpected top-level report fields')
            require(sum('diagnostics' in d for d in documents)==1,'diagnostics must be explicitly opted in')
            state=next(d['diagnostics'] for d in documents if 'diagnostics' in d)
            allowed={'name','description','created_date','created_time','state','ssao','csm','ssr',
                     'reflection','godray','fogVolume','lensflare','bloom','fog','camera','editor',
                     'viewport_x','viewport_y','viewport_width','viewport_height','selected_model'}
            require(set(state)==allowed,'diagnostic snapshot leaked assets or omitted expected fields')
            require(state['viewport_width']>0 and state['viewport_height']>0,'live viewport missing')
            require(isinstance(state['selected_model'],(int,float)),'selection missing')
            require('position' in state['camera'] and 'target' in state['camera'],'live camera missing')
            require('useGTAO' in state['ssao'] and 'useSSGI' in state['ssao'],'GI state missing')
            require('maxSteps' in state['ssr'] and 'intensity' in state['bloom'],'effect tuning missing')
            for d in documents:
                require(re.fullmatch(r'\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ',d['utc']) is not None,'UTC metadata missing')
                require(d['engine_version'] and d['preset'],'engine/preset metadata missing')
            shot_report=next(d for d in documents if d['title']=='Screenshot attachment')
            require(shot_report['screenshot_status']=='saved' and Path(shot_report['screenshot'])==shots[0],
                    'attachment reference/status incorrect')
            image=audit.load_rgb(shots[0])
            require(min(image.shape[:2])>0 and np.isfinite(image).all() and image.max()>image.min(),
                    'attachment invalid or empty')
    finally:
        if original is not None: legacy.write_bytes(original)
        elif legacy.exists(): legacy.unlink()
    print('BUG_REPORT: PASS — explicit saves, escaped text, duplicates, attachment, failures and no-write draft')


if __name__=='__main__':
    main()

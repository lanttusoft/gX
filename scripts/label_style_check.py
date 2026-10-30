#!/usr/bin/env python3
#ai: real label RT evidence for oversized glyphs, color/opacity/shadow and saved top placement/style.
import json
from pathlib import Path
import re
import numpy as np
from PIL import Image
import audit_common as audit
import gfx_label_font_check as fonts
import gfx_authoring_check as authoring

OUT=Path('/tmp/opencode/label_style_check')


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    source=(audit.ROOT/'src/common/utils/billboard.c').read_text()
    width=int(re.search(r'#define TEXT_RT_WIDTH (\d+)',source)[1])
    height=int(re.search(r'#define TEXT_RT_HEIGHT (\d+)',source)[1])
    if width!=8*height or 'billboard_uv(texture && gX_Texture_IsRT(texture->id));' not in source:
        raise RuntimeError('Label RT aspect/UV policy drift')
    probe=authoring.label_fixture()
    params=probe['models'][0]['params']
    params.update(textVisible=True,textBillboard=True,textString='Label',textFontPath='',
        textFontSize=256,textColor=[255,255,255,255],textShadowColor=[0,0,0,0],
        textShadowPixels=0,textAnchorTop=True,textGap=0.2,textWorldSize=[3,0.6],textFit=True)
    with audit.settings_preserved():
        dumps=[]
        for name,color,shadow in [('oversize',[255,255,255,255],[0,0,0,0]),
            ('color',[200,40,20,255],[0,0,0,0]),('opacity',[200,40,20,128],[0,0,0,0]),
            ('shadow',[200,40,20,255],[0,0,0,220])]:
            params.update(textColor=color,textShadowColor=shadow,textShadowPixels=2)
            path=OUT/(name+'.json'); dump=OUT/(name+'.bin')
            path.write_text(json.dumps(probe))
            log=fonts.run_editor(path,dump,frames=35)
            (OUT/(name+'.log')).write_text(log)
            image=fonts.load_dump(dump)
            Image.fromarray(image[::-1]).save(OUT/(name+'.png'))
            mask=image[:,:,3]>20
            ys,xs=np.nonzero(mask)
            if mask.sum()<200 or ys.min()<2 or ys.max()>61 or xs.min()<2 or xs.max()>509:
                raise RuntimeError('Oversize label glyphs clipped/missing at the RT border')
            dumps.append(image)
            print('LABEL_RASTER:',name,'ink=',int(mask.sum()),'bounds=',[int(xs.min()),int(ys.min()),int(xs.max()),int(ys.max())])
        mask=dumps[1][:,:,3]>100
        if (dumps[1][:,:,:3][mask].mean(axis=0)[0] <= dumps[1][:,:,:3][mask].mean(axis=0)[1]*2):
            raise RuntimeError('Requested label color did not reach glyph RT')
        if dumps[2][:,:,3].max()>=dumps[1][:,:,3].max():
            raise RuntimeError('Requested opacity did not reduce glyph alpha')
        if int((dumps[3][:,:,3]>20).sum())<=int((dumps[1][:,:,3]>20).sum()):
            raise RuntimeError('Shadow added no glyph coverage')
        params.update(textColor=[200,40,20,255],textShadowColor=[0,0,0,220],textShadowPixels=2)
        _,saved=authoring.capture('style_roundtrip',probe)
        result=saved['models'][0]['params']
        for field in ('textColor','textShadowColor','textShadowPixels','textGap','textWorldSize','textFit','textAnchorTop'):
            if not np.allclose(np.array(result[field]),np.array(params[field]),atol=1e-6):
                raise RuntimeError('Label style roundtrip changed '+field)
        #ai: asymmetric F must match the raster glyph in the presented scene, not a Y-flipped variant.
        probe['camera']={'position':[0,0,10],'target':[0,0,0],'up':[0,1,0],
            'fovy':45,'projection':0}
        params.update(pos=[0,0,0],rotation=[0,0,0],scale=[0.01,0.01,0.01],
            textAnchorTop=False,textOffset=[0,0,0],textString='F',textBillboard=False,
            textWorldSize=[4,0.5],textColor=[255,0,0,255],textShadowColor=[0,0,0,0],
            textVisible=False)
        off,_=authoring.capture('glyph_orientation_off',probe)
        params['textVisible']=True
        on,_=authoring.capture('glyph_orientation_on',probe)
        path=OUT/'orientation.json'; dump=OUT/'orientation.bin'
        path.write_text(json.dumps(probe))
        fonts.run_editor(path,dump,frames=35)
        expected=fonts.load_dump(dump)[::-1,:,3]>20
        actual=np.abs(on-off).mean(axis=2)>40

        def normalized(mask):
            ys,xs=np.nonzero(mask)
            if len(xs)<30: raise RuntimeError('Orientation glyph missing')
            crop=mask[ys.min():ys.max()+1,xs.min():xs.max()+1]
            return np.asarray(Image.fromarray(crop).resize((64,64),Image.Resampling.NEAREST))

        expected=normalized(expected); actual=normalized(actual)
        def iou(first,second):
            return float(np.logical_and(first,second).sum()/np.logical_or(first,second).sum())
        correct=iou(actual,expected); flipped=iou(actual,expected[::-1])
        if correct<0.75 or correct-flipped<0.2:
            raise RuntimeError(f'Scene glyph UV/orientation mismatch: correct={correct} flipped={flipped}')
        print('LABEL_ORIENTATION: actual/RT IoU=',correct,'Y-flipped negative=',flipped)
    print('LABEL_STYLE: PASS — size256 complete glyphs, color/opacity/shadow and saved top-anchor/style')


if __name__=='__main__': main()

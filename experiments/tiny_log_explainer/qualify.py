#!/usr/bin/env python3
"""Authored semantic regression, NOT an independent real-world accuracy test."""
import json
from train import ROOT, evaluate, features, probabilities

model=json.loads((ROOT/'artifacts/model.json').read_text())
rows=json.loads((ROOT/'corpus/qualification.json').read_text())
details=[]
for index,row in enumerate(rows):
    row['template_group']=f'qualification:{index}'
    scores=probabilities(model['weights'],model['bias'],features(row['text']))
    best=max(range(len(scores)),key=scores.__getitem__)
    label=model['labels'][best]
    accepted=max(scores)>=model['threshold'] and label!='unknown'
    details.append({**row,'prediction':label,'score':max(scores),'accepted':accepted})
report={'purpose':'Authored development semantic regression; NOT blind or real-device accuracy.',
        'model':model['version'],'metrics':evaluate(model['labels'],rows,model['weights'],model['bias'],model['threshold']),
        'details':details}
(ROOT/'artifacts/qualification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
wrong=[r for r in details if r['accepted'] and r['label']!=r['prediction']]
print(json.dumps({k:v for k,v in report['metrics'].items() if k!='confusion'},indent=2))
if wrong:
    print(json.dumps(wrong,indent=2))
    raise SystemExit('A high-confidence semantic regression failed; do not deploy this candidate.')
print('No accepted semantic misclassifications on the authored regression cases.')

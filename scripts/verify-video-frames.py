"""Reject an advancing timeline without visible video; retain a genuine frame."""
import json,shutil,subprocess,sys
from pathlib import Path
from PIL import Image
screen=sys.argv[1]
assert screen in ('player','player-menu')
results=[]
for number in (1,2,3):
    path=Path(f'build/live-{screen}-{number}.png')
    # The video is portrait and centered; this crop excludes title and menu.
    pixels=list(Image.open(path).convert('RGB').crop((450,80,830,270)).getdata())
    visible=sum(max(pixel)>18 for pixel in pixels)/len(pixels)
    results.append({'frame':number,'visibleFraction':visible,'passed':visible>0.10})
passed=[x for x in results if x['passed']]
report_path=Path('build/video-frame-check.json')
report=json.loads(report_path.read_text()) if report_path.exists() else {'screens':{}}
report['commit']=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
report['screens'][screen]={'displayPassed':len(passed)>=2,'frames':results}
report['displayPassed']=all(x['displayPassed'] for x in report['screens'].values())
report['hardwareTested']=False
report_path.write_text(json.dumps(report,indent=2)+'\n')
if len(passed)<2:
    raise SystemExit(f'{screen}: native video is black in the frame samples')
shutil.copyfile(f"build/live-{screen}-{passed[-1]['frame']}.png",f'build/screenshots/live-{screen}.png')
print(screen,'visible video confirmed in',len(passed),'of 3 real frame samples')

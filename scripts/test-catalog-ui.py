"""Exercise actual desktop key input, append position, category reset and search."""
from pathlib import Path
import subprocess as sp, time, os, re, json
Path('build/screenshots').mkdir(parents=True,exist_ok=True)
Path('sdmc:').mkdir(exist_ok=True)
if not Path('romfs:').exists():Path('romfs:').symlink_to('romfs',target_is_directory=True)
trace=Path('sdmc:/switch/hongguo-saf/runtime.log')
results={}
def read():return trace.read_text() if trace.exists() else ''
def wait_for(predicate,seconds=8):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        if process.poll() is not None:raise AssertionError('Native UI exited')
        if predicate(read()):return
        time.sleep(.08)
    raise AssertionError('UI condition timed out')
def key(name):
    sp.run(['xdotool','keydown',name],check=True);time.sleep(.09)
    sp.run(['xdotool','keyup',name],check=True);time.sleep(.09)
def click(x,y):
    sp.run(['xdotool','mousemove',str(x),str(y),'mousedown','1'],check=True);time.sleep(.1)
    sp.run(['xdotool','mouseup','1'],check=True);time.sleep(.3)
for screen in ['home','search']:
    trace.unlink(missing_ok=True)
    output=Path(f'build/catalog-{screen}.log').open('w')
    process=sp.Popen(['build-preview/hongguo-preview'],env={**os.environ,'HONGGUO_PREVIEW_SCREEN':screen,'HONGGUO_UI_TEST':'1'},stdout=output,stderr=sp.STDOUT)
    try:
        wait_for(lambda text:'catalog:page=1:added=18:total=18' in text)
        windows=[]
        def window_ready(_):
            result=sp.run(['xdotool','search','--onlyvisible','--pid',str(process.pid)],text=True,capture_output=True)
            windows[:]=result.stdout.splitlines()
            return bool(windows)
        wait_for(window_ready)
        sp.run(['xdotool','windowfocus','--sync',windows[0]],check=True)
        time.sleep(.6)  # Activity entrance animation temporarily blocks input.
        if screen=='home':
            # Tabs stay fixed above the scroll viewport. Select Latest then enter the grid.
            click(230,125);key('Down')
        else:key('Down')
        sp.run(['xdotool','keydown','Down'],check=True)
        try:wait_for(lambda text:'catalog:page=3:added=8:total=42' in text,seconds=35)
        finally:sp.run(['xdotool','keyup','Down'],check=True)
        time.sleep(.3)
        sp.run(['import','-window','root',f'build/screenshots/{screen}-infinite.png'],check=True)
        before=read().count('catalog:page=')
        for _ in range(6):key('Down')
        assert read().count('catalog:page=')==before,'End of list fetched again'
        offsets=re.findall(r'catalog:offset:requested=([0-9.]+):actual=([0-9.]+)',read())
        assert offsets and all(abs(float(a)-float(b))<1 for a,b in offsets),'Appending changed scroll position'
        assert any(float(a)>200 for a,b in offsets),'No append at a scrolled position tested'
        if screen=='home':
            # Change to Human while far down; new category must start at offset zero.
            click(350,125)
            wait_for(lambda text:'home:category:human' in text and 'catalog:page=1:added=18:total=18' in text.split('home:category:human')[-1])
            time.sleep(.3);last=read().split('home:category:human')[-1]
            assert 'catalog:offset:requested=0.000000:actual=0.000000' in last
            sp.run(['import','-window','root','build/screenshots/category-reset.png'],check=True)
        Path(f'build/catalog-{screen}.trace').write_text(read())
        results[screen]={'passed':True,'uniqueItems':42,'lastPage':3,'offsetPreserved':True,'endStopped':True}
    finally:
        Path(f'build/catalog-{screen}.trace').write_text(read())
        if screen not in results:
            sp.run(['import','-window','root',f'build/screenshots/{screen}-catalog-failure.png'])
        process.terminate()
        try:process.wait(timeout=5)
        except sp.TimeoutExpired:process.kill();process.wait()
        output.close()
report={'commit':sp.check_output(['git','rev-parse','HEAD'],text=True).strip(),'passed':True,'hardwareTested':False,'screens':results}
Path('build/catalog-ui-check.json').write_text(json.dumps(report,indent=2)+'\n')
print('Catalog UI: controller scrolling appends unique cards, preserves offset, stops at end; category resets to top')

# Falla si hay literales con acentos sin _u / fromUTF8 (en JUCE, String("ñ") se corrompe).
import re,sys,glob
bad=0
for f in glob.glob('Source/**/*.cpp',recursive=True)+glob.glob('Source/**/*.h',recursive=True)+glob.glob('Tests/*.cpp'):
    for i,line in enumerate(open(f,encoding='utf-8'),1):
        if 'utf8-ok' in line or line.strip().startswith('-') or line.strip().startswith('//') or line.strip().startswith('*') or line.strip().startswith('/*'): continue
        code=line.split('//')[0] if '"' not in line.split('//')[0][-1:] else line
        for m in re.finditer(r'"((?:[^"\\\n]|\\.)*)"(_u)?',line):
            if re.search(r'[^\x00-\x7F]',m.group(1)) and not m.group(2):
                pre=line[:m.start()]
                if 'fromUTF8' in pre[-40:] or 'CharPointer_UTF8' in pre[-40:]: continue
                print(f"{f}:{i}: {m.group(0)[:60]}"); bad+=1
sys.exit(1 if bad else 0)

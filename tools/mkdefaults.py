# Write ~/Library/Application Support/Framely/defaults.txt for one config of tools/configs.txt
import sys, os
name = sys.argv[1]
for line in open(os.path.join(os.path.dirname(__file__), 'configs.txt')):
    if line.startswith('#') or '|' not in line: continue
    n, args = [x.strip() for x in line.split('|', 1)]
    if n != name: continue
    d = os.path.expanduser('~/Library/Application Support/Framely'); os.makedirs(d, exist_ok=True)
    with open(d + '/defaults.txt', 'w') as f:
        f.write(f'# preset: Test config {n}\n')
        for kv in args.split():
            k, v = kv.split('='); f.write(k + ' ' + v.replace(',', ' ') + '\n')
    print('wrote', n); sys.exit(0)
sys.exit('no such config')

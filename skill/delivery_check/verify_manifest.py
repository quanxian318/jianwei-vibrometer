import argparse,hashlib,json,re
from pathlib import Path,PurePosixPath

def verify(root,manifest,require_ascii=False):
    root=Path(root).resolve();errors=[];count=0
    if not isinstance(manifest.get('files'),list):raise ValueError('files list missing')
    seen=set()
    for item in manifest['files']:
        rel=item['path'];p=PurePosixPath(rel)
        if rel in seen:errors.append('duplicate: '+rel);continue
        seen.add(rel)
        if p.is_absolute() or '..' in p.parts or '\\' in rel or ':' in rel or not rel:
            errors.append('unsafe path: '+rel);continue
        if require_ascii and re.fullmatch(r'[A-Za-z0-9_./-]+',rel) is None:
            errors.append('non-ascii filename: '+rel);continue
        path=(root/rel).resolve()
        if not path.is_relative_to(root):errors.append('path outside root: '+rel);continue
        if not path.is_file():errors.append('missing: '+rel);continue
        h=hashlib.sha256()
        with path.open('rb') as f:
            for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
        if h.hexdigest()!=item['sha256'].lower():errors.append('hash mismatch: '+rel)
        else:count+=1
    return {'verified_files':count,'errors':errors,'ok':not errors}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--root',required=True);p.add_argument('--manifest',required=True)
    p.add_argument('--require-ascii',action='store_true');a=p.parse_args()
    try:r=verify(a.root,json.loads(Path(a.manifest).read_text(encoding='utf-8')),a.require_ascii)
    except (ValueError,KeyError,OSError,TypeError) as e:p.error(str(e))
    print(json.dumps(r,ensure_ascii=False,indent=2));raise SystemExit(0 if r['ok'] else 1)

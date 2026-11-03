import argparse,json,math

def inspect(fs,target,search_max,legacy_ratio=.45):
    vals=[fs,target,search_max,legacy_ratio]
    if not all(math.isfinite(x) and x>0 for x in vals):raise ValueError('inputs must be positive finite values')
    nyquist=fs/2
    ok=target<nyquist and target<=search_max
    return {'fs_hz':fs,'target_hz':target,'nyquist_hz':nyquist,
      'configured_search_max_hz':search_max,'legacy_search_max_hz':legacy_ratio*fs,
      'legacy_excludes_target':target>legacy_ratio*fs,'necessary_checks_passed':ok,
      'close_to_nyquist':target>.45*fs,
      'note':'Passing is necessary, not proof of correct frequency, tracking or amplitude.'}

if __name__=='__main__':
    p=argparse.ArgumentParser(description='Check sampling/search necessary conditions; no device writes.')
    p.add_argument('--fs',type=float,required=True);p.add_argument('--target',type=float,required=True)
    p.add_argument('--search-max',type=float,required=True);p.add_argument('--legacy-ratio',type=float,default=.45)
    a=p.parse_args()
    try:r=inspect(a.fs,a.target,a.search_max,a.legacy_ratio)
    except ValueError as e:p.error(str(e))
    print(json.dumps(r,indent=2));raise SystemExit(0 if r['necessary_checks_passed'] else 2)

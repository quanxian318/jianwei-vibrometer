import argparse,json,re
from pathlib import Path

def audit(util,timing):
    mapping={'Slice LUTs':'lut','Slice Registers':'ff','Slice':'slice','Block RAM Tile':'bram','DSPs':'dsp'}
    resources={}
    for line in util.splitlines():
        cells=[x.strip() for x in line.split('|')[1:-1]]
        if len(cells)==6 and cells[0] in mapping:
            try: used,available,pct=map(lambda x:float(x.replace(',','')),[cells[1],cells[4],cells[5]])
            except ValueError:continue
            resources[mapping[cells[0]]]={'used':used,'available':available,'percent':pct}
    if len(resources)!=5:raise ValueError('required resource rows missing')
    lines=timing.splitlines();values=None
    for i,line in enumerate(lines):
        if 'WNS(ns)' in line and 'TNS(ns)' in line and 'WHS(ns)' in line:
            for row in lines[i+1:i+7]:
                parts=row.split()
                if len(parts)>=12:
                    try:values=[float(x) for x in parts]
                    except ValueError:continue
                    break
            if values is not None:break
    if values is None:raise ValueError('timing summary missing')
    coverage={}
    for key in ['no_clock','unconstrained_internal_endpoints','no_input_delay','no_output_delay','generated_clocks']:
        m=re.search(r'checking '+key+r'\s*\((\d+)\)',timing)
        if m is None:raise ValueError('coverage check missing: '+key)
        coverage[key]=int(m.group(1))
    return {'resources':resources,'wns_ns':values[0],'whs_ns':values[4],'wpws_ns':values[8],
      'constrained_paths_pass':values[0]>=0 and values[4]>=0 and values[8]>=0 and values[2]==0 and values[6]==0 and values[10]==0,
      'listed_coverage_checks_clear':not any(coverage.values()),'coverage_checks':coverage,
      'note':'Positive slack does not establish complete timing constraints or CDC signoff.'}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--util',required=True);p.add_argument('--timing',required=True);a=p.parse_args()
    try:r=audit(Path(a.util).read_text(encoding='utf-8'),Path(a.timing).read_text(encoding='utf-8'))
    except (ValueError,OSError) as e:p.error(str(e))
    print(json.dumps(r,indent=2))

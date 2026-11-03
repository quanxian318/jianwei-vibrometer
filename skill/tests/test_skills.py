import hashlib,importlib.util,json,tempfile,unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
def load(folder,name):
    s=importlib.util.spec_from_file_location(name,ROOT/folder/(name+'.py'))
    m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
sampling=load('sampling_check','check_sampling')
reports=load('implementation_audit','audit_vivado')
delivery=load('delivery_check','verify_manifest')

class Skills(unittest.TestCase):
    def test_sampling_target_22(self):
        r=sampling.inspect(45,22,30)
        self.assertTrue(r['necessary_checks_passed']);self.assertTrue(r['legacy_excludes_target']);self.assertTrue(r['close_to_nyquist'])
    def test_sampling_target_30(self):self.assertFalse(sampling.inspect(45,30,30)['necessary_checks_passed'])
    def test_nyquist_boundary(self):self.assertFalse(sampling.inspect(45,22.5,30)['necessary_checks_passed'])
    def test_search_range(self):self.assertFalse(sampling.inspect(90,22,20)['necessary_checks_passed'])
    def test_invalid_sample_rate(self):
        with self.assertRaises(ValueError):sampling.inspect(0,22,30)
    def test_reports(self):
        p=ROOT/'tests/fixtures'
        r=reports.audit((p/'utilization.rpt').read_text(),(p/'timing.rpt').read_text())
        self.assertEqual(r['resources']['lut']['used'],8945);self.assertEqual(r['resources']['ff']['used'],14420)
        self.assertEqual(r['resources']['bram']['used'],30.5);self.assertEqual(r['resources']['slice']['used'],4017)
        self.assertEqual(r['wns_ns'],.756);self.assertEqual(r['whs_ns'],.028)
        self.assertTrue(r['constrained_paths_pass']);self.assertFalse(r['listed_coverage_checks_clear'])
        self.assertEqual(r['coverage_checks']['no_clock'],592);self.assertEqual(r['coverage_checks']['unconstrained_internal_endpoints'],1132)
    def test_missing_report(self):
        with self.assertRaises(ValueError):reports.audit('','')
    def test_manifest_and_tamper(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'data.txt';p.write_bytes(b'golden')
            m={'files':[{'path':'data.txt','sha256':hashlib.sha256(b'golden').hexdigest()}]}
            self.assertTrue(delivery.verify(t,m,True)['ok']);p.write_bytes(b'changed');self.assertFalse(delivery.verify(t,m)['ok'])
    def test_unsafe_paths(self):
        for path in ['../secret','/secret','C:/secret',r'..\secret']:
            r=delivery.verify('.',{'files':[{'path':path,'sha256':'0'*64}]})
            self.assertFalse(r['ok'])
    def test_filename_rule(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'中文.txt';p.write_bytes(b'ok')
            m={'files':[{'path':p.name,'sha256':hashlib.sha256(b'ok').hexdigest()}]}
            self.assertTrue(delivery.verify(t,m)['ok']);self.assertFalse(delivery.verify(t,m,True)['ok'])

if __name__=='__main__':unittest.main()

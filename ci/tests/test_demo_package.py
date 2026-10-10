import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location("demo_package", Path(__file__).resolve().parents[1] / "package-demo-apks.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


class DemoPackageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.dist = self.root / 'dist'
        self.dist.mkdir()
        self.lock = {'files': []}
        self.vendor = {}
        for abi in mod.ABIS:
            name = abi + '/libavs3a_decoder.so'
            self.vendor[name] = ('test vendor ' + abi).encode()
        self.vendor['model.bin'] = b'test model'
        for name, data in self.vendor.items():
            self.lock['files'].append({'source': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        self.lock_path = self.root / 'lock.json'
        self.lock_path.write_text(json.dumps(self.lock), encoding='utf-8')
        self.bridge = self.apk('bridge')
        self.full = self.apk('full')
        (self.dist/'sdk-manifest.json').write_text(json.dumps({'sdkVersion':'1.2.3','distribution':'full','files':[], 'deviceValidation':'NOT_RUN'}))

    def apk(self, flavor, leak=False):
        path = self.root/(flavor + '.apk')
        with zipfile.ZipFile(path, 'w') as z:
            z.writestr('AndroidManifest.xml',b'test manifest')
            z.writestr('classes.dex',b'test dex')
            for abi in mod.ABIS: z.writestr(f'lib/{abi}/libavs3a_jni.so',b'test bridge')
            if flavor == 'full' or leak:
                for name, data in self.vendor.items():
                    z.writestr('assets/avs3a/model.bin' if name=='model.bin' else 'lib/'+name,data)
        return path

    def test_packages_both_and_checksums_manifest(self):
        entries=mod.package(self.bridge,self.full,'1.2.3',self.lock_path,self.dist)
        self.assertEqual(2,len(entries))
        for line in (self.dist/'SHA256SUMS').read_text().splitlines():
            expected,name=line.split('  ',1)
            self.assertEqual(expected,mod.digest((self.dist/name).read_bytes()))
        manifest=json.loads((self.dist/'sdk-manifest.json').read_text())
        self.assertEqual('NOT_RUN',manifest['deviceValidation'])
        self.assertTrue(all(e['deviceValidation']=='NOT_RUN' for e in manifest['demoApks']))
        mod.package(self.bridge,self.full,'1.2.3',self.lock_path,self.dist)
        self.assertEqual(2,len(json.loads((self.dist/'sdk-manifest.json').read_text())['files']))

    def test_rejects_vendor_leak(self):
        with self.assertRaisesRegex(ValueError,'leaked'):
            mod.verify_apk(self.apk('bridge',leak=True),'bridge',self.lock)

    def test_rejects_vendor_hash_mismatch(self):
        self.lock['files'][0]['sha256']='0'*64
        with self.assertRaisesRegex(ValueError,'lock mismatch'):
            mod.verify_apk(self.full,'full',self.lock)

    def test_rejects_bad_version_before_copy(self):
        with self.assertRaisesRegex(ValueError,'Invalid SDK version'):
            mod.package(self.bridge,self.full,'../bad',self.lock_path,self.dist)
        self.assertFalse((self.dist/'demo').exists())

    def test_rejects_missing_abi(self):
        with zipfile.ZipFile(self.bridge,'w') as z:
            z.writestr('AndroidManifest.xml',b'a');z.writestr('classes.dex',b'd')
        with self.assertRaisesRegex(ValueError,'missing JNI'):
            mod.verify_apk(self.bridge,'bridge',self.lock)


if __name__ == '__main__':
    unittest.main()

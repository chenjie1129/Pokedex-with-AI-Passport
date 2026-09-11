import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from build_identity import generate, identity
from package_firmware import package


class IdentityTests(unittest.TestCase):
    def test_incremental_build_refreshes_version(self):
        source = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temp:
            repo=Path(temp)
            for directory in ('main', 'tools', 'cmake'):
                (repo/directory).mkdir()
            shutil.copy2(source/'tools/build_identity.py',repo/'tools/build_identity.py')
            shutil.copy2(source/'cmake/build_identity.cmake',repo/'cmake/build_identity.cmake')
            (repo/'.gitignore').write_text('build/\n')
            (repo/'main/main.c').write_text('#include "city_build_identity.h"\nint main(void){return CITY_BUILD_DIRTY;}\n')
            (repo/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.16)\ninclude(cmake/build_identity.cmake)\nproject(identity_fixture C)\nadd_executable(fixture main/main.c)\ntarget_include_directories(fixture PRIVATE "${CMAKE_BINARY_DIR}/identity")\n')
            def run(*args):
                subprocess.run(args,cwd=repo,check=True,capture_output=True)
            run('git','init');run('git','config','user.name','Fixture')
            run('git','config','user.email','fixture@example.invalid')
            run('git','add','.');run('git','commit','-m','fixture')
            run('cmake','-S','.', '-B','build')
            run('cmake','--build','build')
            def built():
                return json.loads((repo/'build/identity/build-identity.json').read_text())
            original=built()
            self.assertFalse(original['dirty'])
            with (repo/'main/main.c').open('a') as f: f.write('// edited\n')
            run('cmake','--build','build')
            edited=built()
            self.assertTrue(edited['dirty'])
            self.assertNotEqual(original['version'],edited['version'])
            (repo/'main/new.h').write_text('// untracked\n')
            run('cmake','--build','build')
            self.assertNotEqual(edited['version'],built()['version'])
            self.assertIn('main/new.h',built()['source_files'])
            run('git','add','.');run('git','commit','-m','updated')
            run('cmake','--build','build')
            self.assertFalse(built()['dirty'])
            self.assertNotEqual(original['git_commit'],built()['git_commit'])

    def test_snapshot_and_package(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp); repo=root/'repo'; repo.mkdir()
            def git(*args):
                subprocess.run(['git','-C',str(repo),*args],check=True,capture_output=True)
            git('init');git('config','user.name','Fixture');git('config','user.email','fixture@example.invalid')
            (repo/'.gitignore').write_text('build/\nsdkconfig\n')
            (repo/'main.c').write_text('v1')
            (repo/'docs').mkdir();(repo/'docs/device-installation-guide.md').write_text('fixture')
            git('add','.');git('commit','-m','fixture')
            clean=identity(repo)
            self.assertFalse(clean['dirty'])
            (repo/'main.c').write_text('v2')
            dirty=identity(repo)
            self.assertTrue(dirty['dirty']);self.assertIn('-dirty',dirty['version'])
            self.assertNotEqual(clean['source_sha256'],dirty['source_sha256'])
            (repo/'main.c').write_text('v3')
            self.assertNotEqual(dirty['version'],identity(repo)['version'])
            (repo/'extra.c').write_text('new')
            self.assertIn('extra.c',identity(repo)['source_files'])
            before=identity(repo);(repo/'build').mkdir();(repo/'build/cache').write_text('ignored')
            self.assertEqual(before,identity(repo))
            build=repo/'build';saved=generate(repo,build/'identity')
            self.assertLessEqual(len(saved['version']),31)
            image=bytearray(300);image[0]=0xe9;image[32:36]=bytes.fromhex('3254cdab')
            version=saved['version'].encode();image[48:48+len(version)]=version
            (build/'Pokedex-AI-Passport.bin').write_bytes(image)
            (repo/'sdkconfig').write_text('fixture config')
            (build/'project_description.json').write_text(json.dumps({'config_file':str(repo/'sdkconfig')}))
            with self.assertRaisesRegex(ValueError,'clean source'):
                package(repo,build,root/'release')
            result=package(repo,build,root/'test',allow_dirty=True)
            self.assertEqual(result['version'],saved['version'])
            self.assertTrue((root/'test/SHA256SUMS').exists())
            image[48]=ord('x');(build/'Pokedex-AI-Passport.bin').write_bytes(image)
            with self.assertRaisesRegex(ValueError,'Image version'):
                package(repo,build,root/'wrong',allow_dirty=True)
            (repo/'main.c').write_text('v4')
            with self.assertRaisesRegex(ValueError,'Source changed'):
                package(repo,build,root/'stale',allow_dirty=True)
            git('add','.');git('commit','-m','updated')
            self.assertFalse(identity(repo)['dirty'])
            self.assertNotEqual(clean['git_commit'],identity(repo)['git_commit'])


if __name__=='__main__':
    unittest.main()

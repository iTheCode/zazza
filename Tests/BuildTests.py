import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parent.parent


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / (name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


class BuildTests(unittest.TestCase):
    def test_sdk_manifest_trailing_commas(self):
        manifest = module('build').parse_manifest('''{
            "Version": "1.0.0",
            "Factory Info": {"Vendor": "Zazza",},
            "Classes": [{"Name": "Zazza",},],
        }''')
        self.assertEqual(manifest['Version'], '1.0.0')
        self.assertEqual(manifest['Classes'][0]['Name'], 'Zazza')

    def test_manifest_preserves_quoted_content(self):
        value = {'Version': '1.0.0', 'Name': 'Quoted "name",} and comma,]'}
        self.assertEqual(module('build').parse_manifest(json.dumps(value)), value)

    def test_manifest_rejects_bad_data(self):
        for text in ('{"Version":"../bad"}', '{broken}', '{"Version":"1.0.0",,"Other":1}'):
            with self.assertRaises(ValueError):
                module('build').parse_manifest(text)

    def test_source_package_is_complete_and_preserves_existing(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / 'source.zip'
            packager = module('package_source')
            packager.package(destination)
            original = destination.read_bytes()
            with zipfile.ZipFile(destination) as archive:
                self.assertIsNone(archive.testzip())
                names = archive.namelist()
                for relative in ('.github/workflows/build.yml', '.gitignore', 'CMakeLists.txt', 'LICENSE',
                                 'assets/wallpaper.gif', 'assets/fonts/LICENSE.txt', 'Tests/BuildTests.py',
                                 'Source/PluginProcessor.cpp', 'docs/VALIDATION.md', 'scripts/build.py'):
                    self.assertIn('Zazza/' + relative, names)
                for name in names:
                    self.assertFalse(any(part.startswith(('build', '.tools', '.vecchagent', '__pycache__')) for part in Path(name).parts[:-1]))
                self.assertEqual(archive.read('Zazza/assets/wallpaper.gif'), (ROOT / 'assets/wallpaper.gif').read_bytes())
            with self.assertRaises(FileExistsError):
                packager.package(destination)
            self.assertEqual(destination.read_bytes(), original)


if __name__ == '__main__':
    unittest.main()


class MacSigningTests(unittest.TestCase):
    def test_signing_configuration(self):
        import importlib.util
        from unittest.mock import patch
        spec = importlib.util.spec_from_file_location('zazza_build_signing', Path(__file__).resolve().parents[1] / 'scripts/build.py')
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        with patch.dict(builder.os.environ, {}, clear=True):
            self.assertIsNone(builder.macos_signing())
        with patch.dict(builder.os.environ, {'MACOS_SIGN_IDENTITY': 'Apple Distribution: X (T)'}, clear=True):
            with self.assertRaisesRegex(ValueError, 'Developer ID'):
                builder.macos_signing()
        with patch.dict(builder.os.environ, {'MACOS_SIGN_IDENTITY': 'Developer ID Application: X (T)'}, clear=True):
            with self.assertRaisesRegex(RuntimeError, 'ASC_ISSUER_ID'):
                builder.macos_signing()

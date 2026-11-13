import importlib.util
import json
from pathlib import Path
import tempfile
import subprocess
import unittest
import zipfile
import argparse
from unittest.mock import call, patch

ROOT = Path(__file__).resolve().parent.parent


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / (name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


class BuildTests(unittest.TestCase):
    def test_planned_version_reaches_cmake_and_must_match_plugin(self):
        builder = module('build')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            products = root / 'build/Zazza_artefacts/Release'
            resources = products / 'VST3/Zazza.vst3/Contents/Resources'
            resources.mkdir(parents=True)
            (resources / 'moduleinfo.json').write_text('{"Version":"1.2.3"}')
            (products / 'Standalone').mkdir()
            (products / 'Standalone/Zazza.exe').write_bytes(b'fixture')
            args = argparse.Namespace(version='1.2.3', arch='x86_64', cmake='cmake', build_dir='build',
                                      dist_dir='dist', juce_source=None, jobs=2, pluginval=False, package=False)
            with patch.object(builder, 'ROOT', root), patch.object(builder.platform, 'system', return_value='Windows'), \
                 patch.object(builder.shutil, 'which', return_value='C:/cmake/bin/cmake.exe'), patch.object(builder, 'run') as commands:
                builder.build(args)
                self.assertIn('-DZAZZA_VERSION=1.2.3', commands.call_args_list[0].args)
                args.version = '1.2.4'
                with self.assertRaisesRegex(RuntimeError, 'planned release'):
                    builder.build(args)
                commands.reset_mock()
                args.version = None
                builder.build(args)
                self.assertIn('-UZAZZA_VERSION', commands.call_args_list[0].args)
                for invalid in ('1.2', '01.2.3', '1.2.3-beta.1', '1.2.3;echo bad'):
                    args.version = invalid
                    with self.assertRaisesRegex(ValueError, 'Invalid build version'):
                        builder.build(args)

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
                                 '.github/scripts/apple-signing.sh', '.nvmrc', 'package.json', 'package-lock.json',
                                 'release.config.cjs', 'scripts/plan-release.mjs', 'scripts/verify-release.mjs', 'Tests/ReleaseTests.mjs',
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
    def test_stapler_success_is_bounded_and_does_not_sleep(self):
        builder = module('build')
        with patch.object(builder.subprocess, 'run') as process, patch.object(builder.time, 'sleep') as sleep:
            builder.run_stapler('validate', Path('Zazza.vst3'))
            process.assert_called_once_with(['xcrun', 'stapler', 'validate', 'Zazza.vst3'],
                                            cwd=builder.ROOT, check=True, timeout=120)
            sleep.assert_not_called()

    def test_stapler_retries_service_failure_and_timeout(self):
        builder = module('build')
        for action in ('staple', 'validate'):
            with self.subTest(action=action), \
                 patch.object(builder, 'run', side_effect=[subprocess.CalledProcessError(68, 'stapler'),
                                                          subprocess.TimeoutExpired('stapler', 120), None]) as run, \
                 patch.object(builder.time, 'sleep') as sleep:
                builder.run_stapler(action, Path('Zazza.vst3'))
                self.assertEqual(run.call_args_list, [call('xcrun', 'stapler', action, Path('Zazza.vst3'), timeout=120)] * 3)
                self.assertEqual(sleep.call_args_list, [call(5), call(10)])

    def test_stapler_permanent_errors_fail_immediately(self):
        builder = module('build')
        for code in (64, 65, 66, 73, 77, 1):
            error = subprocess.CalledProcessError(code, 'stapler')
            with self.subTest(code=code), patch.object(builder, 'run', side_effect=error) as run, \
                 patch.object(builder.time, 'sleep') as sleep:
                with self.assertRaises(subprocess.CalledProcessError) as raised:
                    builder.run_stapler('validate', 'Zazza.app')
                self.assertIs(raised.exception, error)
                run.assert_called_once()
                sleep.assert_not_called()

    def test_stapler_retry_exhaustion_still_fails(self):
        builder = module('build')
        for error in (subprocess.CalledProcessError(68, 'stapler'), subprocess.TimeoutExpired('stapler', 120)):
            with self.subTest(error=type(error).__name__), patch.object(builder, 'run', side_effect=error) as run, \
                 patch.object(builder.time, 'sleep') as sleep:
                with self.assertRaises(type(error)) as raised:
                    builder.run_stapler('validate', 'Zazza.app')
                self.assertIs(raised.exception, error)
                self.assertEqual(run.call_count, 5)
                self.assertEqual(sleep.call_args_list, [call(5), call(10), call(20), call(40)])

    def test_notarization_retries_only_failed_validation(self):
        builder = module('build')
        vst, app = Path('Zazza.vst3'), Path('Zazza.app')
        validations = 0

        def command(*args, **kwargs):
            nonlocal validations
            if args[:4] == ('xcrun', 'stapler', 'validate', vst):
                validations += 1
                if validations == 1:
                    raise subprocess.CalledProcessError(68, args)

        with tempfile.TemporaryDirectory() as directory, \
             patch.dict(builder.os.environ, {key: 'fixture' for key in builder.NOTARY_VARS}), \
             patch.object(builder, 'run', side_effect=command) as run, patch.object(builder.time, 'sleep'):
            builder.notarize_macos([vst, app], Path(directory))
            commands = [entry.args for entry in run.call_args_list]
            self.assertEqual(sum(args[:3] == ('xcrun', 'notarytool', 'submit') for args in commands), 1)
            self.assertEqual([args for args in commands if args[:2] == ('xcrun', 'stapler')], [
                ('xcrun', 'stapler', 'staple', vst), ('xcrun', 'stapler', 'validate', vst),
                ('xcrun', 'stapler', 'validate', vst), ('xcrun', 'stapler', 'staple', app),
                ('xcrun', 'stapler', 'validate', app)])
            self.assertEqual(commands[-1], ('spctl', '--assess', '--type', 'execute', '--verbose=2', app))

    def test_notarization_aborts_when_validation_never_recovers(self):
        builder = module('build')

        def command(*args, **kwargs):
            if args[:3] == ('xcrun', 'stapler', 'validate'):
                raise subprocess.CalledProcessError(68, args)

        with tempfile.TemporaryDirectory() as directory, \
             patch.dict(builder.os.environ, {key: 'fixture' for key in builder.NOTARY_VARS}), \
             patch.object(builder, 'run', side_effect=command) as run, patch.object(builder.time, 'sleep'):
            with self.assertRaises(subprocess.CalledProcessError):
                builder.notarize_macos([Path('Zazza.vst3'), Path('Zazza.app')], Path(directory))
            self.assertFalse(any(entry.args[0] == 'spctl' for entry in run.call_args_list))
            self.assertFalse(any(entry.args[:4] == ('xcrun', 'stapler', 'staple', Path('Zazza.app'))
                                 for entry in run.call_args_list))

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

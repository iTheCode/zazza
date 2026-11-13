import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from BuildTests import module


class ValidatorTests(unittest.TestCase):
    def test_native_commands_keep_gui_tests(self):
        validator = module('validate_plugin')
        for system, arch in (('Darwin', 'arm64'), ('Darwin', 'x86_64'), ('Windows', 'x86_64')):
            command = validator.validation_command(Path('validator'), Path('Zazza.vst3'), system, arch)
            self.assertEqual(command[-2:], ['--validate', 'Zazza.vst3'])
            self.assertIn('--strictness-level', command)
            self.assertNotIn('--skip-gui-tests', command)
            if system == 'Darwin':
                self.assertEqual(command[:2], ['arch', '-' + arch])
            else:
                self.assertEqual(command[0], 'validator')
        with self.assertRaises(ValueError):
            validator.validation_command(Path('validator'), Path('plugin'), 'Windows', 'arm64')

    def test_checksum_rejects_tampering(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'validator.zip'
            archive.write_bytes(b'expected download')
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            validator = module('validate_plugin')
            validator.verify_checksum(archive, digest)
            archive.write_bytes(b'wrong download')
            with self.assertRaises(ValueError):
                validator.verify_checksum(archive, digest)

    def validate_with_result(self, result):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            plugin = root / 'Zazza.vst3'; plugin.mkdir()
            validator = module('validate_plugin')
            def run(command, **options):
                self.assertTrue(options['check'])
                self.assertEqual(options['timeout'], 600)
                options['stdout'].write('Test output\n')
                if isinstance(result, Exception):
                    raise result
                options['stdout'].write(result)
            with patch.object(validator, 'install', return_value=root / 'pluginval.exe'), patch.object(validator.subprocess, 'run', side_effect=run):
                validator.validate(plugin, 'x86_64', root / 'validation.log', root / 'cache', 'Windows')

    def test_failed_validator_fails_job(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.validate_with_result(subprocess.CalledProcessError(1, 'pluginval'))

    def test_validator_timeout_fails_job(self):
        with self.assertRaises(subprocess.TimeoutExpired):
            self.validate_with_result(subprocess.TimeoutExpired('pluginval', 600))

    def test_exit_zero_without_confirmation_fails(self):
        with self.assertRaises(RuntimeError):
            self.validate_with_result('No validation performed\n')

    def test_success_requires_confirmation(self):
        self.validate_with_result('SUCCESS\n')

    def test_windows_packaging_is_blocked_on_failed_validation(self):
        builder = module('build')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            products = root / 'build/Zazza_artefacts/Release'
            resources = products / 'VST3/Zazza.vst3/Contents/Resources'; resources.mkdir(parents=True)
            (resources / 'moduleinfo.json').write_text('{"Version":"1.0.0",}', encoding='utf-8')
            (products / 'Standalone').mkdir()
            (products / 'Standalone/Zazza.exe').write_bytes(b'fixture')
            args = argparse.Namespace(arch='x86_64', cmake='cmake', build_dir='build', dist_dir='dist',
                                      juce_source=None, jobs=2, pluginval=True, package=True)
            def run(*command):
                if any(str(arg).endswith('validate_plugin.py') for arg in command):
                    raise subprocess.CalledProcessError(1, command)
            with patch.object(builder, 'ROOT', root), patch.object(builder.platform, 'system', return_value='Windows'), \
                 patch.object(builder.shutil, 'which', return_value='C:/cmake/bin/cmake.exe'), patch.object(builder, 'run', side_effect=run) as commands:
                with self.assertRaises(subprocess.CalledProcessError):
                    builder.build(args)
                self.assertTrue(any('Visual Studio 17 2022' in call.args for call in commands.call_args_list))
                self.assertFalse((root / 'dist').exists())


if __name__ == '__main__':
    unittest.main()

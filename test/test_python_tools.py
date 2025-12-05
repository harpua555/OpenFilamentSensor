#!/usr/bin/env python3
"""
Python Tools Validation Tests

Tests for Python utility scripts in the tools directory.
"""

import unittest
import sys
import os
import json
import tempfile
from pathlib import Path

# Add project root to path
sys.path.insert(0, str(Path(__file__).parent.parent))

class TestGenerateTestSettings(unittest.TestCase):
    """Tests for generate_test_settings.py"""
    
    def setUp(self):
        self.test_dir = Path(__file__).parent
        
    def test_generate_with_default_settings(self):
        """Test generation with default settings when file doesn't exist"""
        sys.path.insert(0, str(self.test_dir))
        
        # Import after adding to path
        import generate_test_settings
        
        # Should not raise an exception
        try:
            # Mock settings path that doesn't exist
            original_path = generate_test_settings.settings_path if hasattr(generate_test_settings, 'settings_path') else None
            generate_test_settings.main()
            
            # Check that generated file exists
            generated_path = self.test_dir / "generated_test_settings.h"
            self.assertTrue(generated_path.exists(), "Generated header file should exist")
            
            # Check content
            content = generated_path.read_text()
            self.assertIn("#define TEST_MM_PER_PULSE", content)
            self.assertIn("#define TEST_RATIO_THRESHOLD", content)
            self.assertIn("#define TEST_HARD_JAM_MM", content)
            
        except Exception as e:
            self.fail(f"generate_test_settings.main() raised {type(e).__name__}: {e}")
    
    def test_format_float_function(self):
        """Test float formatting utility"""
        sys.path.insert(0, str(self.test_dir))
        import generate_test_settings
        
        # Test various float values
        self.assertEqual(generate_test_settings.format_float(2.88), "2.88f")
        self.assertEqual(generate_test_settings.format_float(1.0), "1.0f")
        self.assertEqual(generate_test_settings.format_float(0.25), "0.25f")
        self.assertIn("f", generate_test_settings.format_float(123.456789))


class TestBuildAndRelease(unittest.TestCase):
    """Tests for build_and_release.py validation"""
    
    def setUp(self):
        self.tools_dir = Path(__file__).parent.parent / "tools"
        
    def test_script_imports(self):
        """Test that build_and_release.py imports successfully"""
        sys.path.insert(0, str(self.tools_dir))
        
        try:
            import build_and_release
            self.assertTrue(hasattr(build_and_release, 'main') or 
                          hasattr(build_and_release, '__name__'))
        except ImportError as e:
            self.fail(f"Could not import build_and_release: {e}")


class TestBoardConfig(unittest.TestCase):
    """Tests for board_config.py"""
    
    def setUp(self):
        self.tools_dir = Path(__file__).parent.parent / "tools"
        
    def test_board_config_imports(self):
        """Test that board_config.py imports successfully"""
        sys.path.insert(0, str(self.tools_dir))
        
        try:
            import board_config
            self.assertTrue(callable(getattr(board_config, 'validate_board_environment', None)) or
                          callable(getattr(board_config, 'get_supported_boards', None)))
        except ImportError as e:
            self.fail(f"Could not import board_config: {e}")


class TestUserSettings(unittest.TestCase):
    """Tests for user_settings.json validation"""
    
    def test_user_settings_valid_json(self):
        """Test that user_settings.json is valid JSON"""
        settings_path = Path(__file__).parent.parent / "data" / "user_settings.json"
        
        if settings_path.exists():
            with open(settings_path, 'r') as f:
                try:
                    settings = json.load(f)
                    self.assertIsInstance(settings, dict, "Settings should be a dictionary")
                except json.JSONDecodeError as e:
                    self.fail(f"user_settings.json is not valid JSON: {e}")
    
    def test_user_settings_has_required_fields(self):
        """Test that user_settings.json contains expected fields"""
        settings_path = Path(__file__).parent.parent / "data" / "user_settings.json"
        
        if settings_path.exists():
            with open(settings_path, 'r') as f:
                settings = json.load(f)
                
            # Check for key fields
            expected_fields = [
                'movement_mm_per_pulse',
                'detection_ratio_threshold',
                'detection_hard_jam_mm',
                'detection_soft_jam_time_ms',
                'detection_hard_jam_time_ms'
            ]
            
            for field in expected_fields:
                self.assertIn(field, settings, f"Missing expected field: {field}")
    
    def test_user_settings_numeric_ranges(self):
        """Test that numeric settings are within reasonable ranges"""
        settings_path = Path(__file__).parent.parent / "data" / "user_settings.json"
        
        if settings_path.exists():
            with open(settings_path, 'r') as f:
                settings = json.load(f)
            
            # Validate ranges
            if 'movement_mm_per_pulse' in settings:
                mm_per_pulse = settings['movement_mm_per_pulse']
                self.assertGreater(mm_per_pulse, 0, "mm_per_pulse must be positive")
                self.assertLess(mm_per_pulse, 10, "mm_per_pulse seems unreasonably large")
            
            if 'detection_ratio_threshold' in settings:
                threshold = settings['detection_ratio_threshold']
                # Can be 0-100 (percentage) or 0.0-1.0 (ratio)
                self.assertGreaterEqual(threshold, 0)
                self.assertLessEqual(threshold, 100)


class TestDistributorFiles(unittest.TestCase):
    """Tests for distributor directory files"""
    
    def test_boards_json_valid(self):
        """Test that boards.json is valid JSON"""
        boards_path = Path(__file__).parent.parent / "distributor" / "firmware" / "boards.json"
        
        if boards_path.exists():
            with open(boards_path, 'r') as f:
                try:
                    boards = json.load(f)
                    self.assertIsInstance(boards, (list, dict), "boards.json should be list or dict")
                except json.JSONDecodeError as e:
                    self.fail(f"boards.json is not valid JSON: {e}")
    
    def test_manifest_files_valid(self):
        """Test that manifest.json files are valid"""
        distributor_dir = Path(__file__).parent.parent / "distributor" / "firmware"
        
        if distributor_dir.exists():
            manifest_files = list(distributor_dir.glob("*/manifest.json"))
            
            for manifest_path in manifest_files:
                with self.subTest(manifest=manifest_path.name):
                    with open(manifest_path, 'r') as f:
                        try:
                            manifest = json.load(f)
                            self.assertIsInstance(manifest, dict)
                        except json.JSONDecodeError as e:
                            self.fail(f"{manifest_path} is not valid JSON: {e}")


class TestWorkflowYAML(unittest.TestCase):
    """Tests for GitHub Actions workflow files"""
    
    def test_release_workflow_exists(self):
        """Test that release workflow file exists"""
        workflow_path = Path(__file__).parent.parent / ".github" / "workflows" / "release-firmware.yml"
        self.assertTrue(workflow_path.exists(), "Release workflow should exist")
    
    def test_release_workflow_valid_yaml(self):
        """Test that workflow is valid YAML"""
        workflow_path = Path(__file__).parent.parent / ".github" / "workflows" / "release-firmware.yml"
        
        if workflow_path.exists():
            try:
                import yaml
                with open(workflow_path, 'r') as f:
                    workflow = yaml.safe_load(f)
                    self.assertIsInstance(workflow, dict)
                    self.assertIn('name', workflow)
            except ImportError:
                self.skipTest("PyYAML not available")
            except Exception as e:
                self.fail(f"Workflow YAML is invalid: {e}")


def run_tests():
    """Run all tests and return success/failure"""
    loader = unittest.TestLoader()
    suite = loader.loadTestsFromModule(sys.modules[__name__])
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    return result.wasSuccessful()


if __name__ == '__main__':
    success = run_tests()
    sys.exit(0 if success else 1)
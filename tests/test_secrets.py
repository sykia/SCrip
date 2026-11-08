import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "check_secrets", Path(__file__).parents[1] / "scripts/check_secrets.py"
)
check_secrets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check_secrets)


class SecretScannerTests(unittest.TestCase):
    def test_detects_secret_assignment_without_disclosing_value(self):
        value = "A7x" * 12
        findings = check_secrets.scan_files({"config.json": ('{"client_secret":"' + value + '"}').encode()})
        self.assertEqual(findings, ["config.json"])
        self.assertNotIn(value.encode(), b" ".join(name.encode() for name in findings))

    def test_detects_known_token_and_private_key(self):
        token = b"ghp_" + b"a" * 32
        self.assertTrue(check_secrets.content_has_secret(b"value=" + token))
        begin = b"-----BEGIN RSA " + b"PRIVATE KEY-----"
        end = b"-----END RSA " + b"PRIVATE KEY-----"
        self.assertTrue(check_secrets.content_has_secret(begin + b"\nredacted\n" + end))

    def test_detects_secret_files(self):
        self.assertEqual(check_secrets.scan_files({"config/.env": b"", "keys/app.pem": b""}),
                         ["config/.env", "keys/app.pem"])
        self.assertEqual(check_secrets.scan_files({".env.example": b"", ".env.template": b""}), [])

    def test_ignores_environment_references_placeholders_and_field_names(self):
        content = b'client_secret: env.CLIENT_SECRET\n' + \
                  b'access_token = os.getenv("ACCESS_TOKEN")\n' + \
                  b'{"client_secret": "test-placeholder-value"}\n' + \
                  b'{"access_token": "short-token"}\n'
        self.assertFalse(check_secrets.content_has_secret(content))


if __name__ == "__main__":
    unittest.main()

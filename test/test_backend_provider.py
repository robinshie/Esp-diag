import io
import json
import os
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
from urllib.error import URLError

from fastapi.testclient import TestClient
from esp_backend.ai_provider import AISettings, ExplanationService, MAX_EVIDENCE_BYTES, ProviderUnavailable, evidence_text
from esp_backend.api import create_app
from test.test_backend_native import native_report


class ProviderTest(unittest.TestCase):
    def service(self, provider="ollama", response=None):
        opener = Mock()
        data = response or {"message": {"content": "交换机心跳正常，仅供参考。"}}
        opener.open.side_effect = lambda *args, **kwargs: io.BytesIO(json.dumps(data).encode())
        return ExplanationService(AISettings(provider, "http://127.0.0.1:11434", "local-model", 2, "private-key"), opener)

    def test_disabled_is_default_and_never_contacts_provider(self):
        with patch.dict(os.environ, {}, clear=True):
            self.assertEqual(AISettings.load().provider, "disabled")
        service = ExplanationService(opener=Mock())
        self.assertFalse(service.status()["enabled"])
        with self.assertRaises(ProviderUnavailable):
            service.explain(native_report())
        service.opener.open.assert_not_called()

    def test_configuration_requires_explicit_model_and_valid_url(self):
        for kwargs in ({"provider": "other"}, {"provider": "ollama", "base_url": "file:///tmp/model", "model": "x"},
                       {"provider": "ollama", "base_url": "http://user:pass@localhost", "model": "x"},
                       {"provider": "ollama", "base_url": "http://localhost", "model": ""},
                       {"timeout_seconds": 0}, {"timeout_seconds": 61}):
            with self.assertRaises(ValueError):
                AISettings(**kwargs)
        self.assertNotIn("private-key", repr(self.service().settings))

    def test_ollama_request_is_bounded_readonly_and_based_on_saved_evidence(self):
        service = self.service()
        result = service.explain(native_report(), 0)
        request = service.opener.open.call_args.args[0]
        body = json.loads(request.data)
        self.assertEqual(request.full_url, "http://127.0.0.1:11434/api/chat")
        self.assertFalse(body["stream"])
        self.assertEqual(service.opener.open.call_args.kwargs["timeout"], 2)
        self.assertNotIn("tools", body)
        self.assertEqual(json.loads(body["messages"][1]["content"])["clock"], "uptime")
        self.assertFalse(result["device_control"])
        self.assertEqual(result["source"], "backend_provider")
        self.assertEqual(result["evidence_source"], "esp_native")
        self.assertNotIn("base_url", result)
        self.assertNotIn("private-key", json.dumps(result))

    def test_compatible_provider_and_invalid_response(self):
        service = self.service("openai-compatible", {"choices": [{"message": {"content": "参考解释"}}]})
        self.assertEqual(service.explain(native_report())["text"], "参考解释")
        self.assertTrue(service.opener.open.call_args.args[0].full_url.endswith("/chat/completions"))
        for data in ({"message": {"content": None}}, {"message": {"content": ""}}, {"unexpected": "bad"},
                     {"message": {"content": "x" * 70000}}):
            with self.assertRaises(ProviderUnavailable):
                self.service(response=data).explain(native_report())

    def test_timeout_failure_and_concurrency_release_slot(self):
        service = self.service()
        service.opener.open.side_effect = URLError("secret provider response")
        with self.assertRaisesRegex(ProviderUnavailable, "unavailable") as caught:
            service.explain(native_report())
        self.assertNotIn("secret", str(caught.exception))
        self.assertTrue(service.slot.acquire(blocking=False))
        with self.assertRaisesRegex(ProviderUnavailable, "busy"):
            service.explain(native_report())
        service.slot.release()

    def test_evidence_is_redacted_and_utf8_truncation_is_valid(self):
        report = native_report()
        report["lines"] = [{"text": "password=hidden token:secret\n" + "网络" * MAX_EVIDENCE_BYTES}]
        text, truncated = evidence_text(report)
        self.assertTrue(truncated)
        self.assertLessEqual(len(text.encode()), MAX_EVIDENCE_BYTES)
        self.assertNotIn("hidden", text)
        self.assertNotIn("secret", text)


class ProviderAPITest(unittest.TestCase):
    def test_authentication_saved_evidence_and_disabled_behavior(self):
        settings = SimpleNamespace(token="x" * 32)
        repository = Mock()
        repository.saved_report.return_value = native_report()
        with TestClient(create_app(settings, repository, Mock())) as client:
            path = "/api/reports/boot-a-1/analyze?agent=edge-1"
            self.assertEqual(client.post(path).status_code, 401)
            self.assertEqual(client.get("/api/ai/status").status_code, 401)
            client.headers["Authorization"] = "Bearer " + settings.token
            status = client.get("/api/ai/status").json()
            self.assertFalse(status["enabled"])
            self.assertFalse(status["device_control"])
            self.assertEqual(client.post(path).status_code, 503)
            self.assertEqual(client.post(path + "&line=5").status_code, 404)
            self.assertEqual(client.post(path + "&line=-1").status_code, 422)
            repository.saved_report.return_value = None
            self.assertEqual(client.post(path).status_code, 404)


if __name__ == "__main__":
    unittest.main()

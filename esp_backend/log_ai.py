"""Read-only inference using the same trained artifact as the ESP experiment."""
from functools import lru_cache
import json
import math
from pathlib import Path
import re

SKIP = set('device component incident request req boot boot_id uptime_ms unix_ms duration_ms first_uptime_ms nonce'.split())


@lru_cache(maxsize=1)
def model():
    path = Path(__file__).resolve().parents[1] / 'experiments/tiny_log_explainer/artifacts/model.json'
    return json.loads(path.read_text())


def features(text, dimension):
    words, skip = [], False
    for word in re.findall(r'[a-z0-9_]+', text.lower()):
        if skip:
            skip = False
        elif word in SKIP:
            skip = True
        elif len(word) <= 32 and not any(c.isdigit() for c in word):
            words.append(word)
    tokens = {'w:' + word for word in words}
    tokens.update('b:' + a + ':' + b for a, b in zip(words, words[1:]))
    for word in words:
        parts = [p for p in word.split('_') if p]
        tokens.update('w:' + p for p in parts)
        tokens.update('b:' + a + ':' + b for a, b in zip(parts, parts[1:]))
    result = set()
    for token in tokens:
        value = 2166136261
        for byte in token.encode('ascii'):
            value = ((value ^ byte) * 16777619) & 0xffffffff
        result.add(value % dimension)
    return sorted(result)


def predict(text):
    data = model()
    raw = text.encode('utf-8')
    indices = features(raw[:1024].decode('utf-8', errors='ignore'), data['dimension'])
    scale = 1 / math.sqrt(max(1, len(indices)))
    scores = [bias + sum(row[i] for i in indices) * scale for row, bias in zip(data['weights'], data['bias'])]
    maximum = max(scores)
    values = [math.exp(score - maximum) for score in scores]
    total = sum(values)
    category = max(range(len(values)), key=values.__getitem__)
    score, label = values[category] / total, data['labels'][category]
    return {'experimental': True, 'model': data['version'], 'label': label, 'score': score,
            'threshold': data['threshold'], 'truncated': len(raw) > 1024,
            'accepted': len(indices) >= 2 and len(raw) <= 1024 and score >= data['threshold'] and label != 'unknown'}

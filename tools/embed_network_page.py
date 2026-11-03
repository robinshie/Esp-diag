"""Embed the device pages with isolated shared modules and validate their JS syntax."""
import subprocess
import tempfile
import re
from pathlib import Path
root = Path(__file__).resolve().parents[1]
for name, symbol in (("network", "NETWORK_PAGE"), ("archive", "ARCHIVE_PAGE")):
    page = (root / f"frontend/device/{name}.html").read_text()
    if name == 'archive':
        modules = ['fault-explainer.js', 'model-descriptions.js', 'loop-storm.js', 'report-analysis.js', 'report-ai-view.js']
        scripts = []
        for module in modules:
            source = (root / 'frontend/shared' / module).read_text()
            source = '\n'.join(line for line in source.splitlines() if not line.startswith('import '))
            source = source.replace('export function ', 'function ').replace('export const ', 'const ')
            assert '</script' not in source.lower()
            scripts.append(source)
        page = page.replace("import {renderReportAI} from '../shared/report-ai-view.js';", "const {renderReportAI}=(()=>{\n" + '\n'.join(scripts) + "\nreturn {renderReportAI};\n})();")
    # Syntax-check the exact offline payload, since module merging can introduce
    # declarations that are valid separately but collide in a single script.
    for script in re.findall(r'<script[^>]*>(.*?)</script>', page, re.S):
        with tempfile.NamedTemporaryFile(suffix='.mjs', mode='w') as handle:
            handle.write(script); handle.flush()
            subprocess.run(['node', '--check', handle.name], check=True, capture_output=True, text=True)
    assert ')ESP_PAGE"' not in page
    (root / f"src/generated/{name}_page.h").write_text(
        f'// Generated from frontend/device/{name}.html by tools/embed_network_page.py\n'
        f'#pragma once\nstatic const char {symbol}[] PROGMEM = R"ESP_PAGE('
        + page + ')ESP_PAGE";\n'
    )

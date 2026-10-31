"""Embed the device pages. Run after editing either standalone HTML file."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
explainer_import = "import {explainRecords} from './src/fault-explainer.js';"
explainer = (root / 'frontend/src/fault-explainer.js').read_text().replace('export function ', 'function ')
assert '</script' not in explainer.lower()
for name, symbol in (("network", "NETWORK_PAGE"), ("journal", "JOURNAL_PAGE")):
    page = (root / f"frontend/{name}.html").read_text()
    if name == 'journal':
        assert page.count(explainer_import) == 1
        page = page.replace(explainer_import, explainer)
    assert ')ESP_PAGE"' not in page
    (root / f"src/{name}_page.h").write_text(
        f'// Generated from frontend/{name}.html by tools/embed_network_page.py\n'
        f'#pragma once\nstatic const char {symbol}[] PROGMEM = R"ESP_PAGE('
        + page + ')ESP_PAGE";\n'
    )

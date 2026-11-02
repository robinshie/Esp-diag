"""Export the qualified offline classifier into firmware, or check it is current."""
from pathlib import Path
import argparse

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    pairs = {
        "model.h": (ROOT / "experiments/tiny_log_explainer/artifacts/model.h").read_text(),
        "inference.h": (ROOT / "experiments/tiny_log_explainer/inference.h").read_text().replace('"artifacts/model.h"', '"model.h"'),
    }
    for name, source in pairs.items():
        destination = ROOT / "src/ai" / name
        if args.check:
            if not destination.exists() or destination.read_text() != source:
                raise SystemExit(f"Firmware classifier differs: {destination.relative_to(ROOT)}")
        else:
            destination.write_text(source)
    print("Firmware classifier matches qualified training artifacts")


if __name__ == "__main__":
    main()

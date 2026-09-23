#!/usr/bin/env python3
"""
Chip OPT configuration reader.
Loads chip model and OPT byte definitions from chip_config.json.
"""

import json
import os


class OptConfig:
    """Read chip OPT configuration and per-chip algorithm bindings."""

    def __init__(self, json_path=None, bindings_path=None):
        if json_path is None:
            json_path = os.path.join(os.path.dirname(__file__), 'chip_config.json')
        if bindings_path is None:
            bindings_path = os.path.join(os.path.dirname(__file__), 'algorithm_bindings.json')
        with open(json_path, 'r', encoding='utf-8') as f:
            self.data = json.load(f)
        self.bindings_path = bindings_path
        try:
            with open(bindings_path, 'r', encoding='utf-8') as f:
                self.bindings = json.load(f)
        except (FileNotFoundError, json.JSONDecodeError):
            self.bindings = {}

    def get_chip_models(self):
        """Return sorted list of available chip model names."""
        return sorted(self.data.keys())

    def get_chip(self, model):
        """Return OPT configuration dict for a given chip model, or None."""
        return self.data.get(model)

    def get_algorithm(self, model):
        return self.bindings.get(model, '')

    def set_algorithm(self, model, filename):
        self.bindings[model] = filename
        with open(self.bindings_path, 'w', encoding='utf-8') as f:
            json.dump(self.bindings, f, ensure_ascii=True, indent=2)
            f.write('\n')

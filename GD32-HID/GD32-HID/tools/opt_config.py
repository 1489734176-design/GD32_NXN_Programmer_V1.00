#!/usr/bin/env python3
"""
Chip OPT configuration reader.
Loads chip model and OPT byte definitions from chip_config.json.
"""

import json
import os


class OptConfig:
    """Read chip OPT configuration from JSON file."""

    def __init__(self, json_path=None):
        if json_path is None:
            json_path = os.path.join(os.path.dirname(__file__), 'chip_config.json')
        with open(json_path, 'r', encoding='utf-8') as f:
            self.data = json.load(f)

    def get_chip_models(self):
        """Return sorted list of available chip model names."""
        return sorted(self.data.keys())

    def get_chip(self, model):
        """Return OPT configuration dict for a given chip model, or None."""
        return self.data.get(model)

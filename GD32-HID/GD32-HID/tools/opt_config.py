#!/usr/bin/env python3
"""
Chip OPT configuration reader.
Loads chip model and OPT byte definitions from chip_config.json.
"""

import json
import os


# 芯片与算法固定配套，旧的用户绑定文件不能覆盖此关系。
CHIP_ALGORITHMS = {
    'MM32G0001A': 'MM32G001-OPT_ext.bin',
    'MM32SPIN0230': 'MM32SPIN0230-OPT_ext.bin',
}


class OptConfig:
    """读取芯片OPT配置与固定配套算法。"""

    def __init__(self, json_path=None, bindings_path=None):
        if json_path is None:
            json_path = os.path.join(os.path.dirname(__file__), 'chip_config.json')
        with open(json_path, 'r', encoding='utf-8') as f:
            self.data = json.load(f)
        # 保留bindings_path参数兼容旧调用；不再读取或保存可编辑的算法绑定。

    def get_chip_models(self):
        """Return sorted list of available chip model names."""
        return sorted(self.data.keys())

    def get_chip(self, model):
        """Return OPT configuration dict for a given chip model, or None."""
        return self.data.get(model)

    def get_algorithm(self, model):
        # 未选择或未知芯片返回空名，不能套用其他芯片算法。
        return CHIP_ALGORITHMS.get(model, '')

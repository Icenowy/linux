#!/usr/bin/env python

import sys
print sys.version

from dbg_py import *

if __name__ == "__main__":
    config_devices()
    set_dut_core_clk(25)
    set_dut_iface_clk(25)


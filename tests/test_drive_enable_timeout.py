#!/usr/bin/env python3
"""Keep ECMC's default drive timeout within the documented integration-test budget."""
from pathlib import Path
import re
header = (Path(__file__).resolve().parents[1]/'devEcmcSup/motion/ecmcDriveBase.h').read_text()
seconds = float(re.search(r'#define ERROR_DRV_STATE_MACHINE_TIME_OUT_TIME\s+(\d+)', header)[1])
assert 0 < seconds < 20, 'Review the 20-second missing-enable observation budget in ecmccfg slit tests'
print('Native drive enable timeout fits the documented 20-second integration-test budget.')

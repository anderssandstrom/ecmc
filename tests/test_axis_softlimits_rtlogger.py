#!/usr/bin/env python3
"""Verify that axis soft limits are owned by the motor-independent RT logger port."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'devEcmcSup/motion/ecmcRtLoggerPortDriver.cpp').read_text()

constructor = source[source.index(': asynPortDriver('):source.index('lastMessageParam_(0)')]
assert constructor.count('asynFloat64Mask') == 2, \
    'the rtlogger must expose and interrupt-enable the asynFloat64 interface'

for parameter in ('RTLOG_AXIS_CFG_DLLM', 'RTLOG_AXIS_CFG_DHLM'):
    assert f'createRequiredParam("{parameter}", asynParamFloat64' in source

write = source[source.index('  asynStatus writeFloat64('):
               source.index('  void publishMessage(')]
assert 'setAxisSoftLimitPosBwd(axis, value)' in write
assert 'setAxisSoftLimitPosFwd(axis, value)' in write

publish = source[source.index('  void publishAxisSoftLimits()'):
                 source.index('  void publishEcTiming()')]
assert 'getAxisSoftLimitPosBwd(axis, &low)' in publish
assert 'getAxisSoftLimitPosFwd(axis, &high)' in publish
assert 'setDoubleParam(axis, axisCfgDllmParam_, low)' in publish
assert 'setDoubleParam(axis, axisCfgDhlmParam_, high)' in publish

print('RT logger axis soft-limit read/write bindings passed')

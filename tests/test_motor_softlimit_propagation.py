#!/usr/bin/env python3
"""Exercise real motor-driver limit propagation with the production monitor.

Parameter storage and base-class callbacks are stand-ins. If ESS motor sources
are available, compile their real limit and OFF/DIR field-processing code too.
"""
# Basis: INFERRED: independent layers; COMPAT: startup seed/readback policy.
# DOC: restore units in documentation/motor-record-startup-restore.md.
# Details and coordinate-boundary limitations: motor_softlimits/README.md.
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'devEcmcSup/motor/ecmcMotorRecordAxis.cpp').read_text()


def method(signature, text=source):
    begin = text.index(signature)
    return text[begin:text.index('\n}', begin) + 2] + '\n'


signatures = (
    'static bool ecmcMotorRecordSoftLimitEqual(',
    'int ecmcMotorRecordAxis::applyMotorSoftLimitChange(',
    'bool ecmcMotorRecordAxis::isSoftLimitSyncEnabled()',
    'bool ecmcMotorRecordAxis::softLimitValueMatches(',
    'asynStatus ecmcMotorRecordAxis::updateSoftLimitSyncState(',
    'asynStatus ecmcMotorRecordAxis::syncEcmcSoftLimits(bool',
    'asynStatus ecmcMotorRecordAxis::syncMotorSoftLimits(bool',
    'const char *ecmcMotorRecordAxis::getSoftLimitRestoreStatus()',
    'void ecmcMotorRecordAxis::captureRestoredSoftLimits(',
    'asynStatus ecmcMotorRecordAxis::syncRestoredMotorSoftLimitsToEcmc()',
    'asynStatus ecmcMotorRecordAxis::initializeSoftLimits()',
    'asynStatus ecmcMotorRecordAxis::syncSoftLimitInterfaces(bool updateParams,',
    'asynStatus ecmcMotorRecordAxis::readBackSoftLimits(bool',
    'asynStatus ecmcMotorRecordAxis::setHighLimit(',
    'asynStatus ecmcMotorRecordAxis::setLowLimit(',
)

with tempfile.TemporaryDirectory(prefix='ecmc-limit-propagation-') as directory:
    build = Path(directory)
    (build / 'motor_limit_methods.h').write_text(''.join(map(method, signatures)))
    configured = os.environ.get('ECMC_TEST_MOTOR_SOURCE')
    candidates = [Path(configured)] if configured else [
        root.parent / 'motor/motorApp/MotorSrc',
        root.parent.parent / 'motorECMC_7.2.8.1/motor/motorApp/MotorSrc',
    ]
    motor_src = next((p for p in candidates if (p / 'motorRecord.cc').is_file()), None)
    record_methods = ''
    if configured and motor_src is None:
        raise SystemExit('ECMC_TEST_MOTOR_SOURCE must contain motorRecord.cc and motorDevSup.c')
    if motor_src:
        print('Testing actual ESS motor source:', motor_src, flush=True)
        record = (motor_src / 'motorRecord.cc').read_text()
        device = (motor_src / 'motorDevSup.c').read_text()
        asyn = (motor_src / 'devMotorAsyn.c').read_text()
        record_methods = '#define ECMC_TEST_REAL_MOTOR_RECORD 1\n'
        record_methods += method('double devSupDialToRaw(', device)
        record_methods += method('RTN_STATUS devSupUpdateLimitFromDial(', device)
        for name in ('softLimitsDefined', 'set_user_highlimit', 'set_user_lowlimit',
                     'set_dial_highlimit', 'set_dial_lowlimit', 'set_userlimits'):
            result = 'bool' if name == 'softLimitsDefined' else 'void'
            # Full signature excludes forward declarations with unnamed arguments.
            record_methods += method('static ' + result + ' ' + name + '(motorRecord *pmr)', record)
        record_methods += method('static void new_motor_soft_limits(', asyn)
        special = record[record.index('static long special(DBADDR *paddr, int after)'):]
        begin = special.index('    int dir_positive =')
        end = special.index('    bool changed =', begin)
        init = special[begin:end]
        begin = special.index('    case motorRecordDIR:')
        end = special.index('    case motorRecordHLM:', begin)
        record_methods += ('static void coordinateSpecial(motorRecord *pmr, int field) {\n' +
                           init + 'switch (field) {\n' + special[begin:end] + '\n}}\n')
    else:
        print('SKIP actual motor OFF/DIR processing: set ECMC_TEST_MOTOR_SOURCE; '
              'driver/monitor propagation checks still run.', flush=True)
    (build / 'motor_record_methods.h').write_text(record_methods)
    subprocess.run(['cmake', '-S', str(root / 'tests/motor_softlimits'),
                    '-B', directory, '-DCMAKE_BUILD_TYPE=Debug',
                    '-DCMAKE_CXX_FLAGS_DEBUG=-O0 -g0'], check=True)
    subprocess.run(['cmake', '--build', directory, '--parallel', '2'], check=True)
    subprocess.run([str(build / 'motorSoftLimitTests')], check=True)

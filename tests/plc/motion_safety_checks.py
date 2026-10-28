#!/usr/bin/env python3
"""Command rejection, limits and recovery through real motion PLC functions."""
# Expectation basis: INFERRED: rejection/recovery; COMPAT/REVIEW: endpoints and edge policy.
# Rationale, sources and open questions: ../EXPECTATIONS.md
from suite import run_cases


def add_cases(suite):
    # INFERRED: rejection must not launch an unauthorized move.
    # COMPAT/REVIEW L2: exact error, endpoint and execute-edge/reset policies.
    setup = '''axis 1 virtual
var global.enable 1
var global.execute 0
var global.target 0.5
var global.velocity 2
var global.acceleration 10
var global.deceleration 10
var global.reset 0
'''
    absolute = '''
global.power := mc_power(1, global.enable);
global.move := mc_move_abs(1, global.execute, global.target,
                          global.velocity, global.acceleration, global.deceleration);
if (global.reset) { mc_reset(1); };
global.busy := mc_get_busy(1);
global.position := mc_get_act_pos(1, 1);
global.error := mc_get_axis_err(1);
'''
    done = dict(power=0, move=0, busy=0, position=0.5, error=0)
    reset = '''set global.execute 0
set global.reset 1
run 1
set global.reset 0
step 10
'''

    suite.case('move_disabled_then_enable_and_recover', absolute, done,
               setup.replace('global.enable 1', 'global.enable 0'), '''
step 5
set global.execute 1
run 1
expect global.move 82710
expect_axis 1 enabled 0
expect_axis 1 setpoint 0
''' + reset + '''set global.enable 1
step 10
set global.execute 1
step 2000
''')
    for parameter, value, error, tag in (
            ('velocity', 0, 0x14D12, 'zero_velocity'),
            ('acceleration', 0, 0x14D10, 'zero_acceleration'),
            ('acceleration', -1, 0x14D10, 'negative_acceleration'),
            ('deceleration', 0, 0x14D11, 'zero_deceleration'),
            ('deceleration', -1, 0x14D11, 'negative_deceleration')):
        suite.case('reject_' + tag + '_then_recover', absolute, done, setup, '''
step 10
set global.PARAM VALUE
set global.execute 1
run 1
expect global.move ERROR
expect_axis 1 setpoint 0
expect_axis 1 velocity 0
'''.replace('PARAM', parameter).replace('VALUE', str(value)).replace('ERROR', str(error)) +
                   reset + 'set global.' + parameter + ' ' + ('2' if parameter == 'velocity' else '10') + '''
set global.execute 1
step 2000
''')

    suite.case('plc_permission_block_preserves_target_and_edge', absolute, done, setup, '''
step 10
axis_commands 1 0 0
set global.execute 1
run 1
expect global.power 131155
expect global.move 131155
expect_axis 1 target 0
expect_axis 1 setpoint 0
axis_commands 1 1 0
run 1
expect global.move 0
step 2000
''')
    suite.case('external_command_block_then_reset', absolute, done, setup, '''
step 10
axis_commands 1 1 1
set global.execute 1
run 1
expect global.move 131129
expect global.error 131129
expect_axis 1 target 0
expect_axis 1 setpoint 0
axis_commands 1 1 0
''' + reset + '''set global.execute 1
step 2000
''')
    suite.case('reset_during_move_does_not_restart_held_execute', absolute, done, setup, '''
step 10
set global.execute 1
step 200
expect global.busy 1
set global.reset 1
run 1
set global.reset 0
step 500
expect global.busy 0
expect_axis 1 velocity 0
set global.execute 0
step 1
set global.execute 1
step 2000
''')

    # Out-of-range targets are warned/interlocked rather than silently clamped.
    # A new in-range target must clear that warning and allow motion again.
    for sign, warning, tag in ((1, 0x114D00, 'forward'), (-1, 0x114D01, 'backward')):
        suite.case('soft_limit_' + tag + '_and_recovery', absolute,
                   dict(done, position=0.5 * sign), setup + 'soft_limits 1 -1 1\n', '''
step 10
set global.target TARGET
set global.execute 1
run 1
expect_axis 1 warning WARNING
expect_axis 1 target TARGET
step 1000
expect global.busy 0
expect_axis 1 position 0 0.001
expect_axis 1 velocity 0
set global.execute 0
step 1
set global.execute 1
step 500
expect_axis 1 warning WARNING
expect_axis 1 position 0 0.001
set global.execute 0
step 1
set global.target VALID
set global.execute 1
step 2000
expect_axis 1 warning 0
'''.replace('TARGET', str(2 * sign)).replace('WARNING', str(warning))
                   .replace('VALID', str(0.5 * sign)))
    for bound in (-1, 1):
        suite.case('soft_limit_endpoint_' + ('low' if bound < 0 else 'high'),
                   absolute, dict(done, position=bound), setup + 'soft_limits 1 -1 1\n', '''
step 10
set global.target BOUND
set global.execute 1
step 2000
expect_axis 1 warning 0
'''.replace('BOUND', str(bound)))

    # A physical-limit input is supplied through the real monitor's PLC override.
    # Stop a velocity move, release the input, reset, then move away from it.
    velocity = absolute.replace(
        'mc_move_abs(1, global.execute, global.target,\n                          global.velocity, global.acceleration, global.deceleration)',
        'mc_move_vel(1, global.execute, global.velocity, global.acceleration, global.deceleration)')
    for sign, switches, tag in ((1, '1 0', 'forward'), (-1, '0 1', 'backward')):
        suite.case('hard_limit_' + tag + '_stops_and_recovers', velocity,
                   dict(power=0, move=0, busy=1, error=0), setup + 'var global.position 0\n', '''
step 10
set global.velocity VEL
set global.execute 1
step 200
expect global.busy 1
limit_switches 1 SWITCHES
step 1000
expect_axis 1 velocity 0
expect global.busy 0
limit_switches 1 1 1
'''.replace('VEL', str(2 * sign)).replace('SWITCHES', switches) + reset + '''
set global.velocity REVERSE
set global.execute 1
step 500
expect_axis 1 velocity REVERSE 1e-6
'''.replace('REVERSE', str(-2 * sign)))


if __name__ == '__main__':
    run_cases(add_cases)

#!/usr/bin/env python3
"""Production motion PLC wrappers driving real virtual axes and trajectories."""
from suite import run_cases


def add_cases(suite):
    setup = '''period 0.001
axis 1 virtual
var global.enable 1
var global.execute 0
var global.target 1
var global.halt 0
'''
    status = '''
global.busy := mc_get_busy(1);
global.position := mc_get_act_pos(1, 1);
global.error := mc_get_axis_err(1);
global.ready := mc_get_hw_ready(1);
global.homed := mc_get_homed(1);
'''
    absolute = '''
global.power := mc_power(1, global.enable);
global.move := mc_move_abs(1, global.execute, global.target, 2, 10, 10);
''' + status
    done = dict(power=0, move=0, busy=0, position=1, error=0, ready=1, homed=1)
    # A held execute must not retrigger when the target changes; a falling and
    # rising edge must start a new move. Reuse an axis ID in the next case too.
    suite.case('absolute_move_and_edges', absolute, dict(done, position=2), setup, '''
step 10
expect_axis 1 enabled 1
set global.execute 1
step 10
expect global.move 0
expect global.busy 1
step 2000
expect global.position 1 1e-6
expect global.busy 0
set global.target 2
step 2000
expect global.position 1 1e-6
set global.execute 0
step 1
set global.execute 1
step 2000
''')
    # Ending the previous case with execute high must not consume this edge.
    suite.case('fresh_axis_and_edge_state', absolute, done, setup, '''
step 10
expect global.position 0
set global.execute 1
step 2000
''')
    relative = absolute.replace('mc_move_abs', 'mc_move_rel')
    suite.case('relative_moves', relative, done, setup, '''
step 10
set global.target 2
set global.execute 1
step 2000
expect global.position 2 1e-6
set global.execute 0
step 1
set global.target -1
set global.execute 1
step 2000
''')
    velocity = '''
global.power := mc_power(1, global.enable);
global.move := mc_move_vel(1, global.execute, 2, 10, 10);
if (global.halt) { global.halt_result := mc_halt(1, 1); };
''' + status
    suite.case('velocity_and_halt', velocity,
               dict(power=0, move=0, halt_result=0, busy=0, error=0, ready=1, homed=1),
               setup + 'var global.position 0\n', '''
step 10
set global.execute 1
step 500
expect global.busy 1
expect_axis 1 velocity 2 1e-6
set global.halt 1
step 500
expect_axis 1 velocity 0 1e-6
''')
    suite.case('configured_motion_period', velocity,
               dict(power=0, move=0, halt_result=0, busy=1, error=0, ready=1, homed=1),
               setup.replace('period 0.001', 'period 0.002') + 'var global.position 0\n', '''
step 10
set global.execute 1
step 110
expect_axis 1 velocity 2 1e-6
''')
    suite.case('power_off_stops_axis', absolute,
               dict(power=0, move=0, busy=0, error=0, ready=1, homed=1),
               setup + 'var global.position 0\n', '''
step 10
set global.execute 1
step 200
expect global.busy 1
set global.enable 0
set global.execute 0
step 500
expect_axis 1 enabled 0
expect_axis 1 busy 0
expect_axis 1 velocity 0 1e-6
''')
    # Expression-only execution retains its previous meaning: it does not
    # advance axes. Motion starts advancing only when step is requested.
    suite.case('run_does_not_advance_motion', absolute, done, setup, '''
step 10
set global.execute 1
run 1000
expect_axis 1 setpoint 0
step 2000
''')
    suite.case('explicit_encoder_feedback',
               'global.position := mc_get_act_pos(1,1);\n', dict(position=4),
               'axis 1 virtual\nfeedback 1 4', 'step 5\n')
    reset = '''
if (global.fault) { mc_set_axis_error(1, global.fault); };
if (global.reset) { global.reset_result := mc_reset(1); };
global.error := mc_get_axis_err(1);
global.last := mc_get_err();
'''
    suite.case('axis_error_and_reset', reset,
               dict(reset_result=0, error=0, last=0),
               'axis 1 virtual\nvar global.fault 12345\nvar global.reset 0', '''
run 1
expect global.error 12345
set global.fault 0
set global.reset 1
step 1
set global.reset 0
step 10
''')
    for index, error, tag in ((-1, 0x20502, 'negative'), (128, 0x20502, 'upper'),
                              (2, 0x20508, 'missing')):
        suite.case('motion_invalid_axis_' + tag, '''
global.power := mc_power(ID, 1);
global.move := mc_move_abs(ID, 1, 1, 2, 10, 10);
global.relative := mc_move_rel(ID, 1, 1, 2, 10, 10);
global.velocity := mc_move_vel(ID, 1, 2, 10, 10);
global.halt := mc_halt(ID, 1);
global.reset := mc_reset(ID);
global.busy := mc_get_busy(ID);
global.error := mc_get_axis_err(ID);
'''.replace('ID', str(index)),
                   dict(power=error, move=error, relative=error, velocity=error,
                        halt=error, reset=error, busy=error, error=error))


if __name__ == '__main__':
    run_cases(add_cases)

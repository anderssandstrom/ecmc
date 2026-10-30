from .base import SlitCase


def controller_soft_limit_stopped(sample, forward, low, high):
    """A direction-specific soft-limit response, with bounded/settled motion."""
    for key in ('position', 'setpoint'):
        if not low - .02 <= sample[key] <= high + .02:
            raise AssertionError(f'controller exceeded soft-limit range: {key}={sample[key]}')
    warning = 0x114D00 if forward else 0x114D01
    errors = (0x14C0B, 0x14D06, 0x14E04) if forward else (0x14C0C, 0x14D07, 0x14E05)
    if sample['error'] and sample['error'] not in errors:
        raise AssertionError(f'unexpected controller error 0x{int(sample["error"]):x}')
    if sample['warning'] not in (0, warning):
        raise AssertionError(f'unexpected controller warning 0x{int(sample["warning"]):x}')
    diagnosed = sample['warning'] == warning or sample['interlock'] == 1 or sample['error'] in errors
    return diagnosed and sample['busy'] == 0 and sample['stationary']


class LimitTests(SlitCase):
    def test_forward_hardware_limit(self):
        for axis in self.rig.profile['physical']:
            self.hardware_limit(axis, True)

    def test_backward_hardware_limit(self):
        for axis in self.rig.profile['physical']:
            self.hardware_limit(axis, False)

    def hardware_limit(self, axis, forward):
        sign = 1 if forward else -1
        self.rig.start_move(axis, sign * .7)
        self.rig.wait(lambda: sign * self.rig.get(axis, '-PosAct') > .06, 'motion before limit injection', 4)
        self.rig.plant.inject(axis, **{'forward_ok' if forward else 'backward_ok': False})
        suffix = '-LimFwd' if forward else '-LimBwd'
        self.rig.wait(lambda: self.rig.get(axis, suffix) == 0, 'hardware limit visible at axis', 2)
        self.rig.wait(lambda: abs(self.rig.get(axis, '-VelAct')) < .01, 'hardware limit stops motion', 4)
        self.assertLess(sign * self.rig.get(axis, '-PosAct'), .65)
        self.rig.plant.clear_faults()
        self.rig.reset()
        self.rig.move(axis, 0)

    def test_motor_soft_limits(self):
        axis = 'TR_LO'
        low, high = self.rig.get(axis, '.LLM'), self.rig.get(axis, '.HLM')
        try:
            self.rig.put(axis, '.LLM', -.4)
            self.rig.put(axis, '.HLM', .4)
            for target in (.6, -.6):
                # A rejected command can leave LVIO set after VAL is restored to
                # the last valid target. Rewriting that same target may be a
                # no-op; require a distinct in-range move before each rejection.
                self.rig.move(axis, -.1 if target > 0 else .1)
                self.assertEqual(self.rig.get(axis, '.LVIO'), 0, 'valid motor move clears prior LVIO')
                before = self.rig.get(axis, '-PosAct')
                complete = self.rig.put(axis, '.VAL', target)
                self.rig.wait(lambda: complete.is_set() and self.rig.get(axis, '.LVIO') == 1,
                              'current motor request completes with soft-limit rejection', 3)
                self.assertAlmostEqual(self.rig.get(axis, '-PosAct'), before, delta=.02)
        finally:
            self.rig.put(axis, '.LLM', low)
            self.rig.put(axis, '.HLM', high)

    def test_controller_soft_limits_without_motor_rejection(self):
        # Bypass the motor record's target validation through the normal axis PVs.
        # This checks the controller boundary independently of motor LVIO.
        axis = 'TR_LO'
        self.rig.put(axis, '-SftLimBwdEna', 1)
        self.rig.put(axis, '-SftLimFwdEna', 1)
        self.rig.wait(lambda: self.rig.get(axis, '-SftLimBwdEna-RB') == 1
                      and self.rig.get(axis, '-SftLimFwdEna-RB') == 1, 'controller soft limits enabled', 3)
        low, high = self.rig.get(axis, '.LLM'), self.rig.get(axis, '.HLM')
        self.assertLess(low, -.2)
        self.assertGreater(high, .2)

        def request(target):
            # Masked control-word writes can also execute a pending request.
            # Replace the previous (possibly rejected) target before enabling,
            # and explicitly clear execute before requesting a new movement.
            self.rig.put(axis, '-MtnCmd', 3)
            self.rig.put(axis, '-MtnCmdData', 0)
            self.rig.put(axis, '-TgtPosCmd', target)
            self.rig.put(axis, '-TgtVelCmd', .4)
            self.rig.put(axis, '-ExeCmd', 0)
            self.rig.put(axis, '-EnaCmd', 1)
            self.rig.wait(lambda: self.rig.get(axis, '-EnaAct') == 1, 'enable before axis-PV request', 5)
            self.rig.put(axis, '-ExeCmd', 1)

        def valid_move(target):
            request(target)
            def arrived():
                self.assertEqual(self.rig.get(axis, '-ErrId'), 0, 'in-range axis-PV command faulted')
                return abs(self.rig.get(axis, '-PosAct') - target) < .015 and self.rig.get(axis, '-Busy') == 0
            self.rig.wait(arrived, f'in-range axis-PV command reaches {target}', 8)

        for forward, target in ((True, high + .2), (False, low - .2)):
            # Prove this exact command path executes, then clear old diagnostics
            # with a valid return before testing the invalid target.
            valid_move(.1 if forward else -.1)
            valid_move(0)
            self.rig.wait(lambda: self.rig.get(axis, '-WrnId') not in (0x114D00, 0x114D01)
                          and not self.rig.get(axis, '-SftLimFwdIlock')
                          and not self.rig.get(axis, '-SftLimBwdIlock'), 'previous soft-limit diagnostics clear', 3)
            request(target)
            self.assertAlmostEqual(self.rig.get(axis, '-TgtPosCmd'), target, delta=1e-7)
            interlock = '-SftLimFwdIlock' if forward else '-SftLimBwdIlock'
            def stopped_at_limit():
                sample = {'position': self.rig.get(axis, '-PosAct'), 'setpoint': self.rig.get(axis, '-PosSet'),
                          'error': self.rig.get(axis, '-ErrId'), 'warning': self.rig.get(axis, '-WrnId'),
                          'interlock': self.rig.get(axis, interlock), 'busy': self.rig.get(axis, '-Busy'),
                          'stationary': self.rig._drive_stationary(axis)}
                return controller_soft_limit_stopped(sample, forward, low, high)
            try:
                self.rig.wait(stopped_at_limit, 'direction-specific controller soft-limit response and standstill', 8)
                self.assertEqual(self.rig.get(axis, '.LVIO'), 0,
                                 'controller test must not rely on motor-record rejection')
            finally:
                self.rig.trace.append({'action': 'controller soft-limit response', 'axis': axis,
                                       'target': target, 'snapshot': self.rig.snapshot()})
            self.assertEqual(self.rig.stop_all(), [], 'stop after controller soft-limit request')
            self.rig.reset()
            valid_move(0)

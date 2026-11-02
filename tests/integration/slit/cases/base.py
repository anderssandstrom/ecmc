import unittest

# DriveBase defaults to 15 seconds. Axis autoEnable.enableTimeout is a separate
# timer and does not bound a direct -EnaCmd request. Allow CA publication margin.
DRIVE_ENABLE_FAILURE_WAIT = 20


class SlitCase(unittest.TestCase):
    def setUp(self):
        self.rig.prepare(self.loop)

    def tearDown(self):
        self.rig.plant.clear_faults()
        errors = self.rig.stop_all()
        if errors:
            raise RuntimeError('test cleanup failed: ' + '; '.join(errors))

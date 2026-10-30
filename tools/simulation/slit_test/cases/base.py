import unittest


class SlitCase(unittest.TestCase):
    def setUp(self):
        self.rig.prepare(self.loop)

    def tearDown(self):
        self.rig.plant.clear_faults()
        errors = self.rig.stop_all()
        if errors:
            raise RuntimeError('test cleanup failed: ' + '; '.join(errors))

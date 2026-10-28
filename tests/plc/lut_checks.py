#!/usr/bin/env python3
"""Lookup table interpolation, bounds, reload and isolation."""
from suite import run_cases


def add_cases(suite):
    root, case = suite.root, suite.case
    (root / 'table.lut').write_text('-10 -20\n0 0\n10 30\n')
    (root / 'replacement.lut').write_text('0 100\n5 150\n10 200\n')
    case('lut_interpolation', '''
global.low := lut_get_value(0, -100); global.first := lut_get_value(0, -10);
global.left := lut_get_value(0, -5); global.middle := lut_get_value(0, 0);
global.right := lut_get_value(0, 5); global.last := lut_get_value(0, 10);
global.high := lut_get_value(0, 100);
''', dict(low=-20, first=-20, left=-10, middle=0, right=15, last=30, high=30),
         'lut 0 table.lut')
    case('lut_replaced', 'global.x := lut_get_value(0,5);', dict(x=150),
         'lut 0 table.lut\nlut 0 replacement.lut')
    for tag, index in (('negative', '-1'), ('upper', '16'), ('fraction', '0.5'),
                       ('nan', 'sqrt(-1)'), ('huge', '1e100'), ('missing', '1')):
        case('lut_' + tag, 'global.x := lut_get_value(' + index + ',5);', dict(x=0),
             'lut 0 table.lut')
    case('lut_fresh_case', 'global.x := lut_get_value(0,5);', dict(x=0))


if __name__ == '__main__':
    run_cases(add_cases)

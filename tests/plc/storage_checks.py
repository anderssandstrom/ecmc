#!/usr/bin/env python3
"""Storage buffer modes, functions, bounds and errors."""
# Expectation basis: DOC: retention modes; INFERRED: math/bounds; COMPAT/REVIEW: cursor/sentinels.
# Rationale, sources and open questions: ../EXPECTATIONS.md
from suite import run_cases


def add_cases(suite):
    names, case = suite.names, suite.case
    # DOC: ecmcMisc.h::createDataStorage describes the retention modes.
    # COMPAT/REVIEW L1: cursor/layout and empty aggregate/sentinel conventions.
    # Normal storage discards writes after full; ring replaces from the start;
    # FIFO retains the latest samples in chronological order.
    for mode, values, cursor in ((0, (1, 2, 3), 3), (1, (4, 2, 3), 1),
                                 (2, (2, 3, 4), 0)):
        expected = dict(empty=0, emptyavg=0, emptymin=0, emptymax=0,
                        append=0, full=1, cursor=cursor, size=3,
                        a=values[0], b=values[1], c=values[2],
                        average=sum(values) / 3, minimum=min(values), maximum=max(values),
                        clear=0, cleared=0, resetcursor=0, resetavg=0, zero=0)
        case('storage_mode_' + str(mode), '''
global.empty := ds_is_full(0);
global.emptyavg := ds_get_avg(0);
global.emptymin := ds_get_min(0);
global.emptymax := ds_get_max(0);
ds_append_data(0, 1); ds_append_data(0, 2); ds_append_data(0, 3);
global.append := ds_append_data(0, 4);
global.full := ds_is_full(0); global.cursor := ds_get_buff_id(0);
global.size := ds_get_size(0);
global.a := ds_get_data(0, 0); global.b := ds_get_data(0, 1);
global.c := ds_get_data(0, 2); global.average := ds_get_avg(0);
global.minimum := ds_get_min(0); global.maximum := ds_get_max(0);
global.clear := ds_clear_data(0); global.cleared := ds_is_full(0);
global.resetcursor := ds_get_buff_id(0); global.resetavg := ds_get_avg(0);
global.zero := ds_get_data(0, 0);
''', expected, 'storage 0 3 ' + str(mode))

    # COMPAT/REVIEW L1: cursor=size is accepted as an end sentinel.
    case('storage_cursor', '''
global.set := ds_set_buff_id(0, 2); global.cursor := ds_get_buff_id(0);
ds_append_data(0, 7); global.read := ds_get_data(0, 2);
global.boundary := ds_set_buff_id(0, 3);
global.invalid := ds_set_buff_id(0, 4); global.err := ds_get_err();
global.preserved := ds_get_buff_id(0);
global.write := ds_set_data(0, 1, 9); global.changed := ds_get_data(0, 1);
''', dict(set=0, cursor=2, read=7, boundary=0, invalid=0x20203,
          err=0x20203, preserved=3, write=0, changed=9), 'storage 0 3 0')

    # INFERRED: reject invalid IDs safely. COMPAT: each wrapper's return sentinel.
    # Every supported wrapper's missing-object and invalid-object conventions.
    calls = {'ds_append_data': ('ID, 1', 0x20509, 0x2050A),
             'ds_clear_data': ('ID', 0x20509, 0x2050A),
             'ds_get_data': ('ID, 0', 0, 0),
             'ds_set_data': ('ID, 0, 1', 0x20509, 0x2050A),
             'ds_get_buff_id': ('ID', -1, -1),
             'ds_set_buff_id': ('ID, 0', 0x20509, 0x2050A),
             'ds_is_full': ('ID', -1, -1), 'ds_get_size': ('ID', -1, -1),
             'ds_get_avg': ('ID', 0, 0), 'ds_get_min': ('ID', 0, 0),
             'ds_get_max': ('ID', 0, 0),
             'ds_append_to_ds': ('ID, 0, 1, 0', 0, 0)}
    for function, (args, invalid, missing) in calls.items():
        for tag, index, result, error in (
                ('negative', '-1', invalid, 0x20509),
                ('upper', '1000000', invalid, 0x20509),
                ('fraction', '0.5', invalid, 0x20509),
                ('nan', 'sqrt(-1)', invalid, 0x20509),
                ('missing', '1', missing, 0x2050A)):
            case(function + '_' + tag,
                 'global.result := ' + function + '(' + args.replace('ID', index) + ');\n'
                 'global.error := ds_get_err(); global.reset := ds_err_rst();\n'
                 'global.clean := ds_get_err();',
                 dict(result=result, error=error, reset=0, clean=0), 'storage 0 3 0')

    for index in ('-1', '3', '0.5', 'sqrt(-1)', '1e100'):
        case('storage_element_' + str(len(names)),
             'global.read := ds_get_data(0,' + index + '); global.readerr := ds_get_err();\n'
             'global.write := ds_set_data(0,' + index + ', 9); global.writeerr := ds_get_err();',
             dict(read=0, readerr=0x20203, write=0x20203, writeerr=0x20203), 'storage 0 3 0')
        case('storage_invalid_cursor_' + str(len(names)),
             'global.result := ds_set_buff_id(0,' + index + ');\n'
             'global.err := ds_get_err(); global.cursor := ds_get_buff_id(0);',
             dict(result=0x20203, err=0x20203, cursor=0), 'storage 0 2 0')

    # COMPAT/REVIEW L1: oversized copy count clips to the available source data.
    for mode in (0, 1, 2):
        case('storage_copy_' + str(mode), '''
ds_set_data(0, 0, 2); ds_set_data(0, 1, 4); ds_set_data(0, 2, 6);
global.copy := ds_append_to_ds(0, 1, 100, 1);
global.a := ds_get_data(1, 0); global.b := ds_get_data(1, 1);
global.average := ds_get_avg(1); global.emptycopy := ds_append_to_ds(0, 0, 0, 1);
''', dict(copy=0, a=4, b=6, average=5, emptycopy=0),
             'storage 0 3 0\nstorage 1 2 ' + str(mode))
    for tag, offset, elements, error in (
            ('negative_offset', '-1', '1', 0x20203),
            ('end_offset', '3', '1', 0x20203),
            ('fraction_offset', '0.5', '1', 0x20203),
            ('negative_count', '0', '-1', 0x20028),
            ('fraction_count', '0', '0.5', 0x20028),
            ('nan_count', '0', 'sqrt(-1)', 0x20028),
            ('destination_small', '0', '3', 0x20202)):
        case('storage_copy_' + tag,
             'global.result := ds_append_to_ds(0,' + offset + ',' + elements + ',1);\n'
             'global.error := ds_get_err(); global.unchanged := ds_get_data(1,0);',
             dict(result=error, error=error, unchanged=0), 'storage 0 3 0\nstorage 1 2 0')

    case('storage_error_recovery', '''
global.x := ds_append_data(-1, 1);
global.err := ds_get_err(); global.reset := ds_err_rst(); global.clean := ds_get_err();
global.ok := ds_set_data(0, 0, 7); global.read := ds_get_data(0, 0);
global.size := ds_get_size(0); global.copy := ds_append_to_ds(0, 0, 1, 1);
global.copied := ds_get_data(1, 0); global.clear := ds_clear_data(1);
global.average := ds_get_avg(1);
''', dict(x=0x20509, err=0x20509, reset=0, clean=0, ok=0, read=7,
          size=4, copy=0, copied=7, clear=0, average=0),
         'storage 0 4 0\nstorage 1 4 0')


if __name__ == '__main__':
    run_cases(add_cases)

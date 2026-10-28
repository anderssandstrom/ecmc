#!/usr/bin/env python3
"""Exercise production PLC library wrappers through real ExprTk expressions."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='ecmc-plc-libraries-') as directory:
    root = Path(directory)
    suite = []
    names = []

    def case(name, code, expected, setup=''):
        names.append(name)
        (root / (name + '.plc')).write_text(code)
        suite.append('case ' + name + '\n' + setup + '\n' + ''.join(
            'var global.' + key + ' 0\n' for key in expected) +
            'plc ' + name + '.plc\nrun 1\n' + ''.join(
            'expect global.{} {} 1e-12\n'.format(key, value)
            for key, value in expected.items()) + 'end\n')

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

    case('storage_cursor', '''
global.set := ds_set_buff_id(0, 2); global.cursor := ds_get_buff_id(0);
ds_append_data(0, 7); global.read := ds_get_data(0, 2);
global.boundary := ds_set_buff_id(0, 3);
global.invalid := ds_set_buff_id(0, 4); global.err := ds_get_err();
global.preserved := ds_get_buff_id(0);
global.write := ds_set_data(0, 1, 9); global.changed := ds_get_data(0, 1);
''', dict(set=0, cursor=2, read=7, boundary=0, invalid=0x20203,
          err=0x20203, preserved=3, write=0, changed=9), 'storage 0 3 0')

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

    case('ec_bits', '''
global.set := ec_set_bit(0, 40); global.clear := ec_clr_bit(global.set, 40);
global.flip := ec_flp_bit(0, 32); global.check := ec_chk_bit(global.flip, 32);
global.top := ec_set_bit(0, 63); global.topcheck := ec_chk_bit(global.top, 63);
global.write := ec_wrt_bit(0, 1, 40); global.erase := ec_wrt_bit(global.write, 0, 40);
global.field := ec_wrt_bits(255, 2, 4, 7); global.read := ec_chk_bits(global.field, 4, 7);
global.wide := ec_wrt_bits(0, 7, 0, 63); global.all := ec_chk_bits(7, 0, 63);
global.binary := ec_print_bin(5, 0, 3); global.hex := ec_print_hex(255, 4, 7);
''', dict(set=2**40, clear=0, flip=2**32, check=1, top=2**63, topcheck=1,
          write=2**40, erase=0, field=47, read=2, wide=7, all=7, binary=0, hex=0))
    for call in ('ec_set_bit(7,-1)', 'ec_clr_bit(7,64)', 'ec_chk_bit(7,0.5)',
                 'ec_flp_bit(7,sqrt(-1))', 'ec_wrt_bits(7,1,4,2)',
                 'ec_chk_bits(7,4,2)', 'ec_print_bin(7,4,2)', 'ec_print_hex(7,4,2)'):
        case('ec_invalid_bits_' + str(len(names)),
             'global.x := ' + call + '; global.err := ec_get_err();\n'
             'global.reset := ec_err_rst(); global.clean := ec_get_err();',
             dict(x=7, err=0x20800, reset=0, clean=0))

    case('ec_memory_maps', '''
global.write := ec_set_mm_data(0,0,2); ec_set_mm_data(0,1,4); ec_set_mm_data(0,2,6);
global.copy := ec_mm_cp(0,1); global.a := ec_get_mm_data(1,0); global.b := ec_get_mm_data(1,1);
global.size := ec_get_mm_size(1);
global.type := ec_get_mm_type(1);
global.append := ec_mm_append_to_ds(1,0); global.avg := ds_get_avg(0);
global.scaled := ec_mm_append_to_ds_scale_offset(1,1,2,1); global.scaleavg := ds_get_avg(1);
global.bad := ec_get_mm_data(0,3); global.err := ec_get_err();
global.invalidwrite := ec_set_mm_data(0,-1,9); global.unchanged := ec_get_mm_data(0,0);
''', dict(write=0, copy=0, a=2, b=4, size=2, type=14, append=0, avg=3, scaled=0,
          scaleavg=7, bad=0, err=0x211002, invalidwrite=0x211002, unchanged=2),
         'ec\nec_memmap 0 3\nec_memmap 1 2\nstorage 0 2 0\nstorage 1 2 0')
    case('ec_timestamps', '''
global.now := ec_get_time(); global.low := ec_get_time_l32(); global.high := ec_get_time_u32();
global.receive := ec_get_last_receive_time(); global.send := ec_get_last_send_time();
global.offset := ec_get_time_offset_mono(); global.event := ec_get_slave_input_event_time(3);
global.domain := ec_get_dom_state(0); global.missingdomain := ec_get_dom_state(1);
global.nsec := ec_get_time_local_nsec(61000000123);
global.sec := ec_get_time_local_sec(61000000123);
global.min := ec_get_time_local_min(61000000123);
global.hour := ec_get_time_local_hour(61000000123);
''', dict(now=4294967301, low=5, high=1, receive=4294967200, send=4294967100,
          offset=-123, event=4294966210, domain=1, missingdomain=-0x20056,
          nsec=123, sec=1, min=1, hour=0),
         'ec\nec_time 4294967301 4294967200 4294967100 1000 -123\nec_slave 3 -1 10\nec_domain 0 1')
    case('ec_unresolved_timing', '''
global.missing := ec_get_slave_input_event_time(4); global.missingerr := ec_get_err();
global.unresolved := ec_get_slave_input_event_time(3); global.err := ec_get_err();
''', dict(missing=-0x20011, missingerr=0x20011, unresolved=-0x20801, err=0x20801),
         'ec\nec_slave 3 0 0')
    for function, args, result in (
            ('ec_mm_cp', '0,1', 0x2002F), ('ec_get_mm_type', '0', -0x2002F),
            ('ec_get_mm_data', '0,0', 0), ('ec_set_mm_data', '0,0,1', 0),
            ('ec_get_mm_size', '0', 0), ('ec_mm_append_to_ds', '0,0', 0x2002F),
            ('ec_mm_append_to_ds_scale_offset', '0,0,1,0', 0x2002F)):
        case(function + '_no_map', 'global.x := ' + function + '(' + args + ');\n'
             'global.err := ec_get_err();', dict(x=result, err=0x2002F), 'ec')
    # Master-dependent functions retain their real missing-master return values.
    for function, args, result in (
            ('ec_get_time', '', -0x20032), ('ec_get_last_receive_time', '', -0x20032),
            ('ec_get_last_send_time', '', -0x20032), ('ec_get_time_l32', '', -0x20032),
            ('ec_get_time_u32', '', -0x20032), ('ec_get_time_offset_mono', '', -0x20032),
            ('ec_get_slave_input_event_time', '3', -0x20032),
            ('ec_mm_cp', '0,1', 0x20032), ('ec_get_mm_type', '0', -0x20032),
            ('ec_get_mm_data', '0,0', 0), ('ec_set_mm_data', '0,0,1', 0),
            ('ec_get_mm_size', '0', 0), ('ec_mm_append_to_ds', '0,0', 0),
            ('ec_mm_append_to_ds_scale_offset', '0,0,1,0', 0),
            ('ec_get_dom_state', '0', -0x20032)):
        case(function + '_no_master', 'global.x := ' + function + '(' + args + ');\n'
             'global.err := ec_get_err();', dict(x=result, err=0x20032))

    for tag, setup, slave, result in (('master', '', '3', 0x20032),
                                      ('slave', 'ec', '3', 0x20011),
                                      ('object', 'ec\nec_slave 3 0 0', '3', 0x23505),
                                      ('invalid', '', '-1', 0x23506)):
        code = '''
global.read := ec_sdo_read(S,0); global.write := ec_sdo_write(S,0);
global.busy := ec_sdo_get_busy(S,0); global.error := ec_sdo_get_error(S,0);
global.size := ec_sdo_get_size(S,0); global.capacity := ec_sdo_get_capacity(S,0);
global.value := ec_sdo_get_value(S,0,0); global.err := ec_get_err();
global.set := ec_sdo_set_value(S,0,0,1);
'''.replace('S,', slave + ',')
        case('ec_sdo_missing_' + tag, code, dict(read=result, write=result, busy=-result,
             error=-result, size=-result, capacity=-result, value=0, err=result, set=result), setup)

    path = root / 'libraries.suite'
    path.write_text('\n'.join(suite))
    env = dict(os.environ, TZ='UTC0')
    result = subprocess.run([binary, '--suite', str(path)], env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            universal_newlines=True, timeout=60)
    print(result.stdout, end='')
    assert result.returncode == 0, 'PLC library cases failed'
    assert '{0}/{0} passed'.format(len(names)) in result.stdout
    assert '0101f' in result.stdout, 'bit print output missing'

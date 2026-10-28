#!/usr/bin/env python3
"""EtherCAT PLC wrappers against explicit offline fixtures."""
# Expectation basis: INFERRED: bit arithmetic; DOC: SDO semantics; COMPAT: offline wrapper values.
# Rationale, sources and open questions: ../EXPECTATIONS.md
from suite import run_cases


def add_cases(suite):
    names, case = suite.names, suite.case
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
    suite.output_contains.append('0101f')


if __name__ == '__main__':
    run_cases(add_cases)

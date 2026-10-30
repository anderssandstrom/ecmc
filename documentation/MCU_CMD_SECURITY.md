# MCU-Cmd host-command policy

MCU-Cmd keeps its existing PV names, asyn protocol, command syntax and ordinary
motion, axis/controller settings and diagnostic queries. The ecmc_cfg_tool
client can continue using MCU-Cmd.AOUT and MCU-Cmd.AINP.

Host code and file-loading commands now default to disabled over the asyn
transport. Trusted local `ecmcConfig` and `ecmcConfigOrDie` startup commands
continue to work without any opt-in, subject to their existing runtime guards.

## Explicit remote commissioning opt-in

If remote code/file configuration is required, add this local IOC-shell command
after ECMC registration and before both `iocInit`/`iocBuild` and ECMC runtime:

```iocsh
ecmcAllowNetworkHostCommands(1)
```

The default is 0. Only 0 and 1 are accepted. The setting cannot be enabled using
MCU-Cmd or changed after IOC initialization has begun, including while paused.
There is no writable PV or Cfg command for it. Restart the IOC to change the
startup policy. In an EPICS-first commissioning workflow, set the option before
`iocInit`, then perform remote configuration before switching ECMC to runtime.

Even with this opt-in, privileged host commands are rejected in ECMC runtime.
PLC expression replacement/append and compilation are never permitted in
runtime through this parser. The previous nonempty-expression guard bypass is
fixed for local commands as well as network commands.

## Protected commands

- `Cfg.IocshCmd`
- `Cfg.LoadPlugin`, `Cfg.LoadSafetyPlugin`, `Cfg.LoadCppLogic`
- `Cfg.AppendCppLogicMacros`
- `Cfg.LoadPLCFile`, `Cfg.LoadAxisPLCFile`, `Cfg.LoadPLCLibFile`
- `Cfg.SetPLCExpr`, `Cfg.AppendPLCExpr`, `Cfg.AppendAxisPLCExpr`
- `Cfg.ClearPLCExpr`, `Cfg.CompilePLC`, `Cfg.CompileAxisPLC`
- `Cfg.LoadLUTFile`, `Cfg.LoadAxisEncLookupTable`
- `Cfg.SimLoadRecords`

Denied network operations return `ERROR_MAIN_PARSER_CMD_LOCAL_ONLY` (0x2007B).
Opted-in operations attempted in runtime return
`ERROR_MAIN_APP_MODE_ALREADY_RUNTIME` (0x2001A). Existing global runtime Cfg
blocking can still reject commands separately. The check happens after
ADSPORT/Cfg normalization and applies to every semicolon-separated command.
Batches remain non-transactional: allowed operations in the same batch can
execute even when another operation returns an error.

The ecmc_cfg_tool generic console can still display protected catalog entries;
the IOC enforces the policy regardless of the client. Its ordinary settings
and motion workflows do not require this opt-in. No command signatures change.
`Cfg.SetBlockCfgCmdsInRuntime` behavior is unchanged; this patch does not turn
on that broader restriction, which would also block ordinary Cfg setters.

## Trust boundary

Enabling remote host commands grants powerful commissioning access, including
IOC-shell execution. Only trusted clients should have MCU-Cmd write access on
such an IOC. A read-only Warewulf image limits persistent disk changes but does
not prevent process execution, writes to writable mounts, hardware operations
or a malicious plugin loaded before runtime from continuing to run afterward.
The runtime check is a command policy, not a sandbox for already-loaded code.
Use site EPICS access security and network restrictions independently.

The local C API `motorHandleOneArg` is for trusted IOC configuration only.
Network callers must use `ecmcCmdParser`, which carries the untrusted origin
explicitly through dispatch. No process-global current-caller flag is used.
New parser capabilities that load or execute host code/files must be added to
`cfgCommandRequiresLocalAccess` and the policy tests.

## Tests

Run `python3 tests/test_network_command_policy.py` and
`python3 tests/test_buffer_boundaries.py`. They compile production transport,
normalization, origin routing, policy and expression-handler code with
ASan/UBSan; hardware and IOC-shell side effects are stand-ins. Related loading
regressions are `test_plc_loading.py`, `test_plugin_loading.py`,
`test_cpp_logic_loading.py` and `test_lut_reload_soe.py`.

A target EPICS/Linux module build and deployment smoke test are still required;
these hardware-free tests do not run a production IOC or EtherCAT device.

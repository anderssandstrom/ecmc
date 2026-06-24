# Motion Sequence

This is an initial runtime-editable motion sequencer for ecmc. The sequencer is
intended for deterministic command sequencing inside the ecmc realtime loop,
while still allowing the sequence definition to be inspected and edited from
EPICS/asyn or defined from the startup script.

The current implementation is experimental and should be treated as a first
basic approach. It is designed to avoid changing existing motion behavior unless
a sequence is explicitly created, armed, and started.

## Architecture

- A maximum of `ECMC_MAX_MOTION_SEQUENCES` sequences can be created.
- Each sequence has a dedicated asyn port, default name `ECMC_SEQ<index>`.
- Sequences must be created before steps or PV parameters are used.
- Steps can be edited at runtime via the sequence asyn port.
- Steps can also be defined in the startup script through `Cfg.Seq*` commands.
- Compile runs in a low-priority EPICS thread owned by the sequence.
- Runtime execution is handled from the main ecmc realtime loop.
- Sequence execution runs before normal axis motion, plugins, C++ logic, and
  the safety plugin in the realtime loop.

The basic flow is:

```text
Cfg.CreateMotionSeq(...)
Cfg.Seq...(define steps)
Cfg.CompileMotionSeq(...)
Cfg.ArmMotionSeq(...)
Cfg.StartMotionSeq(...)
```

Compile validates the configured steps and resolves item bindings. Arm copies
the compiled plan to the active realtime plan. Start requests realtime execution
of the active plan.

## Actions

The currently defined action IDs are:

```text
0  NOP
1  MC_Reset
2  MC_Power
3  MC_Home
4  MC_MoveAbsolute
5  MC_MoveRelative
6  WaitInPosition
7  SetItem
8  WaitItem
9  WaitTime
10 RunSequence
11 ArmPositionTrigger
12 WaitTriggerDone
13 ArmTimeTrigger
14 MC_MoveVelocity
15 MC_Halt
16 ExitItem
17 SetEncHomed
18 BranchItem
19 GotoStep
```

Most users should prefer the `Cfg.Seq*` helper commands or the `CmdLine` text
interface instead of writing the numeric action IDs directly.

## Startup Commands

Create a sequence:

```text
Cfg.CreateMotionSeq(seqIndex)
Cfg.CreateMotionSeq(seqIndex,maxSteps)
Cfg.CreateMotionSeq(seqIndex,maxSteps,portName)
```

Generic step definition:

```text
Cfg.SetMotionSeqStep(seqIndex,stepIndex,enabled,action,axis,position,velocity,acceleration,deceleration,timeoutMs)
Cfg.SetMotionSeqStepText(seqIndex,stepIndex,name,transition,onError)
Cfg.SetMotionSeqStepText(seqIndex,stepIndex,name,transition,onError,args)
```

Simplified step helpers:

```text
Cfg.SeqNop(seqIndex,stepIndex)
Cfg.SeqWaitTime(seqIndex,stepIndex,waitMs)
Cfg.SeqReset(seqIndex,stepIndex,axis,timeoutMs)
Cfg.SeqReset(seqIndex,stepIndex,axis,timeoutMs,wait)
Cfg.SeqPower(seqIndex,stepIndex,axis,enable,timeoutMs)
Cfg.SeqPower(seqIndex,stepIndex,axis,enable,timeoutMs,wait)
Cfg.SeqHome(seqIndex,stepIndex,axis,homeSeq,homePos,velTowardsCam,velOffCam,acc,dec,timeoutMs)
Cfg.SeqHome(seqIndex,stepIndex,axis,homeSeq,homePos,velTowardsCam,velOffCam,acc,dec,timeoutMs,wait)
Cfg.SeqMoveAbs(seqIndex,stepIndex,axis,pos,vel,acc,dec,timeoutMs)
Cfg.SeqMoveAbs(seqIndex,stepIndex,axis,pos,vel,acc,dec,timeoutMs,wait)
Cfg.SeqMoveRel(seqIndex,stepIndex,axis,dist,vel,acc,dec,timeoutMs)
Cfg.SeqMoveRel(seqIndex,stepIndex,axis,dist,vel,acc,dec,timeoutMs,wait)
Cfg.SeqMoveVel(seqIndex,stepIndex,axis,vel,acc,dec,timeoutMs)
Cfg.SeqMoveVel(seqIndex,stepIndex,axis,vel,acc,dec,timeoutMs,wait,tolerance)
Cfg.SeqHalt(seqIndex,stepIndex,axis,timeoutMs)
Cfg.SeqHalt(seqIndex,stepIndex,axis,timeoutMs,wait)
Cfg.SeqWaitInPos(seqIndex,stepIndex,axis,timeoutMs)
Cfg.SeqSetEncHomed(seqIndex,stepIndex,axis,homed)
Cfg.SeqSetItem(seqIndex,stepIndex,item,value,timeoutMs)
Cfg.SeqWaitItem(seqIndex,stepIndex,item,op,value,timeoutMs)
Cfg.SeqExitItem(seqIndex,stepIndex,item,op,value,timeoutMs)
Cfg.SeqBranchItem(seqIndex,stepIndex,item,op,value,trueStep)
Cfg.SeqBranchItem(seqIndex,stepIndex,item,op,value,trueStep,falseStep)
Cfg.SeqGotoStep(seqIndex,stepIndex,targetStep)
Cfg.InsertMotionSeqStep(seqIndex,stepIndex)
Cfg.DeleteMotionSeqStep(seqIndex,stepIndex)
Cfg.SeqRunSeq(seqIndex,stepIndex,childSeqIndex,timeoutMs)
Cfg.SeqArmPosTrigger(seqIndex,stepIndex,triggerId,axis,item,startPos,interval,endPos,value,pulseMs)
Cfg.SeqArmTimeTrigger(seqIndex,stepIndex,triggerId,item,delayMs,periodMs,count,value,pulseMs)
Cfg.SeqWaitTriggerDone(seqIndex,stepIndex,triggerId,timeoutMs)
```

`SeqSetEncHomed` sets the homed flag on the axis primary/current encoder and
then advances immediately.

Sequence control:

```text
Cfg.CompileMotionSeq(seqIndex)
Cfg.ArmMotionSeq(seqIndex)
Cfg.StartMotionSeq(seqIndex)
Cfg.StopMotionSeq(seqIndex)
Cfg.ResetMotionSeq(seqIndex)
Cfg.ReportMotionSeq(seqIndex)
Cfg.ReportMotionSeq(seqIndex,stepIndex)
```

These configuration commands are blocked in runtime through
`ecmc_commands_blocklist_rt.json`.

## Item Condition Operators

`SeqWaitItem` and `SeqExitItem` support:

```text
==
!=
>
>=
<
<=
eq
ne
```

Example:

```text
Cfg.SeqWaitItem(0,0,ec0.s1.status,>=,1,5000)
```

This waits until the scalar data item `ec0.s1.status` is greater than or equal
to `1`, or until the step timeout expires.

`SeqExitItem` evaluates the same condition but exits the current sequence when
the condition is fulfilled. If the condition is false, execution continues with
the next step:

```text
Cfg.SeqExitItem(0,1,ec0.s1.abort,==,1,0)
```

`SeqBranchItem` evaluates the condition once and jumps to a configured step ID
when the condition is true. If `falseStep` is provided, a false condition jumps
there; otherwise false continues to the next enabled step:

```text
Cfg.SeqBranchItem(0,2,ec0.s1.ready,==,1,10)
Cfg.SeqBranchItem(0,3,ec0.s1.mode,==,2,20,30)
```

`SeqGotoStep` unconditionally jumps to a configured step ID:

```text
Cfg.SeqGotoStep(0,9,2)
```

`InsertMotionSeqStep` and `DeleteMotionSeqStep` edit the configured step table.
Insert shifts steps at and after the index up by one. Delete shifts later steps
down by one. Branch and goto targets are adjusted when shifted; delete fails if
another step targets the deleted step.

`Read-Next` and `Read-Prev` skip disabled/unconfigured step slots. Direct
`Read-Index` plus `Read-Cmd` can still inspect any slot, including empty gaps
left for sparse numbering.

## Velocity Move and Halt

`SeqMoveVel` is deliberately nonblocking. It issues the velocity command and
advances to the next sequence step immediately. It does not wait for the axis to
reach the requested velocity.

The extended form can optionally wait until actual velocity is within the
configured tolerance:

```text
Cfg.SeqMoveVel(0,0,1,10.0,20.0,20.0,5000,1,0.1)
```

This waits until the actual velocity is between `9.9` and `10.1`, or until the
5000 ms timeout expires. Internally this is stored in the generic step args:

```text
wait=1;tol=0.1
```

Using `wait=0` keeps the immediate nonblocking behavior. The short command form
also defaults to `wait=0`.

`SeqHalt` issues `MC_Halt` and waits until the axis is no longer busy.

This allows a sequence to move continuously until an item condition becomes
true:

```text
Cfg.SeqMoveVel(0,0,1,10.0,20.0,20.0,1000)
Cfg.SeqWaitItem(0,1,ec0.s1.stopInput,==,1,30000)
Cfg.SeqHalt(0,2,1,5000)
```

The `SeqMoveVel` timeout only covers issuing the command. Since the action
normally completes in one realtime cycle, the wait condition timeout belongs
on `SeqWaitItem`, and the stopping timeout belongs on `SeqHalt`.

## Blocking and Nonblocking Motion

Motion helpers accept an optional final `wait` argument. Existing short forms
retain their original behavior:

- reset, power, home, absolute move, relative move, and halt default to `wait=1`
- velocity move defaults to `wait=0`

With `wait=0`, the command is issued and the sequence advances in the same
realtime cycle. The axis continues executing independently.

Example starting two axes in consecutive cycles and then waiting for both:

```text
Cfg.SeqMoveAbs(0,0,1,100.0,10.0,20.0,20.0,1000,0)
Cfg.SeqMoveAbs(0,1,2,50.0,5.0,10.0,10.0,1000,0)
Cfg.SeqWaitInPos(0,2,1,15000)
Cfg.SeqWaitInPos(0,3,2,15000)
```

Internally, the option is stored in `args`:

```text
wait=0
```

This also allows runtime PV editing through `edit.args`. A nonblocking command's
timeout only covers command issue; completion should be checked with a later
wait step.

## Asyn Port Interface

Each sequence has a dedicated asyn port. Parameters are unprefixed on that port.

Editable step row:

```text
edit.index
edit.enabled
edit.action
edit.axis
edit.position
edit.velocity
edit.acceleration
edit.deceleration
edit.timeout_ms
edit.name
edit.transition
edit.onerror
edit.args
```

Readback step row:

```text
read.index
read.enabled
read.action
read.axis
read.position
read.velocity
read.acceleration
read.deceleration
read.timeout_ms
read.name
read.transition
read.onerror
read.args
```

Commands:

```text
cmd.apply
cmd.read
cmd.read_next
cmd.read_prev
cmd.compile
cmd.arm
cmd.start
cmd.stop
cmd.reset
```

Status:

```text
stat.state
stat.valid
stat.armed
stat.running
stat.compile_busy
stat.step_index
stat.action
stat.error_id
stat.step_count
stat.elapsed_ms
stat.step_name
stat.error_text
stat.validation_text
stat.soft_trigger_id
stat.soft_trigger_count
```

The readback row is selected with `read.index`. Writing `cmd.read`,
`cmd.read_next`, or `cmd.read_prev` refreshes the `read.*` parameters.

## Sequence Calls

A sequence can call another sequence with `SeqRunSeq`.

```text
Cfg.SeqRunSeq(parentSeq,step,childSeq,timeoutMs)
```

Example:

```text
Cfg.SeqRunSeq(1,0,0,10000)
Cfg.SeqWaitTime(1,1,500)
Cfg.SeqRunSeq(1,2,0,10000)
```

This means sequence `1` runs sequence `0`, waits 500 ms, then runs sequence `0`
again.

The parent waits for the child sequence to reach `DONE`. If the child errors or
stops, the parent errors. Direct self-call is rejected at compile time. More
advanced recursive graph validation is not implemented yet.

## Position Triggers

Position triggers are armed by a step and then evaluated every realtime cycle
while later steps execute.

```text
Cfg.SeqArmPosTrigger(seq,step,triggerId,axis,item,startPos,interval,endPos,value,pulseMs)
```

Example:

```text
Cfg.SeqArmPosTrigger(0,0,0,1,ec0.s1.output01,10.0,1.0,29.0,1,5)
Cfg.SeqMoveAbs(0,1,1,50.0,10.0,20.0,20.0,10000)
Cfg.SeqWaitTriggerDone(0,2,0,1000)
```

This arms trigger `0` on axis `1`. It fires at:

```text
10.0
11.0
12.0
...
29.0
```

It writes `1` to `ec0.s1.output01` for 5 ms at each trigger point.

The range is `startPos:interval:endPos`. The endpoint is included when it
lands exactly on an interval. Negative intervals are used for reverse motion,
for example `80,-1,20` fires at `80,79,...,20`.

## Time Triggers

Time triggers use the same background trigger engine as position triggers, but
fire based on elapsed time after arming.

```text
Cfg.SeqArmTimeTrigger(seq,step,triggerId,item,delayMs,periodMs,count,value,pulseMs)
```

Example:

```text
Cfg.SeqArmTimeTrigger(0,0,0,ec0.s1.output01,10,5,100,1,1)
Cfg.SeqMoveAbs(0,1,1,50.0,10.0,20.0,20.0,10000)
Cfg.SeqWaitTriggerDone(0,2,0,1000)
```

This fires first after 10 ms, then every 5 ms, for 100 pulses. Each pulse writes
`1` for 1 ms.

Time trigger periods must be positive.

## Soft Triggers

For software-only triggers, use `soft` as the trigger item.

```text
Cfg.SeqArmPosTrigger(0,0,0,1,soft,10.0,1.0,29.0,1,5)
Cfg.SeqArmTimeTrigger(0,1,1,soft,10,5,100,1,1)
```

Soft triggers do not write a data item. Instead, each trigger fire increments:

```text
stat.soft_trigger_count
```

and updates:

```text
stat.soft_trigger_id
```

The EPICS layer can monitor `stat.soft_trigger_count` as the edge source. If
multiple soft triggers fire in the same realtime cycle, the counter increments
for each trigger, but `stat.soft_trigger_id` only contains the last trigger ID
processed in that cycle.

Trigger IDs `0..7` also expose dedicated counters and pulse-active status:

```text
stat.soft_trigger.<id>.count
stat.soft_trigger.<id>.pulse
```

The ecmccfg database maps these to `SoftTrig0Count`/`SoftTrig0Pulse` through
`SoftTrig7Count`/`SoftTrig7Pulse`. Use the counters as the reliable EPICS event
source. The pulse flags are only high while a soft trigger pulse is active and
therefore require a nonzero `pulseMs`.

## Example: Reusable X Scan Line

Sequence `0` defines one X line with detector triggers:

```text
Cfg.CreateMotionSeq(0)
Cfg.SeqArmPosTrigger(0,0,0,1,soft,10.0,1.0,109.0,1,0)
Cfg.SeqMoveAbs(0,1,1,110.0,20.0,50.0,50.0,20000)
Cfg.SeqWaitTriggerDone(0,2,0,2000)
Cfg.CompileMotionSeq(0)
Cfg.ArmMotionSeq(0)
```

Sequence `1` calls the X scan line, steps Y, then calls it again:

```text
Cfg.CreateMotionSeq(1)
Cfg.SeqRunSeq(1,0,0,25000)
Cfg.SeqMoveRel(1,1,2,0.1,2.0,10.0,10.0,5000)
Cfg.SeqRunSeq(1,2,0,25000)
Cfg.CompileMotionSeq(1)
Cfg.ArmMotionSeq(1)
Cfg.StartMotionSeq(1)
```

For a raster scan, either include an X return move in the child sequence or
define separate forward and backward X-line sequences and call them alternately.

## PLC Functions

The ecmc PLC library exposes sequence control and status helpers. Command-style
functions are rising-edge triggered on the `execute` argument.

```text
seq_arm(seq,execute)
seq_set_step(seq,execute,step_id)
```

`seq_arm` arms an already compiled sequence. `seq_set_step` requests a jump to a
configured step ID. The sequence must be armed or running; missing target steps
put the sequence in error.

Status getters:

```text
seq_get_state(seq)
seq_get_valid(seq)
seq_get_armed(seq)
seq_get_running(seq)
seq_get_compile_busy(seq)
seq_get_step(seq)
seq_get_action(seq)
seq_get_error(seq)
seq_get_step_count(seq)
seq_get_elapsed_ms(seq)
```

`seq_get_step` returns the configured step ID of the active step. It returns
`-1` when no step is active.

Example:

```text
arm_err := seq_arm(0, arm_exec);
jump_err := seq_set_step(0, jump_exec, 20);
seq_running := seq_get_running(0);
seq_step := seq_get_step(0);
```

## Current Limitations

- This has not yet been built or tested in the target ecmc environment.
- Generic EPICS records are provided by ecmccfg. Action-specific operator
  screens and convenience records can still be added in the EPICS layer.
- Compile currently uses one low-priority worker thread per sequence.
- Recursive sequence graph validation only rejects direct self-call.
- Soft triggers expose a shared counter and last trigger ID, not a full event
  FIFO.
- Trigger timing is evaluated once per realtime cycle.
- Real output trigger pulse clearing writes `0`; this assumes the trigger item
  is used as a pulse output.

## Files

Implementation:

```text
devEcmcSup/sequence/ecmcMotionSequence.h
devEcmcSup/sequence/ecmcMotionSequence.cpp
```

Command parser integration:

```text
devEcmcSup/com/ecmcCmdParser.c
devEcmcSup/ecmc_commands_blocklist_rt.json
```

EPICS records, startup helper, and user documentation are provided by ecmccfg:

```text
db/sequencer/ecmcMotionSequence.template
scripts/addMotionSequence.cmd
hugo/content/manual/motion_cfg/sequencer.md
```

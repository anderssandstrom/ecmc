/*************************************************************************\
* Copyright (c) 2026 Paul Scherrer Institut
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcMotionDiag.cpp
*
\*************************************************************************/

#include "ecmcMotionDiag.h"
#include "ecmcGeneral.h"
#include "ecmcGlobalsExtern.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <unistd.h>

namespace {

constexpr int kDiagFileLen = 512;

struct DiagRequest {
  int level;
  char file[kDiagFileLen];
};

const char *boolText(bool value) {
  return value ? "true" : "false";
}

const char *commandText(int command) {
  switch (command) {
  case ECMC_CMD_NOCMD:
    return "NO_CMD";
  case ECMC_CMD_JOG:
    return "JOG";
  case ECMC_CMD_MOVEVEL:
    return "MOVE_VEL";
  case ECMC_CMD_MOVEREL:
    return "MOVE_REL";
  case ECMC_CMD_MOVEABS:
    return "MOVE_ABS";
  case ECMC_CMD_MOVEMODULO:
    return "MOVE_MODULO";
  case ECMC_CMD_MOVEPVTABS:
    return "MOVE_PVT_ABS";
  case ECMC_CMD_HOMING:
    return "HOMING";
  case ECMC_CMD_SUPERIMP:
    return "SUPERIMP";
  case ECMC_CMD_GEAR:
    return "GEAR";
  default:
    return "UNKNOWN";
  }
}

const char *dataSourceText(int source) {
  switch (source) {
  case ECMC_DATA_SOURCE_INTERNAL:
    return "INTERNAL";
  case ECMC_DATA_SOURCE_EXTERNAL:
    return "EXTERNAL";
  default:
    return "UNKNOWN";
  }
}

const char *axisTypeText(axisTypes type) {
  switch (type) {
  case ECMC_AXIS_TYPE_REAL:
    return "REAL";
  case ECMC_AXIS_TYPE_VIRTUAL:
    return "VIRTUAL";
  default:
    return "UNKNOWN";
  }
}

const char *masterSlaveStateText(int state) {
  switch (state) {
  case ECMC_MST_SLV_STATE_IDLE:
    return "IDLE";
  case ECMC_MST_SLV_STATE_SLAVES:
    return "SLAVES";
  case ECMC_MST_SLV_STATE_MASTERS:
    return "MASTERS";
  case ECMC_MST_SLV_STATE_RESET:
    return "RESET";
  default:
    return "UNKNOWN";
  }
}

const char *masterSlaveStatusText(int status) {
  switch (status) {
  case ECMC_MST_SLV_STATUS_IDLE:
    return "IDLE";
  case ECMC_MST_SLV_STATUS_SLAVE_ACTIVE:
    return "SLAVE_ACTIVE";
  case ECMC_MST_SLV_STATUS_PREPARING_MASTER:
    return "PREPARING_MASTER";
  case ECMC_MST_SLV_STATUS_WAIT_SLAVE_EXTERNAL:
    return "WAIT_SLAVE_EXTERNAL";
  case ECMC_MST_SLV_STATUS_MASTER_MOVING:
    return "MASTER_MOVING";
  case ECMC_MST_SLV_STATUS_WAIT_MASTER_AT_TARGET:
    return "WAIT_MASTER_AT_TARGET";
  case ECMC_MST_SLV_STATUS_WAIT_MASTER_DISABLE:
    return "WAIT_MASTER_DISABLE";
  case ECMC_MST_SLV_STATUS_FORCED_TIMEOUT_RECOVERY:
    return "FORCED_TIMEOUT_RECOVERY";
  default:
    return "UNKNOWN";
  }
}

const char *masterSlaveTransitionReasonText(int reason) {
  switch (reason) {
  case ECMC_MST_SLV_TRANSITION_NONE:
    return "NONE";
  case ECMC_MST_SLV_TRANSITION_EXTERNAL_STATE_WRITE:
    return "EXTERNAL_STATE_WRITE";
  case ECMC_MST_SLV_TRANSITION_CONTROL_DISABLED:
    return "CONTROL_DISABLED";
  case ECMC_MST_SLV_TRANSITION_SLAVE_COMMAND:
    return "SLAVE_COMMAND";
  case ECMC_MST_SLV_TRANSITION_MASTER_COMMAND:
    return "MASTER_COMMAND";
  case ECMC_MST_SLV_TRANSITION_SLAVE_COMPLETE:
    return "SLAVE_COMPLETE";
  case ECMC_MST_SLV_TRANSITION_MASTER_COMPLETE:
    return "MASTER_COMPLETE";
  case ECMC_MST_SLV_TRANSITION_LOST_ENABLE_OR_ERROR:
    return "LOST_ENABLE_OR_ERROR";
  case ECMC_MST_SLV_TRANSITION_SLAVE_TRAJ_SOURCE_FAILED:
    return "SLAVE_TRAJ_SOURCE_FAILED";
  case ECMC_MST_SLV_TRANSITION_RESET_COMPLETE:
    return "RESET_COMPLETE";
  case ECMC_MST_SLV_TRANSITION_PREPARE_TIMEOUT:
    return "PREPARE_TIMEOUT";
  case ECMC_MST_SLV_TRANSITION_MASTER_DISABLE_TIMEOUT:
    return "MASTER_DISABLE_TIMEOUT";
  default:
    return "UNKNOWN";
  }
}

void jsonString(FILE *fp, const char *text) {
  fputc('"', fp);
  if (text) {
    for (const char *p = text; *p; ++p) {
      switch (*p) {
      case '\\':
        fputs("\\\\", fp);
        break;
      case '"':
        fputs("\\\"", fp);
        break;
      case '\n':
        fputs("\\n", fp);
        break;
      case '\r':
        fputs("\\r", fp);
        break;
      case '\t':
        fputs("\\t", fp);
        break;
      default:
        if (static_cast<unsigned char>(*p) < 0x20) {
          fprintf(fp, "\\u%04x", static_cast<unsigned char>(*p));
        } else {
          fputc(*p, fp);
        }
        break;
      }
    }
  }
  fputc('"', fp);
}

void writeName(FILE *fp, int indent, const char *name) {
  fprintf(fp, "%*s", indent, "");
  jsonString(fp, name);
  fputs(": ", fp);
}

void writeJsonInt(FILE *fp, int indent, const char *name, long long value, bool comma = true) {
  writeName(fp, indent, name);
  fprintf(fp, "%lld%s\n", value, comma ? "," : "");
}

void writeJsonDouble(FILE *fp, int indent, const char *name, double value, bool comma = true) {
  writeName(fp, indent, name);
  fprintf(fp, "%.17g%s\n", value, comma ? "," : "");
}

void writeJsonBool(FILE *fp, int indent, const char *name, bool value, bool comma = true) {
  writeName(fp, indent, name);
  fprintf(fp, "%s%s\n", boolText(value), comma ? "," : "");
}

void writeJsonString(FILE *fp, int indent, const char *name, const char *value, bool comma = true) {
  writeName(fp, indent, name);
  jsonString(fp, value);
  fprintf(fp, "%s\n", comma ? "," : "");
}

void writeTimestamp(FILE *fp) {
  time_t now = time(NULL);
  struct tm localTime;
  localtime_r(&now, &localTime);
  char buffer[64] = {0};
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S%z", &localTime);
  writeJsonString(fp, 2, "timestamp", buffer);
}

void makeDumpFileName(char *buffer, size_t bufferSize) {
  time_t now = time(NULL);
  struct tm localTime;
  localtime_r(&now, &localTime);
  char timeBuffer[32] = {0};
  strftime(timeBuffer, sizeof(timeBuffer), "%Y%m%d_%H%M%S", &localTime);
  snprintf(buffer,
           bufferSize,
           "/tmp/ecmc_motion_diag_%ld_%s.json",
           static_cast<long>(getpid()),
           timeBuffer);
}

int collectAxisBlockers(ecmcAxisBase *axis,
                        const ecmcAxisDataStatus &status,
                        const char **blockers,
                        int maxBlockers) {
  int count = 0;
  auto add = [&](const char *text) {
    if (count < maxBlockers) {
      blockers[count] = text;
    }
    ++count;
  };
  const int command = axis->getCommand();
  if (axis->getError()) {
    add("axis_error");
  }
  if (!axis->getRealTimeStarted()) {
    add("realtime_not_started");
  }
  if (axis->getInStartupPhase()) {
    add("axis_in_startup");
  }
  if (axis->getBlockCom()) {
    add("command_interface_blocked");
  }
  if (axis->getBlocked() || status.statusWord_.blocked) {
    add("blocked_by_axis_group_or_master_slave");
  }
  if (!axis->getHwReady()) {
    add("hardware_not_ready");
  }
  if (status.statusWord_.sumilockfwd) {
    add("forward_interlock");
  }
  if (status.statusWord_.sumilockbwd) {
    add("backward_interlock");
  }
  if (status.statusWord_.limitfwd) {
    add("forward_limit_active");
  }
  if (status.statusWord_.limitbwd) {
    add("backward_limit_active");
  }
  if ((command == ECMC_CMD_MOVEABS ||
       command == ECMC_CMD_MOVEREL ||
       command == ECMC_CMD_MOVEPVTABS) &&
      !axis->getAllowPos()) {
    add("position_motion_disabled");
  }
  if ((command == ECMC_CMD_MOVEVEL ||
       command == ECMC_CMD_JOG) &&
      !axis->getAllowConstVelo()) {
    add("velocity_motion_disabled");
  }
  if (command == ECMC_CMD_HOMING && !axis->getAllowHome()) {
    add("homing_disabled");
  }
  if (command != ECMC_CMD_NOCMD && !axis->getExecute()) {
    add("command_loaded_but_execute_false");
  }
  if (!axis->getEnable()) {
    add("enable_command_false");
  }
  if (axis->getEnable() && !axis->getEnabledOnly()) {
    add("enable_command_true_but_drive_not_enabled");
  }
  return count;
}

void writeBlockerArray(FILE *fp, ecmcAxisBase *axis, const ecmcAxisDataStatus &status) {
  const char *blockers[32] = {0};
  const int blockerCount = collectAxisBlockers(axis, status, blockers, 32);
  fprintf(fp, "      \"motion_blockers\": [");
  for (int i = 0; i < blockerCount && i < 32; ++i) {
    fprintf(fp, "%s", i == 0 ? "" : ", ");
    jsonString(fp, blockers[i]);
  }
  fprintf(fp, "],\n");
}

const char *blockerSeverity(const char *blocker) {
  if (!blocker) {
    return "info";
  }
  if (strcmp(blocker, "axis_error") == 0 ||
      strcmp(blocker, "realtime_not_started") == 0 ||
      strcmp(blocker, "forward_interlock") == 0 ||
      strcmp(blocker, "backward_interlock") == 0 ||
      strcmp(blocker, "hardware_not_ready") == 0) {
    return "error";
  }
  return "warning";
}

const char *blockerSuggestion(const char *blocker) {
  if (!blocker) {
    return "";
  }
  if (strcmp(blocker, "axis_error") == 0) {
    return "Read the axis error ID and reset only after the root cause is removed.";
  }
  if (strcmp(blocker, "realtime_not_started") == 0) {
    return "Check app mode and realtime startup state.";
  }
  if (strcmp(blocker, "axis_in_startup") == 0) {
    return "Wait until startup is finished or check startup sequencing.";
  }
  if (strcmp(blocker, "command_interface_blocked") == 0) {
    return "The axis command interface is blocked; check blockCom and higher-level ownership.";
  }
  if (strcmp(blocker, "blocked_by_axis_group_or_master_slave") == 0) {
    return "Check axis groups and master/slave state machine state.";
  }
  if (strcmp(blocker, "hardware_not_ready") == 0) {
    return "Check drive/encoder hardware ready signals and EtherCAT state.";
  }
  if (strcmp(blocker, "forward_interlock") == 0) {
    return "Decode the forward interlock and check limits, PLC, safety, and trajectory interlocks.";
  }
  if (strcmp(blocker, "backward_interlock") == 0) {
    return "Decode the backward interlock and check limits, PLC, safety, and trajectory interlocks.";
  }
  if (strcmp(blocker, "forward_limit_active") == 0) {
    return "The forward limit is active; verify direction and limit wiring/state.";
  }
  if (strcmp(blocker, "backward_limit_active") == 0) {
    return "The backward limit is active; verify direction and limit wiring/state.";
  }
  if (strcmp(blocker, "position_motion_disabled") == 0) {
    return "Position motion is disabled for this axis.";
  }
  if (strcmp(blocker, "velocity_motion_disabled") == 0) {
    return "Velocity motion is disabled for this axis.";
  }
  if (strcmp(blocker, "homing_disabled") == 0) {
    return "Homing is disabled for this axis.";
  }
  if (strcmp(blocker, "command_loaded_but_execute_false") == 0) {
    return "A motion command is selected but execute is false; check command trigger path.";
  }
  if (strcmp(blocker, "enable_command_false") == 0) {
    return "Enable command is false; enable the axis or check auto-enable settings.";
  }
  if (strcmp(blocker, "enable_command_true_but_drive_not_enabled") == 0) {
    return "Enable command is true but enabled readback is false; check drive enable path.";
  }
  return "Inspect the matching object state in this dump.";
}

void buildAxisReport(int axisIndex, char *buffer, size_t bufferSize) {
  if (!buffer || bufferSize == 0) {
    return;
  }
  if (axisIndex < 0 || axisIndex >= ECMC_MAX_AXES || !axes[axisIndex]) {
    snprintf(buffer,
             bufferSize,
             "Axis %d is not configured.",
             axisIndex);
    return;
  }

  ecmcAxisBase *axis = axes[axisIndex];
  ecmcAxisDataStatus status = {};
  ecmcAxisDataStatus *statusPtr = axis->getAxisStatusDataPtr();
  if (statusPtr) {
    status = *statusPtr;
  }

  const char *blockers[32] = {0};
  const int blockerCount = collectAxisBlockers(axis, status, blockers, 32);
  const int written = snprintf(buffer,
                               bufferSize,
                               "Axis %d id=%d cmd=%s mrreqcnt=%u reqcnt=%u execcnt=%u exec=%d enable=%d enabled=%d busy=%d error=0x%x: ",
                               axisIndex,
                               axis->getAxisID(),
                               commandText(axis->getCommand()),
                               axis->getMotionCommandMotorRecordRequestCounter(),
                               axis->getMotionCommandRequestCounter(),
                               axis->getMotionCommandExecuteCounter(),
                               axis->getExecute() ? 1 : 0,
                               axis->getEnable() ? 1 : 0,
                               axis->getEnabledOnly() ? 1 : 0,
                               axis->getBusy() ? 1 : 0,
                               axis->getErrorID());
  size_t offset = written > 0 ? static_cast<size_t>(written) : 0;
  if (offset >= bufferSize) {
    buffer[bufferSize - 1] = '\0';
    return;
  }

  if (blockerCount <= 0) {
    snprintf(buffer + offset,
             bufferSize - offset,
             "no obvious software blocker.");
    return;
  }

  snprintf(buffer + offset,
           bufferSize - offset,
           "blockers=");
  offset = strlen(buffer);
  for (int i = 0; i < blockerCount && i < 32; ++i) {
    snprintf(buffer + offset,
             bufferSize - offset,
             "%s%s",
             i == 0 ? "" : ",",
             blockers[i]);
    offset = strlen(buffer);
    if (offset >= bufferSize - 1) {
      break;
    }
  }
  if (axis->getBlocked() || status.statusWord_.blocked) {
    offset = strlen(buffer);
    snprintf(buffer + offset,
             bufferSize - offset,
             "; active_sms=");
    offset = strlen(buffer);
    bool firstSms = true;
    for (int i = 0; i < ECMC_MAX_MST_SLVS_SMS; ++i) {
      if (!masterSlaveSMs[i]) {
        continue;
      }
      if (masterSlaveSMs[i]->getState() == ECMC_MST_SLV_STATE_IDLE &&
          masterSlaveSMs[i]->getStatus() == 0) {
        continue;
      }
      snprintf(buffer + offset,
               bufferSize - offset,
               "%s%d:%s,status=%d",
               firstSms ? "" : "|",
               i,
               masterSlaveStateText(masterSlaveSMs[i]->getState()),
               masterSlaveSMs[i]->getStatus());
      firstSms = false;
      offset = strlen(buffer);
      if (offset >= bufferSize - 1) {
        break;
      }
    }
    if (firstSms) {
      offset = strlen(buffer);
      snprintf(buffer + offset,
               bufferSize - offset,
               "none_active");
    }
  }
}

void writeAxisStatusWord(FILE *fp, const ecmcAxisDataStatus &status) {
  fputs("      \"status_word\": {\n", fp);
  writeJsonBool(fp, 8, "enable", status.statusWord_.enable);
  writeJsonBool(fp, 8, "enabled", status.statusWord_.enabled);
  writeJsonBool(fp, 8, "execute", status.statusWord_.execute);
  writeJsonBool(fp, 8, "busy", status.statusWord_.busy);
  writeJsonBool(fp, 8, "at_target", status.statusWord_.attarget);
  writeJsonBool(fp, 8, "moving", status.statusWord_.moving);
  writeJsonBool(fp, 8, "limit_fwd", status.statusWord_.limitfwd);
  writeJsonBool(fp, 8, "limit_bwd", status.statusWord_.limitbwd);
  writeJsonBool(fp, 8, "home_switch", status.statusWord_.homeswitch);
  writeJsonBool(fp, 8, "homed", status.statusWord_.homed);
  writeJsonBool(fp, 8, "in_realtime", status.statusWord_.inrealtime);
  writeJsonBool(fp, 8, "traj_source_external", status.statusWord_.trajsource);
  writeJsonBool(fp, 8, "enc_source_external", status.statusWord_.encsource);
  writeJsonBool(fp, 8, "plc_command_allowed", status.statusWord_.plccmdallowed);
  writeJsonBool(fp, 8, "soft_limit_fwd_enabled", status.statusWord_.softlimfwdena);
  writeJsonBool(fp, 8, "soft_limit_bwd_enabled", status.statusWord_.softlimbwdena);
  writeJsonBool(fp, 8, "in_startup", status.statusWord_.instartup);
  writeJsonBool(fp, 8, "sum_interlock_fwd", status.statusWord_.sumilockfwd);
  writeJsonBool(fp, 8, "sum_interlock_bwd", status.statusWord_.sumilockbwd);
  writeJsonBool(fp, 8, "soft_limit_interlock_fwd", status.statusWord_.softlimilockfwd);
  writeJsonBool(fp, 8, "soft_limit_interlock_bwd", status.statusWord_.softlimilockbwd);
  writeJsonBool(fp, 8, "local_busy", status.statusWord_.localBusy);
  writeJsonBool(fp, 8, "global_busy", status.statusWord_.globalBusy);
  writeJsonBool(fp, 8, "blocked", status.statusWord_.blocked);
  writeJsonInt(fp, 8, "seq_state", status.statusWord_.seqstate);
  writeJsonInt(fp, 8, "last_interlock", status.statusWord_.lastilock, false);
  fputs("      },\n", fp);
}

void writeAxis(FILE *fp, int axisIndex, ecmcAxisBase *axis, bool comma) {
  ecmcAxisDataStatus status = {};
  ecmcAxisDataStatus *statusPtr = axis->getAxisStatusDataPtr();
  if (statusPtr) {
    status = *statusPtr;
  }

  fputs("    {\n", fp);
  writeJsonInt(fp, 6, "index", axisIndex);
  writeJsonInt(fp, 6, "axis_id", axis->getAxisID());
  writeJsonString(fp, 6, "axis_type", axisTypeText(axis->getAxisType()));
  writeJsonString(fp, 6, "command_text", commandText(axis->getCommand()));
  writeJsonInt(fp, 6, "command", axis->getCommand());
  writeJsonInt(fp, 6, "cmd_data", axis->getCmdData());
  writeJsonInt(fp, 6, "motion_command_motor_record_request_counter", axis->getMotionCommandMotorRecordRequestCounter());
  writeJsonInt(fp, 6, "motion_command_request_counter", axis->getMotionCommandRequestCounter());
  writeJsonInt(fp, 6, "motion_command_execute_counter", axis->getMotionCommandExecuteCounter());
  writeJsonBool(fp, 6, "execute", axis->getExecute());
  writeJsonBool(fp, 6, "enable_command", axis->getEnable());
  writeJsonBool(fp, 6, "enabled", axis->getEnabledOnly());
  writeJsonBool(fp, 6, "busy", axis->getBusy());
  writeJsonBool(fp, 6, "local_busy", axis->getLocalBusy());
  writeJsonBool(fp, 6, "global_busy", axis->getGlobalBusy());
  writeJsonBool(fp, 6, "traj_busy", axis->getTrajBusy());
  writeJsonBool(fp, 6, "error", axis->getError());
  writeJsonInt(fp, 6, "error_id", axis->getErrorID());
  writeJsonBool(fp, 6, "blocked", axis->getBlocked());
  writeJsonBool(fp, 6, "block_com", axis->getBlockCom());
  writeJsonBool(fp, 6, "hardware_ready", axis->getHwReady());
  writeJsonBool(fp, 6, "realtime_started", axis->getRealTimeStarted());
  writeJsonBool(fp, 6, "startup_phase", axis->getInStartupPhase());
  writeJsonBool(fp, 6, "allow_position_motion", axis->getAllowPos());
  writeJsonBool(fp, 6, "allow_velocity_motion", axis->getAllowConstVelo());
  writeJsonBool(fp, 6, "allow_home", axis->getAllowHome());
  writeJsonBool(fp, 6, "allow_plc_commands", axis->getAllowCmdFromPLC());
  writeJsonString(fp, 6, "traj_source", dataSourceText(axis->getTrajDataSourceType()));
  writeJsonString(fp, 6, "enc_source", dataSourceText(axis->getEncDataSourceType()));
  writeJsonDouble(fp, 6, "actual_position", status.currentPositionActual);
  writeJsonDouble(fp, 6, "setpoint_position", status.currentPositionSetpoint);
  writeJsonDouble(fp, 6, "target_position", status.currentTargetPosition);
  writeJsonDouble(fp, 6, "actual_velocity", status.currentVelocityActual);
  writeJsonDouble(fp, 6, "setpoint_velocity", status.currentVelocitySetpoint);
  writeJsonDouble(fp, 6, "target_velocity", status.currentVelocityTarget);
  writeJsonDouble(fp, 6, "control_error", status.cntrlError);
  writeJsonDouble(fp, 6, "control_output", status.cntrlOutput);
  writeJsonDouble(fp, 6, "distance_to_stop", status.distToStop);
  writeJsonInt(fp, 6, "cycle_counter", status.cycleCounter);
  writeBlockerArray(fp, axis, status);
  writeAxisStatusWord(fp, status);
  writeJsonInt(fp, 6, "status_error_code", status.errorCode);
  writeJsonInt(fp, 6, "status_warning_code", status.warningCode, false);
  fprintf(fp, "    }%s\n", comma ? "," : "");
}

int countAxes() {
  int count = 0;
  for (int i = 0; i < ECMC_MAX_AXES; ++i) {
    if (axes[i]) {
      ++count;
    }
  }
  return count;
}

int countAxisFindings() {
  int count = 0;
  for (int i = 0; i < ECMC_MAX_AXES; ++i) {
    if (!axes[i]) {
      continue;
    }
    ecmcAxisDataStatus status = {};
    ecmcAxisDataStatus *statusPtr = axes[i]->getAxisStatusDataPtr();
    if (statusPtr) {
      status = *statusPtr;
    }
    const char *blockers[32] = {0};
    const int blockerCount = collectAxisBlockers(axes[i], status, blockers, 32);
    count += blockerCount < 32 ? blockerCount : 32;
  }
  return count;
}

void writeAxes(FILE *fp) {
  fputs("  \"axes\": [\n", fp);
  const int total = countAxes();
  int written = 0;
  for (int i = 0; i < ECMC_MAX_AXES; ++i) {
    if (!axes[i]) {
      continue;
    }
    ++written;
    writeAxis(fp, i, axes[i], written < total);
  }
  fputs("  ],\n", fp);
}

void writeAxisGroup(FILE *fp, int groupIndex, ecmcAxisGroup *group, bool comma) {
  const ecmcAxisGroupStatusSummary summary = group->getStatusSummary();
  fputs("    {\n", fp);
  writeJsonInt(fp, 6, "index", groupIndex);
  writeJsonString(fp, 6, "name", group->getName());
  writeJsonInt(fp, 6, "axis_count", static_cast<long long>(group->size()));
  writeJsonBool(fp, 6, "blocked", group->getBlocked());
  fputs("      \"axes\": [", fp);
  bool first = true;
  for (int i = 0; i < ECMC_MAX_AXES; ++i) {
    if (group->inGroup(i)) {
      fprintf(fp, "%s%d", first ? "" : ", ", i);
      first = false;
    }
  }
  fputs("],\n", fp);
  fputs("      \"summary\": {\n", fp);
  writeJsonBool(fp, 8, "all_enable_cmd", summary.allEnableCmd);
  writeJsonBool(fp, 8, "any_enable_cmd", summary.anyEnableCmd);
  writeJsonBool(fp, 8, "all_enabled", summary.allEnabled);
  writeJsonBool(fp, 8, "any_enabled", summary.anyEnabled);
  writeJsonBool(fp, 8, "all_busy", summary.allBusy);
  writeJsonBool(fp, 8, "any_busy", summary.anyBusy);
  writeJsonBool(fp, 8, "any_interlocked", summary.anyIlocked);
  writeJsonBool(fp, 8, "all_at_target", summary.allAtTarget);
  writeJsonBool(fp, 8, "all_within_control_deadband", summary.allWithinCtrlDb);
  writeJsonBool(fp, 8, "all_traj_external", summary.allTrajExternal);
  writeJsonBool(fp, 8, "any_traj_external", summary.anyTrajExternal);
  writeJsonInt(fp, 8, "first_error_id", summary.firstErrorId, false);
  fputs("      }\n", fp);
  fprintf(fp, "    }%s\n", comma ? "," : "");
}

int countAxisGroups() {
  int count = 0;
  for (int i = 0; i < ECMC_MAX_AXES; ++i) {
    if (axisGroups[i]) {
      ++count;
    }
  }
  return count;
}

int countAxisGroupFindings() {
  int count = 0;
  for (int i = 0; i < ECMC_MAX_AXES; ++i) {
    if (!axisGroups[i]) {
      continue;
    }
    const ecmcAxisGroupStatusSummary summary = axisGroups[i]->getStatusSummary();
    if (axisGroups[i]->getBlocked()) {
      ++count;
    }
    if (summary.anyIlocked) {
      ++count;
    }
    if (summary.firstErrorId) {
      ++count;
    }
  }
  return count;
}

void writeAxisGroups(FILE *fp) {
  fputs("  \"axis_groups\": [\n", fp);
  const int total = countAxisGroups();
  int written = 0;
  for (int i = 0; i < ECMC_MAX_AXES; ++i) {
    if (!axisGroups[i]) {
      continue;
    }
    ++written;
    writeAxisGroup(fp, i, axisGroups[i], written < total);
  }
  fputs("  ],\n", fp);
}

int countMasterSlaveSMs() {
  int count = 0;
  for (int i = 0; i < ECMC_MAX_MST_SLVS_SMS; ++i) {
    if (masterSlaveSMs[i]) {
      ++count;
    }
  }
  return count;
}

int countMasterSlaveSMFindings() {
  int count = 0;
  for (int i = 0; i < ECMC_MAX_MST_SLVS_SMS; ++i) {
    if (!masterSlaveSMs[i]) {
      continue;
    }
    if (!masterSlaveSMs[i]->getEnabled()) {
      ++count;
    }
    if (masterSlaveSMs[i]->getState() != ECMC_MST_SLV_STATE_IDLE) {
      ++count;
    }
    if (masterSlaveSMs[i]->getStatus()) {
      ++count;
    }
    if (masterSlaveSMs[i]->getLastFaultCode()) {
      ++count;
    }
  }
  return count;
}

void writeMasterSlaveSM(FILE *fp, int index, ecmcMasterSlaveStateMachine *sm, bool comma) {
  fputs("    {\n", fp);
  writeJsonInt(fp, 6, "index", index);
  writeJsonString(fp, 6, "name", sm->getName());
  writeJsonString(fp, 6, "state_text", masterSlaveStateText(sm->getState()));
  writeJsonInt(fp, 6, "state", sm->getState());
  writeJsonString(fp, 6, "status_text", masterSlaveStatusText(sm->getStatus()));
  writeJsonInt(fp, 6, "status", sm->getStatus());
  writeJsonInt(fp, 6, "status_word", sm->getStatusWord());
  writeJsonString(fp, 6, "previous_state_text",
                  masterSlaveStateText(sm->getPreviousState()));
  writeJsonInt(fp, 6, "previous_state", sm->getPreviousState());
  writeJsonString(fp, 6, "last_transition_reason_text",
                  masterSlaveTransitionReasonText(sm->getLastTransitionReason()));
  writeJsonInt(fp, 6, "last_transition_reason", sm->getLastTransitionReason());
  writeJsonInt(fp, 6, "execute_cycle_count",
               static_cast<long long>(sm->getExecuteCycleCount()));
  writeJsonInt(fp, 6, "transition_count",
               static_cast<long long>(sm->getTransitionCount()));
  writeJsonInt(fp, 6, "last_transition_execute_cycle",
               static_cast<long long>(sm->getLastTransitionCycle()));
  writeJsonInt(fp, 6, "last_fault_code", sm->getLastFaultCode());
  writeJsonString(fp, 6, "last_fault_text",
                  sm->getLastFaultCode() ? getErrorString(sm->getLastFaultCode()) : "NONE");
  writeJsonString(fp, 6, "last_fault_status_text",
                  masterSlaveStatusText(sm->getLastFaultStatus()));
  writeJsonInt(fp, 6, "last_fault_status", sm->getLastFaultStatus());
  writeJsonInt(fp, 6, "last_fault_execute_cycle",
               static_cast<long long>(sm->getLastFaultCycle()));
  writeJsonBool(fp, 6, "enabled", sm->getEnabled());
  writeJsonBool(fp, 6, "auto_disable_masters", sm->getAutoDisableMasters());
  writeJsonBool(fp, 6, "auto_disable_slaves", sm->getAutoDisableSlaves());
  writeJsonDouble(fp, 6, "master_at_target_timeout_s", sm->getMasterAtTargetTimeout(), false);
  fprintf(fp, "    }%s\n", comma ? "," : "");
}

void writeMasterSlaveSMs(FILE *fp) {
  fputs("  \"master_slave_state_machines\": [\n", fp);
  const int total = countMasterSlaveSMs();
  int written = 0;
  for (int i = 0; i < ECMC_MAX_MST_SLVS_SMS; ++i) {
    if (!masterSlaveSMs[i]) {
      continue;
    }
    ++written;
    writeMasterSlaveSM(fp, i, masterSlaveSMs[i], written < total);
  }
  fputs("  ]\n", fp);
}

void writeFinding(FILE *fp,
                  const char *severity,
                  const char *objectType,
                  int index,
                  const char *code,
                  const char *message,
                  const char *suggestion,
                  bool comma) {
  fputs("      {\n", fp);
  writeJsonString(fp, 8, "severity", severity);
  writeJsonString(fp, 8, "object_type", objectType);
  writeJsonInt(fp, 8, "index", index);
  writeJsonString(fp, 8, "code", code);
  writeJsonString(fp, 8, "message", message);
  writeJsonString(fp, 8, "suggestion", suggestion, false);
  fprintf(fp, "      }%s\n", comma ? "," : "");
}

void writeAxisFindings(FILE *fp, int *written, int totalFindings) {
  for (int i = 0; i < ECMC_MAX_AXES; ++i) {
    if (!axes[i]) {
      continue;
    }
    ecmcAxisDataStatus status = {};
    ecmcAxisDataStatus *statusPtr = axes[i]->getAxisStatusDataPtr();
    if (statusPtr) {
      status = *statusPtr;
    }
    const char *blockers[32] = {0};
    const int blockerCount = collectAxisBlockers(axes[i], status, blockers, 32);
    const int limitedCount = blockerCount < 32 ? blockerCount : 32;
    for (int j = 0; j < limitedCount; ++j) {
      char message[192] = {0};
      snprintf(message,
               sizeof(message),
               "Axis %d may not trigger motion: %s.",
               i,
               blockers[j]);
      ++(*written);
      writeFinding(fp,
                   blockerSeverity(blockers[j]),
                   "axis",
                   i,
                   blockers[j],
                   message,
                   blockerSuggestion(blockers[j]),
                   *written < totalFindings);
    }
  }
}

void writeAxisGroupFindings(FILE *fp, int *written, int totalFindings) {
  for (int i = 0; i < ECMC_MAX_AXES; ++i) {
    if (!axisGroups[i]) {
      continue;
    }
    const ecmcAxisGroupStatusSummary summary = axisGroups[i]->getStatusSummary();
    if (axisGroups[i]->getBlocked()) {
      ++(*written);
      writeFinding(fp,
                   "warning",
                   "axis_group",
                   i,
                   "group_blocked",
                   "Axis group is blocked and may reject or suppress motion commands.",
                   "Check master/slave state and group ownership.",
                   *written < totalFindings);
    }
    if (summary.anyIlocked) {
      ++(*written);
      writeFinding(fp,
                   "error",
                   "axis_group",
                   i,
                   "group_interlocked",
                   "At least one axis in the group is interlocked.",
                   "Inspect the group axes and decode the active axis interlock.",
                   *written < totalFindings);
    }
    if (summary.firstErrorId) {
      ++(*written);
      writeFinding(fp,
                   "error",
                   "axis_group",
                   i,
                   "group_axis_error",
                   "At least one axis in the group is in error.",
                   "Inspect the first_error_id and individual axis errors.",
                   *written < totalFindings);
    }
  }
}

void writeMasterSlaveSMFindings(FILE *fp, int *written, int totalFindings) {
  for (int i = 0; i < ECMC_MAX_MST_SLVS_SMS; ++i) {
    if (!masterSlaveSMs[i]) {
      continue;
    }
    if (!masterSlaveSMs[i]->getEnabled()) {
      ++(*written);
      writeFinding(fp,
                   "warning",
                   "master_slave_state_machine",
                   i,
                   "state_machine_disabled",
                   "Master/slave state machine is disabled.",
                   "If this state machine should arbitrate motion, enable it and verify configuration.",
                   *written < totalFindings);
    }
    if (masterSlaveSMs[i]->getState() != ECMC_MST_SLV_STATE_IDLE) {
      ++(*written);
      writeFinding(fp,
                   "info",
                   "master_slave_state_machine",
                   i,
                   "state_machine_active",
                   "Master/slave state machine is not idle and may be blocking one side.",
                   "Check state_text and whether the requested axis belongs to a blocked group.",
                   *written < totalFindings);
    }
    if (masterSlaveSMs[i]->getStatus()) {
      ++(*written);
      const int status = masterSlaveSMs[i]->getStatus();
      writeFinding(fp,
                   status == ECMC_MST_SLV_STATUS_FORCED_TIMEOUT_RECOVERY ?
                   "error" : "info",
                   "master_slave_state_machine",
                   i,
                   "state_machine_status",
                   "Master/slave state machine reports an active internal phase.",
                   "Inspect status_text, state_text, and whether the requested axis belongs to a blocked group.",
                   *written < totalFindings);
    }
    if (masterSlaveSMs[i]->getLastFaultCode()) {
      ++(*written);
      writeFinding(fp,
                   "warning",
                   "master_slave_state_machine",
                   i,
                   "state_machine_historical_fault",
                   "Master/slave state machine has retained fault history.",
                   "Inspect last_fault_text, last_fault_status_text, and last_fault_execute_cycle.",
                   *written < totalFindings);
    }
  }
}

void writeDiagnosis(FILE *fp) {
  const int totalFindings = countAxisFindings() +
                            countAxisGroupFindings() +
                            countMasterSlaveSMFindings();
  fputs("  \"diagnosis\": {\n", fp);
  writeJsonInt(fp, 4, "finding_count", totalFindings);
  writeJsonString(fp,
                  4,
                  "summary",
                  totalFindings ? "Potential motion blockers found." : "No obvious motion blockers found.");
  fputs("    \"findings\": [\n", fp);
  int written = 0;
  writeAxisFindings(fp, &written, totalFindings);
  writeAxisGroupFindings(fp, &written, totalFindings);
  writeMasterSlaveSMFindings(fp, &written, totalFindings);
  fputs("    ]\n", fp);
  fputs("  },\n", fp);
}

int writeDumpFile(const DiagRequest &request) {
  FILE *fp = fopen(request.file, "w");
  if (!fp) {
    return -1;
  }

  fputs("{\n", fp);
  writeJsonInt(fp, 2, "ecmc_motion_diag_version", 2);
  writeTimestamp(fp);
  writeJsonInt(fp, 2, "level", request.level);
  writeJsonInt(fp, 2, "axis_count", countAxes());
  writeJsonInt(fp, 2, "axis_group_count", countAxisGroups());
  writeJsonInt(fp, 2, "master_slave_state_machine_count", countMasterSlaveSMs());
  writeJsonInt(fp, 2, "app_mode_command", appModeCmd);
  writeJsonInt(fp, 2, "app_mode_status", appModeStat);
  writeJsonInt(fp, 2, "controller_error", controllerError);
  writeJsonString(fp, 2, "controller_error_text", controllerErrorMsg ? controllerErrorMsg : "");
  writeJsonInt(fp, 2, "rt_cycle_counter", static_cast<long long>(ecmcUpdatedCounter));
  writeDiagnosis(fp);
  writeAxes(fp);
  writeAxisGroups(fp);
  writeMasterSlaveSMs(fp);
  fputs("}\n", fp);

  return fclose(fp);
}

}  // namespace

void ecmcMotionDiagMakeDumpFileName(char *buffer, size_t bufferSize) {
  makeDumpFileName(buffer, bufferSize);
}

int ecmcMotionDiagWriteDumpFile(const char *fileName, int level) {
  if (!fileName || fileName[0] == '\0') {
    return -1;
  }
  DiagRequest request = {};
  request.level = level;
  snprintf(request.file, sizeof(request.file), "%s", fileName);
  return writeDumpFile(request);
}

void ecmcMotionDiagBuildAxisReport(int axisIndex, char *buffer, size_t bufferSize) {
  buildAxisReport(axisIndex, buffer, bufferSize);
}

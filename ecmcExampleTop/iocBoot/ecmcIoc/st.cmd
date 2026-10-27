# Run from this directory: ../../bin/<EPICS_HOST_ARCH>/ecmcIoc st.cmd
< envPaths

cd "${TOP}"

## Register all support components
dbLoadDatabase "dbd/ecmcIoc.dbd"
ecmcIoc_registerRecordDeviceDriver pdbbase

epicsEnvSet("PREFIX", "ECMC:CLASSIC:")
epicsEnvSet("ECMC_PORT", "ECMC")

# No EtherCAT master is selected: ecmc starts in masterless mode.
# Arguments: port, maximum parameters, priority, disable auto-connect, ms.
ecmcAsynPortDriverConfigure("$(ECMC_PORT)", 256, 0, 0, 100)
ecmcConfigOrDie("Cfg.SetSampleRate(1000)")
ecmcConfigOrDie("Cfg.SetStartupGate(1)")
ecmcConfigOrDie("Cfg.CreatePLC(0,100)")
ecmcConfigOrDie("Cfg.LoadPLCFile(0,$(TOP)/iocBoot/$(IOC)/counter.plc)")
# Compile before loading records so the PLC variables exist for asyn links.
ecmcConfigOrDie("Cfg.CompilePLC(0)")

dbLoadRecords("db/masterless.db", "P=$(PREFIX),PORT=$(ECMC_PORT)")

# Start the runtime explicitly. The startup gate holds PLC execution until
# EPICS reaches IOC running; no PSI atInit/afterInit hooks are needed.
ecmcConfigOrDie("Cfg.SetAppMode(1)")

cd "${TOP}/iocBoot/${IOC}"
iocInit

ecmcLogBufferPrint

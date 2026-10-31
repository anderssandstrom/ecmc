#!/usr/bin/env python3
"""Build real PSI require/misc and selected ECMC sources against the image's EPICS."""
import json
from pathlib import Path
import shutil
import subprocess
import re

ROOT=Path('/opt/slit')
SRC=ROOT/'sources'
BASE=Path('/opt/epics/base')
ARCH=subprocess.check_output([str(BASE/'startup/EpicsHostArch')],text=True).strip()
version_text=(BASE/'configure/CONFIG_BASE_VERSION').read_text()
parts=[re.search(r'^'+key+r'\s*=\s*(\d+)',version_text,re.M).group(1) for key in ('EPICS_VERSION','EPICS_REVISION','EPICS_MODIFICATION','EPICS_PATCH_LEVEL')]
BASEVER='.'.join(parts[:3]) + ('.'+parts[3] if parts[3]!='0' else '')
JOBS='2'
manifest=json.loads((ROOT/'versions.json').read_text())
POOL=Path('/ioc/modules')

def make(path, target=None):
    log=ROOT/(path.name+'-build.log')
    with log.open('w') as out:
        subprocess.run(['make','-C',str(path),'-j'+JOBS]+([target] if target else []),stdout=out,stderr=subprocess.STDOUT,check=True)

def module(name, files, dbds, extra=''):
    top=ROOT/'build'/name
    top.mkdir(parents=True,exist_ok=True)
    shutil.copytree(SRC/'ecmc/configure',top/'configure',dirs_exist_ok=True)
    (top/'configure/RELEASE').write_text('EPICS_BASE=/opt/epics/base\n')
    (top/'Makefile').write_text('TOP=.\ninclude $(TOP)/configure/CONFIG\nDIRS += configure src\ninclude $(TOP)/configure/RULES_TOP\n')
    src=top/'src'; src.mkdir(exist_ok=True)
    for file in files:
        shutil.copy2(file,src/file.name)
    (src/'version.c').write_text('const char _'+name+'LibRelease[]="'+('1.0.0' if name in ('require','misc') else manifest[name]['ref'])+'";\n')
    sources=' '.join(p.name for p in files if p.suffix in ('.c','.cpp','.y'))
    (src/'Makefile').write_text('TOP=..\ninclude $(TOP)/configure/CONFIG\n'
      f'LIBRARY_IOC = {name}\nDBD += {name}.dbd\n{name}_DBD += '+ ' '.join(dbds)+'\n'
      f'{name}_SRCS += {sources} version.c {name}_registerRecordDeviceDriver.cpp\n'
      f'{name}_LIBS += $(EPICS_BASE_IOC_LIBS)\nUSR_CXXFLAGS += -std=c++17\n'+extra+
      '\ninclude $(TOP)/configure/RULES\n')
    if name == 'require':
        with (src/'Makefile').open('a') as out:
            out.write('dbLoadTemplate.c: dbLoadTemplate_lex.c ../dbLoadTemplate.h\n')
    make(top)
    return top

def install(name, version, top):
    dest=POOL/name/version/('R'+BASEVER)
    dest.mkdir(parents=True,exist_ok=True)
    for directory in ('lib','dbd','db'):
        if (top/directory).is_dir(): shutil.copytree(top/directory,dest/directory,dirs_exist_ok=True)
    return dest

req=SRC/'require'
files=[req/p for p in ['require.c','require.h','require.dbd','runScript.c','runScript.dbd','expr.c','expr.h','dbLoadTemplate.y','dbLoadTemplate_lex.l','dbLoadTemplate.h','dbLoadTemplate.dbd','asprintf.h','strdup.h']]
# The combined generated dbd needs a different name from its constituent.
shutil.copy2(req/'require.dbd', req/'requireSupport.dbd')
files=[p for p in files if p.name!='require.dbd']+[req/'requireSupport.dbd']
top=module('require',files,['requireSupport.dbd','runScript.dbd','dbLoadTemplate.dbd'],
    'USR_INCLUDES_Linux=-idirafter $(EPICS_BASE)/include\nUSR_CFLAGS += -DT_A=\'"'+ARCH+'"\' -DEPICSVERSION=\'"'+BASEVER+'"\'\n')
dest=install('require','1.0.0',top)
(dest/'db').mkdir(exist_ok=True)
shutil.copy2(req/'moduleversion.template', dest/'db/moduleversion.template')
shutil.copy2(req/'iocsh',ROOT/'iocsh')
(ROOT/'iocsh').chmod(0o755)

names='listRecords updateMenuConvert addScan dbll cal disctools exec ifEnvSet mlock ulimit echo dbla threads eval afterInit alarmLogger'.split()
files=[SRC/'misc'/(n+ext) for n in names for ext in ('.c','.dbd')]
install('misc','1.0.0',module('misc',files,[n+'.dbd' for n in names]))

# Add only standard EPICS dynamic-module registration/version metadata to the export.
ecmc=SRC/'ecmc'
release='''SUPPORT=/opt/epics/support
EPICS_BASE=/opt/epics/base
ASYN=/opt/epics/support/asyn
MOTOR=/opt/epics/support/motor
RUCKIG=/opt/epics/support/ruckig
ETHERLAB=/opt/etherlab
'''
(ecmc/'configure/RELEASE.local').write_text(release)
p=ecmc/'devEcmcSup/Makefile'
s=p.read_text(); s=s.replace('include $(TOP)/configure/RULES',
 'DBD += ecmc.dbd\necmc_DBD += ecmcController.dbd ecmcMotorRecordSupport.dbd asyn.dbd motorSupport.dbd\necmc_SRCS += ecmc_registerRecordDeviceDriver.cpp\ninclude $(TOP)/configure/RULES')
p.write_text(s)
(ecmc/'devEcmcSup/main/moduleVersion.c').write_text('const char _ecmcLibRelease[]="'+manifest['ecmc']['ref']+'";\n')
make(ecmc/'configure'); make(ecmc/'exprtkSupport'); make(ecmc/'devEcmcSup')
install('ecmc',manifest['ecmc']['ref'],ecmc)

cfg=SRC/'ecmccfg'
top=module('ecmccfg',list((cfg/'src').glob('*.cpp'))+list((cfg/'src').glob('*.h'))+list((cfg/'dbd').glob('*.dbd')),[p.name for p in (cfg/'dbd').glob('*.dbd')])
cf=install('ecmccfg',manifest['ecmccfg']['ref'],top)
# Match the flattened runtime script/template layout used by PSI driver.makefile.
for folder in ['scripts','naming','general','hardware','motion','protocol','plc_lib']:
    for p in sorted((cfg/folder).rglob('*')):
        if p.is_file() and not p.name.startswith('.'):
            dest=cf/p.name
            if not dest.exists(): shutil.copy2(p,dest)
shutil.copy2(cfg/'startup.cmd',cf/'startup.cmd')
(cf/'db').mkdir(exist_ok=True)
for p in sorted((cfg/'db').rglob('*')):
    if p.is_file() and not p.name.startswith('.'): shutil.copy2(p,cf/'db'/p.name)
comp=POOL/'ecmccomp'/'1.0.0'/('R'+BASEVER); comp.mkdir(parents=True,exist_ok=True)
for p in sorted((SRC/'ecmccomp').rglob('*.cmd')):
    if 'examples' not in p.parts and not (comp/p.name).exists(): shutil.copy2(p,comp/p.name)
# Require can load data-only modules; an empty dependency file marks the release.
(comp/'ecmccomp.dep').touch()
(ROOT/'runtime.env').write_text(f'''export EPICS_BASE={BASE}
export EPICS_HOST_ARCH={ARCH}
export EPICS_MODULES={POOL}
export EPICS_DRIVER_PATH={POOL}
export REQUIRE_VERSION=1.0.0
export ECMC_TEST_VER={manifest['ecmc']['ref']}
export ECMCCFG_TEST_VER={manifest['ecmccfg']['ref']}
export ECMCCOMP_VER=1.0.0
export PATH=/opt/slit/venv/bin:{BASE}/bin/{ARCH}:$PATH
export EPICS_CA_AUTO_ADDR_LIST=NO
export EPICS_CA_ADDR_LIST=127.0.0.1
''')
print('Built module pool:',POOL, 'EPICS', BASEVER)

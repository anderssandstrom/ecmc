#!/usr/bin/env python3
"""Export committed versions, never local worktree edits, into a container bundle."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
def run(*args, **kw):
    return subprocess.check_output(args, text=True, **kw).strip()

def export(repo, ref, dest):
    commit = run('git', '-C', str(repo), 'rev-parse', ref + '^{commit}')
    dest.mkdir(parents=True)
    archive = dest.parent / (dest.name + '.tar')
    subprocess.run(['git', '-C', str(repo), 'archive', '--output', str(archive), commit], check=True)
    with tarfile.open(archive) as tar:
        tar.extractall(dest, filter='data')
    archive.unlink()
    return commit

with tempfile.TemporaryDirectory(prefix='ecmc-slit-bundle-') as temp:
    root = Path(temp)
    sources = root/'sources'
    manifest = {}
    for name, repo, ref in [
        ('ecmc', Path(os.environ.get('ECMC_SOURCE', REPO)), os.environ.get('ECMC_REF', 'v11.2.0_RC1')),
        ('ecmccfg', Path(os.environ.get('ECMCCFG_SOURCE', REPO.parent/'ecmccfg')), os.environ.get('ECMCCFG_REF', 'v11.2.0_RC1')),
        ('ecmccomp', Path(os.environ.get('ECMCCOMP_SOURCE', REPO.parent/'ecmccomp')), os.environ.get('ECMCCOMP_REF', '2f1cb024cd5106cf3af9e1bfbe69e675423718ab')),
    ]:
        commit = export(repo, ref, sources/name)
        manifest[name] = {'ref': ref, 'commit': commit}
        if name == 'ecmc':
            sub = run('git', '-C', str(repo), 'ls-tree', commit, 'exprtkSupport').split()[2]
            # git archive leaves the submodule directory empty.
            (sources/name/'exprtkSupport').rmdir()
            export(repo/'exprtkSupport', sub, sources/name/'exprtkSupport')
            manifest['exprtkSupport'] = {'commit': sub}
    for name, pinned in [('require', '0856f1c9f5c8e14f1fd1ae0d0e7d59be2ac89afb'),
                         ('misc', 'cb1a0c758f7df59b7483c3c57e5946ae9a8117b2')]:
        supplied = os.environ.get(name.upper()+'_SOURCE')
        repo = Path(supplied) if supplied else root/(name+'-git')
        if not supplied:
            subprocess.run(['git','clone','https://github.com/paulscherrerinstitute/'+name+'.git',str(repo)],check=True)
        manifest[name] = {'commit': export(repo, pinned, sources/name)}
    shutil.copytree(HERE, root/'scripts', ignore=shutil.ignore_patterns('__pycache__'))
    (root/'versions.json').write_text(json.dumps(manifest, indent=2)+'\n')
    output = Path(os.environ.get('SLIT_BUNDLE', '/tmp/ecmc-slit-bundle.tar.gz')).resolve()
    with tarfile.open(output, 'w:gz') as tar:
        for name in ('sources','scripts','versions.json'):
            tar.add(root/name, arcname=name)
    print(json.dumps(manifest, indent=2))
    print(output)

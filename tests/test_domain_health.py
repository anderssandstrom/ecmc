#!/usr/bin/env python3
"""Step production domain-health filtering with controlled working counters.

This exercises state checks and summary aggregation, not EtherLab reception,
domain process/queue scheduling, a running axis or physical stopping.
"""
# DOC: ecSetDomainFailedCyclesLimit; COMPAT: boundary, startup priming, offline summary.
# See EXPECTATIONS.md for scope and the existing motion response.
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1] / 'devEcmcSup'


def method(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


domain = (ROOT / 'ethercat/ecmcEcDomain.cpp').read_text()
master = (ROOT / 'ethercat/ecmcEc.cpp').read_text()
stubs = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
enum { EC_WC_ZERO, EC_WC_INCOMPLETE, EC_WC_COMPLETE };
struct ec_domain_state_t {
  int wc_state = EC_WC_ZERO, redundancy_active = 0;
  unsigned working_counter = 0;
};
struct ec_domain_t { ec_domain_state_t state; };
void ecrt_domain_state(ec_domain_t *domain, ec_domain_state_t *state) {
  *state = domain->state;
}
struct ecmcEcDomain {
  ec_domain_t storage;
  ec_domain_t *domain_ = &storage;
  ec_domain_state_t state_;
  int notOKCounter_ = 0, notOKCyclesLimit_ = 0, notOKCounterMax_ = 0;
  unsigned notOKCounterTotal_ = 0;
  int statusOk_ = 0, allowOffLine_ = 0;
  uint32_t statusWord_ = 0, statusWordOld_ = 0;
  int checkState();
  int getOK();
  int setFailedCyclesLimitInterlock(int);
  int getAllowOffline();
};
struct ecmcEc {
  ecmcEcDomain *domains_[2] = {};
  int domainCounter_ = 0;
  bool domainsOK_ = false, ecStatOk_ = false;
  void checkDomainsState();
};
'''
implementation = ''.join(method(domain, signature) for signature in (
    'int ecmcEcDomain::checkState()', 'int ecmcEcDomain::getOK()',
    'int ecmcEcDomain::setFailedCyclesLimitInterlock(int cycles)',
    'int ecmcEcDomain::getAllowOffline()'))
implementation += method(master, 'void ecmcEc::checkDomainsState()')
checks = r'''
int main() {
  for (int limit : {0, 1, 3}) {
    ecmcEcDomain domain;
    domain.setFailedCyclesLimitInterlock(limit);
    // COMPAT: configuration primes the counter, rather than granting fresh
    // tolerance to a domain which has not yet supplied a healthy sample.
    domain.storage.state.wc_state = EC_WC_ZERO;
    assert(!domain.checkState());
    domain.storage.state.wc_state = EC_WC_COMPLETE;
    assert(domain.checkState() && domain.getOK());
    domain.storage.state.wc_state = EC_WC_INCOMPLETE;
    for (int i = 0; i < limit; ++i) assert(domain.checkState());
    assert(!domain.checkState() && !domain.getOK());
    for (int i = 0; i < 8; ++i) assert(!domain.checkState());
    // DOC: consecutive failures; a good sample permits a new full allowance.
    domain.storage.state.wc_state = EC_WC_COMPLETE;
    assert(domain.checkState());
    domain.storage.state.wc_state = EC_WC_ZERO;
    for (int i = 0; i < limit; ++i) assert(domain.checkState());
    assert(!domain.checkState());
    // COMPAT: resetting the configured limit primes the counter again.
    domain.setFailedCyclesLimitInterlock(limit);
    assert(!domain.checkState());
  }
  ecmcEcDomain required, optional;
  required.storage.state.wc_state = EC_WC_COMPLETE;
  optional.storage.state.wc_state = EC_WC_ZERO;
  optional.allowOffLine_ = 1;
  ecmcEc master;
  master.domains_[0] = &required;
  master.domains_[1] = &optional;
  master.domainCounter_ = 2;
  master.checkDomainsState();
  // COMPAT: offline allowance affects the aggregate, not the entry's domain
  // health. Generic bindings must not replace local validity with the summary.
  assert(master.domainsOK_ && master.ecStatOk_ && !optional.getOK());
  optional.allowOffLine_ = 0;
  master.checkDomainsState();
  assert(!master.domainsOK_ && !master.ecStatOk_);
  puts("Domain health: consecutive-failure allowance, recovery, startup priming and offline summary passed.");
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp, exe = Path(directory) / 'health.cpp', Path(directory) / 'health'
    cpp.write_text('#include <initializer_list>\n' + stubs + implementation + checks)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-O2',
                    '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

#include "SpInc.h"
#include <sys/resource.h>

using namespace Dyninst;
using namespace PatchAPI;
using namespace sp;

SpLock g_lock;

void test_entry(SpPoint* pt) {
  //  SetSegfaultSignal();

  Lock(&g_lock);
	PatchFunction* f = Callee(pt);
  if (!f) return;

	sp_print("%s, tid=%ld", f->name().c_str(), (long)GetThreadId());
  sp::Propel(pt);

  Unlock(&g_lock);
}

AGENT_INIT
void MyAgent() {
  sp::SpAgent::ptr agent = sp::SpAgent::Create();
  InitLock(&g_lock);

  StringSet ss;
  ss.insert("new(");
  ss.insert("delete(");
  ss.insert("std::allocator");
  ss.insert("std::");
  ss.insert("__gnu_cxx::");
  ss.insert("CommandLine::InitFromArgv");
  ss.insert("tc_");
  
  agent->SetFuncsNotToInstrument(ss);
  agent->SetInitEntry("test_entry");
  agent->Go();
}

AGENT_FINI
void DumpOutput() {
}
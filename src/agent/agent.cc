/*
 * Copyright (c) 1996-2011 Barton P. Miller
 *
 * We provide the Paradyn Parallel Performance Tools (below
 * described as "Paradyn") on an AS IS basis, and do not warrant its
 * validity or performance.  We reserve the right to update, modify,
 * or discontinue this software at any time.  We shall have no
 * obligation to supply such updates or modifications or any other
 * form of support to you.
 *
 * By your use of Paradyn, you understand and agree that we (or any
 * other person or entity with proprietary rights in Paradyn) are
 * under no obligation to provide either maintenance services,
 * update services, notices of latent defects, or correction of
 * defects for Paradyn.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <sys/resource.h>

#include "agent/agent.h"
#include "agent/context.h"
#include "agent/patchapi/addr_space.h"
#include "common/utils.h"
#include "injector/injector.h"

#include "patchAPI/h/PatchMgr.h"

namespace sp {

	// The only definition of global variables
	SpAddrSpace*  g_as = NULL;
  SpContext*    g_context = NULL;
	SpParser::ptr g_parser;
	SpLock*       g_propel_lock = NULL;

	// Constructor for SpAgent
	SpAgent::ptr
	SpAgent::Create() {

		// Enable core dump.
		if (getenv("SP_COREDUMP")) {
			struct rlimit core_limit;
			core_limit.rlim_cur = RLIM_INFINITY;
			core_limit.rlim_max = RLIM_INFINITY;
			if (setrlimit(RLIMIT_CORE, &core_limit) < 0) {
				sp_perror("ERROR: failed to setup core dump ability\n");
			}
		}
		return ptr(new SpAgent());
	}

	SpAgent::SpAgent() {

		parser_ = SpParser::ptr();
		init_event_ = SpEvent::ptr();
		fini_event_ = SpEvent::ptr();

		allow_ipc_ = false;
		trap_only_ = false;
		parse_only_ = false;
		directcall_only_ = false;
	}

	SpAgent::~SpAgent() {
	}

	// Configuration
	void
	SpAgent::SetParser(SpParser::ptr parser) {
		parser_ = parser;
	}

	void
	SpAgent::SetInitEvent(SpEvent::ptr e) {
		init_event_ = e;
	}

	void
	SpAgent::SetFiniEvent(SpEvent::ptr e) {
		init_event_ = e;
	}

	void
	SpAgent::SetInitEntry(string p) {
		init_entry_ = p;
	}

	void
	SpAgent::SetInitExit(string p) {
		init_exit_ = p;
	}

	void
	SpAgent::SetInitPropeller(SpPropeller::ptr p) {
		init_propeller_ = p;
	}

	void
	SpAgent::EnableParseOnly(bool b) {
		parse_only_ = b;
	}

	void
	SpAgent::EnableDirectcallOnly(bool b) {
		directcall_only_ = b;
	}

	void
	SpAgent::EnableIpc(bool b) {
		allow_ipc_ = b;
	}

	void
	SpAgent::EnableTrapOnly(bool b) {
		trap_only_ = b;
	}

  void
  SpAgent::SetLibrariesToInstrument(const StringSet& libs) {
    for (StringSet::iterator i = libs.begin(); i != libs.end(); i++)
      libs_to_inst_.insert(*i);
  }

	// Here We Go! Self-propelling magic happens!

	void
	SpAgent::Go() {
		sp_debug("==== Start Self-propelled instrumentation @ Process %d ====",
						 getpid());

		// XXX: ignore bash/lsof/Injector for now ...
		if (sp::IsIllegalProgram()) {
			sp_debug("ILLEGAL EXE - avoid instrumenting %s", GetExeName());
			return;
		}

		// Init lock
    // This variable will be freed in SpContext::~SpContext
    g_propel_lock = new SpLock;
		InitLock(g_propel_lock);

    // For quick debugging
		if (getenv("SP_DIRECTCALL_ONLY")) {
      directcall_only_ = true;
    }
    
		if (getenv("SP_TRAP")) {
      trap_only_ = true;
    }

		// Sanity check. If not user-provided configuration, use default ones
		if (!init_event_) {
			sp_debug("INIT EVENT - Use default event");
			init_event_ = SyncEvent::Create();
		}
		if (!fini_event_) {
			sp_debug("FINI EVENT - Use default event");
			fini_event_ = SpEvent::Create();
		}
		if (init_entry_.size() == 0) {
			sp_debug("ENTRY_PAYLOAD - Use default payload entry calls");
			init_entry_ = "default_entry";
		}
		if (init_exit_.size() == 0) {
			sp_debug("EXIT_PAYLOAD - No payload exit calls");
			init_exit_ = "";
		}
		if (!parser_) {
			sp_debug("PARSER - Use default parser");
			parser_ = SpParser::create();
		}
		if (!init_propeller_) {
			sp_debug("PROPELLER - Use default propeller");
			init_propeller_ = SpPropeller::create();
		}

		if (directcall_only_) {
			sp_debug("DIRECT CALL ONLY - only instrument direct calls,"
               " ignoring indirect calls");
		} else {
			sp_debug("DIRECT/INDIRECT CALL - instrument both direct and"
               " indirect calls");
		}
		if (allow_ipc_) {
			sp_debug("MULTI PROCESS - support multiprocess instrumentation");
		} else {
			sp_debug("SINGLE PROCESS - only support single-process "
               "instrumentation");
		}
		if (trap_only_) {
			sp_debug("TRAP ONLY - Only use trap-based instrumentation");
		} else {
			sp_debug("JUMP + TRAP - Use jump and trap for instrumentation");
		}

		// Set up globally unique parser
    // The parser will be freed automatically, because it is a shared ptr
		g_parser = parser_;
		assert(g_parser);
    g_parser->SetLibrariesToInstrument(libs_to_inst_);
    
		// Prepare context
    // XXX: this never gets freed ... should free it when unloading
    //      this shared agent library?
		g_context = SpContext::create(init_propeller_,
																	init_entry_,
																	init_exit_,
																	parser_);
		assert(g_context && g_parser->mgr());

    // Set up globally unique address space object
    // This will be freed in SpContext::~SpContext
		g_as = AS_CAST(g_parser->mgr()->as());
		assert(g_as);

		g_context->set_directcall_only(directcall_only_);
		g_context->set_allow_ipc(allow_ipc_);

    // We may stop here after parsing
		if (parse_only_) {
			sp_debug("PARSE ONLY - exit after parsing, without instrumentation");
			return;
		}

		// Register Events
		init_event_->RegisterEvent();
		fini_event_->RegisterEvent();
	}

}

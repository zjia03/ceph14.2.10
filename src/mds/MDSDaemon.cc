// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include <unistd.h>

#include "include/compat.h"
#include "include/types.h"
#include "include/str_list.h"

#include "common/Clock.h"
#include "common/HeartbeatMap.h"
#include "common/Timer.h"
#include "common/ceph_argparse.h"
#include "common/config.h"
#include "common/entity_name.h"
#include "common/errno.h"
#include "common/perf_counters.h"
#include "common/signal.h"
#include "common/version.h"

#include "global/signal_handler.h"

#include "msg/Messenger.h"
#include "mon/MonClient.h"

#include "osdc/Objecter.h"

#include "MDSMap.h"

#include "MDSDaemon.h"
#include "Server.h"
#include "Locker.h"

#include "SnapServer.h"
#include "SnapClient.h"

#include "events/ESession.h"
#include "events/ESubtreeMap.h"

#include "auth/AuthAuthorizeHandler.h"
#include "auth/RotatingKeyRing.h"
#include "auth/KeyRing.h"

#include "perfglue/cpu_profiler.h"
#include "perfglue/heap_profiler.h"

#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_mds
#undef dout_prefix
#define dout_prefix *_dout << "mds." << name << ' '

// cons/des
MDSDaemon::MDSDaemon(std::string_view n, Messenger *m, MonClient *mc) :
  Dispatcher(m->cct),
  mds_lock("MDSDaemon::mds_lock"),
  stopping(false),
  timer(m->cct, mds_lock),
  gss_ktfile_client(m->cct->_conf.get_val<std::string>("gss_ktab_client_file")),
  beacon(m->cct, mc, n),
  name(n),
  messenger(m),
  monc(mc),
  mgrc(m->cct, m),
  log_client(m->cct, messenger, &mc->monmap, LogClient::NO_FLAGS),
  mds_rank(NULL),
  asok_hook(NULL),
  starttime(mono_clock::now())
{
  orig_argc = 0;
  orig_argv = NULL;

  clog = log_client.create_channel();
  if (!gss_ktfile_client.empty()) {
    // Assert we can export environment variable 
    /* 
        The default client keytab is used, if it is present and readable,
        to automatically obtain initial credentials for GSSAPI client
        applications. The principal name of the first entry in the client
        keytab is used by default when obtaining initial credentials.
        1. The KRB5_CLIENT_KTNAME environment variable.
        2. The default_client_keytab_name profile variable in [libdefaults].
        3. The hardcoded default, DEFCKTNAME.
    */
    const int32_t set_result(setenv("KRB5_CLIENT_KTNAME", 
                                    gss_ktfile_client.c_str(), 1));
    ceph_assert(set_result == 0);
  }

  monc->set_messenger(messenger);

  mdsmap.reset(new MDSMap);
}

MDSDaemon::~MDSDaemon() {
  std::lock_guard lock(mds_lock);

  delete mds_rank;
  mds_rank = NULL;
}

class MDSSocketHook : public AdminSocketHook {
  MDSDaemon *mds;
public:
  explicit MDSSocketHook(MDSDaemon *m) : mds(m) {}
  bool call(std::string_view command, const cmdmap_t& cmdmap,
	    std::string_view format, bufferlist& out) override {
    stringstream ss;
    bool r = mds->asok_command(command, cmdmap, format, ss);
    out.append(ss);
    return r;
  }
};

bool MDSDaemon::asok_command(std::string_view command, const cmdmap_t& cmdmap,
			     std::string_view format, std::ostream& ss)
{
  dout(1) << "asok_command: " << command << " (starting...)" << dendl;

  Formatter *f = Formatter::create(format, "json-pretty", "json-pretty");
  bool handled = false;
  if (command == "status") {
    dump_status(f);
    handled = true;
  } else {
    if (mds_rank == NULL) {
      dout(1) << "Can't run that command on an inactive MDS!" << dendl;
      f->dump_string("error", "mds_not_active");
    } else {
      try {
	handled = mds_rank->handle_asok_command(command, cmdmap, f, ss);
      } catch (const bad_cmd_get& e) {
	ss << e.what();
      }
    }
  }
  f->flush(ss);
  delete f;

  dout(1) << "asok_command: " << command << " (complete)" << dendl;

  return handled;
}

void MDSDaemon::dump_status(Formatter *f)
{
  f->open_object_section("status");
  f->dump_stream("cluster_fsid") << monc->get_fsid();
  if (mds_rank) {
    f->dump_int("whoami", mds_rank->get_nodeid());
  } else {
    f->dump_int("whoami", MDS_RANK_NONE);
  }

  f->dump_int("id", monc->get_global_id());
  f->dump_string("want_state", ceph_mds_state_name(beacon.get_want_state()));
  f->dump_string("state", ceph_mds_state_name(mdsmap->get_state_gid(mds_gid_t(
	    monc->get_global_id()))));
  if (mds_rank) {
    std::lock_guard l(mds_lock);
    mds_rank->dump_status(f);
  }

  f->dump_unsigned("mdsmap_epoch", mdsmap->get_epoch());
  if (mds_rank) {
    f->dump_unsigned("osdmap_epoch", mds_rank->get_osd_epoch());
    f->dump_unsigned("osdmap_epoch_barrier", mds_rank->get_osd_epoch_barrier());
  } else {
    f->dump_unsigned("osdmap_epoch", 0);
    f->dump_unsigned("osdmap_epoch_barrier", 0);
  }

  f->dump_float("uptime", get_uptime().count());

  f->close_section(); // status
}

void MDSDaemon::set_up_admin_socket()
{
  int r;
  AdminSocket *admin_socket = g_ceph_context->get_admin_socket();
  ceph_assert(asok_hook == nullptr);
  asok_hook = new MDSSocketHook(this);
  r = admin_socket->register_command("status", "status", asok_hook,
				     "high-level status of MDS");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dump_ops_in_flight",
				     "dump_ops_in_flight", asok_hook,
				     "show the ops currently in flight");
  ceph_assert(r == 0);
  r = admin_socket->register_command("ops",
				     "ops", asok_hook,
				     "show the ops currently in flight");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dump_blocked_ops", "dump_blocked_ops",
      asok_hook,
      "show the blocked ops currently in flight");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dump_historic_ops", "dump_historic_ops",
				     asok_hook,
				     "show recent ops");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dump_historic_ops_by_duration", "dump_historic_ops_by_duration",
				     asok_hook,
				     "show recent ops, sorted by op duration");
  ceph_assert(r == 0);
  r = admin_socket->register_command("scrub_path",
				     "scrub_path name=path,type=CephString "
				     "name=scrubops,type=CephChoices,"
				     "strings=force|recursive|repair,n=N,req=false",
                                     asok_hook,
                                     "scrub an inode and output results");
  ceph_assert(r == 0);
  r = admin_socket->register_command("tag path",
                                     "tag path name=path,type=CephString"
                                     " name=tag,type=CephString",
                                     asok_hook,
                                     "Apply scrub tag recursively");
   ceph_assert(r == 0);
  r = admin_socket->register_command("flush_path",
                                     "flush_path name=path,type=CephString",
                                     asok_hook,
                                     "flush an inode (and its dirfrags)");
  ceph_assert(r == 0);
  r = admin_socket->register_command("export dir",
                                     "export dir "
                                     "name=path,type=CephString "
                                     "name=rank,type=CephInt",
                                     asok_hook,
                                     "migrate a subtree to named MDS");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dump cache",
                                     "dump cache name=path,type=CephString,req=false",
                                     asok_hook,
                                     "dump metadata cache (optionally to a file)");
  ceph_assert(r == 0);
  r = admin_socket->register_command("cache status",
                                     "cache status",
                                     asok_hook,
                                     "show cache status");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dump tree",
				     "dump tree "
				     "name=root,type=CephString,req=true "
				     "name=depth,type=CephInt,req=false ",
				     asok_hook,
				     "dump metadata cache for subtree");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dump loads",
                                     "dump loads",
                                     asok_hook,
                                     "dump metadata loads");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dump snaps",
                                     "dump snaps name=server,type=CephChoices,strings=--server,req=false",
                                     asok_hook,
                                     "dump snapshots");
  ceph_assert(r == 0);
  r = admin_socket->register_command("session evict",
				     "session evict name=client_id,type=CephString",
				     asok_hook,
				     "Evict a CephFS client");
  ceph_assert(r == 0);
  r = admin_socket->register_command("session ls",
				     "session ls",
				     asok_hook,
				     "Enumerate connected CephFS clients");
  ceph_assert(r == 0);
  r = admin_socket->register_command("session config",
				     "session config name=client_id,type=CephInt,req=true "
				     "name=option,type=CephString,req=true "
				     "name=value,type=CephString,req=false ",
				     asok_hook,
				     "Config a CephFS client session");
  assert(r == 0);
  r = admin_socket->register_command("osdmap barrier",
				     "osdmap barrier name=target_epoch,type=CephInt",
				     asok_hook,
				     "Wait until the MDS has this OSD map epoch");
  ceph_assert(r == 0);
  r = admin_socket->register_command("flush journal",
				     "flush journal",
				     asok_hook,
				     "Flush the journal to the backing store");
  ceph_assert(r == 0);
  r = admin_socket->register_command("force_readonly",
				     "force_readonly",
				     asok_hook,
				     "Force MDS to read-only mode");
  ceph_assert(r == 0);
  r = admin_socket->register_command("get subtrees",
				     "get subtrees",
				     asok_hook,
				     "Return the subtree map");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dirfrag split",
				     "dirfrag split "
                                     "name=path,type=CephString,req=true "
                                     "name=frag,type=CephString,req=true "
                                     "name=bits,type=CephInt,req=true ",
				     asok_hook,
				     "Fragment directory by path");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dirfrag merge",
				     "dirfrag merge "
                                     "name=path,type=CephString,req=true "
                                     "name=frag,type=CephString,req=true",
				     asok_hook,
				     "De-fragment directory by path");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dirfrag ls",
				     "dirfrag ls "
                                     "name=path,type=CephString,req=true",
				     asok_hook,
				     "List fragments in directory");
  ceph_assert(r == 0);
  r = admin_socket->register_command("openfiles ls",
                                     "openfiles ls",
                                     asok_hook,
                                     "List the opening files and their caps");
  ceph_assert(r == 0);
  r = admin_socket->register_command("dump inode",
				     "dump inode " 
                                     "name=number,type=CephInt,req=true",
				     asok_hook,
				     "dump inode by inode number");
  ceph_assert(r == 0);
}

void MDSDaemon::clean_up_admin_socket()
{
  g_ceph_context->get_admin_socket()->unregister_commands(asok_hook);
  delete asok_hook;
  asok_hook = NULL;
}

int MDSDaemon::init()
{
  dout(10) << sizeof(MDSCacheObject) << "\tMDSCacheObject" << dendl;
  dout(10) << sizeof(CInode) << "\tCInode" << dendl;
  dout(10) << sizeof(elist<void*>::item) << "\t elist<>::item   *7=" << 7*sizeof(elist<void*>::item) << dendl;
  dout(10) << sizeof(CInode::mempool_inode) << "\t inode  " << dendl;
  dout(10) << sizeof(CInode::mempool_old_inode) << "\t old_inode " << dendl;
  dout(10) << sizeof(nest_info_t) << "\t  nest_info_t " << dendl;
  dout(10) << sizeof(frag_info_t) << "\t  frag_info_t " << dendl;
  dout(10) << sizeof(SimpleLock) << "\t SimpleLock   *5=" << 5*sizeof(SimpleLock) << dendl;
  dout(10) << sizeof(ScatterLock) << "\t ScatterLock  *3=" << 3*sizeof(ScatterLock) << dendl;
  dout(10) << sizeof(CDentry) << "\tCDentry" << dendl;
  dout(10) << sizeof(elist<void*>::item) << "\t elist<>::item" << dendl;
  dout(10) << sizeof(SimpleLock) << "\t SimpleLock" << dendl;
  dout(10) << sizeof(CDir) << "\tCDir " << dendl;
  dout(10) << sizeof(elist<void*>::item) << "\t elist<>::item   *2=" << 2*sizeof(elist<void*>::item) << dendl;
  dout(10) << sizeof(fnode_t) << "\t fnode_t " << dendl;
  dout(10) << sizeof(nest_info_t) << "\t  nest_info_t *2" << dendl;
  dout(10) << sizeof(frag_info_t) << "\t  frag_info_t *2" << dendl;
  dout(10) << sizeof(Capability) << "\tCapability " << dendl;
  dout(10) << sizeof(xlist<void*>::item) << "\t xlist<>::item   *2=" << 2*sizeof(xlist<void*>::item) << dendl;

  messenger->add_dispatcher_tail(&beacon);
  messenger->add_dispatcher_tail(this);

  // init monc
  monc->set_messenger(messenger);

  monc->set_want_keys(CEPH_ENTITY_TYPE_MON | CEPH_ENTITY_TYPE_OSD |
                      CEPH_ENTITY_TYPE_MDS | CEPH_ENTITY_TYPE_MGR);
  int r = 0;
  r = monc->init();
  if (r < 0) {
    derr << "ERROR: failed to init monc: " << cpp_strerror(-r) << dendl;
    mds_lock.Lock();
    suicide();
    mds_lock.Unlock();
    return r;
  }

  messenger->set_auth_client(monc);
  messenger->set_auth_server(monc);
  monc->set_handle_authentication_dispatcher(this);

  // tell monc about log_client so it will know about mon session resets
  monc->set_log_client(&log_client);

  r = monc->authenticate();
  if (r < 0) {
    derr << "ERROR: failed to authenticate: " << cpp_strerror(-r) << dendl;
    mds_lock.Lock();
    suicide();
    mds_lock.Unlock();
    return r;
  }

  int rotating_auth_attempts = 0;
  auto rotating_auth_timeout =
    g_conf().get_val<int64_t>("rotating_keys_bootstrap_timeout");
  while (monc->wait_auth_rotating(rotating_auth_timeout) < 0) {
    if (++rotating_auth_attempts <= g_conf()->max_rotating_auth_attempts) {
      derr << "unable to obtain rotating service keys; retrying" << dendl;
      continue;
    }
    derr << "ERROR: failed to refresh rotating keys, "
         << "maximum retry time reached." << dendl;
    mds_lock.Lock();
    suicide();
    mds_lock.Unlock();
    return -ETIMEDOUT;
  }

  mds_lock.Lock();
  if (beacon.get_want_state() == CEPH_MDS_STATE_DNE) {
    dout(4) << __func__ << ": terminated already, dropping out" << dendl;
    mds_lock.Unlock();
    return 0;
  }

  monc->sub_want("mdsmap", 0, 0);
  monc->renew_subs();

  mds_lock.Unlock();

  // Set up admin socket before taking mds_lock, so that ordering
  // is consistent (later we take mds_lock within asok callbacks)
  set_up_admin_socket();
  mds_lock.Lock();
  if (beacon.get_want_state() == MDSMap::STATE_DNE) {
    suicide();  // we could do something more graceful here
    dout(4) << __func__ << ": terminated already, dropping out" << dendl;
    mds_lock.Unlock();
    return 0; 
  }

  timer.init();

  beacon.init(*mdsmap);
  messenger->set_myname(entity_name_t::MDS(MDS_RANK_NONE));

  // schedule tick
  reset_tick();
  mds_lock.Unlock();

  return 0;
}

void MDSDaemon::reset_tick()
{
  // cancel old
  if (tick_event) timer.cancel_event(tick_event);

  // schedule
  tick_event = timer.add_event_after(
    g_conf()->mds_tick_interval,
    new FunctionContext([this](int) {
	ceph_assert(mds_lock.is_locked_by_me());
	tick();
      }));
}

void MDSDaemon::tick()
{
  // reschedule
  reset_tick();

  // Call through to subsystems' tick functions
  if (mds_rank) {
    mds_rank->tick();
  }
}

void MDSDaemon::send_command_reply(const MCommand::const_ref &m, MDSRank *mds_rank,
				   int r, bufferlist outbl,
				   std::string_view outs)
{
  auto priv = m->get_connection()->get_priv();
  auto session = static_cast<Session *>(priv.get());
  ceph_assert(session != NULL);
  // If someone is using a closed session for sending commands (e.g.
  // the ceph CLI) then we should feel free to clean up this connection
  // as soon as we've sent them a response.
  const bool live_session =
    session->get_state_seq() > 0 &&
    mds_rank &&
    mds_rank->sessionmap.get_session(session->info.inst.name);

  if (!live_session) {
    // This session only existed to issue commands, so terminate it
    // as soon as we can.
    ceph_assert(session->is_closed());
    session->get_connection()->mark_disposable();
  }
  priv.reset();

  auto reply = MCommandReply::create(r, outs);
  reply->set_tid(m->get_tid());
  reply->set_data(outbl);
  m->get_connection()->send_message2(reply);
}

void MDSDaemon::handle_command(const MCommand::const_ref &m)
{
  auto priv = m->get_connection()->get_priv();
  auto session = static_cast<Session *>(priv.get());
  ceph_assert(session != NULL);

  int r = 0;
  cmdmap_t cmdmap;
  std::stringstream ss;
  std::string outs;
  bufferlist outbl;
  Context *run_after = NULL;
  bool need_reply = true;

  if (!session->auth_caps.allow_all()) {
    dout(1) << __func__
      << ": received command from client without `tell` capability: "
      << *m->get_connection()->peer_addrs << dendl;

    ss << "permission denied";
    r = -EPERM;
  } else if (m->cmd.empty()) {
    r = -EINVAL;
    ss << "no command given";
    outs = ss.str();
  } else if (!cmdmap_from_json(m->cmd, &cmdmap, ss)) {
    r = -EINVAL;
    outs = ss.str();
  } else {
    try {
      r = _handle_command(cmdmap, m, &outbl, &outs, &run_after, &need_reply);
    } catch (const bad_cmd_get& e) {
      outs = e.what();
      r = -EINVAL;
    }
  }
  priv.reset();

  if (need_reply) {
    send_command_reply(m, mds_rank, r, outbl, outs);
  }

  if (run_after) {
    run_after->complete(0);
  }
}

const std::vector<MDSDaemon::MDSCommand>& MDSDaemon::get_commands()
{
  static const std::vector<MDSCommand> commands = {
    MDSCommand("injectargs name=injected_args,type=CephString,n=N", "inject configuration arguments into running MDS"),
    MDSCommand("config set name=key,type=CephString name=value,type=CephString", "Set a configuration option at runtime (not persistent)"),
    MDSCommand("config unset name=key,type=CephString", "Unset a configuration option at runtime (not persistent)"),
    MDSCommand("exit", "Terminate this MDS"),
    MDSCommand("respawn", "Restart this MDS"),
    MDSCommand("session kill name=session_id,type=CephInt", "End a client session"),
    MDSCommand("cpu_profiler name=arg,type=CephChoices,strings=status|flush", "run cpu profiling on daemon"),
    MDSCommand("session ls name=filters,type=CephString,n=N,req=false", "List client sessions"),
    MDSCommand("client ls name=filters,type=CephString,n=N,req=false", "List client sessions"),
    MDSCommand("session evict name=filters,type=CephString,n=N,req=false", "Evict client session(s)"),
    MDSCommand("client evict name=filters,type=CephString,n=N,req=false", "Evict client session(s)"),
    MDSCommand("session config name=client_id,type=CephInt name=option,type=CephString name=value,type=CephString,req=false",
	"Config a client session"),
    MDSCommand("client config name=client_id,type=CephInt name=option,type=CephString name=value,type=CephString,req=false",
	"Config a client session"),
    MDSCommand("damage ls", "List detected metadata damage"),
    MDSCommand("damage rm name=damage_id,type=CephInt", "Remove a damage table entry"),
    MDSCommand("version", "report version of MDS"),
    MDSCommand("heap "
        "name=heapcmd,type=CephChoices,strings=dump|start_profiler|stop_profiler|release|stats",
        "show heap usage info (available only if compiled with tcmalloc)"),
    MDSCommand("cache drop name=timeout,type=CephInt,range=0,req=false", "trim cache and optionally request client to release all caps and flush the journal"),
    MDSCommand("scrub start name=path,type=CephString name=scrubops,type=CephChoices,strings=force|recursive|repair,n=N,req=false name=tag,type=CephString,req=false",
               "scrub an inode and output results"),
    MDSCommand("scrub abort", "Abort in progress scrub operation(s)"),
    MDSCommand("scrub pause", "Pause in progress scrub operation(s)"),
    MDSCommand("scrub resume", "Resume paused scrub operation(s)"),
    MDSCommand("scrub status", "Status of scrub operation"),
  };
  return commands;
};

int MDSDaemon::_handle_command(
    const cmdmap_t &cmdmap,
    const MCommand::const_ref &m,
    bufferlist *outbl,
    std::string *outs,
    Context **run_later,
    bool *need_reply)
{
  ceph_assert(outbl != NULL);
  ceph_assert(outs != NULL);

  class SuicideLater : public Context
  {
    MDSDaemon *mds;

    public:
    explicit SuicideLater(MDSDaemon *mds_) : mds(mds_) {}
    void finish(int r) override {
      // Wait a little to improve chances of caller getting
      // our response before seeing us disappear from mdsmap
      sleep(1);

      mds->suicide();
    }
  };


  class RespawnLater : public Context
  {
    MDSDaemon *mds;

    public:

    explicit RespawnLater(MDSDaemon *mds_) : mds(mds_) {}
    void finish(int r) override {
      // Wait a little to improve chances of caller getting
      // our response before seeing us disappear from mdsmap
      sleep(1);

      mds->respawn();
    }
  };

  std::stringstream ds;
  std::stringstream ss;
  std::string prefix;
  std::string format;
  std::unique_ptr<Formatter> f(Formatter::create(format));
  cmd_getval(cct, cmdmap, "prefix", prefix);

  int r = 0;

  if (prefix == "get_command_descriptions") {
    int cmdnum = 0;
    std::unique_ptr<JSONFormatter> f(std::make_unique<JSONFormatter>());
    f->open_object_section("command_descriptions");
    for (auto& c : get_commands()) {
      ostringstream secname;
      secname << "cmd" << setfill('0') << std::setw(3) << cmdnum;
      dump_cmddesc_to_json(f.get(), m->get_connection()->get_features(),
                           secname.str(), c.cmdstring, c.helpstring,
			   c.module, "*", 0);
      cmdnum++;
    }
    f->close_section();	// command_descriptions

    f->flush(ds);
    goto out; 
  }

  cmd_getval(cct, cmdmap, "format", format);
  if (prefix == "version") {
    if (f) {
      f->open_object_section("version");
      f->dump_string("version", pretty_version_to_str());
      f->close_section();
      f->flush(ds);
    } else {
      ds << pretty_version_to_str();
    }
  } else if (prefix == "injectargs") {
    vector<string> argsvec;
    cmd_getval(cct, cmdmap, "injected_args", argsvec);

    if (argsvec.empty()) {
      r = -EINVAL;
      ss << "ignoring empty injectargs";
      goto out;
    }
    string args = argsvec.front();
    for (vector<string>::iterator a = ++argsvec.begin(); a != argsvec.end(); ++a)
      args += " " + *a;
    r = cct->_conf.injectargs(args, &ss);
  } else if (prefix == "config set") {
    std::string key;
    cmd_getval(cct, cmdmap, "key", key);
    std::string val;
    cmd_getval(cct, cmdmap, "value", val);
    r = cct->_conf.set_val(key, val, &ss);
    if (r == 0) {
      cct->_conf.apply_changes(nullptr);
    }
  } else if (prefix == "config unset") {
    std::string key;
    cmd_getval(cct, cmdmap, "key", key);
    r = cct->_conf.rm_val(key);
    if (r == 0) {
      cct->_conf.apply_changes(nullptr);
    }
    if (r == -ENOENT) {
      r = 0; // idempotent
    }
  } else if (prefix == "exit") {
    // We will send response before executing
    ss << "Exiting...";
    *run_later = new SuicideLater(this);
  } else if (prefix == "respawn") {
    // We will send response before executing
    ss << "Respawning...";
    *run_later = new RespawnLater(this);
  } else if (prefix == "session kill") {
    if (mds_rank == NULL) {
      r = -EINVAL;
      ss << "MDS not active";
      goto out;
    }
    // FIXME harmonize `session kill` with admin socket session evict
    int64_t session_id = 0;
    bool got = cmd_getval(cct, cmdmap, "session_id", session_id);
    ceph_assert(got);
    bool killed = mds_rank->evict_client(session_id, false,
                                         g_conf()->mds_session_blacklist_on_evict,
                                         ss);
    if (!killed)
      r = -ENOENT;
  } else if (prefix == "heap") {
    if (!ceph_using_tcmalloc()) {
      r = -EOPNOTSUPP;
      ss << "could not issue heap profiler command -- not using tcmalloc!";
    } else {
      string heapcmd;
      cmd_getval(cct, cmdmap, "heapcmd", heapcmd);
      vector<string> heapcmd_vec;
      get_str_vec(heapcmd, heapcmd_vec);
      string value;
      if (cmd_getval(cct, cmdmap, "value", value))
	 heapcmd_vec.push_back(value);
      ceph_heap_profiler_handle_command(heapcmd_vec, ds);
    }
  } else if (prefix == "cpu_profiler") {
    string arg;
    cmd_getval(cct, cmdmap, "arg", arg);
    vector<string> argvec;
    get_str_vec(arg, argvec);
    cpu_profiler_handle_command(argvec, ds);
  } else {
    // Give MDSRank a shot at the command
    if (!mds_rank) {
      ss << "MDS not active";
      r = -EINVAL;
    }
    else {
      bool handled;
      try {
        handled = mds_rank->handle_command(cmdmap, m, &r, &ds, &ss,
                                           run_later, need_reply);
	if (!handled) {
	  // MDSDaemon doesn't know this command
	  ss << "unrecognized command! " << prefix;
	  r = -EINVAL;
	}
      } catch (const bad_cmd_get& e) {
	ss << e.what();
	r = -EINVAL;
      }
    }
  }

out:
  *outs = ss.str();
  outbl->append(ds);
  return r;
}

void MDSDaemon::handle_mds_map(const MMDSMap::const_ref &m)
{
  version_t epoch = m->get_epoch();

  // is it new?
  if (epoch <= mdsmap->get_epoch()) {
    dout(5) << "handle_mds_map old map epoch " << epoch << " <= "
            << mdsmap->get_epoch() << ", discarding" << dendl;
    return;
  }

  dout(1) << "Updating MDS map to version " << epoch << " from " << m->get_source() << dendl;

  // keep old map, for a moment
  std::unique_ptr<MDSMap> oldmap;
  oldmap.swap(mdsmap);

  // decode and process
  mdsmap.reset(new MDSMap);
  mdsmap->decode(m->get_encoded());

  monc->sub_got("mdsmap", mdsmap->get_epoch());

  // verify compatset
  CompatSet mdsmap_compat(MDSMap::get_compat_set_all());
  dout(10) << "     my compat " << mdsmap_compat << dendl;
  dout(10) << " mdsmap compat " << mdsmap->compat << dendl;
  if (!mdsmap_compat.writeable(mdsmap->compat)) {
    dout(0) << "handle_mds_map mdsmap compatset " << mdsmap->compat
	    << " not writeable with daemon features " << mdsmap_compat
	    << ", killing myself" << dendl;
    suicide();
    return;
  }

  // Calculate my effective rank (either my owned rank or the rank I'm following if STATE_STANDBY_REPLAY
  const auto addrs = messenger->get_myaddrs();
  const auto myid = monc->get_global_id();
  const auto mygid = mds_gid_t(myid);
  const auto whoami = mdsmap->get_rank_gid(mygid);
  const auto old_state = oldmap->get_state_gid(mygid);
  const auto new_state = mdsmap->get_state_gid(mygid);
  const auto incarnation = mdsmap->get_inc_gid(mygid);
  dout(10) << "my gid is " << myid << dendl;
  dout(10) << "map says I am mds." << whoami << "." << incarnation
	   << " state " << ceph_mds_state_name(new_state) << dendl;
  dout(10) << "msgr says I am " << addrs << dendl;

  // If we're removed from the MDSMap, stop all processing.
  using DS = MDSMap::DaemonState;
  if (old_state != DS::STATE_NULL && new_state == DS::STATE_NULL) {
    const auto& oldinfo = oldmap->get_info_gid(mygid);
    dout(1) << "Map removed me " << oldinfo
            << " from cluster; respawning! See cluster/monitor logs for details." << dendl;
    respawn();
  }

  if (old_state == DS::STATE_NULL && new_state != DS::STATE_NULL) {
    /* The MDS has been added to the FSMap, now we can init the MgrClient */
    mgrc.init();
    messenger->add_dispatcher_tail(&mgrc);
    monc->sub_want("mgrmap", 0, 0);
    monc->renew_subs(); /* MgrMap receipt drives connection to ceph-mgr */
  }

  // mark down any failed peers
  for (const auto& [gid, info] : oldmap->get_mds_info()) {
    if (mdsmap->get_mds_info().count(gid) == 0) {
      dout(10) << " peer mds gid " << gid << " removed from map" << dendl;
      messenger->mark_down_addrs(info.addrs);
    }
  }

  if (whoami == MDS_RANK_NONE) {
    // We do not hold a rank:
    dout(10) <<  __func__ << ": handling map in rankless mode" << dendl;

    if (new_state == DS::STATE_STANDBY) {
      /* Note: STATE_BOOT is never an actual state in the FSMap. The Monitors
       * generally mark a new MDS as STANDBY (although it's possible to
       * immediately be assigned a rank).
       */
      if (old_state == DS::STATE_NULL) {
        dout(1) << "Monitors have assigned me to become a standby." << dendl;
        beacon.set_want_state(*mdsmap, new_state);
      } else if (old_state == DS::STATE_STANDBY) {
        dout(5) << "I am still standby" << dendl;
      }
    } else if (new_state == DS::STATE_NULL) {
      /* We are not in the MDSMap yet! Keep waiting: */
      ceph_assert(beacon.get_want_state() == DS::STATE_BOOT);
      dout(10) << "not in map yet" << dendl;
    } else {
      /* We moved to standby somehow from another state */
      ceph_abort("invalid transition to standby");
    }
  } else {
    // Did we already hold a different rank?  MDSMonitor shouldn't try
    // to change that out from under me!
    if (mds_rank && whoami != mds_rank->get_nodeid()) {
      derr << "Invalid rank transition " << mds_rank->get_nodeid() << "->"
           << whoami << dendl;
      respawn();
    }

    // Did I previously not hold a rank?  Initialize!
    if (mds_rank == NULL) {
      mds_rank = new MDSRankDispatcher(whoami, mds_lock, clog,
          timer, beacon, mdsmap, messenger, monc, &mgrc,
          new FunctionContext([this](int r){respawn();}),
          new FunctionContext([this](int r){suicide();}));
      dout(10) <<  __func__ << ": initializing MDS rank "
               << mds_rank->get_nodeid() << dendl;
      mds_rank->init();
    }

    // MDSRank is active: let him process the map, we have no say.
    dout(10) <<  __func__ << ": handling map as rank "
             << mds_rank->get_nodeid() << dendl;
    mds_rank->handle_mds_map(m, *oldmap);
  }

  beacon.notify_mdsmap(*mdsmap);
}

void MDSDaemon::handle_signal(int signum)
{
  ceph_assert(signum == SIGINT || signum == SIGTERM);
  derr << "*** got signal " << sig_str(signum) << " ***" << dendl;
  {
    std::lock_guard l(mds_lock);
    if (stopping) {
      return;
    }
    suicide();
  }
}

void MDSDaemon::suicide()
{
  ceph_assert(mds_lock.is_locked());
  
  // make sure we don't suicide twice
  ceph_assert(stopping == false);
  stopping = true;

  dout(1) << "suicide! Wanted state "
          << ceph_mds_state_name(beacon.get_want_state()) << dendl;

  if (tick_event) {
    timer.cancel_event(tick_event);
    tick_event = 0;
  }

  clean_up_admin_socket();

  // Inform MDS we are going away, then shut down beacon
  beacon.set_want_state(*mdsmap, MDSMap::STATE_DNE);
  if (!mdsmap->is_dne_gid(mds_gid_t(monc->get_global_id()))) {
    // Notify the MDSMonitor that we're dying, so that it doesn't have to
    // wait for us to go laggy.  Only do this if we're actually in the
    // MDSMap, because otherwise the MDSMonitor will drop our message.
    beacon.send_and_wait(1);
  }
  beacon.shutdown();

  if (mgrc.is_initialized())
    mgrc.shutdown();

  if (mds_rank) {
    mds_rank->shutdown();
  } else {
    timer.shutdown();

    monc->shutdown();
    messenger->shutdown();
  }
}

void MDSDaemon::respawn()
{
  // --- WARNING TO FUTURE COPY/PASTERS ---
  // You must also add a call like
  //
  //   ceph_pthread_setname(pthread_self(), "ceph-mds");
  //
  // to main() so that /proc/$pid/stat field 2 contains "(ceph-mds)"
  // instead of "(exe)", so that killall (and log rotation) will work.

  dout(1) << "respawn!" << dendl;

  /* Dump recent in case the MDS was stuck doing something which caused it to
   * be removed from the MDSMap leading to respawn. */
  g_ceph_context->_log->dump_recent();

  char *new_argv[orig_argc+1];
  dout(1) << " e: '" << orig_argv[0] << "'" << dendl;
  for (int i=0; i<orig_argc; i++) {
    new_argv[i] = (char *)orig_argv[i];
    dout(1) << " " << i << ": '" << orig_argv[i] << "'" << dendl;
  }
  new_argv[orig_argc] = NULL;

  /* Determine the path to our executable, test if Linux /proc/self/exe exists.
   * This allows us to exec the same executable even if it has since been
   * unlinked.
   */
  char exe_path[PATH_MAX] = "";
#ifdef PROCPREFIX
  if (readlink(PROCPREFIX "/proc/self/exe", exe_path, PATH_MAX-1) != -1) {
    dout(1) << "respawning with exe " << exe_path << dendl;
    strcpy(exe_path, PROCPREFIX "/proc/self/exe");
  } else {
#else
  {
#endif
    /* Print CWD for the user's interest */
    char buf[PATH_MAX];
    char *cwd = getcwd(buf, sizeof(buf));
    ceph_assert(cwd);
    dout(1) << " cwd " << cwd << dendl;

    /* Fall back to a best-effort: just running in our CWD */
    strncpy(exe_path, orig_argv[0], PATH_MAX-1);
  }

  dout(1) << " exe_path " << exe_path << dendl;

  unblock_all_signals(NULL);
  execv(exe_path, new_argv);

  dout(0) << "respawn execv " << orig_argv[0]
	  << " failed with " << cpp_strerror(errno) << dendl;

  // We have to assert out here, because suicide() returns, and callers
  // to respawn expect it never to return.
  ceph_abort();
}



bool MDSDaemon::ms_dispatch2(const Message::ref &m)
{
  std::lock_guard l(mds_lock);
  if (stopping) {
    return false;
  }

  // Drop out early if shutting down
  if (beacon.get_want_state() == CEPH_MDS_STATE_DNE) {
    dout(10) << " stopping, discarding " << *m << dendl;
    return true;
  }

  // First see if it's a daemon message
  const bool handled_core = handle_core_message(m);
  if (handled_core) {
    return true;
  }

  // Not core, try it as a rank message
  if (mds_rank) {
    return mds_rank->ms_dispatch(m);
  } else {
    return false;
  }
}

bool MDSDaemon::ms_get_authorizer(int dest_type, AuthAuthorizer **authorizer)
{
  dout(10) << "MDSDaemon::ms_get_authorizer type="
           << ceph_entity_type_name(dest_type) << dendl;

  /* monitor authorization is being handled on different layer */
  if (dest_type == CEPH_ENTITY_TYPE_MON)
    return true;

  *authorizer = monc->build_authorizer(dest_type);
  return *authorizer != NULL;
}


/*
 * high priority messages we always process
 */
bool MDSDaemon::handle_core_message(const Message::const_ref &m)
{
  switch (m->get_type()) {
  case CEPH_MSG_MON_MAP:
    ALLOW_MESSAGES_FROM(CEPH_ENTITY_TYPE_MON);
    break;

    // MDS
  case CEPH_MSG_MDS_MAP:
    ALLOW_MESSAGES_FROM(CEPH_ENTITY_TYPE_MON | CEPH_ENTITY_TYPE_MDS);
    handle_mds_map(MMDSMap::msgref_cast(m));
    break;

    // OSD
  case MSG_COMMAND:
    handle_command(MCommand::msgref_cast(m));
    break;
  case CEPH_MSG_OSD_MAP:
    ALLOW_MESSAGES_FROM(CEPH_ENTITY_TYPE_MON | CEPH_ENTITY_TYPE_OSD);

    if (mds_rank) {
      mds_rank->handle_osd_map();
    }
    break;

  case MSG_MON_COMMAND:
    ALLOW_MESSAGES_FROM(CEPH_ENTITY_TYPE_MON);
    clog->warn() << "dropping `mds tell` command from legacy monitor";
    break;

  default:
    return false;
  }
  return true;
}

void MDSDaemon::ms_handle_connect(Connection *con)
{
}

bool MDSDaemon::ms_handle_reset(Connection *con)
{
  if (con->get_peer_type() != CEPH_ENTITY_TYPE_CLIENT)
    return false;

  std::lock_guard l(mds_lock);
  if (stopping) {
    return false;
  }
  dout(5) << "ms_handle_reset on " << con->get_peer_socket_addr() << dendl;
  if (beacon.get_want_state() == CEPH_MDS_STATE_DNE)
    return false;

  auto priv = con->get_priv();
  if (auto session = static_cast<Session *>(priv.get()); session) {
    if (session->is_closed()) {
      dout(3) << "ms_handle_reset closing connection for session " << session->info.inst << dendl;
      con->mark_down();
      con->set_priv(nullptr);
    }
  } else {
    con->mark_down();
  }
  return false;
}


void MDSDaemon::ms_handle_remote_reset(Connection *con)
{
  if (con->get_peer_type() != CEPH_ENTITY_TYPE_CLIENT)
    return;

  std::lock_guard l(mds_lock);
  if (stopping) {
    return;
  }

  dout(5) << "ms_handle_remote_reset on " << con->get_peer_socket_addr() << dendl;
  if (beacon.get_want_state() == CEPH_MDS_STATE_DNE)
    return;

  auto priv = con->get_priv();
  if (auto session = static_cast<Session *>(priv.get()); session) {
    if (session->is_closed()) {
      dout(3) << "ms_handle_remote_reset closing connection for session " << session->info.inst << dendl;
      con->mark_down();
      con->set_priv(nullptr);
    }
  }
}

bool MDSDaemon::ms_handle_refused(Connection *con)
{
  // do nothing for now
  return false;
}

KeyStore *MDSDaemon::ms_get_auth1_authorizer_keystore()
{
  return monc->rotating_secrets.get();
}

bool MDSDaemon::parse_caps(const AuthCapsInfo& info, MDSAuthCaps& caps)
{
  caps.clear();
  if (info.allow_all) {
    caps.set_allow_all();
    return true;
  } else {
    auto it = info.caps.begin();
    string auth_cap_str;
    try {
      decode(auth_cap_str, it);
    } catch (const buffer::error& e) {
      dout(1) << __func__ << ": cannot decode auth caps buffer of length " << info.caps.length() << dendl;
      return false;
    }

    dout(10) << __func__ << ": parsing auth_cap_str='" << auth_cap_str << "'" << dendl;
    CachedStackStringStream cs;
    if (caps.parse(g_ceph_context, auth_cap_str, cs.get())) {
      return true;
    } else {
      dout(1) << __func__ << ": auth cap parse error: " << cs->strv() << " parsing '" << auth_cap_str << "'" << dendl;
      return false;
    }
  }
}

int MDSDaemon::ms_handle_authentication(Connection *con)
{
  /* N.B. without mds_lock! */
  MDSAuthCaps caps;
  return parse_caps(con->get_peer_caps_info(), caps) ? 0 : -1;
}

void MDSDaemon::ms_handle_accept(Connection *con)
{
  entity_name_t n(con->get_peer_type(), con->get_peer_global_id());
  std::lock_guard l(mds_lock);
  if (stopping) {
    return;
  }

  // We allow connections and assign Session instances to connections
  // even if we have not been assigned a rank, because clients with
  // "allow *" are allowed to connect and do 'tell' operations before
  // we have a rank.
  Session *s = NULL;
  if (mds_rank) {
    // If we do hold a rank, see if this is an existing client establishing
    // a new connection, rather than a new client
    s = mds_rank->sessionmap.get_session(n);
  }

  // Wire up a Session* to this connection
  // It doesn't go into a SessionMap instance until it sends an explicit
  // request to open a session (initial state of Session is `closed`)
  if (!s) {
    s = new Session(con);
    dout(10) << " new session " << s << " for " << s->info.inst
	     << " con " << con << dendl;
    con->set_priv(RefCountedPtr{s, false});
    if (mds_rank) {
      mds_rank->kick_waiters_for_any_client_connection();
    }
  } else {
    dout(10) << " existing session " << s << " for " << s->info.inst
	     << " existing con " << s->get_connection()
	     << ", new/authorizing con " << con << dendl;
    con->set_priv(RefCountedPtr{s});
  }

  parse_caps(con->get_peer_caps_info(), s->auth_caps);

  dout(10) << "ms_handle_accept " << con->get_peer_socket_addr() << " con " << con << " session " << s << dendl;
  if (s) {
    if (s->get_connection() != con) {
      dout(10) << " session connection " << s->get_connection()
	       << " -> " << con << dendl;
      s->set_connection(con);

      // send out any queued messages
      while (!s->preopen_out_queue.empty()) {
	con->send_message2(s->preopen_out_queue.front());
	s->preopen_out_queue.pop_front();
      }
    }
  }
}

bool MDSDaemon::is_clean_shutdown()
{
  if (mds_rank) {
    return mds_rank->is_stopped();
  } else {
    return true;
  }
}

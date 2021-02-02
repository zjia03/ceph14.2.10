// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_MGR_MGRSESSION_H
#define CEPH_MGR_MGRSESSION_H

#include "common/RefCountedObj.h"
#include "common/entity_name.h"
#include "msg/msg_types.h"
#include "MgrCap.h"


/**
 * Session state associated with the Connection.
 */
struct MgrSession : public RefCountedObject {
  uint64_t global_id = 0;
  EntityName entity_name;
  entity_inst_t inst;

  int osd_id = -1;  ///< osd id (if an osd)

  MgrCap caps;

  std::set<std::string> declared_types;

  explicit MgrSession(CephContext *cct) : RefCountedObject(cct, 0) {}
  ~MgrSession() override {}

  const entity_addr_t& get_peer_addr() const {
    return inst.addr;
  }
};

typedef boost::intrusive_ptr<MgrSession> MgrSessionRef;


#endif

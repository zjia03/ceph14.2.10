#ifndef CEPH_RGW_SYNC_MODULE_PUBSUB_REST_H
#define CEPH_RGW_SYNC_MODULE_PUBSUB_REST_H

#include "rgw_rest.h"

class RGWRESTMgr_PubSub : public RGWRESTMgr {
public:
  virtual RGWHandler_REST* get_handler(struct req_state* s,
                               const rgw::auth::StrategyRegistry& auth_registry,
                               const std::string& frontend_prefix) override;
};

#endif

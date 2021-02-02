#ifndef CEPH_RGW_SERVICES_ZONE_H
#define CEPH_RGW_SERVICES_ZONE_H


#include "rgw/rgw_service.h"


class RGWSI_RADOS;
class RGWSI_SysObj;
class RGWSI_SyncModules;

class RGWRealm;
class RGWZoneGroup;
class RGWZone;
class RGWZoneParams;
class RGWPeriod;
class RGWZonePlacementInfo;

class RGWRESTConn;

class RGWSI_Zone : public RGWServiceInstance
{
  friend struct RGWServices_Def;

  RGWSI_SysObj *sysobj_svc{nullptr};
  RGWSI_RADOS *rados_svc{nullptr};
  RGWSI_SyncModules *sync_modules_svc{nullptr};

  RGWRealm *realm{nullptr};
  RGWZoneGroup *zonegroup{nullptr};
  RGWZone *zone_public_config{nullptr}; /* external zone params, e.g., entrypoints, log flags, etc. */  
  RGWZoneParams *zone_params{nullptr}; /* internal zone params, e.g., rados pools */
  RGWPeriod *current_period{nullptr};
  uint32_t zone_short_id{0};
  bool writeable_zone{false};

  RGWRESTConn *rest_master_conn{nullptr};
  map<string, RGWRESTConn *> zone_conn_map;
  std::vector<const RGWZone*> data_sync_source_zones;
  map<string, RGWRESTConn *> zone_data_notify_to_map;
  map<string, RGWRESTConn *> zonegroup_conn_map;

  map<string, string> zone_id_by_name;
  map<string, RGWZone> zone_by_id;

  void init(RGWSI_SysObj *_sysobj_svc,
           RGWSI_RADOS *_rados_svc,
           RGWSI_SyncModules *_sync_modules_svc);
  int do_start() override;
  void shutdown() override;

  int replace_region_with_zonegroup();
  int init_zg_from_period(bool *initialized);
  int init_zg_from_local(bool *creating_defaults);
  int convert_regionmap();

  int update_placement_map();
public:
  RGWSI_Zone(CephContext *cct);
  ~RGWSI_Zone();

  const RGWZoneParams& get_zone_params() const;
  const RGWPeriod& get_current_period() const;
  const RGWRealm& get_realm() const;
  const RGWZoneGroup& get_zonegroup() const;
  int get_zonegroup(const string& id, RGWZoneGroup& zonegroup) const;
  const RGWZone& get_zone() const;

  const string& zone_name();
  const string& zone_id();
  uint32_t get_zone_short_id() const;

  const string& get_current_period_id();
  bool has_zonegroup_api(const std::string& api) const;

  bool zone_is_writeable();
  bool zone_syncs_from(const RGWZone& target_zone, const RGWZone& source_zone) const;
  bool get_redirect_zone_endpoint(string *endpoint);

  RGWRESTConn *get_master_conn() {
    return rest_master_conn;
  }

  map<string, RGWRESTConn *>& get_zonegroup_conn_map() {
    return zonegroup_conn_map;
  }

  map<string, RGWRESTConn *>& get_zone_conn_map() {
    return zone_conn_map;
  }

  std::vector<const RGWZone*>& get_data_sync_source_zones() {
    return data_sync_source_zones;
  }

  map<string, RGWRESTConn *>& get_zone_data_notify_to_map() {
    return zone_data_notify_to_map;
  }

  bool find_zone_by_id(const string& id, RGWZone **zone);

  RGWRESTConn *get_zone_conn_by_id(const string& id);
  RGWRESTConn *get_zone_conn_by_name(const string& name);
  bool find_zone_id_by_name(const string& name, string *id);

  int select_bucket_placement(const RGWUserInfo& user_info, const string& zonegroup_id,
                              const rgw_placement_rule& rule,
                              rgw_placement_rule *pselected_rule, RGWZonePlacementInfo *rule_info);
  int select_legacy_bucket_placement(RGWZonePlacementInfo *rule_info);
  int select_new_bucket_location(const RGWUserInfo& user_info, const string& zonegroup_id,
                                 const rgw_placement_rule& rule,
                                 rgw_placement_rule *pselected_rule_name, RGWZonePlacementInfo *rule_info);
  int select_bucket_location_by_rule(const rgw_placement_rule& location_rule, RGWZonePlacementInfo *rule_info);

  int add_bucket_placement(const rgw_pool& new_pool);
  int remove_bucket_placement(const rgw_pool& old_pool);
  int list_placement_set(set<rgw_pool>& names);

  bool is_meta_master() const;

  bool need_to_log_data() const;
  bool need_to_log_metadata() const;
  bool can_reshard() const;
  bool is_syncing_bucket_meta(const rgw_bucket& bucket);

  int list_zonegroups(list<string>& zonegroups);
  int list_regions(list<string>& regions);
  int list_zones(list<string>& zones);
  int list_realms(list<string>& realms);
  int list_periods(list<string>& periods);
  int list_periods(const string& current_period, list<string>& periods);
};

#endif

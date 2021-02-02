// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2013 Inktank Storage, Inc.
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef ECTRANSACTION_H
#define ECTRANSACTION_H

#include "OSD.h"
#include "PGBackend.h"
#include "ECUtil.h"
#include "erasure-code/ErasureCodeInterface.h"
#include "PGTransaction.h"
#include "ExtentCache.h"
#include "hi_coreutil.h"
namespace ECTransaction {
  struct WritePlan {
    PGTransactionUPtr t;
    bool invalidates_cache = false; // Yes, both are possible
    map<hobject_t,extent_set> to_read;
    map<hobject_t,extent_set> will_write; // superset of to_read
    map<hobject_t,extent_set> to_read_chunk_align;

    map<hobject_t,ECUtil::HashInfoRef> hash_infos;
  };

  bool requires_overwrite(
    uint64_t prev_size,
    const PGTransaction::ObjectOperation &op);

  template <typename F>
  WritePlan get_write_plan(
    bool partial_write,
    const ECUtil::stripe_info_t &sinfo,
    PGTransactionUPtr &&t,
    F &&get_hinfo,
    DoutPrefixProvider *dpp) {
    WritePlan plan;
    t->safe_create_traverse(
      [&](pair<const hobject_t, PGTransaction::ObjectOperation> &i) {
	ECUtil::HashInfoRef hinfo = get_hinfo(i.first);
	plan.hash_infos[i.first] = hinfo;

	uint64_t projected_size =
	  hinfo->get_projected_total_logical_size(sinfo);
	uint64_t chunk_size = sinfo.get_chunk_size();
	ceph_assert(chunk_size);
	ldpp_dout(dpp, 20) << __func__ << ": projected_size=" << projected_size
	  << "  projected_total_chunk_size=" << hinfo->get_projected_total_chunk_size()
	  << "  stripe_width=" << sinfo.get_stripe_width() << "  chunk_size=" << chunk_size
	  << "  partial_write=" << partial_write << dendl;

	if (i.second.deletes_first()) {
	  ldpp_dout(dpp, 20) << __func__ << ": delete, setting projected size"
			     << " to 0" << dendl;
	  projected_size = 0;
	}

	hobject_t source;
	if (i.second.has_source(&source)) {
	  plan.invalidates_cache = true;

	  ECUtil::HashInfoRef shinfo = get_hinfo(source);
	  projected_size = shinfo->get_projected_total_logical_size(sinfo);
	  ldpp_dout(dpp, 20) << __func__ << ": second.has_source projected_size=" << projected_size<<dendl;
	  plan.hash_infos[source] = shinfo;
	}

	auto &will_write = plan.will_write[i.first];
	if (i.second.truncate &&
	    i.second.truncate->first < projected_size) {
	  if (!(sinfo.logical_offset_is_stripe_aligned(
		  i.second.truncate->first))) {
	    plan.to_read[i.first].union_insert(
	      sinfo.logical_to_prev_stripe_offset(i.second.truncate->first),
	      sinfo.get_stripe_width());

	    ldpp_dout(dpp, 20) << __func__ << ": unaligned truncate" << dendl;

	    will_write.union_insert(
	      sinfo.logical_to_prev_stripe_offset(i.second.truncate->first),
	      sinfo.get_stripe_width());
	  }
	  projected_size = sinfo.logical_to_next_stripe_offset(
	    i.second.truncate->first);
	  ldpp_dout(dpp, 20) << __func__ << ": i.second.truncate projected_size=" << projected_size<<dendl;
	}

	extent_set raw_write_set;
	for (auto &&extent: i.second.buffer_updates) {
	  using BufferUpdate = PGTransaction::ObjectOperation::BufferUpdate;
	  if (boost::get<BufferUpdate::CloneRange>(&(extent.get_val()))) {
	    ceph_assert(
	      0 ==
	      "CloneRange is not allowed, do_op should have returned ENOTSUPP");
	  }
	  raw_write_set.insert(extent.get_off(), extent.get_len());
	  ldpp_dout(dpp, 20) << __func__ << ": extent.get_off()=" << extent.get_off() << " extent.get_len()=" << extent.get_len() << dendl;
	}

	auto orig_size = projected_size;
	for (auto extent = raw_write_set.begin();
	     extent != raw_write_set.end();
	     ++extent) {
	  uint64_t head_start =
	    sinfo.logical_to_prev_stripe_offset(extent.get_start());
	  uint64_t head_finish =
	    sinfo.logical_to_next_stripe_offset(extent.get_start());
	  ldpp_dout(dpp, 20) << __func__ << ": head_start=" << head_start << " head_finish=" << head_finish
	   << "  projected_size=" << projected_size << " orig_size=" << orig_size << dendl;
	  if (head_start > projected_size) {
	    head_start = projected_size;
	  }
	  if (head_start != head_finish &&
	      head_start < orig_size) {
	    ceph_assert(head_finish <= orig_size);
	    ceph_assert(head_finish - head_start == sinfo.get_stripe_width());
	    ldpp_dout(dpp, 20) << __func__ << ": reading partial head stripe "
			       << head_start << "~" << sinfo.get_stripe_width()
			       << dendl;
	    plan.to_read[i.first].union_insert(
	      head_start, sinfo.get_stripe_width());
	  }

	  uint64_t tail_start =
	    sinfo.logical_to_prev_stripe_offset(
	      extent.get_start() + extent.get_len());
	  uint64_t tail_finish =
	    sinfo.logical_to_next_stripe_offset(
	      extent.get_start() + extent.get_len());
	  ldpp_dout(dpp, 20) << __func__ <<"tail_start=" <<tail_start<<"tail_finish="<<tail_finish<<
	    "   orig_size="<<orig_size<<dendl;
	  if (tail_start != tail_finish &&
	      (head_start == head_finish || tail_start != head_start) &&
	      tail_start < orig_size) {
	    ceph_assert(tail_finish <= orig_size);
	    ceph_assert(tail_finish - tail_start == sinfo.get_stripe_width());
	    ldpp_dout(dpp, 20) << __func__ << ": reading partial tail stripe "
			       << tail_start << "~" << sinfo.get_stripe_width()
			       << dendl;
	    plan.to_read[i.first].union_insert(
	      tail_start, sinfo.get_stripe_width());
	  }

	  if (head_start != tail_finish) {
	    ceph_assert(
	      sinfo.logical_offset_is_stripe_aligned(
		tail_finish - head_start)
	      );
	    will_write.union_insert(
	      head_start, tail_finish - head_start);
	    ldpp_dout(dpp, 20) << __func__ << ": head_start != taik_finish " << head_start << "!=" <<tail_finish<< dendl;
	    if (tail_finish > projected_size)
	      projected_size = tail_finish;
	  } else {
	    ceph_assert(tail_finish <= projected_size);
	  }
	    uint64_t hi_head_start = 0, hi_head_len = 0;

	    if(
	        partial_write && HiSetWriteSection(extent.get_start(), extent.get_len(), chunk_size, hi_head_start, hi_head_len)
	    ) {
	      plan.to_read_chunk_align[i.first].union_insert(hi_head_start, hi_head_len);
	    } else {
	      plan.to_read_chunk_align[i.first] = will_write;
	      partial_write = false;
	    }
	}

	if (i.second.truncate &&
	    i.second.truncate->second > projected_size) {
	  uint64_t truncating_to =
	    sinfo.logical_to_next_stripe_offset(i.second.truncate->second);
	  ldpp_dout(dpp, 20) << __func__ << ": truncating out to "
			     <<  truncating_to
			     << dendl;
	  will_write.union_insert(projected_size,
				  truncating_to - projected_size);
	  projected_size = truncating_to;
	}
	 if (partial_write) {
	     if(plan.to_read.count(i.first) != 0) {
		 map<uint64_t, uint64_t> write_set;
		 raw_write_set.move_into(write_set);
		 map<uint64_t, uint64_t> to_read;
		 plan.to_read[i.first].move_into(to_read);
		 ldpp_dout(dpp, 20) << __func__ << ": partial_write=" << partial_write << " write_set=" << write_set
		     << " to_read=" << to_read << dendl;
                 if(HiRebuildToread(write_set, chunk_size, to_read)) {
	             ldpp_dout(dpp, 20) << __func__ << ": to_read=" << to_read << dendl;
		     plan.to_read[i.first].clear();
		     plan.to_read[i.first].insert(extent_set(to_read));
		 }
	     }
	 }
	 
	ldpp_dout(dpp, 20) << __func__ << ": " << i.first
			   << " projected size "
			   << projected_size
			   << dendl;
	hinfo->set_projected_total_logical_size(
	  sinfo,
	  projected_size);

	/* validate post conditions:
	 * to_read should have an entry for i.first iff it isn't empty
	 * and if we are reading from i.first, we can't be renaming or
	 * cloning it */
	ceph_assert(plan.to_read.count(i.first) == 0 ||
	       (!plan.to_read.at(i.first).empty() &&
		!i.second.has_source()));
      });
    plan.t = std::move(t);
    return plan;
  }

  void generate_transactions(
    WritePlan &plan,
    ErasureCodeInterfaceRef &ecimpl,
    pg_t pgid,
    const ECUtil::stripe_info_t &sinfo,
    const map<hobject_t,extent_map> &partial_extents,
    vector<pg_log_entry_t> &entries,
    map<hobject_t,extent_map> *written,
    map<shard_id_t, ObjectStore::Transaction> *transactions,
    set<shard_id_t> &read_sid,
    set<hobject_t> *temp_added,
    set<hobject_t> *temp_removed,
    DoutPrefixProvider *dpp,
    bool &have_append);

};


#endif

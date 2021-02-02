// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2017 Red Hat, Inc
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#pragma once

#include <system_error>

namespace ceph::net {

/// net error codes
enum class error {
  success = 0,
  bad_connect_banner,
  bad_peer_address,
  negotiation_failure,
  read_eof,
  connection_aborted,
  connection_refused,
  connection_reset,
};

/// net error category
const std::error_category& net_category();

inline std::error_code make_error_code(error e)
{
  return {static_cast<int>(e), net_category()};
}

inline std::error_condition make_error_condition(error e)
{
  return {static_cast<int>(e), net_category()};
}

} // namespace ceph::net

namespace std {

/// enables implicit conversion to std::error_condition
template <>
struct is_error_condition_enum<ceph::net::error> : public true_type {};

} // namespace std

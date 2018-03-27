/**
 * This file include all common types internal to derecho and 
 * not necessarily being known by a client program.
 *
 */
#pragma once
#ifndef DERECHO_INTERNAL_H
#define DERECHO_INTERNAL_H

#include <functional>
#include <map>
#include <sys/types.h>

#include "persistent/HLC.hpp"
#include "persistent/Persistent.hpp"

namespace derecho {
/** Type alias for the internal Subgroup IDs generated by ViewManager.
 * This allows us to change exactly which numeric type we use to store it. */
using subgroup_id_t = uint32_t;
/** Type alias for a message's unique "sequence number" or index.
 * This allows us to change exactly which numeric type we use to store it.*/
using message_id_t = int32_t;

// for persistence manager
using persistence_manager_make_version_func_t = std::function<void(
        const subgroup_id_t &,
        const persistent::version_t &,
        const HLC &)>;
using persistence_manager_post_persist_func_t = std::function<void(
        const subgroup_id_t &,
        const persistent::version_t &)>;
using persistence_manager_callbacks_t = std::tuple<persistence_manager_make_version_func_t, persistence_manager_post_persist_func_t>;
}

#endif  //DERECHO_INTERNAL_H

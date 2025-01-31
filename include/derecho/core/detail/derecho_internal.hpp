/**
 * This file include all common types internal to derecho and
 * not necessarily being known by a client program.
 *
 */
#pragma once

#include "../derecho_type_definitions.hpp"
#include "derecho/persistent/HLC.hpp"
#include "derecho/persistent/PersistentInterface.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <sys/types.h>
#include <utility>

namespace persistent {
class PersistentRegistry;
}

namespace derecho {
/** Type alias for the internal Subgroup IDs generated by ViewManager.
 * This allows us to change exactly which numeric type we use to store it. */
using subgroup_id_t = uint32_t;
/** Type alias for a message's unique "sequence number" or index.
 * This allows us to change exactly which numeric type we use to store it.*/
using message_id_t = int32_t;
/**
 * Type of the numeric ID used to refer to subgroup types within a Group; this is
 * currently computed as the index of the subgroup type within Group's template
 * parameters.
 */
using subgroup_type_id_t = uint32_t;

/**
 * The function type for message delivery event callbacks. Expected parameters:
 * Parameter 1: ID of the subgroup in which the message was delivered
 * Parameter 2: Message sender's node ID
 * Parameter 3: Message ID
 * Parameter 4: Pair containing (message body, body size)
 * Parameter 5: Persistent version associated with the message
 */
using message_callback_t = std::function<void(subgroup_id_t, node_id_t, message_id_t, std::optional<std::pair<uint8_t*, long long int>>, persistent::version_t)>;
/**
 * The function type for persistence callback functions. Expected parameters:
 * Parameter 1: ID of the subgroup in which a version was persisted
 * Parameter 2: The new version that was persisted
 */
using persistence_callback_t = std::function<void(subgroup_id_t, persistent::version_t)>;
/**
 * The function type for verification callback functions. Expected parameters:
 * Parameter 1: ID of the subgroup in which a new version has been verified
 * Parameter 2: The version number up to which the log has been verified
 */
using verified_callback_t = std::function<void(subgroup_id_t, persistent::version_t)>;
/**
 * The type of the function used by MulticastGroup to notify RPCManager of a new message.
 * Matches the type signature of RPCManager::rpc_message_handler (but as a free function).
 */
using rpc_handler_t = std::function<void(subgroup_id_t, node_id_t, persistent::version_t, uint64_t, uint8_t*, uint32_t)>;

/**
 * Bundles together a set of callback functions for message delivery events.
 * These will be invoked by MulticastGroup or ViewManager to hand control back
 * to the client if it wants to implement custom logic to respond to each
 * message's arrival. (Note, this is a client-facing constructor argument,
 * not an internal data structure).
 */
struct UserMessageCallbacks {
    /** A function to be called each time a message reaches global stability in the group. */
    message_callback_t global_stability_callback;
    /** A function to be called when a new version of a subgroup's state finishes persisting locally */
    persistence_callback_t local_persistence_callback = nullptr;
    /** A function to be called when a new version of a subgroup's state has been persisted on all replicas */
    persistence_callback_t global_persistence_callback = nullptr;
    /** A function to be called when a new version of a subgroup's state has been signed correctly by all replicas */
    verified_callback_t global_verified_callback = nullptr;
};

/** The type of factory function the user must provide to the Group constructor,
 * to construct each Replicated Object that is assigned to a subgroup */
template <typename T>
using Factory = std::function<std::unique_ptr<T>(persistent::PersistentRegistry*, subgroup_id_t subgroup_id)>;

/** Type of the no-argument factory function needed by ExternalGroupClient */
template <typename T>
using NoArgFactory = std::function<std::unique_ptr<T>()>;

// to post the next version in a subgroup
using subgroup_post_next_version_func_t = std::function<void(
        const subgroup_id_t&,
        const persistent::version_t&,
        const uint64_t&)>;

}  // namespace derecho

/**
 * @file persistence_manager.h
 *
 * @date Jun 20, 2017
 */
#include <derecho/core/detail/persistence_manager.hpp>
#include <derecho/core/detail/view_manager.hpp>
#include <derecho/openssl/signature.hpp>

namespace derecho {

PersistenceManager::PersistenceManager(
        std::shared_ptr<PublicKeyStore> public_key_store,
        std::map<subgroup_id_t, std::reference_wrapper<ReplicatedObject>>& objects_map,
        const persistence_callback_t& _persistence_callback)
        : thread_shutdown(false),
          node_public_keys(public_key_store),
          signature_size(0),
          persistence_callback(_persistence_callback),
          objects_by_subgroup_id(objects_map) {
    // initialize semaphore
    if(sem_init(&persistence_request_sem, 1, 0) != 0) {
        throw derecho_exception("Cannot initialize persistent_request_sem:errno=" + std::to_string(errno));
    }
    if(getConfBoolean(CONF_PERS_SIGNED_LOG)) {
        signature_size = openssl::EnvelopeKey::from_pem_private(getConfString(CONF_PERS_PRIVATE_KEY_FILE)).get_max_size();
    }
}

PersistenceManager::~PersistenceManager() {
    sem_destroy(&persistence_request_sem);
}

void PersistenceManager::set_view_manager(ViewManager& view_manager) {
    this->view_manager = &view_manager;
}

std::size_t PersistenceManager::get_signature_size() const {
    return signature_size;
}

/** Start the persistent thread. */
void PersistenceManager::start() {
    this->persist_thread = std::thread{[this]() {
        pthread_setname_np(pthread_self(), "persist");
        do {
            // wait for semaphore
            sem_wait(&persistence_request_sem);
            while(prq_lock.test_and_set(std::memory_order_acquire))  // acquire lock
                ;                                                    // spin
            if(this->persistence_request_queue.empty()) {
                prq_lock.clear(std::memory_order_release);  // release lock
                if(this->thread_shutdown) {
                    break;
                }
                continue;
            }

            subgroup_id_t subgroup_id = std::get<0>(persistence_request_queue.front());
            persistent::version_t version = std::get<1>(persistence_request_queue.front());
            persistence_request_queue.pop();
            prq_lock.clear(std::memory_order_release);  // release lock

            // persist
            try {
                //To reduce the time this thread holds the View lock, put the signature in a local array
                //and copy it into the SST once signing is done. (We could use the SST signatures field
                //directly as the signature array, but that would require holding the lock for longer.)
                unsigned char signature[signature_size];

                auto search = objects_by_subgroup_id.find(subgroup_id);
                if(search != objects_by_subgroup_id.end()) {
                    search->second.get().persist(version, signature);
                }
                // read lock the view
                SharedLockedReference<View> view_and_lock = view_manager->get_current_view();
                // update the signature and persisted_num in SST
                View& Vc = view_and_lock.get();
                if(signature_size > 0) {
                    //This will effectively do nothing if signature_size==0, but an unnecessary put() will still have overhead
                    gmssst::set(&(Vc.gmsSST->signatures[Vc.gmsSST->get_local_index()][subgroup_id * signature_size]),
                                signature, signature_size);
                    Vc.gmsSST->put(Vc.multicast_group->get_shard_sst_indices(subgroup_id),
                                   (char*)std::addressof(Vc.gmsSST->signatures[0][subgroup_id * signature_size]) - Vc.gmsSST->getBaseAddress(),
                                   signature_size);
                }
                gmssst::set(Vc.gmsSST->persisted_num[Vc.gmsSST->get_local_index()][subgroup_id], version);
                Vc.gmsSST->put(Vc.multicast_group->get_shard_sst_indices(subgroup_id),
                               (char*)std::addressof(Vc.gmsSST->persisted_num[0][subgroup_id]) - Vc.gmsSST->getBaseAddress(),
                               sizeof(long long int));
            } catch(uint64_t exp) {
                dbg_default_debug("exception on persist():subgroup={},ver={},exp={}.", subgroup_id, version, exp);
                std::cout << "exception on persistent:subgroup=" << subgroup_id << ",ver=" << version << "exception=0x" << std::hex << exp << std::endl;
            }

            // callback
            if(this->persistence_callback != nullptr) {
                this->persistence_callback(subgroup_id, version);
            }

            if(this->thread_shutdown) {
                while(prq_lock.test_and_set(std::memory_order_acquire))  // acquire lock
                    ;                                                    // spin
                if(persistence_request_queue.empty()) {
                    prq_lock.clear(std::memory_order_release);  // release lock
                    break;                                      // finish
                }
                prq_lock.clear(std::memory_order_release);  // release lock
            }
        } while(true);
    }};
}

/** post a persistence request */
void PersistenceManager::post_persist_request(const subgroup_id_t& subgroup_id, const persistent::version_t& version) {
    // request enqueue
    while(prq_lock.test_and_set(std::memory_order_acquire))  // acquire lock
        ;                                                    // spin
    persistence_request_queue.push(std::make_tuple(subgroup_id, version));
    prq_lock.clear(std::memory_order_release);  // release lock
    // post semaphore
    sem_post(&persistence_request_sem);
}

/** make a version */
void PersistenceManager::make_version(const subgroup_id_t& subgroup_id,
                                      const persistent::version_t& version, const HLC& mhlc) {
    auto search = objects_by_subgroup_id.find(subgroup_id);
    if(search != objects_by_subgroup_id.end()) {
        search->second.get().make_version(version, mhlc);
    }
}

/** shutdown the thread
 * @wait - wait till the thread finished or not.
 */
void PersistenceManager::shutdown(bool wait) {
    // if(replicated_objects == nullptr) return;  //skip for raw subgroups - NO DON'T

    thread_shutdown = true;
    sem_post(&persistence_request_sem);  // kick the persistence thread in case it is sleeping

    if(wait) {
        this->persist_thread.join();
    }
}
}  // namespace derecho

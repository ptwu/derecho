/* Same experiment as derecho_bw_test.cpp really,
 * but creates varying number of subgroups and sends messages in all
 * Better to separate it so that it can be managed independently
 */
#include <atomic>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <time.h>
#include <typeindex>
#include <vector>

#include "aggregate_bandwidth.h"
#include "derecho/derecho.h"
#include "log_results.h"
#include "rdmc/rdmc.h"
#include "rdmc/util.h"

using std::cout;
using std::endl;
using std::map;
using std::vector;

using namespace derecho;

template <typename T>
struct volatile_wrapper {
    volatile T t;
    volatile_wrapper(T t) : t(t) {}
    bool operator<(const T t1) {
        return t < t1;
    }
};

struct exp_result {
    uint32_t num_nodes;
    long long unsigned int max_msg_size;
    uint32_t subgroup_size;
    double bw;

    void print(std::ofstream &fout) {
        fout << num_nodes << " "
             << max_msg_size << " "
             << subgroup_size << " "
             << bw << endl;
    }
};

int main(int argc, char *argv[]) {
    try {
        if(argc < 3) {
            cout << "Insufficient number of command line arguments" << endl;
            cout << "Enter num_nodes, subgroup_size" << endl;
            cout << "Thank you" << endl;
            return -1;
        }
        pthread_setname_np(pthread_self(), "sbgrp_scaling");

        const uint32_t num_nodes = std::stoi(argv[1]);

        Conf::initialize(argc, argv);

	uint64_t max_msg_size = getConfUInt64(CONF_DERECHO_MAX_PAYLOAD_SIZE);
        uint32_t num_messages = ((max_msg_size < 20000) ? 10000 : 1000);

	uint32_t node_id = getConfUInt32(CONF_DERECHO_LOCAL_ID);

        // will resize it as and when convenient
        uint32_t subgroup_size = std::stoi(argv[2]);
        const auto num_subgroups = num_nodes;
        vector<uint32_t> send_subgroup_indices;
        map<uint32_t, uint32_t> subgroup_to_local_index;
        for(uint i = 0; i < subgroup_size; ++i) {
            auto j = ((int32_t)(node_id + num_nodes) - (int32_t)i) % num_nodes;
            subgroup_to_local_index[j] = i;
            if(j < num_subgroups) {
                send_subgroup_indices.push_back(j);
            }
        }
        vector<vector<volatile_wrapper<long long int>>> received_message_indices(subgroup_size);
        for(uint i = 0; i < subgroup_size; ++i) {
            received_message_indices[i].resize(subgroup_size, -1);
        }
        auto stability_callback = [&num_messages,
                                   &num_nodes,
                                   subgroup_to_local_index,
                                   &received_message_indices](uint32_t subgroup_num, int sender_id, long long int index,
                                                              std::optional<std::pair<char*, long long int>> data, persistent::version_t ver) mutable {
            int sender_rank = (sender_id - subgroup_num + num_nodes) % num_nodes;
            received_message_indices[subgroup_to_local_index.at(subgroup_num)][sender_rank] = index;
        };

        auto membership_function = [num_nodes, subgroup_size](const View &curr_view, int &next_unassigned_rank) {
            auto num_members = curr_view.members.size();
            if(num_members < num_nodes) {
                throw subgroup_provisioning_exception();
            }
            subgroup_shard_layout_t subgroup_vector(num_members);
            for(uint i = 0; i < num_members; ++i) {
                vector<uint32_t> members(subgroup_size);
                for(uint j = 0; j < subgroup_size; ++j) {
                    members[j] = (i + j) % num_members;
                }
                subgroup_vector[i].emplace_back(curr_view.make_subview(members));
            }
            next_unassigned_rank = curr_view.members.size();

            return subgroup_vector;
        };

        std::map<std::type_index, shard_view_generator_t> subgroup_map = {{std::type_index(typeid(RawObject)), membership_function}};
        SubgroupInfo raw_groups(subgroup_map);

        Group<> managed_group(CallbackSet{stability_callback},
                              raw_groups);

	cout << "Finished constructing/joining ManagedGroup" << endl;

        while(managed_group.get_members().size() < num_nodes) {
        }
        auto members_order = managed_group.get_members();
	auto node_rank = managed_group.get_my_rank();
	
        vector<RawSubgroup> subgroups;
        for(uint i = 0; i < subgroup_size; ++i) {
            subgroups.emplace_back(managed_group.get_subgroup<RawObject>((node_id - i + num_nodes) % num_nodes));
        }

        auto send_some = [&](uint32_t num_subgroups) {
            const auto num_subgroups_to_send = send_subgroup_indices.size();
            for(uint i = 0; i < num_subgroups * num_messages; ++i) {
                uint j = i % num_subgroups_to_send;
                subgroups[j].send(max_msg_size, [](char* buf){});
            }
        };

        auto is_complete = [&](uint32_t num_subgroups) {
            for(auto p : subgroup_to_local_index) {
                auto subgroup_num = p.first;
                if(subgroup_num >= num_subgroups) {
                    continue;
                }
                auto i = p.second;
                for(uint j = 0; j < subgroup_size; ++j) {
                    if(received_message_indices[i][j] < num_messages - 1) {
                        return false;
                    }
                }
            }
            return true;
        };

        struct timespec start_time, end_time;
        long long int nanoseconds_elapsed;
        double bw, avg_bw;
        // start timer
        clock_gettime(CLOCK_REALTIME, &start_time);
        send_some(num_subgroups);
        while(!is_complete(num_subgroups)) {
        }
        clock_gettime(CLOCK_REALTIME, &end_time);
        nanoseconds_elapsed = (end_time.tv_sec - start_time.tv_sec) * (long long int)1e9 + (end_time.tv_nsec - start_time.tv_nsec);
        bw = (max_msg_size * num_messages * num_nodes * send_subgroup_indices.size() + 0.0) / nanoseconds_elapsed;
        avg_bw = aggregate_bandwidth(members_order, node_id, bw);
        if(node_rank == 0) {
            log_results(exp_result{num_nodes, max_msg_size, subgroup_size, avg_bw},
                        "data_subgroup_scaling");
        }

        managed_group.barrier_sync();
        // managed_group.leave();
        // sst::verbs_destroy();
        exit(0);
        cout << "Finished destroying managed_group" << endl;
        std::this_thread::sleep_for(std::chrono::seconds(10));
    } catch(const std::exception &e) {
        cout << "Exception in main: " << e.what() << endl;
        cout << "main shutting down" << endl;
    }
}

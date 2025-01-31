#include <derecho/conf/conf.hpp>
#include <derecho/core/derecho.hpp>
#include <derecho/mutils-serialization/SerializationSupport.hpp>
#include <derecho/persistent/Persistent.hpp>

#include <iostream>
#include <sstream>

using derecho::ExternalClientCaller;
using derecho::Replicated;
using std::cout;
using std::endl;

class TestObject : public derecho::NotificationSupport,
                   public derecho::GroupReference,
                   public mutils::ByteRepresentable {
private:
    using derecho::GroupReference::group;
    std::string data;

public:
    TestObject(const std::string& initial_data = "") : data(initial_data) {}

    void notify(const derecho::NotificationMessage& msg) const {
        derecho::NotificationSupport::notify(msg);
    }

    void ordered_set_data(const std::string& new_data) {
        data = new_data;
    }

    std::string read_data() const {
        return data;
    }
    bool set_data(const std::string& new_data) const;

    REGISTER_RPC_FUNCTIONS(TestObject, P2P_TARGETS(notify, read_data, set_data), ORDERED_TARGETS(ordered_set_data));

    DEFAULT_SERIALIZATION_SUPPORT(TestObject, data);
};

bool TestObject::set_data(const std::string& new_data) const {
    Replicated<TestObject>& this_subgroup_handle = group->get_subgroup<TestObject>(this->subgroup_index);
    derecho::rpc::QueryResults<void> send_results = this_subgroup_handle.ordered_send<RPC_NAME(ordered_set_data)>(new_data);
    send_results.get();
    return true;
}

class TestPersistentObject : public mutils::ByteRepresentable,
                             public derecho::NotificationSupport,
                             public derecho::GroupReference,
                             public derecho::PersistsFields {
private:
    using derecho::GroupReference::group;
    persistent::Persistent<std::string> persistent_data;

public:
    TestPersistentObject(persistent::PersistentRegistry* registry) : persistent_data(registry) {}

    TestPersistentObject(persistent::Persistent<std::string>& other_value)
            : persistent_data(std::move(other_value)) {}

    std::string get_data() const {
        return *persistent_data;
    }

    void ordered_set_data(const std::string& new_value) {
        *persistent_data = new_value;
    }

    void notify(const derecho::NotificationMessage& msg) const {
        derecho::NotificationSupport::notify(msg);
    }

    bool set_data(const std::string& new_data) const;

    REGISTER_RPC_FUNCTIONS(TestPersistentObject, P2P_TARGETS(notify, get_data, set_data), ORDERED_TARGETS(ordered_set_data));

    DEFAULT_SERIALIZATION_SUPPORT(TestPersistentObject, persistent_data);
};

bool TestPersistentObject::set_data(const std::string& new_data) const {
    Replicated<TestPersistentObject>& this_subgroup_handle = group->get_subgroup<TestPersistentObject>(this->subgroup_index);
    derecho::rpc::QueryResults<void> send_results = this_subgroup_handle.ordered_send<RPC_NAME(ordered_set_data)>(new_data);
    send_results.get();
    return true;
}

void run_nonpersistent_test(uint32_t external_node_id, int num_senders, int num_nodes, uint32_t num_messages) {
    node_id_t my_id = derecho::getConfUInt64(CONF_DERECHO_LOCAL_ID);
    uint64_t max_msg_size = derecho::getConfUInt64(CONF_SUBGROUP_DEFAULT_MAX_PAYLOAD_SIZE);

    if(external_node_id != my_id) {
        // Put each node in its own subgroup, which has 1 shard with 1 member
        derecho::SubgroupInfo subgroup_info{derecho::DefaultSubgroupAllocator(
                {{std::type_index(typeid(TestObject)),
                  derecho::identical_subgroups_policy(num_nodes, derecho::fixed_even_shards(1, 1))}})};
        auto object_factory = [](persistent::PersistentRegistry*, derecho::subgroup_id_t) { return std::make_unique<TestObject>(); };

        derecho::Group<TestObject> group({}, subgroup_info, {}, std::vector<derecho::view_upcall_t>{}, object_factory);
        std::cout << "Finished constructing/joining Group" << std::endl;

        uint32_t my_subgroup_index = group.get_my_subgroup_indexes<TestObject>()[0];
        if(my_subgroup_index < static_cast<uint32_t>(num_senders)) {
            std::cout << "Waiting for the external node to connect" << std::endl;
            while(!group.get_client_callback<TestObject>(my_subgroup_index).has_external_client(external_node_id)) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            for(uint i = 0; i < num_messages; ++i) {
                derecho::ExternalClientCallback<TestObject>& handle = group.get_client_callback<TestObject>(my_subgroup_index);
                uint64_t msg_size = max_msg_size - 128;
                derecho::NotificationMessage message(1, msg_size);
                for(uint64_t j = 0; j < msg_size - 1; ++j) {
                    message.body[j] = 'a' + j % 26;
                }
                // notification!
                handle.p2p_send<RPC_NAME(notify)>(external_node_id, message);
            }
            std::cout << "Done sending all notifications" << std::endl;
        } else {
            std::cout << "Not sending any notifications." << std::endl;
        }
        std::cout << "Press enter when finished with test." << std::endl;
        std::cin.get();
        group.leave(true);
    } else {
        auto dummy_object_factory = []() { return std::make_unique<TestObject>(); };
        derecho::ExternalGroupClient<TestObject> group(dummy_object_factory);

        cout << "Finished constructing ExternalGroupClient" << endl;

        // Each member node is in its own subgroup, and the first num_senders of them will send notifications
        std::vector<node_id_t> members = group.get_members();
        std::vector<std::reference_wrapper<ExternalClientCaller<TestObject, decltype(group)>>> client_callers;
        for(int subgroup_num = 0; subgroup_num < num_senders; ++subgroup_num) {
            client_callers.emplace_back(group.get_subgroup_caller<TestObject>(subgroup_num));
        }

        for(int subgroup_num = 0; subgroup_num < num_senders; ++subgroup_num) {
            // register notification handler
            client_callers[subgroup_num].get().add_p2p_connection(members[subgroup_num]);
            client_callers[subgroup_num].get().register_notification_handler([=](const derecho::NotificationMessage& message) {
                std::cout << "Notification successful from subgroup " << subgroup_num << "!  Message type = "
                          << message.message_type << " Size: " << message.size << ", Data: " << message.body << std::endl;
            });
        }

        std::cout << "Awaiting notifications." << std::endl;
        std::cout << "Press enter when finished with test." << std::endl;
        std::cin.get();
    }
}

void run_persistent_test(uint32_t external_node_id, int num_senders, int num_nodes, uint32_t num_messages) {
    node_id_t my_id = derecho::getConfUInt64(CONF_DERECHO_LOCAL_ID);
    uint64_t max_msg_size = derecho::getConfUInt64(CONF_SUBGROUP_DEFAULT_MAX_PAYLOAD_SIZE);

    if(external_node_id != my_id) {
        // Put each node in its own subgroup, which has 1 shard with 1 member
        derecho::SubgroupInfo subgroup_info{derecho::DefaultSubgroupAllocator(
                {{std::type_index(typeid(TestPersistentObject)),
                  derecho::identical_subgroups_policy(num_nodes, derecho::fixed_even_shards(1, 1))}})};
        auto object_factory = [](persistent::PersistentRegistry* registry, derecho::subgroup_id_t) {
            return std::make_unique<TestPersistentObject>(registry);
        };

        derecho::Group<TestPersistentObject> group({}, subgroup_info, {}, std::vector<derecho::view_upcall_t>{}, object_factory);
        std::cout << "Finished constructing/joining Group" << std::endl;

        uint32_t my_subgroup_index = group.get_my_subgroup_indexes<TestPersistentObject>()[0];
        if(my_subgroup_index < static_cast<uint32_t>(num_senders)) {
            std::cout << "Waiting for the external node to connect" << std::endl;
            while(!group.get_client_callback<TestPersistentObject>(my_subgroup_index).has_external_client(external_node_id)) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            for(uint i = 0; i < num_messages; ++i) {
                derecho::ExternalClientCallback<TestPersistentObject>& handle = group.get_client_callback<TestPersistentObject>(my_subgroup_index);
                uint64_t msg_size = max_msg_size - 128;
                derecho::NotificationMessage message(1, msg_size);
                for(uint64_t j = 0; j < msg_size - 1; ++j) {
                    message.body[j] = 'a' + j % 26;
                }
                // notification!
                handle.p2p_send<RPC_NAME(notify)>(external_node_id, message);
            }
            std::cout << "Done sending all notifications" << std::endl;
        } else {
            std::cout << "Not sending any notifications." << std::endl;
        }
        std::cout << "Press enter when finished with test." << std::endl;
        std::cin.get();
        group.leave(true);
    } else {
        // A Persistent<T> constructed with a null PersistentRegistry won't work, but we don't need it to work
        auto dummy_object_factory = []() { return std::make_unique<TestPersistentObject>(nullptr); };

        derecho::ExternalGroupClient<TestPersistentObject> group(dummy_object_factory);

        cout << "Finished constructing ExternalGroupClient" << endl;

        // Each member node is in its own subgroup, and the first num_senders of them will send notifications
        std::vector<node_id_t> members = group.get_members();
        std::vector<std::reference_wrapper<ExternalClientCaller<TestPersistentObject, decltype(group)>>> client_callers;
        for(int subgroup_num = 0; subgroup_num < num_senders; ++subgroup_num) {
            client_callers.emplace_back(group.get_subgroup_caller<TestPersistentObject>(subgroup_num));
        }

        for(int subgroup_num = 0; subgroup_num < num_senders; ++subgroup_num) {
            // register notification handler
            client_callers[subgroup_num].get().add_p2p_connection(members[subgroup_num]);
            client_callers[subgroup_num].get().register_notification_handler([=](const derecho::NotificationMessage& message) {
                std::cout << "Notification successful from subgroup " << subgroup_num << "!  Message type = "
                          << message.message_type << " Size: " << message.size << ", Data: " << message.body << std::endl;
            });
        }

        std::cout << "Awaiting notifications." << std::endl;
        std::cout << "Press enter when finished with test." << std::endl;
        std::cin.get();
    }
}

int main(int argc, char** argv) {
    const int num_args = 5;
    if(argc < (num_args + 1) || (argc > (num_args + 1) && strcmp("--", argv[argc - (num_args + 1)]) != 0)) {
        cout << "Invalid command line arguments." << endl;
        cout << "USAGE:" << argv[0] << "[ derecho-config-list -- ] external_node_id num_senders num_nodes num_messages persistence_on" << endl;
        cout << "Thank you" << endl;
        return -1;
    }
    derecho::Conf::initialize(argc, argv);
    const uint32_t external_node_id = std::stoi(argv[argc - num_args]);
    const int num_senders = std::stoi(argv[argc - num_args + 1]);
    const int num_of_nodes = std::stoi(argv[argc - num_args + 2]);
    const uint32_t count = std::stoi(argv[argc - num_args + 3]);
    const bool persistence_on = std::stoi(argv[argc - num_args + 4]) != 0;

    if(num_senders > num_of_nodes) {
        cout << "Invalid command line arguments: num_senders can't be greater than num_nodes" << endl;
        cout << "USAGE:" << argv[0] << "[ derecho-config-list -- ] external_node_id num_senders num_nodes num_messages persistence_on" << endl;
        return -1;
    }

    if(persistence_on) {
        run_persistent_test(external_node_id, num_senders, num_of_nodes, count);
    } else {
        run_nonpersistent_test(external_node_id, num_senders, num_of_nodes, count);
    }
}

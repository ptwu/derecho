#include <derecho/conf/conf.hpp>
#include <derecho/core/derecho.hpp>
#include <derecho/mutils-serialization/SerializationSupport.hpp>
#include <derecho/persistent/Persistent.hpp>

#include <iostream>
#include <sstream>

using derecho::Bytes;
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

    void notify(const Bytes& msg) const {
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

    void notify(const Bytes& msg) const {
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

void run_nonpersistent_test(uint32_t external_node_id, bool is_sender, int num_nodes, uint32_t num_messages) {
    node_id_t my_id = derecho::getConfUInt64(CONF_DERECHO_LOCAL_ID);
    uint64_t max_msg_size = derecho::getConfUInt64(CONF_SUBGROUP_DEFAULT_MAX_PAYLOAD_SIZE);

    if(external_node_id != my_id) {
        //Put each node in its own subgroup, which has 1 shard with 1 member
        derecho::SubgroupInfo subgroup_info{derecho::DefaultSubgroupAllocator(
                {{std::type_index(typeid(TestObject)),
                  derecho::identical_subgroups_policy(num_nodes, derecho::fixed_even_shards(1, 1))}})};
        auto object_factory = [](persistent::PersistentRegistry*, derecho::subgroup_id_t) { return std::make_unique<TestObject>(); };

        derecho::Group<TestObject> group({}, subgroup_info, {}, std::vector<derecho::view_upcall_t>{}, object_factory);
        std::cout << "Finished constructing/joining Group" << std::endl;

        if(is_sender) {
            for(uint i = 0; i < num_messages; ++i) {
                // group.notify(2, bytes);
                derecho::ExternalClientCallback<TestObject>& handle = group.get_client_callback<TestObject>(my_id);
                std::cout << "acquired notification support callback!" << std::endl;
                uint64_t msg_size = max_msg_size - 128;
                char* bbuf = new char[msg_size];
                bzero(bbuf, msg_size);
                for(uint64_t j = 0; j < msg_size - 1; ++j) {
                    bbuf[j] = 'a' + j % 26;
                }
                Bytes bytes(bbuf, msg_size);
                // notification!
                handle.p2p_send<RPC_NAME(notify)>(external_node_id, bytes);
                delete bbuf;
            }
        }
        while(true) {
        }

    } else {
        derecho::ExternalGroupClient<TestObject> group;

        cout << "Finished constructing ExternalGroupClient" << endl;

        std::vector<node_id_t> members = group.get_members();
        std::vector<node_id_t> shard_members = group.get_shard_members(0, 0);
        ExternalClientCaller<TestObject, decltype(group)>& handle1 = group.get_subgroup_caller<TestObject>(0);
        ExternalClientCaller<TestObject, decltype(group)>& handle2 = group.get_subgroup_caller<TestObject>(1);

        // register notification handler
        handle1.register_notification([](const derecho::Bytes& data) { std::cout << "Notification Successful from 0! Data: " << data.bytes << " Size: " << data.size << std::endl; }, 0);
        handle2.register_notification([](const derecho::Bytes& data) { std::cout << "Notification Successful from 1! Data: " << data.bytes << " Size: " << data.size << std::endl; }, 1);
        handle2.register_notification([](const derecho::Bytes& data) { std::cout << "Another Victory from 1! Data: " << data.bytes << " Size: " << data.size << std::endl; }, 1);

        cout << "Reached end of scope, entering infinite loop so program doesn't exit" << std::endl;
        while(true) {
        }
    }
}

/*
 * This will not compile because TestPersistentObject does not have a no-argument constructor.
 * Subgroups with persistent members never have no-argument constructors, because they need the PersistentRegistry* argument

void run_persistent_test(uint32_t external_node_id, bool is_sender, int num_nodes, uint32_t num_messages) {
    node_id_t my_id = derecho::getConfUInt64(CONF_DERECHO_LOCAL_ID);
    uint64_t max_msg_size = derecho::getConfUInt64(CONF_SUBGROUP_DEFAULT_MAX_PAYLOAD_SIZE);

    if(external_node_id != my_id) {
        //Put each node in its own subgroup, which has 1 shard with 1 member
        derecho::SubgroupInfo subgroup_info{derecho::DefaultSubgroupAllocator(
                {{std::type_index(typeid(TestPersistentObject)),
                  derecho::identical_subgroups_policy(num_nodes, derecho::fixed_even_shards(1, 1))}})};
        auto object_factory = [](persistent::PersistentRegistry* registry, derecho::subgroup_id_t) { return std::make_unique<TestPersistentObject>(registry); };

        derecho::Group<TestPersistentObject> group({}, subgroup_info, {}, std::vector<derecho::view_upcall_t>{}, object_factory);
        std::cout << "Finished constructing/joining Group" << std::endl;

        if(is_sender) {
            for(uint i = 0; i < num_messages; ++i) {
                // group.notify(2, bytes);
                derecho::ExternalClientCallback<TestPersistentObject>& handle = group.get_client_callback<TestPersistentObject>(my_id);
                std::cout << "acquired notification support callback!" << std::endl;
                uint64_t msg_size = max_msg_size - 128;
                char* bbuf = new char[msg_size];
                bzero(bbuf, msg_size);
                for(uint64_t j = 0; j < msg_size - 1; ++j) {
                    bbuf[j] = 'a' + j % 26;
                }
                Bytes bytes(bbuf, msg_size);
                // notification!
                handle.p2p_send<RPC_NAME(notify)>(external_node_id, bytes);
                delete bbuf;
            }
        }
        while(true) {
        }

    } else {
        derecho::ExternalGroupClient<TestPersistentObject> group;

        cout << "Finished constructing ExternalGroupClient" << endl;

        std::vector<node_id_t> members = group.get_members();
        std::vector<node_id_t> shard_members = group.get_shard_members(0, 0);
        ExternalClientCaller<TestPersistentObject, decltype(group)>& handle1 = group.get_subgroup_caller<TestPersistentObject>(0);
        ExternalClientCaller<TestPersistentObject, decltype(group)>& handle2 = group.get_subgroup_caller<TestPersistentObject>(1);

        // register notification handler
        handle1.register_notification([](const derecho::Bytes& data) { std::cout << "Notification Successful from 0! Data: " << data.bytes << " Size: " << data.size << std::endl; }, 0);
        handle2.register_notification([](const derecho::Bytes& data) { std::cout << "Notification Successful from 1! Data: " << data.bytes << " Size: " << data.size << std::endl; }, 1);
        handle2.register_notification([](const derecho::Bytes& data) { std::cout << "Another Victory from 1! Data: " << data.bytes << " Size: " << data.size << std::endl; }, 1);

        cout << "Reached end of scope, entering infinite loop so program doesn't exit" << std::endl;
        while(true) {
        }
    }
}
*/
int main(int argc, char** argv) {
    const int num_args = 5;
    if(argc < (num_args + 1) || (argc > (num_args + 1) && strcmp("--", argv[argc - (num_args + 1)]) != 0)) {
        cout << "Invalid command line arguments." << endl;
        cout << "USAGE:" << argv[0] << "[ derecho-config-list -- ] external_node_id is_sender num_nodes num_messages persistence_on" << endl;
        cout << "Thank you" << endl;
        return -1;
    }
    derecho::Conf::initialize(argc, argv);
    const uint32_t external_node_id = std::stoi(argv[argc - num_args]);
    const bool is_sender = std::stoi(argv[argc - num_args + 1]) != 0;
    int num_of_nodes = std::stoi(argv[argc - num_args + 2]);
    const uint32_t count = std::stoi(argv[argc - num_args + 3]);
    const bool persistence_on = std::stoi(argv[argc - num_args + 4]) != 0;

    if(persistence_on) {
        // run_persistent_test(external_node_id, is_sender, num_of_nodes, count);
    } else {
        run_nonpersistent_test(external_node_id, is_sender, num_of_nodes, count);
    }
}

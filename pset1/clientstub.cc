#include <rpc/client.h>
#include <map>
#include <memory>
#include <future>
#include <iostream>
#include <vector>

#include "rpcgame.hh"

class RPCGameClient {
public:
    RPCGameClient(const std::string& address, uint16_t port, size_t window_size = 32, size_t batch_size = 128)
        : _client(address, port), _window_size(window_size), _batch_size(batch_size) {
        _client.set_timeout(30000);  // 30 second timeout
    }

    void send_try(const char* name, size_t name_len, uint64_t count) {
        // Add to batch
        BatchedRequest req;
        req.serial = _serial;
        req.name = std::string(name, name_len);
        req.count = count;
        _current_batch.push_back(std::move(req));
        ++_serial;

        // Flush if batch is full
        if (_current_batch.size() >= _batch_size) {
            flush_batch();
        }

        // Wait if window is full
        while (_in_flight_batches >= _window_size) {
            process_one_response();
        }
    }

    void finish() {
        // Flush any remaining batch
        if (!_current_batch.empty()) {
            flush_batch();
        }

        // Wait for all in-flight requests to complete
        while (_in_flight_batches > 0) {
            process_one_response();
        }

        // Deliver any buffered responses
        deliver_buffered_responses();

        // Now send Done request
        auto result = _client.call("Done").as<std::tuple<std::string, std::string>>();
        
        const std::string& resp_client_checksum = std::get<0>(result);
        const std::string& resp_server_checksum = std::get<1>(result);
        
        // Parse response
        const std::string& my_client_checksum = client_checksum();
        const std::string& my_server_checksum = server_checksum();
        
        bool ok = my_client_checksum == resp_client_checksum
            && my_server_checksum == resp_server_checksum;
        std::cout << "client checksums: "
            << my_client_checksum << "/" << resp_client_checksum
            << "\nserver checksums: "
            << my_server_checksum << "/" << resp_server_checksum
            << "\nmatch: " << (ok ? "true\n" : "false\n");
    }

private:
    struct BatchedRequest {
        uint64_t serial;
        std::string name;
        uint64_t count;
    };

    struct BatchInfo {
        std::vector<uint64_t> serials;
        std::future<RPCLIB_MSGPACK::object_handle> future;
    };

    void flush_batch() {
        if (_current_batch.empty()) {
            return;
        }

        // Prepare batch data
        std::vector<uint64_t> serials;
        std::vector<std::string> names;
        std::vector<uint64_t> counts;

        for (const auto& req : _current_batch) {
            serials.push_back(req.serial);
            names.push_back(req.name);
            counts.push_back(req.count);
        }

        // Send async batch request
        auto future = _client.async_call("TryBatch", serials, names, counts);
        
        BatchInfo batch_info;
        batch_info.serials = std::move(serials);
        batch_info.future = std::move(future);
        
        _pending_batches.push_back(std::move(batch_info));
        ++_in_flight_batches;
        
        _current_batch.clear();
    }

    void process_one_response() {
        if (_pending_batches.empty()) {
            return;
        }

        // Get the oldest batch
        auto& batch_info = _pending_batches.front();
        
        // Wait for this batch response
        auto values = batch_info.future.get().template as<std::vector<uint64_t>>();
        
        // Buffer all responses from this batch
        for (size_t i = 0; i < batch_info.serials.size(); ++i) {
            _buffered_responses[batch_info.serials[i]] = values[i];
        }
        
        // Remove from pending
        _pending_batches.pop_front();
        --_in_flight_batches;

        // Deliver responses in order
        deliver_buffered_responses();
    }

    void deliver_buffered_responses() {
        // Deliver all consecutive responses starting from next_expected_serial
        while (true) {
            auto it = _buffered_responses.find(_next_expected_serial);
            if (it == _buffered_responses.end()) {
                break;  // Next expected response not yet received
            }
            
            // Deliver this response
            client_recv_try_response(it->second);
            _buffered_responses.erase(it);
            ++_next_expected_serial;
        }
    }

    rpc::client _client;
    uint64_t _serial = 1;
    uint64_t _next_expected_serial = 1;
    size_t _window_size;
    size_t _batch_size;
    size_t _in_flight_batches = 0;
    std::vector<BatchedRequest> _current_batch;
    std::deque<BatchInfo> _pending_batches;
    std::map<uint64_t, uint64_t> _buffered_responses;  // serial -> value
};


static std::unique_ptr<RPCGameClient> client;


void client_connect(std::string address) {
    // Parse address as "host:port"
    size_t colon_pos = address.find(':');
    std::string host = address.substr(0, colon_pos);
    uint16_t port = std::stoi(address.substr(colon_pos + 1));
    
    client = std::make_unique<RPCGameClient>(host, port, 50, 10);  // window=50, batch=10
}

void client_send_try(const char* name, size_t name_len, uint64_t count) {
    client->send_try(name, name_len, count);
}

void client_finish() {
    client->finish();
}
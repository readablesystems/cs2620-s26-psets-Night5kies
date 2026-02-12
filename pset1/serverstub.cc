#include <rpc/server.h>
#include <memory>
#include <thread>
#include <iostream>
#include <vector>

#include "rpcgame.hh"

static std::unique_ptr<rpc::server> server;

// RPC handler for single Try (keep for compatibility)
uint64_t handle_try(uint64_t serial, const std::string& name, uint64_t count) {
    uint64_t value = server_process_try(serial, name.data(), name.size(), count);
    return value;
}

// RPC handler for batched Try
std::vector<uint64_t> handle_try_batch(const std::vector<uint64_t>& serials,
                                        const std::vector<std::string>& names,
                                        const std::vector<uint64_t>& counts) {
    std::vector<uint64_t> results;
    results.reserve(serials.size());
    
    for (size_t i = 0; i < serials.size(); ++i) {
        uint64_t value = server_process_try(serials[i], names[i].data(), 
                                            names[i].size(), counts[i]);
        results.push_back(value);
    }
    
    return results;
}

// RPC handler for Done
std::tuple<std::string, std::string> handle_done() {
    // calculate checksums
    const std::string& client_csum = client_checksum();
    const std::string& server_csum = server_checksum();

    // shut down server after 100ms
    std::thread shutdown_thread([] () {
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(100ms);
        server->stop();
    });
    shutdown_thread.detach();

    return std::make_tuple(client_csum, server_csum);
}

void server_start(std::string address) {
    // Parse address as "host:port"
    size_t colon_pos = address.find(':');
    uint16_t port = std::stoi(address.substr(colon_pos + 1));

    // Create server - bind to all interfaces (0.0.0.0)
    server = std::make_unique<rpc::server>(port);
    
    // Bind RPC functions
    server->bind("Try", &handle_try);
    server->bind("TryBatch", &handle_try_batch);
    server->bind("Done", &handle_done);
    
    std::cout << "Server listening on " << address << "\n";
    
    // Run server (blocks until stopped)
    server->run();
    
    std::cout << "Server exiting\n";
}
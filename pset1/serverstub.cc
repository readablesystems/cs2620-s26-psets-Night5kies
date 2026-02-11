#include "rpcgame.hh"

#include <rpc/server.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>

static std::unique_ptr<rpc::server> server_ptr;

static std::mutex g_try_mu;

static inline void parse_address(const std::string& address, std::string& host_out, uint16_t& port_out) {
    auto pos = address.rfind(':');
    if (pos == std::string::npos) {
        std::cerr << "Bad address (expected host:port): " << address << "\n";
        std::exit(1);
    }
    host_out = address.substr(0, pos);
    port_out = static_cast<uint16_t>(std::stoi(address.substr(pos + 1)));
}

void server_start(std::string address) {
    std::string host;
    uint16_t port = 0;
    parse_address(address, host, port);

    server_ptr = std::make_unique<rpc::server>(port);

    server_ptr->bind("Try", [](uint64_t serial, const std::string& name, uint64_t count) -> uint64_t {
        std::lock_guard<std::mutex> lk(g_try_mu);
        return server_process_try(serial, name.data(), name.size(), count);
    });

    server_ptr->bind("Done", []() -> std::tuple<std::string, std::string> {
        std::string client_csum = client_checksum();
        std::string server_csum = server_checksum();

        static std::once_flag shutdown_once;
        std::call_once(shutdown_once, [] {
            std::thread([] {
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(100ms);
                if (server_ptr) {
                    server_ptr->close_sessions();
                    server_ptr->stop();
                }
            }).detach();
        });

        return {std::move(client_csum), std::move(server_csum)};
    });

    std::cout << "Server listening on " << address << "\n";
    server_ptr->run();
    std::cout << "Server exiting\n";
}

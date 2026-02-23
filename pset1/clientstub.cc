#include "rpcgame.hh"

#include <rpc/client.h>
#include <rpc/msgpack.hpp>  // clmdep_msgpack::object_handle

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

class RPCGameClient {
public:
    static constexpr int WINDOW = 128;
    static constexpr int WORKERS = 2; // can be 1; callback is serialized anyway

    RPCGameClient(std::string host, uint16_t port)
        : _cli(std::move(host), port) {
        _workers.reserve(WORKERS);
        for (int i = 0; i < WORKERS; ++i) {
            _workers.emplace_back([this] { worker_loop(); });
        }
    }

    ~RPCGameClient() {
        {
            std::lock_guard<std::mutex> lk(_qmu);
            _stop = true;
        }
        _qcv.notify_all();
        for (auto& t : _workers) {
            if (t.joinable()) t.join();
        }
    }

    void send_try(const char* name, size_t name_len, uint64_t count) {
        {
            std::unique_lock<std::mutex> lk(_mu);
            _cv.wait(lk, [&] { return _in_flight < WINDOW; });
            ++_in_flight;
        }
    };

        const uint64_t serial = _serial.fetch_add(1, std::memory_order_relaxed);

        // rpclib requires owning string for the message
        std::string name_str(name, name_len);

        std::future<clmdep_msgpack::object_handle> fut =
            _cli.async_call("Try", serial, std::move(name_str), count);

        {
            std::lock_guard<std::mutex> lk(_qmu);
            _pending.emplace(std::move(fut));
        }
        _qcv.notify_one();
    }

    void finish() {
        {
            std::unique_lock<std::mutex> lk(_mu);
            _cv.wait(lk, [&] { return _in_flight == 0; });
        }

        auto oh = _cli.call("Done");
        auto tup = oh.as<std::tuple<std::string, std::string>>();

        const std::string& resp_client = std::get<0>(tup);
        const std::string& resp_server = std::get<1>(tup);

        // These should be called after all Try responses processed
        std::string my_client_checksum = client_checksum();
        std::string my_server_checksum = server_checksum();

        bool match = (my_client_checksum == resp_client) &&
                     (my_server_checksum == resp_server);

        std::cout << "client checksums: "
                  << my_client_checksum << "/" << resp_client
                  << "\nserver checksums: "
                  << my_server_checksum << "/" << resp_server
                  << "\nmatch: " << (match ? "true\n" : "false\n");
    }

private:
    void release_slot() {
        std::lock_guard<std::mutex> lk(_mu);
        if (_in_flight > 0) --_in_flight;
        _cv.notify_all();
    }

    void worker_loop() {
        while (true) {
            std::future<clmdep_msgpack::object_handle> fut;

            {
                std::unique_lock<std::mutex> lk(_qmu);
                _qcv.wait(lk, [&] { return _stop || !_pending.empty(); });
                if (_stop && _pending.empty()) return;
                fut = std::move(_pending.front());
                _pending.pop();
            }

            try {
                clmdep_msgpack::object_handle oh = fut.get();
                uint64_t value = oh.get().as<uint64_t>();

                // CRITICAL: serialize callback to match gRPC single-CQ-thread behavior
                {
                    std::lock_guard<std::mutex> lk(_recv_mu);
                    client_recv_try_response(value);
                }
            } catch (const std::exception& e) {
                release_slot();
                std::cerr << "Try RPC failed: " << e.what() << "\n";
                std::exit(1);
            }

            release_slot();
        }
    }

private:
    rpc::client _cli;
    std::atomic<uint64_t> _serial{1};

    std::mutex _mu;
    std::condition_variable _cv;
    int _in_flight = 0;

    std::mutex _qmu;
    std::condition_variable _qcv;
    std::queue<std::future<clmdep_msgpack::object_handle>> _pending;
    bool _stop = false;

    std::vector<std::thread> _workers;

    // Serialize client_recv_try_response() to prevent heap corruption
    std::mutex _recv_mu;
};

static std::unique_ptr<RPCGameClient> client;

static inline void parse_address(const std::string& address, std::string& host_out, uint16_t& port_out) {
    auto pos = address.rfind(':');
    if (pos == std::string::npos) {
        std::cerr << "Bad address (expected host:port): " << address << "\n";
        std::exit(1);
    }
    host_out = address.substr(0, pos);
    port_out = static_cast<uint16_t>(std::stoi(address.substr(pos + 1)));
}

void client_connect(std::string address) {
    std::string host;
    uint16_t port = 0;
    parse_address(address, host, port);
    client = std::make_unique<RPCGameClient>(std::move(host), port);
}

void client_send_try(const char* name, size_t name_len, uint64_t count) {
    client->send_try_windowed(name, name_len, count);
}

void client_finish() {
    client->finish_windowed();
}

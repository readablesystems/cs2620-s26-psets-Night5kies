// rpcgame_client_windowed_async_copy_avoid.cc

#include <grpcpp/grpcpp.h>
#include "rpcgame.grpc.pb.h"
#include "rpcgame.hh"

#include <atomic>
#include <charconv>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>

// Convert unsigned integer to chars and set protobuf string field without heap allocation.
template <class UInt, class Setter>
inline void set_uint_as_string(Setter&& set_fn, UInt v) {
    static_assert(std::is_unsigned_v<UInt>, "Use unsigned integer types here.");
    char buf[32]; // enough for uint64_t
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v);
    
    if (ec != std::errc{}) {
        std::cerr << "to_chars failed\n";
        std::exit(1);
    }
    set_fn(buf, static_cast<size_t>(ptr - buf));
}

class RPCGameClient {
public:
    static constexpr int WINDOW = 128; // tune: 1,8,32,64,128...

    explicit RPCGameClient(std::shared_ptr<grpc::Channel> channel)
        : _stub(RPCGame::NewStub(std::move(channel))) {
        _cq_thread = std::thread([this] { cq_loop(); });
    }

    ~RPCGameClient() {
        _cq.Shutdown();
        if (_cq_thread.joinable()) _cq_thread.join();
    }

    void send_try_windowed(const char* name, size_t name_len, uint64_t count) {
        // Backpressure: cap in-flight Try calls.
        {
            std::unique_lock<std::mutex> lk(_mu);
            _cv.wait(lk, [&] { return _in_flight < WINDOW; });
            ++_in_flight;
            if (_in_flight > _max_in_flight) {
                _max_in_flight = _in_flight;
            }
        }

        auto* call = new TryCall(this);

        // serial/count: avoid std::to_string allocations
        const uint64_t serial = _serial.fetch_add(1, std::memory_order_relaxed);
        set_uint_as_string([&](const char* p, size_t n) { call->request.set_serial(p, n); }, serial);

        // name: avoid temporary std::string(name, len)
        call->request.set_name(name, name_len);

        set_uint_as_string([&](const char* p, size_t n) { call->request.set_count(p, n); }, count);

        call->rpc = _stub->AsyncTry(&call->context, call->request, &_cq);
        call->rpc->Finish(&call->response, &call->status, (void*)static_cast<BaseCall*>(call));
    }

    void finish_windowed() {
        // Wait for all Try RPCs to complete.
        {
            std::unique_lock<std::mutex> lk(_mu);
            _cv.wait(lk, [&] { return _in_flight == 0; });
        }

        auto* call = new DoneCall(this);
        call->rpc = _stub->AsyncDone(&call->context, call->request, &_cq);
        call->rpc->Finish(&call->response, &call->status, (void*)static_cast<BaseCall*>(call));

        // Prevent program exit before Done prints results (common in assignments).
        {
            std::unique_lock<std::mutex> lk(_mu);
            _cv.wait(lk, [&] { return _done_seen; });
        }
    }

private:
    struct BaseCall {
        explicit BaseCall(RPCGameClient* parent) : parent(parent) {}
        virtual ~BaseCall() = default;
        virtual void complete(bool ok) = 0;
        RPCGameClient* parent;
    };

    struct TryCall final : BaseCall {
        explicit TryCall(RPCGameClient* p) : BaseCall(p) {}

        TryRequest request;
        TryResponse response;
        grpc::Status status;

        std::unique_ptr<grpc::ClientAsyncResponseReader<TryResponse>> rpc;
        grpc::ClientContext context;

        void complete(bool ok) override {
            auto release_slot = [&] {
                std::lock_guard<std::mutex> lk(parent->_mu);
                --parent->_in_flight;
                parent->_cv.notify_all();
            };

            if (!ok || !status.ok()) {
                release_slot();
                std::cerr << "Try RPC failed: " << status.error_code()
                          << ": " << status.error_message() << "\n";
                std::exit(1);
            }

            // Avoid copying response.value() into a separate std::string
            const std::string& valuestr = response.value();
            uint64_t value = from_str_chars<uint64_t>(valuestr);

            client_recv_try_response(value);

            release_slot();
            delete this;
        }
    };

    struct DoneCall final : BaseCall {
        explicit DoneCall(RPCGameClient* p) : BaseCall(p) {}

        DoneRequest request;
        DoneResponse response;
        grpc::Status status;

        std::unique_ptr<grpc::ClientAsyncResponseReader<DoneResponse>> rpc;
        grpc::ClientContext context;

        void complete(bool ok) override {
            if (!ok || !status.ok()) {
                std::cerr << "Done RPC failed: " << status.error_code()
                          << ": " << status.error_message() << "\n";
                std::exit(1);
            }

            // Avoid extra copies when comparing response fields
            const std::string my_client_checksum = client_checksum();
            const std::string my_server_checksum = server_checksum();

            const std::string& resp_client = response.client_checksum();
            const std::string& resp_server = response.server_checksum();

            bool match = (my_client_checksum == resp_client) &&
                         (my_server_checksum == resp_server);

            std::cout << "client checksums: "
                      << my_client_checksum << "/" << resp_client
                      << "\nserver checksums: "
                      << my_server_checksum << "/" << resp_server
                      << "\nmatch: " << (match ? "true\n" : "false\n")
                      << "max in-flight: " << parent->_max_in_flight << "\n";

            {
                std::lock_guard<std::mutex> lk(parent->_mu);
                parent->_done_seen = true;
                parent->_cv.notify_all();
            }

            delete this;
        }
    };

    void cq_loop() {
        void* tag = nullptr;
        bool ok = false;
        while (_cq.Next(&tag, &ok)) {
            static_cast<BaseCall*>(tag)->complete(ok);
        }
    }

private:
    std::unique_ptr<RPCGame::Stub> _stub;
    std::atomic<uint64_t> _serial{1};

    grpc::CompletionQueue _cq;
    std::thread _cq_thread;

    std::mutex _mu;
    std::condition_variable _cv;
    int _in_flight = 0;
    int _max_in_flight = 0;
    bool _done_seen = false;
};

// ---- C wrappers ----

static std::unique_ptr<RPCGameClient> client;

void client_connect(std::string address) {
    grpc::ChannelArguments args;

    // Compression often hurts tiny messages; flip back to GZIP if required.
    args.SetCompressionAlgorithm(GRPC_COMPRESS_NONE);
    // args.SetCompressionAlgorithm(GRPC_COMPRESS_GZIP);

    client = std::make_unique<RPCGameClient>(
        grpc::CreateCustomChannel(address, grpc::InsecureChannelCredentials(), args));
}

void client_send_try(const char* name, size_t name_len, uint64_t count) {
    client->send_try_windowed(name, name_len, count);
}

void client_finish() {
    client->finish_windowed();
}

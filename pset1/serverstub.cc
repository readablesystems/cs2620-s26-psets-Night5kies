#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "rpcgame.grpc.pb.h"
#include "rpcgame.hh"

#include <thread>
#include <type_traits>

static std::unique_ptr<grpc::Server> server;

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

class RPCGameServiceImpl final : public RPCGame::Service {
public:
    grpc::Status Try(grpc::ServerContext* context, const TryRequest* request,
                     TryResponse* response) {
        // parse request
        const std::string& serialstr = request->serial();
        uint64_t serial = from_str_chars<uint64_t>(serialstr);
        const std::string& name = request->name();
        const std::string& countstr = request->count();
        uint64_t count = from_str_chars<uint64_t>(countstr);

        // process parameters
        uint64_t value = server_process_try(serial, name.data(), name.size(),
                                            count);

        // construct response
        set_uint_as_string([&](const char* p, size_t n) { response->set_value(p, n); }, value);
        return grpc::Status::OK;
    }

    grpc::Status Done(grpc::ServerContext* context, const DoneRequest* request,
                      DoneResponse* response) {
        // calculate checksums
        std::string client_csum = client_checksum();
        std::string server_csum = server_checksum();

        // construct response
        response->set_client_checksum(client_csum);
        response->set_server_checksum(server_csum);

        // shut down server after 0.5sec
        std::thread shutdown_thread([] () {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(100ms);
            server->Shutdown();
        });
        shutdown_thread.detach();

        return grpc::Status::OK;
    }
};

void server_start(std::string address) {
    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();

    grpc::ServerBuilder builder;
    // Request a compressed channel
    builder.SetDefaultCompressionAlgorithm(GRPC_COMPRESS_NONE); // Disable compression for server channel
    // Listen on the given address without any authentication
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    // Register `service` as the instance through which we'll communicate with
    // clients
    RPCGameServiceImpl service;
    builder.RegisterService(&service);
    // Finally assemble the server
    server = builder.BuildAndStart();
    std::cout << "Server listening on " << address << "\n";

    // Wait for the server to shutdown. Note that some other thread must be
    // responsible for shutting down the server for this call to ever return.
    server->Wait();

    std::cout << "Server exiting\n";
}

#include <grpcpp/grpcpp.h>
#include <memory>
#include <map>

#include "rpcgame.grpc.pb.h"
#include "rpcgame.hh"

class RPCGameClient {
public:
    RPCGameClient(std::shared_ptr<grpc::Channel> channel, size_t window_size = 128)
        : _stub(RPCGame::NewStub(channel)), _window_size(window_size) {
    }

    void send_try(const char* name, size_t name_len, uint64_t count) {
        // Wait if window is full
        while (_in_flight >= _window_size) {
            process_one_response();
        }

        // Construct request with move semantics
        auto call = new TryCall();
        call->serial = _serial;
        call->request.set_serial(std::to_string(_serial));  // unavoidable for number->string
        call->request.set_name(name, name_len);  // direct set with pointer and length
        call->request.set_count(std::to_string(count));  // unavoidable for number->string
        ++_serial;

        // Send async request
        call->rpc = _stub->AsyncTry(&call->context, call->request, &_cq);
        call->rpc->Finish(&call->response, &call->status, (void*)call);
        
        ++_in_flight;
    }

    void finish() {
        // Wait for all in-flight requests to complete
        while (_in_flight > 0) {
            process_one_response();
        }

        // Deliver any buffered responses
        deliver_buffered_responses();

        // Now send Done request
        grpc::ClientContext context;
        DoneResponse response;
        grpc::Status status = _stub->Done(&context, DoneRequest(), &response);
        
        if (!status.ok()) {
            std::cerr << status.error_code() << ": " << status.error_message()
                << "\n";
            exit(1);
        }

        // Parse response - use const references to avoid copies
        const std::string& my_client_checksum = client_checksum();
        const std::string& my_server_checksum = server_checksum();
        const std::string& resp_client_checksum = response.client_checksum();
        const std::string& resp_server_checksum = response.server_checksum();
        
        bool ok = my_client_checksum == resp_client_checksum
            && my_server_checksum == resp_server_checksum;
        std::cout << "client checksums: "
            << my_client_checksum << "/" << resp_client_checksum
            << "\nserver checksums: "
            << my_server_checksum << "/" << resp_server_checksum
            << "\nmatch: " << (ok ? "true\n" : "false\n");
    }

private:
    struct TryCall {
        grpc::ClientContext context;
        TryRequest request;
        TryResponse response;
        grpc::Status status;
        std::unique_ptr<grpc::ClientAsyncResponseReader<TryResponse>> rpc;
        uint64_t serial;
    };

    void process_one_response() {
        void* tag;
        bool ok = false;
        
        // Block until next result is available
        if (!_cq.Next(&tag, &ok)) {
            std::cerr << "Completion queue shutdown unexpectedly\n";
            exit(1);
        }

        if (!ok) {
            std::cerr << "Request failed\n";
            exit(1);
        }

        // Retrieve and take ownership of the call
        std::unique_ptr<TryCall> call(static_cast<TryCall*>(tag));
        --_in_flight;

        if (!call->status.ok()) {
            std::cerr << call->status.error_code() << ": " 
                << call->status.error_message() << "\n";
            exit(1);
        }

        // Parse response - use const reference to avoid copy
        const std::string& valuestr = call->response.value();
        uint64_t value = from_str_chars<uint64_t>(valuestr);

        // Buffer the response with its serial number
        _buffered_responses[call->serial] = value;

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

    std::unique_ptr<RPCGame::Stub> _stub;
    grpc::CompletionQueue _cq;
    uint64_t _serial = 1;
    uint64_t _next_expected_serial = 1;
    size_t _window_size;
    size_t _in_flight = 0;
    std::map<uint64_t, uint64_t> _buffered_responses;  // serial -> value
};


static std::unique_ptr<RPCGameClient> client;


void client_connect(std::string address) {
    // Request a compressed channel
    grpc::ChannelArguments args;
    args.SetCompressionAlgorithm(GRPC_COMPRESS_NONE);
    
    // Create client with window size of 50 (adjust as needed)
    client = std::make_unique<RPCGameClient>(
        grpc::CreateCustomChannel(address, grpc::InsecureChannelCredentials(), args),
        50);  // window size parameter
}

void client_send_try(const char* name, size_t name_len, uint64_t count) {
    client->send_try(name, name_len, count);
}

void client_finish() {
    client->finish();
}
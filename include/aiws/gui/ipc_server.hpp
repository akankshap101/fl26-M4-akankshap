#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace aiws::gui{

// Background Unix-domain socket receiver. The callback must arrange GUI-thread delivery.
class IpcServer{
public:
    using AddCallback = std::function<void(const std::string&)>;
    IpcServer() = default;
    ~IpcServer();
    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    bool start(const std::string& socket_path, AddCallback on_add, std::string& error);
    void stop() noexcept;
    bool running() const noexcept{
        return worker_.joinable();
    }

private:
    void serve() noexcept;
    std::atomic_bool stopping_{false};
    std::thread worker_;
    std::string path_;
    AddCallback on_add_;
    int listening_fd_{-1};
};

std::string default_ipc_socket_path();
}

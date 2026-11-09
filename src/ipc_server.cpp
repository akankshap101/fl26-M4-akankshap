#include "aiws/gui/ipc_server.hpp"

#ifdef __unix__
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

namespace aiws::gui{

std::string default_ipc_socket_path(){
    const char* user = std::getenv("USER");
    if(user && *user){
        return "/tmp/aiws-" + std::string(user) + ".sock";
    }
#ifdef __unix__
    return "/tmp/aiws-" + std::to_string(getuid()) + ".sock";
#else
    return "/tmp/aiws-unknown.sock";
#endif
}

IpcServer::~IpcServer(){
    stop();
}

#ifdef __unix__
namespace{
constexpr std::uint32_t kMaxPathBytes = 1024 * 1024;

bool receive_exact(int fd, void* data, std::size_t count, const std::atomic_bool& stopping, std::chrono::steady_clock::time_point deadline){
    auto* dest = static_cast<unsigned char*>(data);
    while(count && !stopping.load()){
        if(std::chrono::steady_clock::now() >= deadline){
            return false;
        }
        pollfd pfd{fd, POLLIN, 0};
        const int ready = poll(&pfd, 1, 100);
        if(ready == 0){
            continue;
        }
        if(ready < 0){
            if(errno == EINTR){
                continue;
            }
            return false;
        }
        if(pfd.revents & (POLLERR | POLLNVAL)){
            return false;
        }
        const ssize_t n = recv(fd, dest, count, 0);
        if(n <= 0){
            if(n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)){
                continue;
            }
            return false;
        }
        dest += n;
        count -= static_cast<std::size_t>(n);
    }
    return count == 0;
}

void handle_client(int fd, const IpcServer::AddCallback& on_add, const std::atomic_bool& stopping){
    // Protocol: network-order uint32 byte count, then exactly that many UTF-8 path bytes.
    // Reply: one byte: '0' accepted, '1' unreadable/invalid.
    std::uint32_t network_size = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    if(!receive_exact(fd, &network_size, sizeof(network_size), stopping, deadline)){
        return;
    }
    const std::uint32_t count = ntohl(network_size);
    char answer = '1';
    if(count > 0 && count <= kMaxPathBytes){
        std::string path(count, '\0');
        if(receive_exact(fd, path.data(), count, stopping, deadline) && path.front() == '/' &&
           path.find('\0') == std::string::npos){
            const int file = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
            if(file >= 0){
                struct stat metadata{};
                const bool readable = fstat(file, &metadata) == 0 && S_ISREG(metadata.st_mode);
                close(file);
                if(readable){
                    // Validate readability without waiting for the GUI thread.
                    on_add(path);
                    answer = '0';
                }
            }
        }
    }
#ifdef MSG_NOSIGNAL
    (void)send(fd, &answer, 1, MSG_NOSIGNAL);
#else
    (void)send(fd, &answer, 1, 0);
#endif
}
}
#endif

bool IpcServer::start(const std::string& socket_path, AddCallback on_add, std::string& error){
#ifdef __unix__
    if(worker_.joinable()){
        error = "Server already running";
        return false;
    }
    if(!on_add || socket_path.empty() || socket_path.find('\0') != std::string::npos ||
       socket_path.size() >= sizeof(sockaddr_un::sun_path)){
        error = "Socket path is empty or too long";
        return false;
    }
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if(fd < 0){
        error = std::strerror(errno);
        return false;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, socket_path.c_str(), socket_path.size() + 1);
    // Don't replace an active server's socket.
    struct stat existing{};
    if(lstat(socket_path.c_str(), &existing) == 0){
        if(!S_ISSOCK(existing.st_mode)){
            error = "Socket path already names a non-socket file";
            close(fd);
            return false;
        }
        int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        const int connected =
            probe < 0 ? -1 : connect(probe, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        const int connect_error = errno;
        if(probe >= 0){
            close(probe);
        }
        if(probe < 0 || connected == 0 || connect_error != ECONNREFUSED){
            error = "Another server owns this socket, or its status cannot be determined";
            close(fd);
            return false;
        }
        if(unlink(socket_path.c_str()) != 0){
            error = std::strerror(errno);
            close(fd);
            return false;
        }
    }
    if(bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0){
        error = std::strerror(errno);
        close(fd);
        return false;
    }
    if(listen(fd, 16) != 0){
        error = std::strerror(errno);
        close(fd);
        unlink(socket_path.c_str());
        return false;
    }
    path_ = socket_path;
    on_add_ = std::move(on_add);
    listening_fd_ = fd;
    stopping_.store(false);
    try{
        worker_ = std::thread(&IpcServer::serve, this);
    }catch(...){
        close(fd);
        listening_fd_ = -1;
        unlink(path_.c_str());
        error = "Cannot start receiver thread";
        return false;
    }
    return true;
#else
    (void)socket_path;
    (void)on_add;
    error = "CR-1 requires a POSIX platform (use the course Docker container)";
    return false;
#endif
}

void IpcServer::stop() noexcept{
    stopping_.store(true);
    if(worker_.joinable()){
        worker_.join();
    }
}

void IpcServer::serve() noexcept{
#ifdef __unix__
    while(!stopping_.load()){
        pollfd pfd{listening_fd_, POLLIN, 0};
        int n = poll(&pfd, 1, 50);
        if(n <= 0 || !(pfd.revents & POLLIN)){
            continue;
        }
        int client = accept4(listening_fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if(client < 0){
            continue;
        }
        try{
            handle_client(client, on_add_, stopping_);
        }catch(...){
            // Keep serving if one request fails.
        }
        close(client);
    }
    close(listening_fd_);
    listening_fd_ = -1;
    unlink(path_.c_str());
#endif
}
}

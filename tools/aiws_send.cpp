#ifdef __unix__
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#ifdef __unix__
namespace{
using Clock = std::chrono::steady_clock;
struct Socket{
    int fd;
    ~Socket(){
        if(fd >= 0){
            close(fd);
        }
    }
};

bool wait_for(int fd, short events, Clock::time_point deadline){
    for(;;){
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if(remaining <= 0){
            return false;
        }
        pollfd pfd{fd, events, 0};
        const int ready = poll(&pfd, 1, static_cast<int>(remaining));
        if(ready < 0 && errno == EINTR){
            continue;
        }
        return ready > 0 && !(pfd.revents & (POLLERR | POLLNVAL)) && (pfd.revents & events);
    }
}

bool send_all(int fd, const void* data, std::size_t count, Clock::time_point deadline){
    auto* source = static_cast<const char*>(data);
    while(count > 0){
        if(!wait_for(fd, POLLOUT, deadline)){
            return false;
        }
        const ssize_t sent = send(fd, source, count, MSG_NOSIGNAL);
        if(sent < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)){
            continue;
        }
        if(sent <= 0){
            return false;
        }
        source += sent;
        count -= static_cast<std::size_t>(sent);
    }
    return true;
}
}
#endif

int main(int argc, char** argv){
    const char* user = std::getenv("USER");
    std::string socket_path;
    if(user && *user){
        socket_path = "/tmp/aiws-" + std::string(user) + ".sock";
    }
#ifdef __unix__
    else{
        socket_path = "/tmp/aiws-" + std::to_string(getuid()) + ".sock";
    }
#endif
    int i = 1;
    if(i + 1 < argc && std::string(argv[i]) == "--socket"){
        socket_path = argv[i + 1];
        i += 2;
    }
    if(argc - i != 2 || std::string(argv[i]) != "add"){
        std::cerr << "Usage: aiws_send [--socket PATH] add FILE\n";
        return 1;
    }
#ifdef __unix__
    if(!*argv[i + 1]){
        std::cerr << "Empty document path\n";
        return 1;
    }
    std::error_code error;
    const auto absolute = std::filesystem::absolute(argv[i + 1], error);
    if(error){
        std::cerr << "Cannot resolve document path: " << error.message() << '\n';
        return 1;
    }
    const std::string path = absolute.string();
    if(path.size() > 1024 * 1024){
        std::cerr << "Document path too long\n";
        return 1;
    }
    if(socket_path.empty() || socket_path.size() >= sizeof(sockaddr_un::sun_path)){
        std::cerr << "Socket path is empty or too long\n";
        return 2;
    }
    Socket socket{::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)};
    if(socket.fd < 0){
        std::cerr << "Socket creation failed\n";
        return 2;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
    const int connected = connect(socket.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if(connected < 0){
        if((errno != EINPROGRESS && errno != EINTR) ||
           !wait_for(socket.fd, POLLOUT, Clock::now() + std::chrono::seconds(2))){
            std::cerr << "No running AI Workspace could be reached within 2 seconds\n";
            return 2;
        }
        int status = 0;
        socklen_t size = sizeof(status);
        if(getsockopt(socket.fd, SOL_SOCKET, SO_ERROR, &status, &size) != 0 || status != 0){
            std::cerr << "Could not connect to AI Workspace\n";
            return 2;
        }
    }
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    const std::uint32_t length = htonl(static_cast<std::uint32_t>(path.size()));
    if(!send_all(socket.fd, &length, sizeof(length), deadline) ||
       !send_all(socket.fd, path.data(), path.size(), deadline)){
        std::cerr << "Communication failed while sending request\n";
        return 2;
    }
    char reply = 0;
    for(;;){
        if(!wait_for(socket.fd, POLLIN, deadline)){
            std::cerr << "Communication failed or timed out\n";
            return 2;
        }
        const ssize_t received = recv(socket.fd, &reply, 1, 0);
        if(received < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)){
            continue;
        }
        if(received != 1){
            std::cerr << "Server closed without a reply\n";
            return 2;
        }
        break;
    }
    if(reply == '0'){
        return 0;
    }
    if(reply == '1'){
        std::cerr << "Application rejected the path: file is not readable or valid\n";
        return 1;
    }
    std::cerr << "Invalid server reply\n";
    return 2;
#else
    std::cerr << "Remote Add requires Linux/POSIX sockets; use the course container\n";
    return 2;
#endif
}

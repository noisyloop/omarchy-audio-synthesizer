#pragma once

// JSON-lines control protocol over a Unix socket (docs/protocol.md).

#include "engine.h"

#include <filesystem>
#include <string>
#include <vector>

namespace sg {

struct Paths {
    std::filesystem::path socket;
    std::filesystem::path factoryPresets;
    std::filesystem::path userPresets;
    std::filesystem::path state;  // favorites.json, session.json
};

class Server {
public:
    Server(Engine& engine, Paths paths, bool dummyAudio);
    ~Server();

    /** Use a listening fd from systemd (LISTEN_FDS), or create the socket. */
    bool listen(int inheritedFd, std::string& error);
    /** Poll loop; returns when `stop` becomes true. */
    void run(const volatile bool& stop);
    void saveSession();
    void restoreSession();

private:
    struct Client {
        int fd = -1;
        std::string in, out;
        bool status = false, scope = false;
    };
    void accept();
    void readClient(Client& c);
    void handle(Client& c, const std::string& line);
    void send(Client& c, const Json& msg);
    void broadcast(const Json& msg);
    void sendTelemetry();
    Json stateMessage();
    Json presetListMessage();
    void setPower(bool on, Json* error);
    bool loadPresetByName(const std::string& name, std::string& error);
    void reply(Client& c, const char* type, const std::string& message);

    Engine& engine_;
    Paths paths_;
    bool dummyAudio_;
    int listenFd_ = -1;
    bool ownSocket_ = false;
    std::vector<Client> clients_;
    bool powered_ = false;
    std::string audioError_;
    std::vector<std::string> favorites_;
};

} // namespace sg

// CLIENT SIDE - Using WinSock to connect to a TCP server
// This program connects to the server, sends a sentence, and receives the reversed response.

#include <winsock2.h>
#include <ws2tcpip.h>
#include "WinSockClientH.h"


#pragma comment(lib, "ws2_32.lib")

#define DEFAULT_BUFFER_SIZE 1024

Client::Client() : clientSocket(INVALID_SOCKET), connectionStatus(false) {
    audioMgr->init();
}
Client::~Client() {
    disconnect();
}

bool Client::connect(const std::string& host, unsigned int port, const std::string& _username) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        addMessage("WSAStartup Failed", "Server");
        return false;
    }

    clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        addMessage("Socket creation Failed", "Server");
        WSACleanup();
        return false;
    }

    sockaddr_in server_address = {};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &server_address.sin_addr) <= 0) {
        addMessage("Invalid Address", "Server");
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    if (::connect(clientSocket, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address)) == SOCKET_ERROR) {
        addMessage("Connection failed", "Server");
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    username = _username;
    send(clientSocket, username.c_str(), static_cast<int>(username.size()), 0);

    char ack_buffer[256] = { 0 };
    int bytes = recv(clientSocket, ack_buffer, sizeof(ack_buffer) - 1, 0);
    if (bytes > 0) {
        ack_buffer[bytes] = '\0';
        addMessage(std::string(ack_buffer), "Server");
    }

    connectionStatus = true;
    receiveThread = std::jthread([this](std::stop_token stop_token) { receiveLoop(stop_token); });

    return true;
}

bool Client::sendMessage(const std::string& message) {
    if (!connectionStatus || clientSocket == INVALID_SOCKET) {
        return false;
    }
    if (send(clientSocket, message.c_str(), static_cast<int>(message.size()), 0) == SOCKET_ERROR) {
        addMessage("Send Failed", "Server");
        return false;
    }
    addMessage(message, username);

    return true;
}

bool Client::sendDirectMessage(const std::string& selected_user, const std::string& message) {
    if (!connectionStatus || clientSocket == INVALID_SOCKET) {
        return false;
    }
    std::string dm_message = "DM|" + username + "|" + selected_user + "|" + message;
    if (send(clientSocket, dm_message.c_str(), static_cast<int>(dm_message.size()), 0) == SOCKET_ERROR) {
        addMessage("Send Failed", "Server");
        return false;
    }
    addDirectMessage(message, username, selected_user);

    return true;
}

void Client::disconnect() {
    if (connectionStatus) {
        receiveThread.request_stop();
        if (clientSocket != INVALID_SOCKET) {
            closesocket(clientSocket);
            clientSocket = INVALID_SOCKET;
        }
        connectionStatus = false;
        {
            std::lock_guard<std::mutex> lock(chatMutex);
            chatHistory.clear();
        }
        {
            std::lock_guard<std::mutex> lock(dmMutex);
            dmHistory.clear();
            unreadDM.clear();
        }
        {
            std::lock_guard<std::mutex> lock(usersMutex);
            connectedUsers.clear();
        }
    }
}

bool Client::isConnected() const {
    return connectionStatus;
}

void Client::updateOpenWindows(const std::map<std::string, bool>& openWindows) {
    std::lock_guard<std::mutex> lock(dmMutex);
    openDMWindows = openWindows;
}

void Client::clearUnreadDM(const std::string& user) {
    std::lock_guard<std::mutex> lock(dmMutex);
    unreadDM[user] = false;
}

std::vector<ChatMessage> Client::getChatHistory() {
    std::lock_guard<std::mutex> lock(chatMutex);
    return chatHistory;
}

std::vector<ChatMessage> Client::getDMHistory(const std::string user) {
    std::lock_guard<std::mutex> lock(dmMutex);
    return dmHistory[user];
}

std::vector<std::string> Client::getConnectedUsers() {
    std::lock_guard<std::mutex> lock(usersMutex);
    return connectedUsers;
}
std::map<std::string, bool> Client::getUnreadDM() {
    std::lock_guard<std::mutex> lock(dmMutex);
    return unreadDM;
}

void Client::receiveLoop(std::stop_token stop_token) {
    char buffer[1024] = { 0 };

    while (connectionStatus && !stop_token.stop_requested()) {
        int bytes_received = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            std::string received(buffer);

            if (received.substr(0, 9) == "USERLIST|") {
                std::string userListStr = received.substr(9);

                std::vector<std::string> users;
                size_t start = 0;
                size_t end = userListStr.find(',');

                while (end != std::string::npos) {
                    users.push_back(userListStr.substr(start, end - start));
                    start = end + 1;
                    end = userListStr.find(',', start);
                }
                users.push_back(userListStr.substr(start));

                {
                    std::lock_guard<std::mutex> lock(usersMutex);
                    connectedUsers = users;
                }
                {
                    //removes unread messages from disconnected users
                    std::lock_guard<std::mutex> lock(dmMutex);
                    for (auto it = unreadDM.begin(); it != unreadDM.end();) {
                        if (std::find(users.begin(), users.end(), it->first) == users.end())
                            it = unreadDM.erase(it);
                        else
                            ++it;
                    }
                }
                continue;
            }
            else if (received.substr(0, 3) == "DM|") {
                size_t firstPipe = received.find('|');
                if (firstPipe != std::string::npos) {
                    size_t secondPipe = received.find('|', firstPipe + 1);
                    if (secondPipe != std::string::npos) {
                        size_t thridPipe = received.find('|', secondPipe + 1);
                        if (thridPipe != std::string::npos) {
                            std::string from = received.substr(firstPipe + 1, secondPipe - firstPipe - 1);
                            std::string to = received.substr(secondPipe + 1, thridPipe - secondPipe - 1);
                            std::string text = received.substr(thridPipe + 1);
                            addDirectMessage(text, from, from);
                            audioMgr->playNotification("DM");
                            {
                                std::lock_guard<std::mutex> lock(dmMutex);
                                if (openDMWindows.find(from) == openDMWindows.end() || !openDMWindows[from])
                                    unreadDM[from] = true;
                            }
                        }
                    }
                }
            }
            else if (received.find('|') != std::string::npos) {
                size_t pipePos = received.find('|');
                std::string from = received.substr(0, pipePos);
                std::string text = received.substr(pipePos + 1);
                addMessage(text, from);
                if(from == "Server")
                    audioMgr->playNotification("Server");
                else
                    audioMgr->playNotification("Global");
            }
            else {
                addMessage(received, "Server");
            }
        }
        else if (bytes_received == 0) {
            // Server disconnected
            addMessage("Server disconnected", "Server");
            audioMgr->playNotification("Server");
            connectionStatus = false;
            break;
        }
    }
}
void Client::addMessage(const std::string& text, const std::string& from) {
    std::lock_guard<std::mutex> lock(chatMutex);
    chatHistory.push_back({ text, from });
}
void Client::addDirectMessage(const std::string& text, const std::string& from, const std::string& other) {
    std::lock_guard<std::mutex> lock(dmMutex);
    ChatMessage cm(text, from);
    dmHistory[other].push_back(cm);
}

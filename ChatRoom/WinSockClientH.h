#pragma once

#include "AudioManager.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <map>

#pragma comment(lib, "ws2_32.lib")

struct ChatMessage {
    std::string text;
    std::string from;
};

class Client {
private:
    SOCKET clientSocket;
    bool connectionStatus;
    std::vector<ChatMessage> chatHistory;
    std::map<std::string, std::vector<ChatMessage>> dmHistory;
    std::mutex chatMutex;
    std::mutex dmMutex;
    std::mutex usersMutex;
    std::jthread receiveThread;
    std::string username = "";
    std::vector<std::string> connectedUsers;
    std::map<std::string, bool> unreadDM;
    std::map<std::string, bool> openDMWindows;

    AudioManager* audioMgr = new AudioManager;

public:
    Client();
    ~Client();

    bool connect(const std::string& host, unsigned int port, const std::string& username);
    bool sendMessage(const std::string& message);
    bool sendDirectMessage(const std::string& selected_user, const std::string& message);
    void disconnect();
    bool isConnected() const;
    void updateOpenWindows(const std::map<std::string, bool>& openWindows);
    void clearUnreadDM(const std::string& user);
    std::vector<ChatMessage> getChatHistory();
    std::vector<ChatMessage> getDMHistory(const std::string user);
    std::vector<std::string> getConnectedUsers();
    std::map<std::string, bool> getUnreadDM();
private:
    void receiveLoop(std::stop_token stop_token);
    void addMessage(const std::string& text, const std::string& from);
    void addDirectMessage(const std::string& text, const std::string& from, const std::string& other);

};

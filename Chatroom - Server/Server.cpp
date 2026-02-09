#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <mutex>

struct ClientInfo {
    SOCKET socket;
    std::string username;
};

std::vector<ClientInfo> connectedClients;
std::mutex clientMutex;

void broadcastUserList() {
    std::lock_guard<std::mutex> lock(clientMutex);
    std::string userlist = "USERLIST|";
    for (size_t i = 0; i < connectedClients.size(); i++) {
        userlist += connectedClients[i].username;
        if (i < connectedClients.size() - 1)
            userlist += ",";
    }

    for (const auto& client : connectedClients)
        send(client.socket, userlist.c_str(), static_cast<int>(userlist.size()), 0);
}

void handleClient(SOCKET client_socket, const std::string& username, const std::string& client_ip, int client_port) {
    std::cout << "[" << username << " @ " << client_ip << "|" << client_port << "] Client handler started" << std::endl;

    while (true) {
        char buffer[1024] = { 0 };
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            std::cout << "[" << username << "] Received: " << buffer << std::endl;

            std::string message(buffer);

            if (message.substr(0, 3) == "DM|") {
                size_t firstPipe = message.find('|');
                if (firstPipe != std::string::npos) {
                    size_t secondPipe = message.find('|', firstPipe + 1);
                    if (secondPipe != std::string::npos) {
                        size_t thirdPipe = message.find('|', secondPipe + 1);
                        if (thirdPipe != std::string::npos) {
                            std::string to = message.substr(secondPipe + 1, thirdPipe - secondPipe - 1);

                            std::lock_guard<std::mutex> lock(clientMutex);
                            for (const auto& client : connectedClients) {
                                if (client.username == to) {
                                    send(client.socket, message.c_str(), static_cast<int>(message.size()), 0);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            else {
                std::string formatted_msg = username + "|" + message;
                {
                    std::lock_guard<std::mutex> lock(clientMutex);
                    for (const auto& client : connectedClients)
                        if (client.socket != client_socket)
                            send(client.socket, formatted_msg.c_str(), static_cast<int>(formatted_msg.size()), 0);
                }
            }
        }
        else if (bytes_received == 0) {
            // Client disconnected gracefully - socket closed
            std::cout << "[" << username << "] Client disconnected" << std::endl;
            break;
        }
        else {
            // Error occurred
            std::cerr << "[" << username << "] recv failed: " << WSAGetLastError() << std::endl;
            break;
        }
    }

    std::string disconnect_msg = "Server|" + username + " has disconnected";
    {
        std::lock_guard<std::mutex> lock(clientMutex);
        for (const auto& client : connectedClients) {
            if (client.socket != client_socket) {
                send(client.socket, disconnect_msg.c_str(), static_cast<int>(disconnect_msg.size()), 0);
            }
        }

        connectedClients.erase(std::remove_if(connectedClients.begin(), connectedClients.end(),
                [client_socket](const ClientInfo& c) { return c.socket == client_socket; }),
            connectedClients.end()
        );
    }
    broadcastUserList();

    // Clean up this client's socket
    closesocket(client_socket);
    std::cout << "[" << username << "] Connection closed" << std::endl;
}

void doWork(SOCKET server_socket) {
    while (true) {
        sockaddr_in client_address = {};
        int client_address_len = sizeof(client_address);
        SOCKET client_socket = accept(server_socket, (sockaddr*)&client_address, &client_address_len);

        if (client_socket == INVALID_SOCKET) {
            std::cerr << "Accept failed with error| " << WSAGetLastError() << std::endl;
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_address.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "Accepted connection from " << client_ip << "|" << ntohs(client_address.sin_port) << std::endl;

        char username_buffer[256] = { 0 };
        int bytes_received = recv(client_socket, username_buffer, sizeof(username_buffer) - 1, 0);

        std::string username;
        if (bytes_received > 0) {
            username_buffer[bytes_received] = '\0';
            username = std::string(username_buffer);
            std::cout << "User '" << username << "' connected from " << client_ip << "|" << ntohs(client_address.sin_port) << std::endl;
        }
        else {
            std::cerr << "Failed to receive username, closing connection" << std::endl;
            closesocket(client_socket);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(clientMutex);

            if (username.find('|') != std::string::npos) {
                std::cout << "Username cannot contain '|', rejecting connection" << std::endl;
                std::string error_msg = "Username cannot contain '|', rejecting connection";
                send(client_socket, error_msg.c_str(), static_cast<int>(error_msg.size()), 0);
                closesocket(client_socket);
                continue;
            }

            bool usernameExists = false;
            for (const auto& client : connectedClients)
                if (client.username == username) {
                    usernameExists = true;
                    break;
                }

            if (usernameExists) {
                std::cout << "Username '" << username << "' already taken, rejecting connection" << std::endl;
                std::string error_msg = username + " already taken, rejecting connection";
                send(client_socket, error_msg.c_str(), static_cast<int>(error_msg.size()), 0);
                closesocket(client_socket);
                continue;
            }
        }

        std::string welcome_msg = "Welcome, " + username + "!";
        send(client_socket, welcome_msg.c_str(), static_cast<int>(welcome_msg.size()), 0);

        {
            std::lock_guard<std::mutex> lock(clientMutex);
            connectedClients.push_back({ client_socket, username });

            std::string join_msg = "Server|" + username + " has joined the chat";
            for (const auto& client : connectedClients)
                if (client.socket != client_socket)
                    send(client.socket, join_msg.c_str(), static_cast<int>(join_msg.size()), 0);
        }

        broadcastUserList();

        // Spawn a new thread to handle this client
        std::thread client_thread([client_socket, username, client_ip, client_address]() {
            handleClient(client_socket, username, client_ip, ntohs(client_address.sin_port));
            });

        // Detach so thread runs independently
        client_thread.detach();
    }
}
#pragma comment(lib, "Ws2_32.lib")
int server() {
    // Step 1: Initialize WinSock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed with error| " << WSAGetLastError() << std::endl;
        return 1;
    }

    // Step 2: Create a socket
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed with error| " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // Step 3: Bind the socket
    sockaddr_in server_address = {};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(65432);  // Server port
    server_address.sin_addr.s_addr = INADDR_ANY; // Accept connections on any IP address

    if (bind(server_socket, (sockaddr*)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
        std::cerr << "Bind failed with error| " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    // Step 4: Listen for incoming connections
    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed with error| " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "Server is listening on port 65432..." << std::endl;

    //single thread continuously accepting connections
    doWork(server_socket);

    closesocket(server_socket);
    WSACleanup();
    return 0;
}

int main() {
    server();
    return 0;
}

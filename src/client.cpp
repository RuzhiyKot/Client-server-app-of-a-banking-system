#include "client.h"
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <thread>

BankClient::BankClient(const std::string& serverHost, int serverPort)
    : serverHost_(serverHost), serverPort_(serverPort), sockfd_(-1), connected_(false) {}

bool BankClient::connectToServer() {
    sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_ < 0) {
        std::cerr << "Error creating socket" << std::endl;
        return false;
    }
    
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort_);
    
    if (inet_pton(AF_INET, serverHost_.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid server address" << std::endl;
        close(sockfd_);
        return false;
    }
    
    if (connect(sockfd_, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Connection failed" << std::endl;
        close(sockfd_);
        return false;
    }
    
    connected_ = true;
    std::cout << "Connected to bank server at " << serverHost_ << ":" << serverPort_ << std::endl;
    return true;
}

void BankClient::disconnect() {
    if (connected_) {
        sendCommand("LOGOUT");
        close(sockfd_);
        connected_ = false;
    }
}

void BankClient::run() {
    if (!connected_) {
        std::cerr << "Not connected to server" << std::endl;
        return;
    }
    
    std::thread receiver([this]() {
        while (connected_) {
            std::string response = receiveResponse();
            if (!response.empty()) {
                std::cout << "\n=== Server Response ===" << std::endl;
                std::cout << response << std::endl;
                std::cout << "=======================" << std::endl;
                std::cout << "> " << std::flush;
            }
        }
    });
    
    displayMenu();
    processUserInput();
    
    receiver.detach();
    disconnect();
}

void BankClient::displayMenu() {
    std::cout << "\n=== Secure Bank System Client ===" << std::endl;
    std::cout << "Type commands to interact with the bank system." << std::endl;
    std::cout << "Type 'HELP' for available commands." << std::endl;
    std::cout << "Type 'EXIT' to quit." << std::endl;
    std::cout << "==================================" << std::endl;
}

void BankClient::processUserInput() {
    std::string input;
    
    while (connected_) {
        std::cout << "> ";
        std::getline(std::cin, input);
        
        if (input == "EXIT" || input == "QUIT") {
            break;
        }
        
        if (!sendCommand(input)) {
            std::cout << "Connection lost" << std::endl;
            break;
        }
    }
}

bool BankClient::sendCommand(const std::string& command) {
    if (!connected_) return false;
    
    std::string cmd = command + "\n";
    if (send(sockfd_, cmd.c_str(), cmd.length(), 0) < 0) {
        connected_ = false;
        return false;
    }
    return true;
}

std::string BankClient::receiveResponse() {
    if (!connected_) return "";
    
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    
    int bytesRead = recv(sockfd_, buffer, sizeof(buffer) - 1, 0);
    if (bytesRead <= 0) {
        connected_ = false;
        return "";
    }
    
    return std::string(buffer);
}

void BankClient::clearInputBuffer() {
    std::cin.clear();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

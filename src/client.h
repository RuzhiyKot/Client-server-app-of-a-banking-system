#ifndef CLIENT_H
#define CLIENT_H

#include <string>

class BankClient {
public:
    BankClient(const std::string& serverHost, int serverPort);
    bool connectToServer();
    void disconnect();
    void run();
    
private:
    std::string serverHost_;
    int serverPort_;
    int sockfd_;
    bool connected_;
    
    void displayMenu();
    void processUserInput();
    bool sendCommand(const std::string& command);
    std::string receiveResponse();
    void clearInputBuffer();
};

#endif

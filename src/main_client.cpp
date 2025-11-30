#include <iostream>
#include <cstdlib>
#include "client.h"

int main(int argc, char* argv[]) {
    std::string serverHost = "127.0.0.1";
    int serverPort = 8080;
    
    if (argc > 1) {
        serverHost = argv[1];
    }
    if (argc > 2) {
        serverPort = std::atoi(argv[2]);
    }
    
    std::cout << "Connecting to Secure Bank System..." << std::endl;
    std::cout << "Server: " << serverHost << ":" << serverPort << std::endl;
    
    BankClient client(serverHost, serverPort);
    
    if (!client.connectToServer()) {
        std::cerr << "Failed to connect to server" << std::endl;
        return 1;
    }
    
    client.run();
    
    return 0;
}
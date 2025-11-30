#include <iostream>
#include <cstdlib>
#include "server.h"

int main(int argc, char* argv[]) {
    int port = 8080;
    std::string dbFilename = "data/accounts.dat";
    
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }
    if (argc > 2) {
        dbFilename = argv[2];
    }
    
    std::cout << "Starting Secure Bank Server..." << std::endl;
    std::cout << "Port: " << port << std::endl;
    std::cout << "Database: " << dbFilename << std::endl;
    
    BankServer server(port, dbFilename);
    
    if (!server.start()) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    std::cout << "Bank server running. Press Enter to stop..." << std::endl;
    std::cin.get();
    
    server.stop();
    std::cout << "Server stopped." << std::endl;
    
    return 0;
}

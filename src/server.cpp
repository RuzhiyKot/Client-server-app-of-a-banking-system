#include "server.h"
#include "crypto.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <chrono>
#include <fstream>
#include <filesystem>

BankServer::BankServer(int port, const std::string& dbFilename) 
    : port_(port), running_(false), database_(dbFilename) {
    
    // Создаем директорию для данных если нужно
    std::filesystem::create_directories("data");
    
    // База уже загружена в конструкторе Database
    checkAndCreateSuperUsers();
    
    // Загружаем состояние сервера (очереди запросов)
    loadServerState();
}

bool BankServer::start() {
    running_ = true;
    serverThread_ = std::thread(&BankServer::run, this);
    std::cout << "Bank server started on port " << port_ << std::endl;
    return true;
}

BankServer::~BankServer() {
    stop();
}

void BankServer::stop() {
    running_ = false;
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
    saveServerState();
}

void BankServer::saveDatabase() {
    database_.saveToFile();
}

void BankServer::saveServerState() {
    std::cout << "Saving server state..." << std::endl;
    saveDatabase();
    saveQueuesToFile();
    std::cout << "Server state saved successfully" << std::endl;
}

void BankServer::loadServerState() {
    std::cout << "Loading server state..." << std::endl;
    loadQueuesFromFile();
    std::cout << "Server state loaded successfully" << std::endl;
}

void BankServer::saveQueuesToFile() {
    // Сохраняем очередь верификации
    std::ofstream verificationFile("data/verification_queue.dat");
    if (verificationFile) {
        std::queue<ApprovalRequest> tempQueue = verificationQueue_;
        while (!tempQueue.empty()) {
            const auto& request = tempQueue.front();
            verificationFile << request.requestId << "|"
                           << request.clientAccountId << "|"
                           << request.operationType << "|"
                           << request.amount << "|"
                           << request.targetAccount << "|"
                           << request.description << "|"
                           << request.timestamp << "|"
                           << request.status << "\n";
            tempQueue.pop();
        }
    }
}

void BankServer::loadQueuesFromFile() {
    // Очищаем текущие очереди
    while (!verificationQueue_.empty()) verificationQueue_.pop();
    
    // Загружаем очередь верификации
    std::ifstream verificationFile("data/verification_queue.dat");
    if (verificationFile) {
        std::string line;
        while (std::getline(verificationFile, line)) {
            if (line.empty()) continue;
            
            std::stringstream ss(line);
            ApprovalRequest request;
            std::string amountStr, timestampStr;
            
            if (std::getline(ss, request.requestId, '|') &&
                std::getline(ss, request.clientAccountId, '|') &&
                std::getline(ss, request.operationType, '|') &&
                std::getline(ss, amountStr, '|') &&
                std::getline(ss, request.targetAccount, '|') &&
                std::getline(ss, request.description, '|') &&
                std::getline(ss, timestampStr, '|') &&
                std::getline(ss, request.status, '|')) {
                
                try { request.amount = std::stod(amountStr); } catch (...) { request.amount = 0; }
                try { request.timestamp = std::stol(timestampStr); } catch (...) { request.timestamp = std::time(nullptr); }
                
                verificationQueue_.push(request);
            }
        }
        std::cout << "Loaded " << verificationQueue_.size() << " verification requests from disk." << std::endl;
    }
}

void BankServer::cleanupVerificationQueue() {
    std::lock_guard<std::mutex> lock(approvalMutex_);
    
    std::queue<ApprovalRequest> cleanedQueue;
    while (!verificationQueue_.empty()) {
        ApprovalRequest request = verificationQueue_.front();
        verificationQueue_.pop();
        
        // Проверяем, существует ли клиент и нуждается ли он в верификации
        ClientData* client = database_.findClient(request.clientAccountId);
        if (client && client->status == ClientStatus::PENDING_VERIFICATION) {
            cleanedQueue.push(request);
        }
    }
    verificationQueue_ = cleanedQueue;
}

void BankServer::run() {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        std::cerr << "Error creating socket" << std::endl;
        return;
    }
    
    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port_);
    
    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Error binding socket" << std::endl;
        close(serverSocket);
        return;
    }
    
    if (listen(serverSocket, 10) < 0) {
        std::cerr << "Error listening on socket" << std::endl;
        close(serverSocket);
        return;
    }
    
    std::cout << "Server listening on port " << port_ << std::endl;
    
    while (running_) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(serverSocket, &readfds);
        
        timeval timeout{0, 100000};
        
        int activity = select(serverSocket + 1, &readfds, nullptr, nullptr, &timeout);
        
        if (activity > 0 && FD_ISSET(serverSocket, &readfds)) {
            int clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientLen);
            if (clientSocket >= 0) {
                std::lock_guard<std::mutex> lock(clientsMutex_);
                clients_[clientSocket] = ClientSession{"", nullptr, std::time(nullptr), false};
                std::thread clientThread(&BankServer::handleClient, this, clientSocket);
                clientThread.detach();
                
                char clientIP[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
                std::cout << "New client connected from " << clientIP << std::endl;
            }
        }
    }
    
    close(serverSocket);
}

void BankServer::handleClient(int clientSocket) {
    char buffer[1024];
    
    sendResponse(clientSocket, 
        "Welcome to Secure Bank System!\n"
        "Available commands:\n"
        "RATES - view current interest rates\n"
        "REGISTER \"Full Name\" \"Birth Date\" \"Passport\" \"Password\" - create account\n"
        "LOGIN <account_id> <password> - login to existing account\n"
        "SUPERLOGIN <account_id> <password> - security officer login\n"
        "HELP - show all commands");
    
    while (running_) {
        memset(buffer, 0, sizeof(buffer));
        int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytesRead <= 0) {
            break;
        }
        
        std::string command(buffer);
        command.erase(command.find_last_not_of(" \n\r\t") + 1);
        
        ClientSession* session = nullptr;
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            auto it = clients_.find(clientSocket);
            if (it != clients_.end()) {
                session = &it->second;
            }
        }
        
        if (session) {
            processCommand(clientSocket, *session, command);
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        if (clients_.find(clientSocket) != clients_.end()) {
            if (isSuperUser(clients_[clientSocket].accountId)) {
                superUsers_.erase(clients_[clientSocket].accountId);
            }
        }
        clients_.erase(clientSocket);
    }
    close(clientSocket);
    std::cout << "Client disconnected" << std::endl;
}

void BankServer::processCommand(int clientSocket, ClientSession& session, const std::string& command) {
    std::vector<std::string> args;
    std::string currentArg;
    bool inQuotes = false;
    
    // Парсим аргументы
    for (size_t i = 0; i < command.length(); ++i) {
        char c = command[i];
        
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ' ' && !inQuotes) {
            if (!currentArg.empty()) {
                args.push_back(currentArg);
                currentArg.clear();
            }
        } else {
            currentArg += c;
        }
    }
    
    // Добавляем последний аргумент
    if (!currentArg.empty()) {
        args.push_back(currentArg);
    }
    
    if (args.empty()) {
        sendResponse(clientSocket, "ERROR: Empty command");
        return;
    }
    
    std::string cmd = args[0];
    args.erase(args.begin()); // Удаляем команду из аргументов
    
    // Команды, доступные без авторизации
    if (cmd == "RATES") {
        handleRatesInfo(clientSocket);
        return;
    } 
    else if (cmd == "HELP") {
        std::string helpText = "Available commands:\n"
        "RATES - view current interest rates\n";
        
        // Показываем команды для неавторизованных пользователей
        if (!session.isAuthenticated) {
            helpText += "REGISTER \"Full Name\" \"Birth Date\" \"Passport\" \"Password\" - create account\n"
                       "LOGIN <account_id> <password>\n"
                       "SUPERLOGIN <account_id> <password> - security officer login\n";
        } else {
            // Команды для авторизованных пользователей
            helpText += "ACCOUNTS - list all your accounts\n"
                       "DEPOSIT <amount> [description] - deposit to first account\n"
                       "DEPOSIT_TO <account_index> <amount> [description] - deposit to specific account\n"
                       "WITHDRAW <amount> [description] - withdraw from first account\n"
                       "WITHDRAW_FROM <account_index> <amount> [description] - withdraw from specific account\n"
                       "TRANSFER <target_accountID> <amount> [description] - transfer from first account\n"
                       "TRANSFER_FROM <account_index> <target_accountID> <amount> [description]\n"
                       "HISTORY [account_index] - show transaction history\n"
                       "CREATE_ACCOUNT <type> - create new account (0=Savings, 1=Checking, 2=Credit, 3=Deposit)\n"
                       "INFO - show client information\n";
            
            if (isSuperUser(session.accountId)) {
                helpText += "SECURITY OFFICER COMMANDS:\n"
                           "PENDING_REQUESTS - show pending operation requests\n"
                           "PENDING_VERIFICATIONS - show pending verification requests\n"
                           "APPROVE <request_index> - approve operation\n"
                           "REJECT <request_index> - reject operation\n"
                           "VERIFY <client_index> - verify client account\n"
                           "SET_RATES <credit_rate> <deposit_rate> - set interest rates\n"
                           "SETTINGS - show current bank settings\n";
            }
            
            helpText += "LOGOUT - logout from system\n";
        }
        
        helpText += "HELP - show this help\n"
                    "EXIT - quit the application";
        
        sendResponse(clientSocket, helpText);
        return;
    }
    
    else if (cmd == "REGISTER") {
        if (session.isAuthenticated) {
            sendResponse(clientSocket, "ERROR: You are already logged in. Please logout first to register a new account.");
        } else {
            handleRegister(clientSocket, args);
        }
        return;
    } 
    else if (cmd == "LOGIN") {
        if (session.isAuthenticated) {
            sendResponse(clientSocket, "ERROR: You are already logged in. Please logout first to login with different account.");
        } else {
            handleLogin(clientSocket, args);
        }
        return;
    } 
    else if (cmd == "SUPERLOGIN") {
        if (session.isAuthenticated) {
            sendResponse(clientSocket, "ERROR: You are already logged in. Please logout first to login with different account.");
        } else {
            handleSuperLogin(clientSocket, args);
        }
        return;
    } 
    
    else if (!session.isAuthenticated) {
        sendResponse(clientSocket, "ERROR: Please login first. Available commands without login: RATES, REGISTER, LOGIN, SUPERLOGIN, HELP");
        return;
    } 
    
    // Команды, требующие авторизации
    else if (cmd == "DEPOSIT") {
        handleDeposit(clientSocket, session, args);
    } else if (cmd == "DEPOSIT_TO") {
        handleDepositToAccount(clientSocket, session, args);
    } else if (cmd == "WITHDRAW") {
        handleWithdraw(clientSocket, session, args);
    } else if (cmd == "WITHDRAW_FROM") {
        handleWithdrawFromAccount(clientSocket, session, args);
    } else if (cmd == "TRANSFER") {
        handleTransfer(clientSocket, session, args);
    } else if (cmd == "TRANSFER_FROM") {
        handleTransferFromAccount(clientSocket, session, args);
    } else if (cmd == "HISTORY") {
        handleHistory(clientSocket, session, args);
    } else if (cmd == "ACCOUNTS") {
        handleAccountList(clientSocket, session);
    } else if (cmd == "CREATE_ACCOUNT") {
        handleCreateAccount(clientSocket, session, args);
    } else if (cmd == "INFO") {
        handleInfo(clientSocket, session);
    } else if (cmd == "PENDING_REQUESTS") {
        handlePendingRequests(clientSocket, session);
    } else if (cmd == "PENDING_VERIFICATIONS") {
        handlePendingVerifications(clientSocket, session);
    } else if (cmd == "APPROVE") {
        handleApproveRequest(clientSocket, session, args);
    } else if (cmd == "REJECT") {
        handleRejectRequest(clientSocket, session, args);
    } else if (cmd == "VERIFY") {
        handleVerifyClient(clientSocket, session, args);
    } else if (cmd == "SET_RATES") {
        handleSetRates(clientSocket, session, args);
    } else if (cmd == "SETTINGS") {
        handleSettings(clientSocket, session);
    } else if (cmd == "LOGOUT") {
        session.isAuthenticated = false;
        session.clientData = nullptr;
        if (isSuperUser(session.accountId)) {
            superUsers_.erase(session.accountId);
        }
        sendResponse(clientSocket, "Logged out successfully");
    } else {
        sendResponse(clientSocket, "ERROR: Unknown command. Type HELP for available commands.");
    }
}

void BankServer::sendResponse(int clientSocket, const std::string& response) {
    send(clientSocket, response.c_str(), response.length(), 0);
}

void BankServer::handleRatesInfo(int clientSocket) {
    BankSettings settings = database_.getSettings();
    
    std::stringstream response;
    response << "Current Bank Rates:\n"
             << "Credit Interest Rate: " << settings.creditInterestRate << "%\n"
             << "Deposit Interest Rate: " << settings.depositInterestRate << "%\n"
             << "Large Operation Threshold: $" << settings.largeOperationThreshold << "\n"
             << "Large Loan Threshold: $" << settings.largeLoanThreshold << "\n\n"
             << "New users must be verified to access full functionality.";
    
    sendResponse(clientSocket, response.str());
}

void BankServer::handleRegister(int clientSocket, const std::vector<std::string>& args) {
    // Получаем сессию для проверки авторизации
    ClientSession* session = nullptr;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto it = clients_.find(clientSocket);
        if (it != clients_.end()) {
            session = &it->second;
        }
    }
    
    // Проверяем, не авторизован ли уже пользователь
    if (session && session->isAuthenticated) {
        sendResponse(clientSocket, "ERROR: You are already logged in. Please logout first to register a new account.");
        return;
    }
    
    std::vector<std::string> parsedArgs;
    std::string currentArg;
    bool inQuotes = false;
    
    for (const auto& arg : args) {
        if (!inQuotes && arg.front() == '"') {
            inQuotes = true;
            currentArg = arg.substr(1);
        } else if (inQuotes && arg.back() == '"') {
            inQuotes = false;
            currentArg += " " + arg.substr(0, arg.length() - 1);
            parsedArgs.push_back(currentArg);
            currentArg.clear();
        } else if (inQuotes) {
            currentArg += " " + arg;
        } else {
            parsedArgs.push_back(arg);
        }
    }
    
    if (parsedArgs.size() < 4) {
        sendResponse(clientSocket, 
            "ERROR: Usage: REGISTER \"Full Name\" \"Birth Date\" \"Passport Data\" \"Password\"\n"
            "Example: REGISTER \"Ivanov Ivan Ivanovich\" \"1990-05-15\" \"4510123456\" \"mypassword123\"");
        return;
    }
    
    std::string fullName = parsedArgs[0];
    std::string birthDate = parsedArgs[1];
    std::string passportData = parsedArgs[2];
    std::string password = parsedArgs[3];
    
    // Валидация
    if (fullName.empty() || fullName.length() < 5 || fullName.find(' ') == std::string::npos) {
        sendResponse(clientSocket, "ERROR: Full name must be at least 5 characters long and contain first and last name separated by space");
        return;
    }
    
    if (birthDate.length() != 10 || birthDate[4] != '-' || birthDate[7] != '-') {
        sendResponse(clientSocket, "ERROR: Birth date must be in format YYYY-MM-DD");
        return;
    }
    
    try {
        int year = std::stoi(birthDate.substr(0, 4));
        int month = std::stoi(birthDate.substr(5, 2));
        int day = std::stoi(birthDate.substr(8, 2));
        
        if (year < 1900 || year > 2025 || month < 1 || month > 12 || day < 1 || day > 31) {
            sendResponse(clientSocket, "ERROR: Invalid birth date");
            return;
        }
    } catch (...) {
        sendResponse(clientSocket, "ERROR: Invalid birth date format");
        return;
    }
    
    if (passportData.length() != 10 || !std::all_of(passportData.begin(), passportData.end(), ::isdigit)) {
        sendResponse(clientSocket, "ERROR: Passport data must be exactly 10 digits");
        return;
    }
    
    if (password.length() < 6) {
        sendResponse(clientSocket, "ERROR: Password must be at least 6 characters long");
        return;
    }
    
    if (database_.isPassportExists(passportData)) {
        sendResponse(clientSocket, "ERROR: User with this passport data already exists");
        return;
    }
    
    // Генерация accountId
    std::string accountId;
    do {
        accountId = "ACC" + std::to_string(1000 + (rand() % 9000));
    } while (database_.findClient(accountId) != nullptr);
    
    ClientData newClient;
    newClient.accountId = accountId;
    newClient.fullName = fullName;
    newClient.birthDate = birthDate;
    newClient.passportData = passportData;
    newClient.passwordHash = Crypto::hashPassword(password);
    newClient.status = ClientStatus::PENDING_VERIFICATION;
    
    if (database_.addClient(newClient)) {
        // Сохраняем базу данных
        saveDatabase();
        
        // Создаем запрос на верификацию
        std::string requestId = createVerificationRequest(accountId, fullName);
        
        std::stringstream response;
        response << "SUCCESS: Registration completed!\n"
                 << "Your account ID: " << accountId << " (SAVE THIS!)\n"
                 << "Full Name: " << fullName << "\n"
                 << "Status: PENDING VERIFICATION\n\n"
                 << "As an unverified user, you have limited functionality:\n"
                 << "- Max transaction: $" << database_.getSettings().largeOperationThreshold / 10 << "\n"
                 << "- No credit accounts\n"
                 << "- No deposit accounts\n\n"
                 << "Your account is awaiting security verification.\n"
                 << "You can login now with: LOGIN " << accountId << " " << password;
        
        sendResponse(clientSocket, response.str());
        std::cout << "New client registered: " << accountId << " - " << fullName << std::endl;
    } else {
        sendResponse(clientSocket, "ERROR: Registration failed");
    }
}

std::string BankServer::createVerificationRequest(const std::string& clientAccountId, const std::string& clientName) {
    std::lock_guard<std::mutex> lock(approvalMutex_);
    
    // Проверяем, нет ли уже запроса на верификацию для этого клиента
    std::queue<ApprovalRequest> tempQueue = verificationQueue_;
    while (!tempQueue.empty()) {
        if (tempQueue.front().clientAccountId == clientAccountId) {
            return tempQueue.front().requestId; // Запрос уже существует
        }
        tempQueue.pop();
    }
    
    // Находим клиента для получения полной информации
    ClientData* client = database_.findClient(clientAccountId);
    
    ApprovalRequest request;
    request.requestId = generateRequestId();
    request.clientAccountId = clientAccountId;
    request.operationType = "VERIFICATION";
    request.amount = 0;
    request.targetAccount = "";
    
    // Формируем описание
    std::stringstream desc;
    desc << "Name: " << clientName;
    if (client) {
        desc << " | Birth: " << client->birthDate << " | Passport: " << client->passportData;
    }
    request.description = desc.str();
    
    request.timestamp = std::time(nullptr);
    request.status = "PENDING";
    
    verificationQueue_.push(request);
    
    // Сохраняем очередь
    saveQueuesToFile();
    
    std::cout << "Verification request created: " << request.requestId 
              << " for " << clientAccountId << " - " << clientName << std::endl;
    
    return request.requestId;
}

void BankServer::handleLogin(int clientSocket, const std::vector<std::string>& args) {
    // Получаем сессию для проверки авторизации
    ClientSession* session = nullptr;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto it = clients_.find(clientSocket);
        if (it != clients_.end()) {
            session = &it->second;
        }
    }
    
    // Проверяем, не авторизован ли уже пользователь
    if (session && session->isAuthenticated) {
        sendResponse(clientSocket, "ERROR: You are already logged in. Please logout first to login with different account.");
        return;
    }
    
    if (args.size() != 2) {
        sendResponse(clientSocket, "ERROR: Usage: LOGIN <account_id> <password>");
        return;
    }
    
    ClientData* client = database_.authenticateClient(args[0], args[1]);
    if (client) {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto it = clients_.find(clientSocket);
        if (it != clients_.end()) {
            it->second.accountId = args[0];
            it->second.clientData = client;
            it->second.isAuthenticated = true;
            it->second.loginTime = std::time(nullptr);
        }
        
        std::stringstream response;
        response << "SUCCESS: Login successful\n"
                 << "Account: " << client->accountId << "\n"
                 << "Status: " << (client->status == ClientStatus::VERIFIED ? "VERIFIED" : "PENDING VERIFICATION") << "\n"
                 << "Accounts: " << client->accounts.size();
        
        if (client->status != ClientStatus::VERIFIED) {
            response << "\n\nNOTE: Your account is not yet verified.\n"
                     << "Some features are limited until security verification.";
        }
        
        sendResponse(clientSocket, response.str());
        std::cout << "Client logged in: " << args[0] << std::endl;
    } else {
        sendResponse(clientSocket, "ERROR: Invalid account ID or password");
    }
}

void BankServer::handleSuperLogin(int clientSocket, const std::vector<std::string>& args) {
    // Получаем сессию для проверки авторизации
    ClientSession* session = nullptr;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto it = clients_.find(clientSocket);
        if (it != clients_.end()) {
            session = &it->second;
        }
    }
    
    // Проверяем, не авторизован ли уже пользователь
    if (session && session->isAuthenticated) {
        sendResponse(clientSocket, "ERROR: You are already logged in. Please logout first to login with different account.");
        return;
    }
    
    if (args.size() != 2) {
        sendResponse(clientSocket, "ERROR: Usage: SUPERLOGIN <account_id> <password>");
        return;
    }
    
    ClientData* client = database_.authenticateClient(args[0], args[1]);
    if (client && isSuperUser(args[0])) {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto it = clients_.find(clientSocket);
        if (it != clients_.end()) {
            it->second.accountId = args[0];
            it->second.clientData = client;
            it->second.isAuthenticated = true;
            it->second.loginTime = std::time(nullptr);
        }
        
        superUsers_[args[0]] = it->second;
        
        sendResponse(clientSocket, "SUCCESS: Security officer login successful");
        std::cout << "Security officer logged in: " << args[0] << std::endl;
    } else {
        sendResponse(clientSocket, "ERROR: Invalid security credentials");
    }
}

bool BankServer::isSuperUser(const std::string& accountId) {
    return accountId == "SUPER001";
}

bool BankServer::isClientVerified(ClientSession& session) {
    return session.clientData && session.clientData->status == ClientStatus::VERIFIED;
}

bool BankServer::canPerformOperation(ClientSession& session, const std::string& operationType, double amount) {
    if (!session.clientData) return false;
    
    BankSettings settings = database_.getSettings();
    
    // Неверифицированные пользователи ограничены
    if (session.clientData->status != ClientStatus::VERIFIED) {
        if (operationType == "CREATE_ACCOUNT") {
            return true;
        }
        else if (operationType == "TRANSFER" || operationType == "WITHDRAW") {
            // Лимит для неверифицированных
            double unverifiedLimit = settings.largeOperationThreshold / 10;
            if (amount > unverifiedLimit) {
                return false;
            }
        }
        else if (operationType == "CREDIT_OPERATION") {
            return false; // Неверифицированные не могут работать с кредитами
        }
    }
    
    return true;
}

void BankServer::handleDeposit(int clientSocket, ClientSession& session, const std::vector<std::string>& args) {
    if (args.size() < 1) {
        sendResponse(clientSocket, "ERROR: Usage: DEPOSIT <amount> [description]");
        return;
    }
    
    try {
        double amount = std::stod(args[0]);
        std::string description = args.size() > 1 ? args[1] : "";
        
        if (session.clientData->accounts.empty()) {
            sendResponse(clientSocket, "ERROR: No accounts available");
            return;
        }
        
        if (!canPerformOperation(session, "DEPOSIT", amount)) {
            sendResponse(clientSocket, "ERROR: Operation not allowed for unverified accounts");
            return;
        }
        
        if (session.clientData->accounts[0].deposit(amount, description)) {
            database_.saveToFile();
            sendResponse(clientSocket, "DEPOSIT successful");
        } else {
            sendResponse(clientSocket, "ERROR: Deposit failed");
        }
    } catch (const std::exception& e) {
        sendResponse(clientSocket, "ERROR: Invalid amount");
    }
}

void BankServer::handleDepositToAccount(int clientSocket, ClientSession& session, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        sendResponse(clientSocket, "ERROR: Usage: DEPOSIT_TO <account_index> <amount> [description]");
        return;
    }
    
    try {
        int accountIndex = std::stoi(args[0]);
        double amount = std::stod(args[1]);
        std::string description = args.size() > 2 ? args[2] : "";
        
        if (accountIndex < 0 || accountIndex >= static_cast<int>(session.clientData->accounts.size())) {
            sendResponse(clientSocket, "ERROR: Invalid account index");
            return;
        }
        
        if (!canPerformOperation(session, "DEPOSIT", amount)) {
            sendResponse(clientSocket, "ERROR: Operation not allowed for unverified accounts");
            return;
        }
        
        if (session.clientData->accounts[accountIndex].deposit(amount, description)) {
            database_.saveToFile();
            sendResponse(clientSocket, "DEPOSIT successful to account " + 
                        session.clientData->accounts[accountIndex].getNumber());
        } else {
            sendResponse(clientSocket, "ERROR: Deposit failed");
        }
    } catch (const std::exception& e) {
        sendResponse(clientSocket, "ERROR: Invalid amount or account index");
    }
}

void BankServer::handleWithdraw(int clientSocket, ClientSession& session, const std::vector<std::string>& args) {
    if (args.size() < 1) {
        sendResponse(clientSocket, "ERROR: Usage: WITHDRAW <amount> [description]");
        return;
    }
    
    try {
        double amount = std::stod(args[0]);
        std::string description = args.size() > 1 ? args[1] : "";
        BankSettings settings = database_.getSettings();
        
        if (session.clientData->accounts.empty()) {
            sendResponse(clientSocket, "ERROR: No accounts available");
            return;
        }
        
        if (!canPerformOperation(session, "WITHDRAW", amount)) {
            sendResponse(clientSocket, "ERROR: Operation not allowed for unverified accounts or amount too large");
            return;
        }
        
        // Проверка на крупную операцию для верифицированных пользователей
        if (isClientVerified(session) && amount > settings.largeOperationThreshold) {
            sendResponse(clientSocket, 
                "NOTICE: Large withdrawal requires security approval.\n"
                "Request sent to security department. Please wait...");
            
            std::string requestId = createApprovalRequest(
                session.accountId, "WITHDRAW", amount, "", description
            );
            
            if (!waitForApproval(requestId, 30)) {
                sendResponse(clientSocket, "ERROR: Operation rejected by security or timeout exceeded");
                return;
            }
        }
        
        if (session.clientData->accounts[0].withdraw(amount, description)) {
            database_.saveToFile();
            sendResponse(clientSocket, "WITHDRAW successful");
        } else {
            sendResponse(clientSocket, "ERROR: Withdrawal failed - insufficient funds");
        }
    } catch (const std::exception& e) {
        sendResponse(clientSocket, "ERROR: Invalid amount");
    }
}

void BankServer::handleWithdrawFromAccount(int clientSocket, ClientSession& session, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        sendResponse(clientSocket, "ERROR: Usage: WITHDRAW_FROM <account_index> <amount> [description]");
        return;
    }
    
    try {
        int accountIndex = std::stoi(args[0]);
        double amount = std::stod(args[1]);
        std::string description = args.size() > 2 ? args[2] : "";
        BankSettings settings = database_.getSettings();
        
        if (accountIndex < 0 || accountIndex >= static_cast<int>(session.clientData->accounts.size())) {
            sendResponse(clientSocket, "ERROR: Invalid account index");
            return;
        }
        
        if (!canPerformOperation(session, "WITHDRAW", amount)) {
            sendResponse(clientSocket, "ERROR: Operation not allowed for unverified accounts or amount too large");
            return;
        }
        
        // Проверка на крупную операцию для верифицированных пользователей
        if (isClientVerified(session) && amount > settings.largeOperationThreshold) {
            sendResponse(clientSocket, 
                "NOTICE: Large withdrawal requires security approval.\n"
                "Request sent to security department. Please wait...");
            
            std::string requestId = createApprovalRequest(
                session.accountId, "WITHDRAW", amount, "", description
            );
            
            if (!waitForApproval(requestId, 30)) {
                sendResponse(clientSocket, "ERROR: Operation rejected by security or timeout exceeded");
                return;
            }
        }
        
        if (session.clientData->accounts[accountIndex].withdraw(amount, description)) {
            database_.saveToFile();
            sendResponse(clientSocket, "WITHDRAW successful from account " + 
                        session.clientData->accounts[accountIndex].getNumber());
        } else {
            sendResponse(clientSocket, "ERROR: Withdrawal failed - insufficient funds");
        }
    } catch (const std::exception& e) {
        sendResponse(clientSocket, "ERROR: Invalid amount or account index");
    }
}

void BankServer::handleTransfer(int clientSocket, ClientSession& session, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        sendResponse(clientSocket, "ERROR: Usage: TRANSFER <target_accountID> <amount> [description]");
        return;
    }
    
    try {
        std::string targetAccount = args[0];
        double amount = std::stod(args[1]);
        std::string description = args.size() > 2 ? args[2] : "";
        BankSettings settings = database_.getSettings();
        
        if (session.clientData->accounts.empty()) {
            sendResponse(clientSocket, "ERROR: No accounts available");
            return;
        }
        
        if (!canPerformOperation(session, "TRANSFER", amount)) {
            sendResponse(clientSocket, "ERROR: Operation not allowed for unverified accounts or amount too large");
            return;
        }
        
        ClientData* targetClient = database_.findClient(targetAccount);
        if (!targetClient || targetClient->accounts.empty()) {
            sendResponse(clientSocket, "ERROR: Target account not found");
            return;
        }
        
        // Проверка на крупную операцию для верифицированных пользователей
        if (isClientVerified(session) && amount > settings.largeOperationThreshold) {
            sendResponse(clientSocket, 
                "NOTICE: Large transfer requires security approval.\n"
                "Request sent to security department. Please wait...");
            
            std::string requestId = createApprovalRequest(
                session.accountId, "TRANSFER", amount, targetAccount, description
            );
            
            if (!waitForApproval(requestId, 30)) {
                sendResponse(clientSocket, "ERROR: Operation rejected by security or timeout exceeded");
                return;
            }
        }
        
        if (session.clientData->accounts[0].transfer(targetClient->accounts[0], amount, description)) {
            database_.saveToFile();
            sendResponse(clientSocket, "TRANSFER successful");
        } else {
            sendResponse(clientSocket, "ERROR: Transfer failed - insufficient funds");
        }
    } catch (const std::exception& e) {
        sendResponse(clientSocket, "ERROR: Invalid amount");
    }
}

void BankServer::handleTransferFromAccount(int clientSocket, ClientSession& session, const std::vector<std::string>& args) {
    if (args.size() < 3) {
        sendResponse(clientSocket, "ERROR: Usage: TRANSFER_FROM <account_index> <target_accountID> <amount> [description]");
        return;
    }
    
    try {
        int accountIndex = std::stoi(args[0]);
        std::string targetAccount = args[1];
        double amount = std::stod(args[2]);
        std::string description = args.size() > 3 ? args[3] : "";
        BankSettings settings = database_.getSettings();
        
        if (accountIndex < 0 || accountIndex >= static_cast<int>(session.clientData->accounts.size())) {
            sendResponse(clientSocket, "ERROR: Invalid account index");
            return;
        }
        
        if (!canPerformOperation(session, "TRANSFER", amount)) {
            sendResponse(clientSocket, "ERROR: Operation not allowed for unverified accounts or amount too large");
            return;
        }
        
        ClientData* targetClient = database_.findClient(targetAccount);
        if (!targetClient || targetClient->accounts.empty()) {
            sendResponse(clientSocket, "ERROR: Target account not found");
            return;
        }
        
        // Проверка на крупную операцию для верифицированных пользователей
        if (isClientVerified(session) && amount > settings.largeOperationThreshold) {
            sendResponse(clientSocket, 
                "NOTICE: Large transfer requires security approval.\n"
                "Request sent to security department. Please wait...");
            
            std::string requestId = createApprovalRequest(
                session.accountId, "TRANSFER", amount, targetAccount, description
            );
            
            if (!waitForApproval(requestId, 30)) {
                sendResponse(clientSocket, "ERROR: Operation rejected by security or timeout exceeded");
                return;
            }
        }
        
        if (session.clientData->accounts[accountIndex].transfer(targetClient->accounts[0], amount, description)) {
            database_.saveToFile();
            sendResponse(clientSocket, "TRANSFER successful from account " + 
                        session.clientData->accounts[accountIndex].getNumber());
        } else {
            sendResponse(clientSocket, "ERROR: Transfer failed - insufficient funds");
        }
    } catch (const std::exception& e) {
        sendResponse(clientSocket, "ERROR: Invalid amount or account index");
    }
}

void BankServer::handleCreateAccount(int clientSocket, ClientSession& session, const std::vector<std::string>& args) {
    if (args.size() < 1) {
        sendResponse(clientSocket, "ERROR: Usage: CREATE_ACCOUNT <type>");
        return;
    }
    
    try {
        int type = std::stoi(args[0]);
        if (type < 0 || type > 3) {
            sendResponse(clientSocket, "ERROR: Invalid account type. Use: 0=Savings, 1=Checking, 2=Credit, 3=Deposit");
            return;
        }
        
        AccountType accountType = static_cast<AccountType>(type);
        
        // Проверка прав для неверифицированных пользователей
        if (!isClientVerified(session) && (accountType == AccountType::CREDIT || accountType == AccountType::DEPOSIT)) {
            sendResponse(clientSocket, "ERROR: Credit and Deposit accounts require account verification");
            return;
        }
        
        if (!canPerformOperation(session, "CREATE_ACCOUNT")) {
            sendResponse(clientSocket, "ERROR: Cannot create accounts at this time");
            return;
        }
        
        std::string prefix;
        switch (accountType) {
            case AccountType::SAVINGS: prefix = "SAV"; break;
            case AccountType::CHECKING: prefix = "CHK"; break;
            case AccountType::CREDIT: prefix = "CRD"; break;
            case AccountType::DEPOSIT: prefix = "DEP"; break;
        }
        
        std::string newAccountNumber = session.accountId + "_" + prefix + "_" + 
                                     std::to_string(session.clientData->accounts.size() + 1);
        
        Account newAccount(newAccountNumber, accountType, 0.0);
        
        // Лимит для кредитного счёта
        if (accountType == AccountType::CREDIT) {
            BankSettings settings = database_.getSettings();
            newAccount.setCreditLimit(settings.largeLoanThreshold);
        }
        
        session.clientData->accounts.push_back(newAccount);
        database_.saveToFile();
        
        std::stringstream response;
        response << "SUCCESS: New " << newAccount.getTypeString() 
                 << " account created: " << newAccountNumber;
        
        if (accountType == AccountType::CREDIT) {
            response << " with credit limit: $" << newAccount.getCreditLimit();
        }
        
        sendResponse(clientSocket, response.str());
        
    } catch (const std::exception& e) {
        sendResponse(clientSocket, "ERROR: Invalid account type");
    }
}

void BankServer::handleAccountList(int clientSocket, ClientSession& session) {
    std::stringstream response;
    response << "Your accounts:\n";
    
    for (size_t i = 0; i < session.clientData->accounts.size(); ++i) {
        const auto& account = session.clientData->accounts[i];
        response << "[" << i << "] " << account.getNumber() 
                 << " (" << account.getTypeString() << "): $" 
                 << account.getBalance();
        if (account.getCreditLimit() > 0) {
            response << " (Credit limit: $" << account.getCreditLimit() << ")";
        }
        response << "\n";
    }
    
    if (session.clientData->accounts.empty()) {
        response << "No accounts yet.";
    }
    
    sendResponse(clientSocket, response.str());
}

void BankServer::handleHistory(int clientSocket, ClientSession& session, const std::vector<std::string>& args) {
    int accountIndex = 0;
    
    if (!args.empty()) {
        try {
            accountIndex = std::stoi(args[0]);
        } catch (...) {
            sendResponse(clientSocket, "ERROR: Invalid account index");
            return;
        }
    }
    
    if (accountIndex < 0 || accountIndex >= static_cast<int>(session.clientData->accounts.size())) {
        sendResponse(clientSocket, "ERROR: Invalid account index");
        return;
    }
    
    std::stringstream response;
    response << "Transaction history for " << session.clientData->accounts[accountIndex].getNumber() << ":\n";
    
    const auto& transactions = session.clientData->accounts[accountIndex].getTransactionHistory();
    for (const auto& txn : transactions) {
        response << txn.id << ": " << txn.type << " $" << txn.amount;
        if (!txn.description.empty()) {
            response << " (" << txn.description << ")";
        }
        if (!txn.targetAccount.empty()) {
            response << " -> " << txn.targetAccount;
        }
        response << "\n";
    }
    
    if (transactions.empty()) {
        response << "No transactions found";
    }
    
    sendResponse(clientSocket, response.str());
}

void BankServer::handleInfo(int clientSocket, ClientSession& session) {
    std::stringstream response;
    response << "Client Information:\n"
             << "Account ID: " << session.clientData->accountId << "\n"
             << "Full Name: " << session.clientData->fullName << "\n"
             << "Birth Date: " << session.clientData->birthDate << "\n"
             << "Status: " << (session.clientData->status == ClientStatus::VERIFIED ? "VERIFIED" : "PENDING VERIFICATION") << "\n"
             << "Number of accounts: " << session.clientData->accounts.size() << "\n";
    
    if (session.clientData->status != ClientStatus::VERIFIED) {
        BankSettings settings = database_.getSettings();
        response << "\nUNVERIFIED ACCOUNT LIMITATIONS:\n"
                 << "- Max transaction: $" << settings.largeOperationThreshold / 10 << "\n"
                 << "- No credit accounts\n"
                 << "- No deposit accounts\n"
                 << "- Awaiting security verification";
    }
    
    sendResponse(clientSocket, response.str());
}

std::string BankServer::generateRequestId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 9999);
    
    std::stringstream ss;
    ss << "REQ" << std::time(nullptr) << dis(gen);
    return ss.str();
}

std::string BankServer::createApprovalRequest(const std::string& clientAccountId, const std::string& operationType, 
                                             double amount, const std::string& targetAccount, const std::string& description) {
    std::lock_guard<std::mutex> lock(approvalMutex_);
    
    ApprovalRequest request;
    request.requestId = generateRequestId();
    request.clientAccountId = clientAccountId;
    request.operationType = operationType;
    request.amount = amount;
    request.targetAccount = targetAccount;
    request.description = description;
    request.timestamp = std::time(nullptr);
    request.status = "PENDING";
    
    approvalQueue_.push(request);
    approvalCV_.notify_all();
    
    std::cout << "Approval request created: " << request.requestId 
              << " for " << clientAccountId << " - " << operationType << " $" << amount << std::endl;
    
    return request.requestId;
}

bool BankServer::waitForApproval(const std::string& requestId, int timeoutSeconds) {
    auto startTime = std::chrono::steady_clock::now();
    
    while (true) {
        std::unique_lock<std::mutex> lock(approvalMutex_);
        
        // Ищем запрос в очереди
        std::queue<ApprovalRequest> tempQueue = approvalQueue_;
        bool found = false;
        std::string status;
        
        while (!tempQueue.empty()) {
            const auto& request = tempQueue.front();
            if (request.requestId == requestId) {
                status = request.status;
                found = true;
                break;
            }
            tempQueue.pop();
        }
        
        if (found) {
            if (status == "APPROVED") {
                // Удаляем одобренный запрос из очереди
                std::queue<ApprovalRequest> newQueue;
                while (!approvalQueue_.empty()) {
                    if (approvalQueue_.front().requestId != requestId) {
                        newQueue.push(approvalQueue_.front());
                    }
                    approvalQueue_.pop();
                }
                approvalQueue_ = newQueue;
                return true;
            } else if (status == "REJECTED") {
                // Удаляем отклоненный запрос из очереди
                std::queue<ApprovalRequest> newQueue;
                while (!approvalQueue_.empty()) {
                    if (approvalQueue_.front().requestId != requestId) {
                        newQueue.push(approvalQueue_.front());
                    }
                    approvalQueue_.pop();
                }
                approvalQueue_ = newQueue;
                return false;
            }
            // Если статус все еще PENDING, продолжаем ждать
        } else {
            // Если запрос не найден, значит он был обработан и удален
            // Предполагаем, что он был одобрен (так как отклоненные мы явно обрабатываем)
            return true;
        }
        
        // Проверяем таймаут
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - startTime);
        
        if (elapsed.count() >= timeoutSeconds) {
            std::cout << "Approval timeout for request: " << requestId << std::endl;
            
            // Удаляем запрос по таймауту
            std::queue<ApprovalRequest> newQueue;
            while (!approvalQueue_.empty()) {
                if (approvalQueue_.front().requestId != requestId) {
                    newQueue.push(approvalQueue_.front());
                }
                approvalQueue_.pop();
            }
            approvalQueue_ = newQueue;
            
            return false;
        }
        
        // Ждем уведомления
        approvalCV_.wait_for(lock, std::chrono::seconds(1));
    }
}

bool BankServer::waitForVerification(const std::string& requestId, int timeoutSeconds) {
    auto startTime = std::chrono::steady_clock::now();
    
    while (true) {
        std::unique_lock<std::mutex> lock(approvalMutex_);
        
        std::queue<ApprovalRequest> tempQueue = verificationQueue_;
        bool found = false;
        
        while (!tempQueue.empty()) {
            if (tempQueue.front().requestId == requestId) {
                found = true;
                break;
            }
            tempQueue.pop();
        }
        
        // Если запрос удален из очереди - значит верифицирован
        if (!found) {
            return true;
        }
        
        // Проверяем таймаут
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - startTime);
        
        if (elapsed.count() >= timeoutSeconds) {
            std::cout << "Verification timeout for request: " << requestId << std::endl;
            return false;
        }
        
        // Ждем уведомления
        approvalCV_.wait_for(lock, std::chrono::seconds(1));
    }
}

void BankServer::handlePendingRequests(int clientSocket, ClientSession& session) {
    if (!isSuperUser(session.accountId)) {
        sendResponse(clientSocket, "ERROR: Access denied. Super user privileges required.");
        return;
    }
    
    std::lock_guard<std::mutex> lock(approvalMutex_);
    
    if (approvalQueue_.empty()) {
        sendResponse(clientSocket, "No pending operation requests.");
        return;
    }
    
    std::stringstream response;
    response << "Pending Operation Requests:\n";
    
    std::queue<ApprovalRequest> tempQueue = approvalQueue_;
    int index = 0;
    
    while (!tempQueue.empty()) {
        const auto& request = tempQueue.front();
        response << "[" << index << "] " << request.requestId 
                 << " | Client: " << request.clientAccountId
                 << " | Operation: " << request.operationType
                 << " | Amount: $" << request.amount;
        
        if (!request.targetAccount.empty()) {
            response << " | To: " << request.targetAccount;
        }
        
        if (!request.description.empty()) {
            response << " | Desc: " << request.description;
        }
        
        response << " | Time: " << std::ctime(&request.timestamp);
        tempQueue.pop();
        index++;
    }
    
    sendResponse(clientSocket, response.str());
}

void BankServer::handlePendingVerifications(int clientSocket, ClientSession& session) {
    if (!isSuperUser(session.accountId)) {
        sendResponse(clientSocket, "ERROR: Access denied. Super user privileges required.");
        return;
    }
    
    // Очищаем очередь от невалидных запросов
    cleanupVerificationQueue();
    
    std::lock_guard<std::mutex> lock(approvalMutex_);
    
    if (verificationQueue_.empty()) {
        sendResponse(clientSocket, "No pending verification requests.");
        return;
    }
    
    std::stringstream response;
    response << "Pending Verification Requests:\n";
    
    std::queue<ApprovalRequest> tempQueue = verificationQueue_;
    int index = 0;
    
    while (!tempQueue.empty()) {
        const auto& request = tempQueue.front();
        ClientData* client = database_.findClient(request.clientAccountId);
        
        response << "[" << index << "] " << request.requestId 
                 << " | Client: " << request.clientAccountId
                 << " | Name: " << (client ? client->fullName : "Unknown")
                 << " | Passport: " << (client ? client->passportData : "Unknown")
                 << " | Time: " << std::ctime(&request.timestamp);
        tempQueue.pop();
        index++;
    }
    
    sendResponse(clientSocket, response.str());
}

void BankServer::handleApproveRequest(int clientSocket, ClientSession& session, const std::vector<std::string>& args) {
    if (!isSuperUser(session.accountId)) {
        sendResponse(clientSocket, "ERROR: Access denied. Super user privileges required.");
        return;
    }
    
    if (args.size() < 1) {
        sendResponse(clientSocket, "ERROR: Usage: APPROVE <request_index>");
        return;
    }
    
    try {
        int requestIndex = std::stoi(args[0]);
        
        std::lock_guard<std::mutex> lock(approvalMutex_);
        
        if (approvalQueue_.empty()) {
            sendResponse(clientSocket, "ERROR: No pending requests");
            return;
        }
        
        // Создаем временную копию для поиска
        std::queue<ApprovalRequest> tempQueue = approvalQueue_;
        std::vector<ApprovalRequest> requests;
        
        while (!tempQueue.empty()) {
            requests.push_back(tempQueue.front());
            tempQueue.pop();
        }
        
        if (requestIndex < 0 || requestIndex >= static_cast<int>(requests.size())) {
            sendResponse(clientSocket, "ERROR: Invalid request index");
            return;
        }
        
        // Находим и обновляем целевой запрос
        ApprovalRequest targetRequest = requests[requestIndex];
        targetRequest.status = "APPROVED";
        
        // Создаем новую очередь с обновленным запросом
        std::queue<ApprovalRequest> newQueue;
        for (size_t i = 0; i < requests.size(); i++) {
            if (i == static_cast<size_t>(requestIndex)) {
                newQueue.push(targetRequest);
            } else {
                newQueue.push(requests[i]);
            }
        }
        approvalQueue_ = newQueue;
        
        approvalCV_.notify_all();
        
        sendResponse(clientSocket, "SUCCESS: Request " + targetRequest.requestId + " approved");
        std::cout << "Request approved: " << targetRequest.requestId 
                  << " by " << session.accountId << std::endl;
        
    } catch (const std::exception& e) {
        sendResponse(clientSocket, "ERROR: Invalid request index");
    }
}

void BankServer::handleRejectRequest(int clientSocket, ClientSession& session, const std::vector<std::string>& args) {
    if (!isSuperUser(session.accountId)) {
        sendResponse(clientSocket, "ERROR: Access denied. Super user privileges required.");
        return;
    }
    
    if (args.size() < 1) {
        sendResponse(clientSocket, "ERROR: Usage: REJECT <request_index>");
        return;
    }
    
    try {
        int requestIndex = std::stoi(args[0]);
        
        std::lock_guard<std::mutex> lock(approvalMutex_);
        
        if (approvalQueue_.empty()) {
            sendResponse(clientSocket, "ERROR: No pending requests");
            return;
        }
        
        // Создаем временную копию для поиска
        std::queue<ApprovalRequest> tempQueue = approvalQueue_;
        std::vector<ApprovalRequest> requests;
        
        while (!tempQueue.empty()) {
            requests.push_back(tempQueue.front());
            tempQueue.pop();
        }
        
        if (requestIndex < 0 || requestIndex >= static_cast<int>(requests.size())) {
            sendResponse(clientSocket, "ERROR: Invalid request index");
            return;
        }
        
        // Находим и обновляем целевой запрос
        ApprovalRequest targetRequest = requests[requestIndex];
        targetRequest.status = "REJECTED";
        
        // Создаем новую очередь с обновленным запросом
        std::queue<ApprovalRequest> newQueue;
        for (size_t i = 0; i < requests.size(); i++) {
            if (i == static_cast<size_t>(requestIndex)) {
                newQueue.push(targetRequest);
            } else {
                newQueue.push(requests[i]);
            }
        }
        approvalQueue_ = newQueue;
        
        approvalCV_.notify_all();
        
        sendResponse(clientSocket, "SUCCESS: Request " + targetRequest.requestId + " rejected");
        std::cout << "Request rejected: " << targetRequest.requestId 
                  << " by " << session.accountId << std::endl;
        
    } catch (const std::exception& e) {
        sendResponse(clientSocket, "ERROR: Invalid request index");
    }
}

void BankServer::handleVerifyClient(int clientSocket, ClientSession& session, const std::vector<std::string>& args) {
    if (!isSuperUser(session.accountId)) {
        sendResponse(clientSocket, "ERROR: Access denied. Super user privileges required.");
        return;
    }
    
    if (args.size() < 1) {
        sendResponse(clientSocket, "ERROR: Usage: VERIFY <verification_index>");
        return;
    }
    
    try {
        int verificationIndex = std::stoi(args[0]);
        
        std::lock_guard<std::mutex> lock(approvalMutex_);
        
        if (verificationQueue_.empty()) {
            sendResponse(clientSocket, "ERROR: No pending verifications");
            return;
        }
        
        std::vector<ApprovalRequest> requests;
        std::queue<ApprovalRequest> tempQueue = verificationQueue_;
        while (!tempQueue.empty()) {
            requests.push_back(tempQueue.front());
            tempQueue.pop();
        }
        
        if (verificationIndex < 0 || verificationIndex >= static_cast<int>(requests.size())) {
            sendResponse(clientSocket, "ERROR: Invalid verification index");
            return;
        }
        
        ApprovalRequest targetRequest = requests[verificationIndex];
        
        // Верифицируем клиента
        if (database_.verifyClient(targetRequest.clientAccountId)) {
            // Сохраняем базу
            saveDatabase();
            
            // Удаляем запрос из очереди
            std::queue<ApprovalRequest> newQueue;
            for (size_t i = 0; i < requests.size(); i++) {
                if (i != static_cast<size_t>(verificationIndex)) {
                    newQueue.push(requests[i]);
                }
            }
            verificationQueue_ = newQueue;
            
            // Сохраняем очередь
            saveQueuesToFile();
            
            sendResponse(clientSocket, "SUCCESS: Client " + targetRequest.clientAccountId + " verified");
            std::cout << "Client verified: " << targetRequest.clientAccountId 
                      << " by " << session.accountId << std::endl;
        } else {
            sendResponse(clientSocket, "ERROR: Failed to verify client " + targetRequest.clientAccountId);
        }
        
    } catch (const std::exception& e) {
        sendResponse(clientSocket, "ERROR: Invalid verification index");
    }
}

void BankServer::handleSetRates(int clientSocket, ClientSession& session, const std::vector<std::string>& args) {
    if (!isSuperUser(session.accountId)) {
        sendResponse(clientSocket, "ERROR: Access denied. Super user privileges required.");
        return;
    }
    
    if (args.size() < 2) {
        sendResponse(clientSocket, "ERROR: Usage: SET_RATES <credit_rate> <deposit_rate>");
        return;
    }
    
    try {
        double creditRate = std::stod(args[0]);
        double depositRate = std::stod(args[1]);
        
        BankSettings settings = database_.getSettings();
        settings.creditInterestRate = creditRate;
        settings.depositInterestRate = depositRate;
        
        database_.saveSettings(settings);
        
        std::stringstream response;
        response << "SUCCESS: Interest rates updated\n"
                 << "Credit Rate: " << creditRate << "%\n"
                 << "Deposit Rate: " << depositRate << "%";
        
        sendResponse(clientSocket, response.str());
        
    } catch (const std::exception& e) {
        sendResponse(clientSocket, "ERROR: Invalid rates");
    }
}

void BankServer::handleSettings(int clientSocket, ClientSession& session) {
    if (!isSuperUser(session.accountId)) {
        sendResponse(clientSocket, "ERROR: Access denied. Super user privileges required.");
        return;
    }
    
    BankSettings settings = database_.getSettings();
    
    std::stringstream response;
    response << "Bank Settings:\n"
             << "Credit Interest Rate: " << settings.creditInterestRate << "%\n"
             << "Deposit Interest Rate: " << settings.depositInterestRate << "%\n"
             << "Large Operation Threshold: $" << settings.largeOperationThreshold << "\n"
             << "Large Loan Threshold: $" << settings.largeLoanThreshold << "\n"
             << "Unverified User Limit: $" << settings.largeOperationThreshold / 10;
    
    sendResponse(clientSocket, response.str());
}

void BankServer::handleTakeLoan(int clientSocket, ClientSession& session, const std::vector<std::string>& args) {
    sendResponse(clientSocket, "INFO: Loan functionality will be implemented in future version");
}

void BankServer::handleLoanPayment(int clientSocket, ClientSession& session, const std::vector<std::string>& args) {
    sendResponse(clientSocket, "INFO: Loan functionality will be implemented in future version");
}

void BankServer::handleOpenDeposit(int clientSocket, ClientSession& session, const std::vector<std::string>& args) {
    sendResponse(clientSocket, "INFO: Deposit functionality will be implemented in future version");
}

void BankServer::handleCloseDeposit(int clientSocket, ClientSession& session, const std::vector<std::string>& args) {
    sendResponse(clientSocket, "INFO: Deposit functionality will be implemented in future version");
}

void BankServer::handleLoanInfo(int clientSocket, ClientSession& session) {
    sendResponse(clientSocket, "INFO: No active loans - functionality will be implemented in future version");
}

void BankServer::handleDepositInfo(int clientSocket, ClientSession& session) {
    sendResponse(clientSocket, "INFO: No active deposits - functionality will be implemented in future version");
}

void BankServer::handleAccrueInterest(int clientSocket, ClientSession& session) {
    sendResponse(clientSocket, "INFO: Interest accrual will be implemented in future version");
}

void BankServer::checkAndCreateSuperUsers() {
    ClientData* superUser = database_.findClient("SUPER001");
    if (!superUser) {
        ClientData newSuperUser;
        newSuperUser.accountId = "SUPER001";
        newSuperUser.fullName = "Security Officer";
        newSuperUser.birthDate = "1980-01-01";
        newSuperUser.passportData = "SUPER001";
        newSuperUser.passwordHash = Crypto::hashPassword("superpass123");
        newSuperUser.status = ClientStatus::VERIFIED;
        
        Account superAccount("SUPER_ACC", AccountType::CHECKING, 0.0);
        newSuperUser.accounts.push_back(superAccount);
        
        database_.addClient(newSuperUser);
        std::cout << "Super user created: SUPER001 / superpass123" << std::endl;
    }
}

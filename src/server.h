#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <queue>
#include <condition_variable>
#include <fstream>
#include "database.h"

struct ClientSession {
    std::string accountId;
    ClientData* clientData;
    std::time_t loginTime;
    bool isAuthenticated;
};

struct ApprovalRequest {
    std::string requestId;
    std::string clientAccountId;
    std::string operationType;
    double amount;
    std::string targetAccount;
    std::string description;
    std::time_t timestamp;
    std::string status;
};

class BankServer {
public:
    BankServer(int port, const std::string& dbFilename);
    ~BankServer();
    
    bool start();
    void stop();
    void run();
    
private:
    int port_;
    std::atomic<bool> running_;
    std::thread serverThread_;
    Database database_;
    std::unordered_map<int, ClientSession> clients_;
    std::mutex clientsMutex_;
    
    // Система одобрения операций
    std::unordered_map<std::string, ClientSession> superUsers_;
    std::queue<ApprovalRequest> approvalQueue_;
    std::queue<ApprovalRequest> verificationQueue_;
    std::mutex approvalMutex_;
    std::condition_variable approvalCV_;
    
    void handleClient(int clientSocket);
    void processCommand(int clientSocket, ClientSession& session, const std::string& command);
    void sendResponse(int clientSocket, const std::string& response);
    
    // Основные команды
    void handleRegister(int clientSocket, const std::vector<std::string>& args);
    void handleLogin(int clientSocket, const std::vector<std::string>& args);
    void handleSuperLogin(int clientSocket, const std::vector<std::string>& args);
    void handleDeposit(int clientSocket, ClientSession& session, const std::vector<std::string>& args);
    void handleDepositToAccount(int clientSocket, ClientSession& session, const std::vector<std::string>& args);
    void handleWithdraw(int clientSocket, ClientSession& session, const std::vector<std::string>& args);
    void handleWithdrawFromAccount(int clientSocket, ClientSession& session, const std::vector<std::string>& args);
    void handleTransfer(int clientSocket, ClientSession& session, const std::vector<std::string>& args);
    void handleTransferFromAccount(int clientSocket, ClientSession& session, const std::vector<std::string>& args);
    void handleHistory(int clientSocket, ClientSession& session, const std::vector<std::string>& args);
    void handleAccountList(int clientSocket, ClientSession& session);
    void handleCreateAccount(int clientSocket, ClientSession& session, const std::vector<std::string>& args);
    void handleInfo(int clientSocket, ClientSession& session);
    void handleRatesInfo(int clientSocket);
    
    // Кредитные и депозитные операции
    void handleTakeLoan(int clientSocket, ClientSession& session, const std::vector<std::string>& args);
    void handleLoanPayment(int clientSocket, ClientSession& session, const std::vector<std::string>& args);
    void handleOpenDeposit(int clientSocket, ClientSession& session, const std::vector<std::string>& args);
    void handleCloseDeposit(int clientSocket, ClientSession& session, const std::vector<std::string>& args);
    void handleLoanInfo(int clientSocket, ClientSession& session);
    void handleDepositInfo(int clientSocket, ClientSession& session);
    void handleAccrueInterest(int clientSocket, ClientSession& session);
    
    // Команды для супер-пользователя
    void handleApproveRequest(int clientSocket, ClientSession& session, const std::vector<std::string>& args);
    void handleRejectRequest(int clientSocket, ClientSession& session, const std::vector<std::string>& args);
    void handlePendingRequests(int clientSocket, ClientSession& session);
    void handlePendingVerifications(int clientSocket, ClientSession& session);
    void handleVerifyClient(int clientSocket, ClientSession& session, const std::vector<std::string>& args);
    void handleSetRates(int clientSocket, ClientSession& session, const std::vector<std::string>& args);
    void handleSettings(int clientSocket, ClientSession& session);
    
    // Система одобрения
    std::string createApprovalRequest(const std::string& clientAccountId, const std::string& operationType, 
                                     double amount, const std::string& targetAccount, const std::string& description);
    std::string createVerificationRequest(const std::string& clientAccountId, const std::string& clientName);
    bool waitForApproval(const std::string& requestId, int timeoutSeconds = 30);
    bool waitForVerification(const std::string& requestId, int timeoutSeconds = 30);
    void checkAndCreateSuperUsers();
    
    // Сохранение и загрузка состояния
    void saveServerState();
    void loadServerState();
    void saveQueuesToFile();
    void loadQueuesFromFile();
    void saveDatabase();
    
    // Утилиты
    bool isSuperUser(const std::string& accountId);
    bool isClientVerified(ClientSession& session);
    bool canPerformOperation(ClientSession& session, const std::string& operationType, double amount = 0);
    std::string generateRequestId();
    void cleanupVerificationQueue(); 
};

#endif
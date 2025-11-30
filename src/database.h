#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include <vector>
#include <unordered_map>
#include "account.h"

#include <iostream>

enum class ClientStatus {
    PENDING_VERIFICATION,
    VERIFIED,
    BLOCKED
};

struct ClientData {
    std::string accountId;
    std::string fullName;
    std::string birthDate;
    std::string passportData;
    std::string passwordHash;
    ClientStatus status;
    std::vector<Account> accounts;
};

struct BankSettings {
    double creditInterestRate = 12.0;
    double depositInterestRate = 6.5;
    double largeOperationThreshold = 150000.0;
    double largeLoanThreshold = 50000.0;
};

class Database {
public:
    Database(const std::string& filename);
    bool loadFromFile();
    bool saveToFile();
    bool addClient(const ClientData& client);
    bool removeClient(const std::string& accountId);
    bool updateClient(const ClientData& client);
    ClientData* findClient(const std::string& accountId);
    ClientData* authenticateClient(const std::string& accountId, const std::string& password);
    std::vector<std::string> getAllAccountIds();
    bool isPassportExists(const std::string& passportData);
    bool verifyClient(const std::string& accountId);
    
    // Работа со счетами
    bool updateClientAccounts(const std::string& accountId, const std::vector<Account>& accounts);
    bool addAccountToClient(const std::string& accountId, const Account& account);
    bool findAccount(const std::string& accountNumber, ClientData** owner = nullptr, Account** account = nullptr);
    
    // Получение списков клиентов
    std::vector<ClientData*> getAllClients();
    std::vector<ClientData*> getClientsByStatus(ClientStatus status);
    
    // Статистика
    size_t getClientCount() const;
    size_t getTotalAccountsCount() const;
    double getTotalBalance() const;
    
    // Настройки
    bool loadSettings();
    bool saveSettings(const BankSettings& settings);
    BankSettings getSettings() const { return settings_; }
    
    // Резервное копирование
    bool backupDatabase(const std::string& backupPath);
    bool restoreFromBackup(const std::string& backupPath);
    
    // Утилиты
    void clearDatabase();
    // Метод для отладки - вывод информации о клиентах
    void debugPrintClients() {
        std::cout << "=== DEBUG: Database Contents ===" << std::endl;
        for (const auto& pair : clients_) {
            const ClientData& client = pair.second;
            std::cout << "Client: " << client.accountId 
                      << " | Accounts: " << client.accounts.size() 
                      << " | Status: " << static_cast<int>(client.status) << std::endl;
            
            for (const auto& account : client.accounts) {
                std::cout << "  - Account: " << account.getNumber() 
                          << " | Type: " << static_cast<int>(account.getType())
                          << " | Balance: " << account.getBalance() << std::endl;
            }
        }
        std::cout << "=== END DEBUG ===" << std::endl;
    }

private:
    std::string filename_;
    std::unordered_map<std::string, ClientData> clients_;
    BankSettings settings_;
    std::string encryptionKey_ = "bank-system-key-2024";
    
    std::string settingsFilename() const { return filename_ + ".settings"; }
};

#endif

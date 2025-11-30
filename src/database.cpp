#include "database.h"
#include "crypto.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>

Database::Database(const std::string& filename) : filename_(filename) {
    settings_.creditInterestRate = 12.0;
    settings_.depositInterestRate = 6.5;
    settings_.largeOperationThreshold = 150000.0;
    settings_.largeLoanThreshold = 50000.0;
    
    // Загружаем данные при создании
    loadFromFile();
}

bool Database::loadFromFile() {
    std::ifstream file(filename_, std::ios::binary);
    if (!file) {
        std::cout << "Database file not found, creating new one." << std::endl;
        clients_.clear();
        return true;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string encryptedData = buffer.str();
    
    if (encryptedData.empty()) {
        clients_.clear();
        std::cout << "Database file is empty, starting fresh." << std::endl;
        return true;
    }
    
    try {
        std::string decryptedData = Crypto::decrypt(encryptedData, encryptionKey_);
        
        if (decryptedData.empty()) {
            clients_.clear();
            return true;
        }
        
        std::stringstream ss(decryptedData);
        std::unordered_map<std::string, ClientData> newClients;
        std::string line;
        
        while (std::getline(ss, line)) {
            if (line.empty() || line == "===") continue;
            
            std::stringstream lineStream(line);
            ClientData client;
            std::string accountCountStr, statusStr;
            
            // Читаем основную информацию о клиенте
            if (!std::getline(lineStream, client.accountId, '|') ||
                !std::getline(lineStream, client.fullName, '|') ||
                !std::getline(lineStream, client.birthDate, '|') ||
                !std::getline(lineStream, client.passportData, '|') ||
                !std::getline(lineStream, client.passwordHash, '|') ||
                !std::getline(lineStream, statusStr, '|') ||
                !std::getline(lineStream, accountCountStr, '|')) {
                std::cerr << "Warning: Incomplete client data, skipping line: " << line << std::endl;
                continue;
            }
            
            try {
                client.status = static_cast<ClientStatus>(std::stoi(statusStr));
                int accountCount = std::stoi(accountCountStr);
                
                // Читаем счета клиента
                for (int i = 0; i < accountCount; i++) {
                    if (!std::getline(ss, line) || line.empty() || line == "===") {
                        std::cerr << "Warning: Missing account data for client " << client.accountId << std::endl;
                        break;
                    }
                    
                    std::stringstream accStream(line);
                    std::string accountNumber, typeStr, balanceStr, limitStr, statusStr, txnCountStr;
                    
                    if (!std::getline(accStream, accountNumber, '|') ||
                        !std::getline(accStream, typeStr, '|') ||
                        !std::getline(accStream, balanceStr, '|') ||
                        !std::getline(accStream, limitStr, '|') ||
                        !std::getline(accStream, statusStr, '|') ||
                        !std::getline(accStream, txnCountStr, '|')) {
                        std::cerr << "Warning: Incomplete account data, skipping: " << line << std::endl;
                        continue;
                    }
                    
                    AccountType type = static_cast<AccountType>(std::stoi(typeStr));
                    Account account(accountNumber, type, std::stod(balanceStr));
                    account.setCreditLimit(std::stod(limitStr));
                    account.setStatus(static_cast<AccountStatus>(std::stoi(statusStr)));
                    
                    // Читаем транзакции
                    try {
                        int txnCount = std::stoi(txnCountStr);
                        for (int j = 0; j < txnCount; j++) {
                            if (!std::getline(ss, line) || line.empty() || line == "===") break;
                            
                            std::stringstream txnStream(line);
                            std::string txnId, timestampStr, txnType, amountStr, desc, targetAcc;
                            
                            if (std::getline(txnStream, txnId, '|') &&
                                std::getline(txnStream, timestampStr, '|') &&
                                std::getline(txnStream, txnType, '|') &&
                                std::getline(txnStream, amountStr, '|') &&
                                std::getline(txnStream, desc, '|') &&
                                std::getline(txnStream, targetAcc, '|')) {
                                
                                // Добавляем транзакцию в счет
                                account.addTransaction(txnType, std::stod(amountStr), desc, targetAcc);
                                // Устанавливаем timestamp последней транзакции
                                auto& transactions = const_cast<std::vector<Transaction>&>(account.getTransactionHistory());
                                if (!transactions.empty()) {
                                    transactions.back().timestamp = std::stol(timestampStr);
                                    transactions.back().id = txnId;
                                }
                            }
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "Warning: Error reading transactions for account " << accountNumber << ": " << e.what() << std::endl;
                        // Продолжаем без транзакций
                    }
                    
                    client.accounts.push_back(account);
                }
                
                newClients[client.accountId] = client;
                
            } catch (const std::exception& e) {
                std::cerr << "Warning: Error parsing client data: " << e.what() << std::endl;
                std::cerr << "Problematic line: " << line << std::endl;
                continue;
            }
        }
        
        clients_ = std::move(newClients);
        std::cout << "Loaded " << clients_.size() << " clients with " << getTotalAccountsCount() << " accounts from database." << std::endl;
        
        // Загружаем настройки
        loadSettings();
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error loading database: " << e.what() << std::endl;
        clients_.clear();
        return false;
    }
}

bool Database::saveToFile() {
    std::stringstream ss;
    
    for (const auto& pair : clients_) {
        const ClientData& client = pair.second;
        ss << client.accountId << "|"
           << client.fullName << "|"
           << client.birthDate << "|"
           << client.passportData << "|"
           << client.passwordHash << "|"
           << static_cast<int>(client.status) << "|"
           << client.accounts.size() << "|\n";
        
        for (const Account& account : client.accounts) {
            ss << account.getNumber() << "|"
               << static_cast<int>(account.getType()) << "|"
               << account.getBalance() << "|"
               << account.getCreditLimit() << "|"
               << static_cast<int>(account.getStatus()) << "|";
            
            // Сохраняем историю транзакций
            const auto& transactions = account.getTransactionHistory();
            ss << transactions.size() << "|\n";
            
            for (const auto& txn : transactions) {
                ss << txn.id << "|" << txn.timestamp << "|" << txn.type << "|"
                   << txn.amount << "|" << txn.description << "|" << txn.targetAccount << "|\n";
            }
        }
        ss << "===\n"; // Разделитель между клиентами
    }
    
    std::string data = ss.str();
    std::string encryptedData = Crypto::encrypt(data, encryptionKey_);
    
    // Создаем директорию если нужно
    std::string dir = filename_.substr(0, filename_.find_last_of('/'));
    if (!dir.empty()) {
        system(("mkdir -p " + dir).c_str());
    }
    
    std::ofstream file(filename_, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Could not open database file for writing: " << filename_ << std::endl;
        return false;
    }
    
    file << encryptedData;
    file.close();
    
    // Сохраняем настройки
    saveSettings(settings_);
    
    return true;
}

bool Database::addClient(const ClientData& client) {
    if (clients_.find(client.accountId) != clients_.end()) {
        std::cout << "Client " << client.accountId << " already exists." << std::endl;
        return false;
    }
    
    clients_[client.accountId] = client;
    bool success = saveToFile();
    
    if (success) {
        std::cout << "Client " << client.accountId << " added successfully." << std::endl;
    } else {
        std::cerr << "Failed to save client " << client.accountId << " to database." << std::endl;
        // Откатываем изменения в памяти при ошибке сохранения
        clients_.erase(client.accountId);
    }
    
    return success;
}

bool Database::removeClient(const std::string& accountId) {
    auto it = clients_.find(accountId);
    if (it == clients_.end()) {
        std::cout << "Client " << accountId << " not found." << std::endl;
        return false;
    }
    
    clients_.erase(it);
    bool success = saveToFile();
    
    if (success) {
        std::cout << "Client " << accountId << " removed successfully." << std::endl;
    } else {
        std::cerr << "Failed to remove client " << accountId << " from database." << std::endl;
    }
    
    return success;
}

ClientData* Database::findClient(const std::string& accountId) {
    auto it = clients_.find(accountId);
    return it != clients_.end() ? &it->second : nullptr;
}

ClientData* Database::authenticateClient(const std::string& accountId, const std::string& password) {
    ClientData* client = findClient(accountId);
    if (client && Crypto::verifyPassword(password, client->passwordHash)) {
        return client;
    }
    return nullptr;
}

std::vector<std::string> Database::getAllAccountIds() {
    std::vector<std::string> ids;
    for (const auto& pair : clients_) {
        ids.push_back(pair.first);
    }
    return ids;
}

bool Database::isPassportExists(const std::string& passportData) {
    for (const auto& pair : clients_) {
        if (pair.second.passportData == passportData) {
            return true;
        }
    }
    return false;
}

bool Database::verifyClient(const std::string& accountId) {
    ClientData* client = findClient(accountId);
    if (client) {
        client->status = ClientStatus::VERIFIED;
        return saveToFile();
    }
    return false;
}

bool Database::loadSettings() {
    std::string settingsFile = settingsFilename();
    std::ifstream file(settingsFile, std::ios::binary);
    if (!file) {
        std::cout << "Settings file not found, using default settings." << std::endl;
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string encryptedData = buffer.str();
    
    if (encryptedData.empty()) {
        return false;
    }
    
    try {
        std::string decryptedData = Crypto::decrypt(encryptedData, encryptionKey_);
        std::stringstream ss(decryptedData);
        
        std::string line;
        if (std::getline(ss, line)) {
            std::stringstream settingsStream(line);
            std::string creditRate, depositRate, operationThreshold, loanThreshold;
            
            if (std::getline(settingsStream, creditRate, '|') &&
                std::getline(settingsStream, depositRate, '|') &&
                std::getline(settingsStream, operationThreshold, '|') &&
                std::getline(settingsStream, loanThreshold, '|')) {
                
                settings_.creditInterestRate = std::stod(creditRate);
                settings_.depositInterestRate = std::stod(depositRate);
                settings_.largeOperationThreshold = std::stod(operationThreshold);
                settings_.largeLoanThreshold = std::stod(loanThreshold);
                
                std::cout << "Settings loaded successfully." << std::endl;
                return true;
            }
        }
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Error loading settings: " << e.what() << std::endl;
        return false;
    }
}

bool Database::saveSettings(const BankSettings& settings) {
    settings_ = settings;
    
    std::stringstream ss;
    ss << settings.creditInterestRate << "|"
       << settings.depositInterestRate << "|"
       << settings.largeOperationThreshold << "|"
       << settings.largeLoanThreshold << "|\n";
    
    std::string data = ss.str();
    std::string encryptedData = Crypto::encrypt(data, encryptionKey_);
    
    // Создаем директорию если нужно
    std::string dir = filename_.substr(0, filename_.find_last_of('/'));
    if (!dir.empty()) {
        system(("mkdir -p " + dir).c_str());
    }
    
    std::ofstream file(settingsFilename(), std::ios::binary);
    if (!file) {
        std::cerr << "Error: Could not open settings file for writing." << std::endl;
        return false;
    }
    
    file << encryptedData;
    file.close();
    
    return true;
}

// Дополнительные методы

bool Database::updateClient(const ClientData& client) {
    auto it = clients_.find(client.accountId);
    if (it == clients_.end()) {
        return false;
    }
    
    it->second = client;
    return saveToFile();
}

bool Database::updateClientAccounts(const std::string& accountId, const std::vector<Account>& accounts) {
    ClientData* client = findClient(accountId);
    if (!client) {
        return false;
    }
    
    client->accounts = accounts;
    return saveToFile();
}

bool Database::addAccountToClient(const std::string& accountId, const Account& account) {
    ClientData* client = findClient(accountId);
    if (!client) {
        return false;
    }
    
    // Проверяем, нет ли уже счета с таким номером
    for (const auto& acc : client->accounts) {
        if (acc.getNumber() == account.getNumber()) {
            return false;
        }
    }
    
    client->accounts.push_back(account);
    return saveToFile();
}

bool Database::findAccount(const std::string& accountNumber, ClientData** owner, Account** account) {
    for (auto& pair : clients_) {
        for (auto& acc : pair.second.accounts) {
            if (acc.getNumber() == accountNumber) {
                if (owner) *owner = &pair.second;
                if (account) *account = &acc;
                return true;
            }
        }
    }
    return false;
}

std::vector<ClientData*> Database::getAllClients() {
    std::vector<ClientData*> result;
    for (auto& pair : clients_) {
        result.push_back(&pair.second);
    }
    return result;
}

std::vector<ClientData*> Database::getClientsByStatus(ClientStatus status) {
    std::vector<ClientData*> result;
    for (auto& pair : clients_) {
        if (pair.second.status == status) {
            result.push_back(&pair.second);
        }
    }
    return result;
}

size_t Database::getClientCount() const {
    return clients_.size();
}

size_t Database::getTotalAccountsCount() const {
    size_t count = 0;
    for (const auto& pair : clients_) {
        count += pair.second.accounts.size();
    }
    return count;
}

double Database::getTotalBalance() const {
    double total = 0;
    for (const auto& pair : clients_) {
        for (const auto& account : pair.second.accounts) {
            total += account.getBalance();
        }
    }
    return total;
}

void Database::clearDatabase() {
    clients_.clear();
    saveToFile();
    std::cout << "Database cleared." << std::endl;
}

bool Database::backupDatabase(const std::string& backupPath) {
    // Создаем резервную копию основного файла
    std::ifstream src(filename_, std::ios::binary);
    if (!src) {
        std::cerr << "Cannot open source database for backup." << std::endl;
        return false;
    }
    
    // Создаем директорию для бэкапа если нужно
    std::string dir = backupPath.substr(0, backupPath.find_last_of('/'));
    if (!dir.empty()) {
        system(("mkdir -p " + dir).c_str());
    }
    
    std::ofstream dst(backupPath, std::ios::binary);
    if (!dst) {
        std::cerr << "Cannot create backup file." << std::endl;
        return false;
    }
    
    dst << src.rdbuf();
    
    // Создаем резервную копию настроек
    std::string settingsBackupPath = backupPath + ".settings";
    std::ifstream srcSettings(settingsFilename(), std::ios::binary);
    if (srcSettings) {
        std::ofstream dstSettings(settingsBackupPath, std::ios::binary);
        if (dstSettings) {
            dstSettings << srcSettings.rdbuf();
        }
    }
    
    std::cout << "Database backup created: " << backupPath << std::endl;
    return true;
}

bool Database::restoreFromBackup(const std::string& backupPath) {
    // Восстанавливаем из резервной копии
    std::ifstream src(backupPath, std::ios::binary);
    if (!src) {
        std::cerr << "Cannot open backup file." << std::endl;
        return false;
    }
    
    // Создаем директорию если нужно
    std::string dir = filename_.substr(0, filename_.find_last_of('/'));
    if (!dir.empty()) {
        system(("mkdir -p " + dir).c_str());
    }
    
    std::ofstream dst(filename_, std::ios::binary);
    if (!dst) {
        std::cerr << "Cannot restore database." << std::endl;
        return false;
    }
    
    dst << src.rdbuf();
    
    // Восстанавливаем настройки
    std::string settingsBackupPath = backupPath + ".settings";
    std::ifstream srcSettings(settingsBackupPath, std::ios::binary);
    if (srcSettings) {
        std::ofstream dstSettings(settingsFilename(), std::ios::binary);
        if (dstSettings) {
            dstSettings << srcSettings.rdbuf();
        }
    }
    
    // Перезагружаем данные
    loadFromFile();
    
    std::cout << "Database restored from backup: " << backupPath << std::endl;
    return true;
}

#ifndef __ACCOUNT_H__
#define __ACCOUNT_H__

#include <string>
#include <ctime>
#include <vector>
#include <random>
#include <memory>

enum class AccountType {
    SAVINGS,
    CHECKING,
    CREDIT,
    DEPOSIT
};

enum class AccountStatus {
    ACTIVE,
    BLOCKED,
    CLOSED
};

struct Transaction {
    std::string id;
    std::time_t timestamp;
    std::string type;
    double amount;
    std::string description;
    std::string targetAccount;
};

class Account {
public:
    Account(const std::string& number, AccountType type, double balance = 0.0);
    
    bool deposit(double amount, const std::string& description = "");
    bool withdraw(double amount, const std::string& description = "");
    bool transfer(Account& target, double amount, const std::string& description = "");
    
    // Геттеры
    std::string getNumber() const { return number_; }
    AccountType getType() const { return type_; }
    double getBalance() const { return balance_; }
    double getCreditLimit() const { return creditLimit_; }
    const std::vector<Transaction>& getTransactionHistory() const { return transactions_; }
    AccountStatus getStatus() const { return status_; }
    
    // Сеттеры
    void setCreditLimit(double limit);
    void setStatus(AccountStatus status) { status_ = status; }
    
    // Работа с транзакциями
    void addTransaction(const std::string& type, double amount, 
                       const std::string& description = "", 
                       const std::string& targetAccount = "");
    
    std::string getTypeString() const;
    
private:
    std::string number_;
    AccountType type_;
    double balance_;
    double creditLimit_;
    AccountStatus status_;
    std::vector<Transaction> transactions_;
    
    std::string generateTransactionId();
};

#endif

#include "account.h"
#include <sstream>
#include <iomanip>
#include <random>
#include <iostream>
#include <algorithm>

Account::Account(const std::string& number, AccountType type, double balance)
    : number_(number), type_(type), balance_(balance), creditLimit_(0.0), status_(AccountStatus::ACTIVE) {}

bool Account::deposit(double amount, const std::string& description) {
    if (amount <= 0) return false;
    
    balance_ += amount;
    addTransaction("DEPOSIT", amount, description);
    return true;
}

bool Account::withdraw(double amount, const std::string& description) {
    if (amount <= 0) return false;
    
    double availableBalance = balance_ + creditLimit_;
    if (amount > availableBalance) return false;
    
    balance_ -= amount;
    addTransaction("WITHDRAW", -amount, description);
    return true;
}

bool Account::transfer(Account& target, double amount, const std::string& description) {
    std::string transferDescription = description.empty() ? 
        "Transfer to " + target.getNumber() : description;
    
    if (!withdraw(amount, transferDescription)) {
        return false;
    }
    
    std::string receiveDescription = "Transfer from " + number_;
    if (!target.deposit(amount, receiveDescription)) {
        // Откатываем операцию
        balance_ += amount;
        if (!transactions_.empty()) {
            transactions_.pop_back();
        }
        return false;
    }
    
    return true;
}

void Account::setCreditLimit(double limit) {
    creditLimit_ = limit;
}

void Account::addTransaction(const std::string& type, double amount, 
                           const std::string& description, const std::string& targetAccount) {
    Transaction transaction;
    transaction.id = generateTransactionId();
    transaction.timestamp = std::time(nullptr);
    transaction.type = type;
    transaction.amount = amount;
    transaction.description = description;
    transaction.targetAccount = targetAccount;
    
    transactions_.push_back(transaction);
}

std::string Account::generateTransactionId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << "TXN";
    for (int i = 0; i < 12; i++) {
        ss << std::hex << dis(gen);
    }
    return ss.str();
}

std::string Account::getTypeString() const {
    switch (type_) {
        case AccountType::SAVINGS: return "Savings";
        case AccountType::CHECKING: return "Checking";
        case AccountType::CREDIT: return "Credit";
        case AccountType::DEPOSIT: return "Deposit";
        default: return "Unknown";
    }
}

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include "database.h"
#include "crypto.h"

// Функция для вычисления реальной ширины строки с учетом Unicode
size_t utf8_strlen(const std::string& str) {
    size_t len = 0;
    for (char c : str) {
        if ((c & 0xC0) != 0x80) {
            len++;
        }
    }
    return len;
}

// Функция для создания строки фиксированной ширины с учетом Unicode
std::string pad_string(const std::string& str, size_t width) {
    size_t utf8_len = utf8_strlen(str);
    if (utf8_len >= width) {
        return str;
    }
    return str + std::string(width - utf8_len, ' ');
}

std::string formatBalance(double balance) {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << balance;
    return ss.str();
}

std::string formatAccountType(AccountType type) {
    switch (type) {
        case AccountType::SAVINGS: return "Сберегательный";
        case AccountType::CHECKING: return "Расчетный";
        case AccountType::CREDIT: return "Кредитный";
        case AccountType::DEPOSIT: return "Депозитный";
        default: return "Неизвестный";
    }
}

std::string formatClientStatus(ClientStatus status) {
    switch (status) {
        case ClientStatus::PENDING_VERIFICATION: return "Ожидает верификации";
        case ClientStatus::VERIFIED: return "Верифицирован";
        case ClientStatus::BLOCKED: return "Заблокирован";
        default: return "Неизвестно";
    }
}

void printBoxLine(const std::string& content, int width, char left = '|', char right = '|') {
    std::cout << left << " " << pad_string(content, width - 4) << " " << right << std::endl;
}

void printClientData(const ClientData& client, int clientIndex) {
    const int BOX_WIDTH = 60;
    
    
    std::cout << "+" << std::string(BOX_WIDTH - 2, '-') << "+" << std::endl;
    
    // Заголовок с номером клиента
    std::string header = "Клиент #" + std::to_string(clientIndex) + ": " + client.accountId;
    printBoxLine(header, BOX_WIDTH);
    
    std::cout << "+" << std::string(BOX_WIDTH - 2, '-') << "+" << std::endl;
    
    // Основная информация
    printBoxLine("ФИО: " + client.fullName, BOX_WIDTH);
    printBoxLine("Дата рождения: " + client.birthDate, BOX_WIDTH);
    printBoxLine("Паспорт: " + client.passportData, BOX_WIDTH);
    printBoxLine("Статус: " + formatClientStatus(client.status), BOX_WIDTH);
    printBoxLine("Количество счетов: " + std::to_string(client.accounts.size()), BOX_WIDTH);
    
    if (!client.accounts.empty()) {
       
        std::cout << "+" << std::string(BOX_WIDTH - 2, '-') << "+" << std::endl;
        
        // Общая статистика по счетам
        double totalBalance = 0;
        double totalCreditLimit = 0;
        
        for (const auto& account : client.accounts) {
            totalBalance += account.getBalance();
            totalCreditLimit += account.getCreditLimit();
        }
        
        printBoxLine("Общий баланс: $" + formatBalance(totalBalance), BOX_WIDTH);
        if (totalCreditLimit > 0) {
            printBoxLine("Кредитный лимит: $" + formatBalance(totalCreditLimit), BOX_WIDTH);
            printBoxLine("Доступно: $" + formatBalance(totalBalance + totalCreditLimit), BOX_WIDTH);
        }
        
        std::cout << "+" << std::string(BOX_WIDTH - 2, '-') << "+" << std::endl;
        printBoxLine("Счета:", BOX_WIDTH);
        
        for (size_t i = 0; i < client.accounts.size(); ++i) {
            const auto& account = client.accounts[i];
            
            std::string accountLine = "  " + std::to_string(i + 1) + ". " + account.getNumber();
            printBoxLine(accountLine, BOX_WIDTH);
            
            std::string typeLine = "     Тип: " + formatAccountType(account.getType());
            printBoxLine(typeLine, BOX_WIDTH);
            
            std::string balanceLine = "     Баланс: $" + formatBalance(account.getBalance());
            printBoxLine(balanceLine, BOX_WIDTH);
            
            if (account.getCreditLimit() > 0) {
                std::string creditLine = "     Кредитный лимит: $" + formatBalance(account.getCreditLimit());
                printBoxLine(creditLine, BOX_WIDTH);
            }
            
            // Последние транзакции
            const auto& transactions = account.getTransactionHistory();
            if (!transactions.empty()) {
                printBoxLine("     Последние транзакции:", BOX_WIDTH);
                
                int showCount = std::min(2, static_cast<int>(transactions.size()));
                for (int j = transactions.size() - showCount; j < static_cast<int>(transactions.size()); ++j) {
                    const auto& txn = transactions[j];
                    std::string txnStr = "       • " + txn.type + " $" + formatBalance(std::abs(txn.amount));
                    if (txn.amount < 0) {
                        txnStr = "       • " + txn.type + " -$" + formatBalance(std::abs(txn.amount));
                    }
                    if (utf8_strlen(txnStr) > BOX_WIDTH - 10) {
                        txnStr = txnStr.substr(0, BOX_WIDTH - 13) + "...";
                    }
                    printBoxLine(txnStr, BOX_WIDTH);
                }
            }
            
            if (i < client.accounts.size() - 1) {
                printBoxLine("", BOX_WIDTH); 
            }
        }
    }
    
    std::cout << "+" << std::string(BOX_WIDTH - 2, '-') << "+" << std::endl << std::endl;
}

void viewEncryptedFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cout << "ОШИБКА: Файл не найден: " << filename << std::endl;
        return;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string encryptedData = buffer.str();
    
    std::cout << "ЗАШИФРОВАННЫЙ ФАЙЛ: " << filename << std::endl;
    std::cout << "Размер: " << encryptedData.length() << " байт" << std::endl;
    std::cout << "Первые 200 символов:" << std::endl;
    std::cout << "-----------------------------------------" << std::endl;
    
    if (encryptedData.length() > 200) {
        std::cout << encryptedData.substr(0, 200) << "..." << std::endl;
    } else {
        std::cout << encryptedData << std::endl;
    }
    std::cout << "-----------------------------------------" << std::endl;
}

void viewDecryptedData(const std::string& filename) {
    Database db(filename);
    if (!db.loadFromFile()) {
        std::cout << "ОШИБКА: Ошибка загрузки базы данных" << std::endl;
        return;
    }
    
    std::cout << std::endl;
    std::cout << "РАСШИФРОВАННЫЕ ДАННЫЕ" << std::endl;
    std::cout << "=========================================" << std::endl << std::endl;
    
    // Показываем настройки банка
    BankSettings settings = db.getSettings();
    std::cout << "НАСТРОЙКИ БАНКА:" << std::endl;
    std::cout << "+----------------------------------------------------------+" << std::endl;
    std::cout << "| " << pad_string("Процент по кредитам: " + std::to_string(settings.creditInterestRate) + "%", 56) << " |" << std::endl;
    std::cout << "| " << pad_string("Процент по депозитам: " + std::to_string(settings.depositInterestRate) + "%", 56) << " |" << std::endl;
    std::cout << "| " << pad_string("Лимит крупных операций: $" + formatBalance(settings.largeOperationThreshold), 56) << " |" << std::endl;
    std::cout << "| " << pad_string("Лимит кредитов: $" + formatBalance(settings.largeLoanThreshold), 56) << " |" << std::endl;
    std::cout << "| " << pad_string("Лимит для неверифицированных: $" + formatBalance(settings.largeOperationThreshold / 10), 56) << " |" << std::endl;
    std::cout << "+----------------------------------------------------------+" << std::endl << std::endl;
    
    // Показываем всех клиентов
    std::cout << "КЛИЕНТЫ БАНКА:" << std::endl;
    std::cout << "=========================================" << std::endl << std::endl;
    
    auto allAccounts = db.getAllAccountIds();
    int clientIndex = 1;
    for (const auto& accountId : allAccounts) {
        ClientData* client = db.findClient(accountId);
        if (client) {
            printClientData(*client, clientIndex++);
        }
    }
    
    // Общая статистика
    std::cout << "ОБЩАЯ СТАТИСТИКА БАНКА:" << std::endl;
    std::cout << "+---------------------------------------+" << std::endl;
    
    int verified = 0, pending = 0, blocked = 0;
    int totalAccounts = 0;
    double totalBalance = 0;
    double totalCreditLimit = 0;
    
    for (const auto& accountId : allAccounts) {
        ClientData* client = db.findClient(accountId);
        if (client) {
            switch (client->status) {
                case ClientStatus::VERIFIED: verified++; break;
                case ClientStatus::PENDING_VERIFICATION: pending++; break;
                case ClientStatus::BLOCKED: blocked++; break;
            }
            
            totalAccounts += client->accounts.size();
            
            for (const auto& account : client->accounts) {
                totalBalance += account.getBalance();
                totalCreditLimit += account.getCreditLimit();
            }
        }
    }
    
    std::cout << "| " << pad_string("Всего клиентов: " + std::to_string(allAccounts.size()), 38) << "|" << std::endl;
    std::cout << "| " << pad_string("Верифицированных: " + std::to_string(verified), 38) << "|" << std::endl;
    std::cout << "| " << pad_string("Ожидают верификации: " + std::to_string(pending), 38) << "|" << std::endl;
    std::cout << "| " << pad_string("Заблокированных: " + std::to_string(blocked), 38) << "|" << std::endl;
    std::cout << "| " << pad_string("Всего счетов: " + std::to_string(totalAccounts), 38) << "|" << std::endl;
    std::cout << "| " << pad_string("Общий баланс: $" + formatBalance(totalBalance), 38) << "|" << std::endl;
    std::cout << "| " << pad_string("Общий кредитный лимит: $" + formatBalance(totalCreditLimit), 38) << "|" << std::endl;
    std::cout << "| " << pad_string("Всего доступно: $" + formatBalance(totalBalance + totalCreditLimit), 38) << "|" << std::endl;
    std::cout << "+---------------------------------------+" << std::endl;
}

void testEncryption() {
    std::cout << std::endl;
    std::cout << "ТЕСТ ШИФРОВАНИЯ" << std::endl;
    std::cout << "=========================================" << std::endl;
    
    std::string testText = "Секретные банковские данные: ACC001, баланс $5000";
    std::string key = "bank-system-key-2024";
    
    std::string encrypted = Crypto::encrypt(testText, key);
    std::string decrypted = Crypto::decrypt(encrypted, key);
    
    std::cout << "Исходный текст: " << std::endl;
    std::cout << "  " << testText << std::endl << std::endl;
    
    std::cout << "Зашифрованные данные: " << std::endl;
    std::cout << "  " << encrypted << std::endl << std::endl;
    
    std::cout << "Расшифрованный текст: " << std::endl;
    std::cout << "  " << decrypted << std::endl << std::endl;
    
    bool testPassed = (testText == decrypted);
    std::cout << "Результат теста: " << (testPassed ? "УСПЕХ" : "ОШИБКА") << std::endl;
}

int main(int argc, char* argv[]) {
    std::string filename = "data/accounts.dat";
    
    if (argc > 1) {
        filename = argv[1];
    }
    
    std::cout << "ПРОСМОТР БАЗЫ ДАННЫХ БАНКА" << std::endl;
    std::cout << "=========================================" << std::endl << std::endl;
    
    // Показываем зашифрованный файл
    viewEncryptedFile(filename);
    
    // Показываем расшифрованные данные
    viewDecryptedData(filename);
    
    // Тест шифрования
    testEncryption();
    
    // Показываем файл настроек
    std::string settingsFile = filename + ".settings";
    std::cout << std::endl;
    std::cout << "ФАЙЛ НАСТРОЕК: " << settingsFile << std::endl;
    viewEncryptedFile(settingsFile);
    
    return 0;
}


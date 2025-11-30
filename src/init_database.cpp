#include <iostream>
#include "database.h"
#include "crypto.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <random>

// Функция для генерации ID запроса
std::string generateRequestId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 9999);
    
    std::stringstream ss;
    ss << "REQ" << std::time(nullptr) << dis(gen);
    return ss.str();
}

// Функция для создания файла с запросами верификации
void createVerificationRequests() {
    std::cout << "Creating verification requests for test users..." << std::endl;
    
    // Создаем директорию для данных если нужно
    std::filesystem::create_directories("data");
    
    std::ofstream verificationFile("data/verification_queue.dat");
    if (!verificationFile) {
        std::cerr << "Warning: Could not create verification queue file" << std::endl;
        return;
    }
    
    // Текущее время для всех запросов
    std::time_t currentTime = std::time(nullptr);
    
    // Запрос верификации для ACC1003
    std::string requestId1 = generateRequestId();
    verificationFile << requestId1 << "|"
                    << "ACC1003" << "|"
                    << "VERIFICATION" << "|"
                    << "0" << "|"
                    << "" << "|"
                    << "Name: Sidorov Alexey Petrovich | Birth: 1995-08-10 | Passport: 4510789123" << "|"
                    << currentTime << "|"
                    << "PENDING" << "|\n";
    
    std::cout << "✅ Created verification request for ACC1003: " << requestId1 << std::endl;
    
    verificationFile.close();
}

void createTestUsers(Database& db) {
    std::cout << "Creating test users..." << std::endl;

    // Создаем директорию для данных
    std::filesystem::create_directories("data");

    // Верифицированный клиент 1
    ClientData client1;
    client1.accountId = "ACC1001";
    client1.fullName = "Ivanov Ivan Ivanovich";
    client1.birthDate = "1990-05-15";
    client1.passportData = "4510123456";
    client1.passwordHash = Crypto::hashPassword("password123");
    client1.status = ClientStatus::VERIFIED;
    
    Account acc1("ACC1001_SAV_1", AccountType::SAVINGS, 50000.0);
    Account acc2("ACC1001_CHK_2", AccountType::CHECKING, 25000.0);
    Account acc3("ACC1001_CRD_3", AccountType::CREDIT, 0.0);
    acc3.setCreditLimit(50000.0);
    
    // Добавляем тестовые транзакции для проверки
    acc1.deposit(50000.0, "Initial deposit");
    acc2.deposit(25000.0, "Initial deposit");
    
    client1.accounts.push_back(acc1);
    client1.accounts.push_back(acc2);
    client1.accounts.push_back(acc3);
    
    // Верифицированный клиент 2
    ClientData client2;
    client2.accountId = "ACC1002";
    client2.fullName = "Petrova Anna Sergeevna";
    client2.birthDate = "1985-12-20";
    client2.passportData = "4510654321";
    client2.passwordHash = Crypto::hashPassword("qwerty456");
    client2.status = ClientStatus::VERIFIED;
    
    Account acc4("ACC1002_SAV_1", AccountType::SAVINGS, 75000.0);
    Account acc5("ACC1002_DEP_2", AccountType::DEPOSIT, 50000.0);
    
    acc4.deposit(75000.0, "Initial deposit");
    acc5.deposit(50000.0, "Initial deposit");
    
    client2.accounts.push_back(acc4);
    client2.accounts.push_back(acc5);
    
    // Неверифицированный клиент 3
    ClientData client3;
    client3.accountId = "ACC1003";
    client3.fullName = "Sidorov Alexey Petrovich";
    client3.birthDate = "1995-08-10";
    client3.passportData = "4510789123";
    client3.passwordHash = Crypto::hashPassword("test789");
    client3.status = ClientStatus::PENDING_VERIFICATION;
    
    Account acc6("ACC1003_SAV_1", AccountType::SAVINGS, 5000.0);
    acc6.deposit(5000.0, "Initial deposit");
    client3.accounts.push_back(acc6);
    
    // Супер-пользователь
    ClientData superUser;
    superUser.accountId = "SUPER001";
    superUser.fullName = "Security Officer";
    superUser.birthDate = "1980-01-01";
    superUser.passportData = "SUPER001";
    superUser.passwordHash = Crypto::hashPassword("superpass123");
    superUser.status = ClientStatus::VERIFIED;
    
    Account superAcc("SUPER001_CHK_1", AccountType::CHECKING, 0.0);
    superUser.accounts.push_back(superAcc);
    
    // Добавляем всех клиентов
    int successCount = 0;
    
    if (db.addClient(client1)) successCount++;
    if (db.addClient(client2)) successCount++;
    if (db.addClient(client3)) successCount++;
    if (db.addClient(superUser)) successCount++;
    
    std::cout << "✅ Successfully created " << successCount << "/4 test users!" << std::endl;
    
    // Проверяем, что все счета сохранились
    std::cout << "\n📊 Account creation verification:" << std::endl;
    std::cout << "ACC1001 accounts: " << client1.accounts.size() << std::endl;
    std::cout << "ACC1002 accounts: " << client2.accounts.size() << std::endl;
    std::cout << "ACC1003 accounts: " << client3.accounts.size() << std::endl;
    std::cout << "SUPER001 accounts: " << superUser.accounts.size() << std::endl;
    
    // Создаем запросы верификации для неверифицированных пользователей
    createVerificationRequests();
    
    // Выводим информацию для входа
    std::cout << "\n🔐 ТЕСТОВЫЕ УЧЕТНЫЕ ЗАПИСИ:" << std::endl;
    std::cout << "─────────────────────────────" << std::endl;
    std::cout << "Обычный клиент: ACC1001 / password123" << std::endl;
    std::cout << "Обычный клиент: ACC1002 / qwerty456" << std::endl;
    std::cout << "Неверифицированный: ACC1003 / test789" << std::endl;
    std::cout << "Сотрудник безопасности: SUPER001 / superpass123" << std::endl;
    std::cout << "\n💡 Для верификации ACC1003 используйте команды:" << std::endl;
    std::cout << "   SUPERLOGIN SUPER001 superpass123" << std::endl;
    std::cout << "   PENDING_VERIFICATIONS" << std::endl;
    std::cout << "   VERIFY 0" << std::endl;
}

int main() {
    std::cout << "Initializing test database..." << std::endl;
    
    Database db("data/accounts.dat");
    
    // База автоматически загружается в конструкторе
    // Если файла нет - создается пустая база
    // Создаем тестовых пользователей
    createTestUsers(db);
    
    // Выводим итоговую информацию
    auto allAccounts = db.getAllAccountIds();
    std::cout << "\n🎉 Database initialization complete!" << std::endl;
    std::cout << "Total clients in database: " << allAccounts.size() << std::endl;
    std::cout << "Total accounts: " << db.getTotalAccountsCount() << std::endl;
    std::cout << "Total balance: $" << db.getTotalBalance() << std::endl;
    
    // Отладочная информация
    db.debugPrintClients();
    
    return 0;
}

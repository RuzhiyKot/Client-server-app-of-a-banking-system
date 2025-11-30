#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <sstream>
#include "../src/server.h"
#include "../src/client.h"
#include "../src/database.h"
#include "../src/account.h"
#include "../src/crypto.h"

class BankSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Очищаем тестовые данные перед каждым тестом
        std::system("rm -rf test_data");
        std::system("mkdir -p test_data");
        setupTestData();
    }
    
    void TearDown() override {
        if (server_ && server_thread_.joinable()) {
            server_->stop();
            server_thread_.join();
        }
        // Даем время для завершения
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::system("rm -rf test_data");
    }
    
    void setupTestData() {
        Database db("test_data/accounts.dat");
        
        // Создаем тестового клиента
        ClientData client;
        client.accountId = "TEST001";
        client.fullName = "Test User";
        client.birthDate = "1990-01-01";
        client.passportData = "1234567890";
        client.passwordHash = Crypto::hashPassword("testpass");
        client.status = ClientStatus::VERIFIED;
        
        Account acc("TEST001_SAV_1", AccountType::SAVINGS, 100000.0);
        client.accounts.push_back(acc);
        
        db.addClient(client);
        
        // Создаем суперпользователя
        ClientData superUser;
        superUser.accountId = "SUPER001";
        superUser.fullName = "Security Officer";
        superUser.birthDate = "1980-01-01";
        superUser.passportData = "SUPER001";
        superUser.passwordHash = Crypto::hashPassword("superpass");
        superUser.status = ClientStatus::VERIFIED;
        
        Account superAcc("SUPER_ACC", AccountType::CHECKING, 0.0);
        superUser.accounts.push_back(superAcc);
        
        db.addClient(superUser);
    }
    
    void startTestServer(int port = 9090) {
        server_ = std::make_unique<BankServer>(port, "test_data/accounts.dat");
        server_thread_ = std::thread([this]() {
            server_->start();
        });
        // Даем серверу больше времени для запуска
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::string sendCommandAndReadResponse(const std::string& command, int port = 9090) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            return "SOCKET_ERROR";
        }
        
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
        
        // Таймаут подключения
        timeval timeout{3, 0}; // 3 секунды
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        
        if (connect(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(sockfd);
            return "CONNECT_ERROR";
        }
        
        std::string welcome_message = readSocketResponse(sockfd);
        
        // Отправляем команду
        std::string cmd = command + "\n";
        if (send(sockfd, cmd.c_str(), cmd.length(), 0) < 0) {
            close(sockfd);
            return "SEND_ERROR";
        }
        
        // Ответ на команду
        std::string response = readSocketResponse(sockfd);
        
        close(sockfd);
        return response;
    }
    
    std::string readSocketResponse(int sockfd) {
        std::string response;
        char buffer[256];
        
        // Читаем все доступные данные с таймаутом
        while (true) {
            memset(buffer, 0, sizeof(buffer));
            int bytes_read = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
            
            if (bytes_read > 0) {
                response.append(buffer, bytes_read);
                // Проверяем, есть ли еще данные для чтения
                timeval timeout{0, 100000}; // 100ms
                setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
                
                bytes_read = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
                if (bytes_read <= 0) {
                    break;
                }
                response.append(buffer, bytes_read);
            } else {
                break;
            }
        }
        
        return response;
    }
    
    // Вспомогательная функция для отправки нескольких команд в одной сессии
    std::vector<std::string> sendMultipleCommands(const std::vector<std::string>& commands, int port = 9090) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            return {"SOCKET_ERROR"};
        }
        
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
        
        timeval timeout{3, 0};
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        
        if (connect(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(sockfd);
            return {"CONNECT_ERROR"};
        }
        
        readSocketResponse(sockfd);
        
        std::vector<std::string> responses;
        for (const auto& command : commands) {
            std::string cmd = command + "\n";
            if (send(sockfd, cmd.c_str(), cmd.length(), 0) < 0) {
                responses.push_back("SEND_ERROR");
                break;
            }
            
            std::string response = readSocketResponse(sockfd);
            responses.push_back(response);
            
            // Пауза между командами
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        close(sockfd);
        return responses;
    }

    std::unique_ptr<BankServer> server_;
    std::thread server_thread_;
};

// Тест 1: Запуск сервера
TEST_F(BankSystemTest, ServerStartup) {
    EXPECT_NO_THROW({
        startTestServer();
    });
    
    std::string response = sendCommandAndReadResponse("HELP");
    EXPECT_NE(response.find("Available commands"), std::string::npos) 
        << "Server should respond to HELP. Got: " << response;
}

// Тест 2: Запуск клиента
TEST_F(BankSystemTest, ClientStartup) {
    // Просто проверяем, что клиент компилируется
    EXPECT_NO_THROW({
        BankClient client("127.0.0.1", 9090);
    });
}

// Тест 3: Логин суперпользователя
TEST_F(BankSystemTest, SuperUserLogin) {
    startTestServer();
    
    std::string response = sendCommandAndReadResponse("SUPERLOGIN SUPER001 superpass");
    
    EXPECT_NE(response.find("SUCCESS"), std::string::npos) 
        << "Superuser login should succeed. Got: " << response;
}

// Тест 4: Логин клиента
TEST_F(BankSystemTest, ClientLogin) {
    startTestServer();
    
    std::string response = sendCommandAndReadResponse("LOGIN TEST001 testpass");
    
    EXPECT_NE(response.find("SUCCESS"), std::string::npos)
        << "Client login should succeed. Got: " << response;
}

// Тест 5: Основные транзакции
TEST_F(BankSystemTest, BasicTransactions) {
    startTestServer();
    
    // Используем одну сессию для нескольких команд
    std::vector<std::string> commands = {
        "LOGIN TEST001 testpass",
        "DEPOSIT 5000",
        "WITHDRAW 2000"
    };
    
    std::vector<std::string> responses = sendMultipleCommands(commands);
    
    // Проверяем ответ на DEPOSIT (второй ответ)
    if (responses.size() > 1) {
        EXPECT_NE(responses[1].find("successful"), std::string::npos)
            << "Deposit should succeed. Got: " << responses[1];
    }
}

// Тест 6: Регистрация нового пользователя
TEST_F(BankSystemTest, UserRegistration) {
    startTestServer();
    
    std::string response = sendCommandAndReadResponse(
        "REGISTER \"New Test User\" \"1995-05-15\" \"9876543210\" \"newpassword\""
    );
    
    EXPECT_NE(response.find("SUCCESS"), std::string::npos)
        << "Registration should succeed. Got: " << response;
    EXPECT_NE(response.find("ACC"), std::string::npos)
        << "Should generate account ID. Got: " << response;
}

// Тест 7: Изменение процентных ставок
TEST_F(BankSystemTest, InterestRateChange) {
    startTestServer();
    
    // Используем одну сессию для суперпользователя
    std::vector<std::string> commands = {
        "SUPERLOGIN SUPER001 superpass",
        "SET_RATES 15.0 8.0",
        "SETTINGS"
    };
    
    std::vector<std::string> responses = sendMultipleCommands(commands);
    
    // Проверяем SET_RATES 
    if (responses.size() > 1) {
        EXPECT_NE(responses[1].find("SUCCESS"), std::string::npos)
            << "Rate change should succeed. Got: " << responses[1];
    }
    
    // Проверяем через базу данных напрямую
    Database db("test_data/accounts.dat");
    BankSettings settings = db.getSettings();
    EXPECT_EQ(settings.creditInterestRate, 15.0);
    EXPECT_EQ(settings.depositInterestRate, 8.0);
}

// Тест 8: Неудачный логин
TEST_F(BankSystemTest, FailedLogin) {
    startTestServer();
    
    std::string response = sendCommandAndReadResponse("LOGIN TEST001 wrongpass");
    
    EXPECT_NE(response.find("ERROR"), std::string::npos)
        << "Should get error for wrong password. Got: " << response;
}

// Тест 9: Верификация пользователя
TEST_F(BankSystemTest, UserVerification) {
    startTestServer();
    
    // Сначала регистрируем нового пользователя
    std::string reg_response = sendCommandAndReadResponse(
        "REGISTER \"Unverified User\" \"1998-08-20\" \"1111111111\" \"unverifiedpass\""
    );
    
    // Извлекаем ID нового пользователя
    size_t acc_pos = reg_response.find("ACC");
    ASSERT_NE(acc_pos, std::string::npos) << "Should generate account ID";
    std::string new_user_id = reg_response.substr(acc_pos, 7);
    
    // Суперпользователь проверяет и верифицирует
    std::vector<std::string> commands = {
        "SUPERLOGIN SUPER001 superpass",
        "PENDING_VERIFICATIONS", 
        "VERIFY 0"
    };
    
    std::vector<std::string> responses = sendMultipleCommands(commands);
    
    // Проверяем верификацию
    if (responses.size() > 2) {
        EXPECT_NE(responses[2].find("SUCCESS"), std::string::npos)
            << "Verification should succeed. Got: " << responses[2];
    }
    
    // Проверяем статус в базе данных
    Database db("test_data/accounts.dat");
    ClientData* client = db.findClient(new_user_id);
    EXPECT_NE(client, nullptr);
    EXPECT_EQ(client->status, ClientStatus::VERIFIED);
}

// Тест 10: Крупный перевод с одобрением
TEST_F(BankSystemTest, LargeTransferWithApproval) {
    // Сначала создаем тестовые данные в базе
    Database db("test_data/accounts.dat");
    
    // Создаем получателя
    ClientData receiver;
    receiver.accountId = "RECEIVER1";
    receiver.fullName = "Receiver User";
    receiver.birthDate = "1992-03-10";
    receiver.passportData = "2222222222";
    receiver.passwordHash = Crypto::hashPassword("receiverpass");
    receiver.status = ClientStatus::VERIFIED;
    
    Account rec_acc("RECEIVER1_SAV_1", AccountType::SAVINGS, 50000.0);
    receiver.accounts.push_back(rec_acc);
    db.addClient(receiver);
    
    // Увеличиваем баланс отправителя
    ClientData* sender = db.findClient("TEST001");
    ASSERT_NE(sender, nullptr);
    // Добавляем достаточно средств для перевода
    sender->accounts[0].deposit(250000.0, "Test funding for large transfer");
    db.updateClient(*sender);
    
    // Запускаем сервер после настройки данных
    startTestServer();
    
    // Клиент логинится и пытается сделать крупный перевод
    std::vector<std::string> client_commands = {
        "LOGIN TEST001 testpass", 
        "TRANSFER RECEIVER1 200000 Large business transfer"
    };
    
    std::vector<std::string> client_responses = sendMultipleCommands(client_commands);
    
    // Проверяем что логин успешен
    ASSERT_GT(client_responses.size(), 0);
    if (client_responses[0].find("ERROR") != std::string::npos) {
        FAIL() << "Login failed: " << client_responses[0];
    }
    
    // Проверяем что перевод требует одобрения
    ASSERT_GT(client_responses.size(), 1) << "Not enough responses from server";
    EXPECT_NE(client_responses[1].find("approval"), std::string::npos)
        << "Should require approval for large transfer. Got: " << client_responses[1];
    
    // Даем время для обработки запроса
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    // Суперпользователь проверяет и одобряет запрос
    std::vector<std::string> super_commands = {
        "SUPERLOGIN SUPER001 superpass",
        "PENDING_REQUESTS",
        "APPROVE 0"
    };
    
    std::vector<std::string> super_responses = sendMultipleCommands(super_commands);
    
    // Проверяем что суперлогин успешен
    ASSERT_GT(super_responses.size(), 0);
    if (super_responses[0].find("ERROR") != std::string::npos) {
        FAIL() << "Super login failed: " << super_responses[0];
    }
    
    // Проверяем что есть pending requests
    ASSERT_GT(super_responses.size(), 1);
    EXPECT_NE(super_responses[1].find("Pending"), std::string::npos)
        << "Should show pending requests. Got: " << super_responses[1];
    
    // Проверяем одобрение
    ASSERT_GT(super_responses.size(), 2);
    EXPECT_NE(super_responses[2].find("SUCCESS"), std::string::npos)
        << "Approval should succeed. Got: " << super_responses[2];
    
    // Даем время для выполнения перевода
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    // Проверяем что перевод выполнился через базу данных
    Database db_after("test_data/accounts.dat");
    ClientData* sender_after = db_after.findClient("TEST001");
    ClientData* receiver_after = db_after.findClient("RECEIVER1");
    
    ASSERT_NE(sender_after, nullptr);
    ASSERT_NE(receiver_after, nullptr);
    
    // Проверяем изменения балансов
    double sender_balance = sender_after->accounts[0].getBalance();
    double receiver_balance = receiver_after->accounts[0].getBalance();
    
    // Исходные балансы: sender = 100000 + 250000 = 350000, receiver = 50000
    // После перевода 200000: sender = 150000, receiver = 250000
    EXPECT_NEAR(sender_balance, 150000.0, 0.01)
        << "Sender balance should be around 150000 after transfer";
    EXPECT_NEAR(receiver_balance, 250000.0, 0.01)
        << "Receiver balance should be around 250000 after transfer";
}

// Тест 11: Крупный перевод с отказом
TEST_F(BankSystemTest, LargeTransferWithRejection) {
    // Сначала создаем тестовые данные в базе
    Database db("test_data/accounts.dat");
    
    // Создаем получателя
    ClientData receiver;
    receiver.accountId = "RECEIVER2";
    receiver.fullName = "Another Receiver";
    receiver.birthDate = "1993-04-15";
    receiver.passportData = "3333333333";
    receiver.passwordHash = Crypto::hashPassword("receiverpass2");
    receiver.status = ClientStatus::VERIFIED;
    
    Account rec_acc("RECEIVER2_SAV_1", AccountType::SAVINGS, 50000.0);
    receiver.accounts.push_back(rec_acc);
    db.addClient(receiver);
    
    // Увеличиваем баланс отправителя
    ClientData* sender = db.findClient("TEST001");
    ASSERT_NE(sender, nullptr);
    sender->accounts[0].deposit(250000.0, "Test funding for rejection test");
    db.updateClient(*sender);
    
    // Сохраняем исходные балансы
    double initial_sender_balance = sender->accounts[0].getBalance();
    double initial_receiver_balance = receiver.accounts[0].getBalance();
    
    // Запускаем сервер после настройки данных
    startTestServer();
    
    // Клиент логинится и пытается сделать крупный перевод
    std::vector<std::string> client_commands = {
        "LOGIN TEST001 testpass",
        "TRANSFER RECEIVER2 200000 Suspicious large transfer"
    };
    
    std::vector<std::string> client_responses = sendMultipleCommands(client_commands);
    
    // Проверяем что логин успешен
    ASSERT_GT(client_responses.size(), 0);
    if (client_responses[0].find("ERROR") != std::string::npos) {
        FAIL() << "Login failed: " << client_responses[0];
    }
    
    // Проверяем что перевод требует одобрения
    ASSERT_GT(client_responses.size(), 1);
    EXPECT_NE(client_responses[1].find("approval"), std::string::npos)
        << "Should require approval for large transfer. Got: " << client_responses[1];
    
    // Даем время для обработки запроса
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    // Суперпользователь проверяет и отклоняет запрос
    std::vector<std::string> super_commands = {
        "SUPERLOGIN SUPER001 superpass", 
        "PENDING_REQUESTS",
        "REJECT 0"
    };
    
    std::vector<std::string> super_responses = sendMultipleCommands(super_commands);
    
    // Проверяем что суперлогин успешен
    ASSERT_GT(super_responses.size(), 0);
    if (super_responses[0].find("ERROR") != std::string::npos) {
        FAIL() << "Super login failed: " << super_responses[0];
    }
    
    // Проверяем что есть pending requests
    ASSERT_GT(super_responses.size(), 1);
    EXPECT_NE(super_responses[1].find("Pending"), std::string::npos)
        << "Should show pending requests. Got: " << super_responses[1];
    
    // Проверяем отклонение
    ASSERT_GT(super_responses.size(), 2);
    EXPECT_NE(super_responses[2].find("SUCCESS"), std::string::npos)
        << "Rejection should succeed. Got: " << super_responses[2];
    
    // Даем время для обработки отклонения
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    // Проверяем что балансы НЕ изменились
    Database db_after("test_data/accounts.dat");
    ClientData* sender_after = db_after.findClient("TEST001");
    ClientData* receiver_after = db_after.findClient("RECEIVER2");
    
    ASSERT_NE(sender_after, nullptr);
    ASSERT_NE(receiver_after, nullptr);
    
    EXPECT_NEAR(sender_after->accounts[0].getBalance(), initial_sender_balance, 0.01)
        << "Sender balance should not change after rejected transfer";
    EXPECT_NEAR(receiver_after->accounts[0].getBalance(), initial_receiver_balance, 0.01)
        << "Receiver balance should not change after rejected transfer";
}

// Тест 12: Создание различных типов счетов
TEST_F(BankSystemTest, AccountCreation) {
    startTestServer();
    
    std::vector<std::string> commands = {
        "LOGIN TEST001 testpass",
        "CREATE_ACCOUNT 0", // Savings
        "CREATE_ACCOUNT 1", // Checking
        "CREATE_ACCOUNT 2", // Credit
        "ACCOUNTS"
    };
    
    std::vector<std::string> responses = sendMultipleCommands(commands);
    
    // Проверяем создание счетов
    if (responses.size() > 4) {
        EXPECT_NE(responses[4].find("TEST001_SAV_1"), std::string::npos);
        EXPECT_NE(responses[4].find("TEST001_SAV_2"), std::string::npos);
        EXPECT_NE(responses[4].find("TEST001_CHK_3"), std::string::npos);
        EXPECT_NE(responses[4].find("TEST001_CRD_4"), std::string::npos);
    }
}

// Тест 13: Перевод между счетами одного пользователя
TEST_F(BankSystemTest, InternalTransfer) {
    startTestServer();
    
    std::vector<std::string> commands = {
        "LOGIN TEST001 testpass",
        "CREATE_ACCOUNT 1", // Checking account
        "DEPOSIT_TO 1 50000 Funding", // Пополняем новый счет
        "TRANSFER_FROM 1 TEST001 10000 Internal transfer"
    };
    
    std::vector<std::string> responses = sendMultipleCommands(commands);
    
    // Проверяем что внутренний перевод успешен
    if (responses.size() > 3) {
        EXPECT_NE(responses[3].find("successful"), std::string::npos)
            << "Internal transfer should succeed. Got: " << responses[3];
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "=== Bank System Unit Tests ===" << std::endl;
    
    int result = RUN_ALL_TESTS();
    
    std::cout << "=== Tests completed ===" << std::endl;
    return result;
}

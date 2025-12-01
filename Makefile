CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

# Автоматическое определение платформы
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
    LDFLAGS = -lpthread
endif
ifeq ($(UNAME_S),Darwin)
    LDFLAGS = -lpthread
endif
ifeq ($(OS),Windows_NT)
    LDFLAGS = -lws2_32
endif

SRCDIR = src
OBJDIR = obj
BINDIR = bin

SERVER_SOURCES = $(SRCDIR)/main_server.cpp $(SRCDIR)/server.cpp $(SRCDIR)/database.cpp \
                 $(SRCDIR)/account.cpp $(SRCDIR)/crypto.cpp
CLIENT_SOURCES = $(SRCDIR)/main_client.cpp $(SRCDIR)/client.cpp
INIT_SOURCES = $(SRCDIR)/init_database.cpp $(SRCDIR)/database.cpp $(SRCDIR)/account.cpp $(SRCDIR)/crypto.cpp
VIEW_SOURCES = $(SRCDIR)/view_database.cpp $(SRCDIR)/database.cpp $(SRCDIR)/account.cpp $(SRCDIR)/crypto.cpp

SERVER_OBJECTS = $(SERVER_SOURCES:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
CLIENT_OBJECTS = $(CLIENT_SOURCES:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
INIT_OBJECTS = $(INIT_SOURCES:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
VIEW_OBJECTS = $(VIEW_SOURCES:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)

SERVER_TARGET = $(BINDIR)/bank_server
CLIENT_TARGET = $(BINDIR)/bank_client
INIT_TARGET = $(BINDIR)/init_db
VIEW_TARGET = $(BINDIR)/view_db

all: $(SERVER_TARGET) $(CLIENT_TARGET) $(INIT_TARGET) $(VIEW_TARGET)

$(SERVER_TARGET): $(SERVER_OBJECTS) | $(BINDIR)
	$(CXX) $(SERVER_OBJECTS) -o $@ $(LDFLAGS)

$(CLIENT_TARGET): $(CLIENT_OBJECTS) | $(BINDIR)
	$(CXX) $(CLIENT_OBJECTS) -o $@ $(LDFLAGS)

$(INIT_TARGET): $(INIT_OBJECTS) | $(BINDIR)
	$(CXX) $(INIT_OBJECTS) -o $@ $(LDFLAGS)

$(VIEW_TARGET): $(VIEW_OBJECTS) | $(BINDIR)
	$(CXX) $(VIEW_OBJECTS) -o $@ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(BINDIR):
	mkdir -p $(BINDIR)
	mkdir -p data

# Дополнительные цели для удобства
server: $(SERVER_TARGET)

client: $(CLIENT_TARGET)

init: $(INIT_TARGET)

view: $(VIEW_TARGET)

# Цель для быстрой перекомпиляции всех утилит просмотра
view_all: $(VIEW_TARGET)

# Цель для запуска тестовой базы данных
setup: $(INIT_TARGET)
	./$(INIT_TARGET)

# Цель для запуска сервера с тестовой базой
run_server: setup server
	./$(SERVER_TARGET)

# Цель для запуска клиента
run_client: client
	./$(CLIENT_TARGET)

# Цель для просмотр данных accounts.dat
view_data: $(VIEW_TARGET)
	./$(VIEW_TARGET) data/accounts.dat

# Цель для просмотра очереди верификации
# view_verification: $(VIEW_TARGET)
# 	./$(VIEW_TARGET) data/verification_queue.dat

clean:
	rm -rf $(OBJDIR) $(BINDIR)

# Очистка только данных базы (сохраняет скомпилированные программы)
clean_data:
	rm -rf data

# Полная очистка (данные + скомпилированные программы)
distclean: clean clean_data

.PHONY: all server client init view view_all setup run_server run_client clean clean_data distclean
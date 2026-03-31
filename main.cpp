#include <iostream>
#include <thread>   // Для роботи з потоками (паралельне виконання)
#include <mutex>    
#include <cstring>
#include <unistd.h> // Для системних викликів (close)
#include <arpa/inet.h> // Головна бібліотека для роботи з сокетами (мережею)

// Налаштування портів та буфера
#define TCP_PORT 5001   // Порт для основного чату (TCP)
#define UDP_PORT 5005   // Порт для пошуку сервера в мережі (UDP)
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 20  // Статичне обмеження кількості користувачів

// Глобальні змінні для керування клієнтами
int client_sockets[MAX_CLIENTS]; // Масив для зберігання номерів (дескрипторів) підключених сокетів
std::mutex clients_mutex;        // Охоронець, який не дає потокам одночасно змінювати масив

// Початкове заповнення масиву (всі слоти вільні)
void init_clients() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_sockets[i] = -1; // -1 означає, що за цим "столом" ніхто не сидить
    }
}

// Функція, яка працює окремо для кожного підключеного клієнта
void handleClient(int client_socket) {
    char buffer[BUFFER_SIZE];
    while (true) {
        memset(buffer, 0, BUFFER_SIZE); // Очищаємо кошик перед прийомом даних
        
        // Чекаємо на повідомлення від клієнта
        ssize_t bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
        
        // Якщо клієнт відключився або сталася помилка
        if (bytes_received <= 0) {
            std::cout << "\n[Сервер]: Клієнт відключився." << std::endl;
            close(client_socket); // Закриваємо з'єднання
            
            // Видаляємо клієнта з нашого списку (звільняємо слот)
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_sockets[i] == client_socket) {
                    client_sockets[i] = -1;
                    break;
                }
            }
            break; // Виходимо з циклу, завершуючи роботу потоку
        }

        std::string message(buffer, bytes_received);
        std::cout << "[Отримано]: " << message << std::endl;
        std::cout.flush(); // Примусовий вивід у консоль (актуально для Xcode)

        // РОЗСИЛКА (ФОРУМ): відправляємо отримане повідомлення ВСІМ активним клієнтам
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] != -1) {
                std::string broadcast_msg = "[Чат]: " + message;
                send(client_sockets[i], broadcast_msg.c_str(), broadcast_msg.size(), 0);
            }
        }
    }
}

// Функція-маяк (UDP): відповідає тим, хто шукає сервер у мережі
void runDiscoveryServer() {
    // Створюємо UDP сокет (SOCK_DGRAM)
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in udp_addr{};
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons(UDP_PORT); // Слухаємо на порту 5005
    udp_addr.sin_addr.s_addr = INADDR_ANY;

    // Прив'язуємо сокет до адреси
    if (bind(udp_sock, (sockaddr*)&udp_addr, sizeof(udp_addr)) < 0) {
        perror("UDP Bind failed");
        return;
    }

    std::cout << "[UDP]: Слухаю бродкаст-запити на порту " << UDP_PORT << "..." << std::endl;
    std::cout.flush();

    char buffer[BUFFER_SIZE];
    while (true) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        memset(buffer, 0, BUFFER_SIZE);
        
        // Очікуємо на "крик" у мережі
        recvfrom(udp_sock, buffer, BUFFER_SIZE, 0, (sockaddr*)&client_addr, &addr_len);
        
        // Якщо прийшов правильний пароль — відповідаємо клієнту, де ми знаходимось
        if (strcmp(buffer, "WHERE_IS_SERVER?") == 0) {
            const char* response = "I_AM_SERVER";
            sendto(udp_sock, response, strlen(response), 0, (sockaddr*)&client_addr, addr_len);
        }
    }
}

int main() {
    init_clients(); // Готуємо масив сокетів

    // 1. Запускаємо UDP потік "у фоні", щоб клієнти могли нас знайти
    std::thread(runDiscoveryServer).detach();

    // 2. Налаштовуємо основний TCP сервер для чату
    int server_sock = socket(AF_INET, SOCK_STREAM, 0); // SOCK_STREAM = TCP
    int opt = 1;
    // Дозволяємо повторно використовувати порт відразу після перезапуску сервера
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Слухати всі мережеві інтерфейси
    server_addr.sin_port = htons(TCP_PORT);   // Порт 5001

    // Прив'язуємо сервер до порта
    if (bind(server_sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("TCP Bind failed");
        return 1;
    }

    // Починаємо слухати (черга до 10 підключень)
    listen(server_sock, 10);
    std::cout << "[TCP]: Сервер форуму запущено на порту " << TCP_PORT << std::endl;
    std::cout.flush();

    // Головний цикл: постійно чекаємо на нових гостей
    while (true) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        
        // accept() блокує програму, поки не прийде новий клієнт
        int client_sock = accept(server_sock, (sockaddr*)&client_addr, &addr_len);
        
        if (client_sock >= 0) {
            bool added = false;
            {
                // Закриваємо доступ іншим потокам на час запису в масив
                std::lock_guard<std::mutex> lock(clients_mutex);
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (client_sockets[i] == -1) { // Шукаємо вільне місце 
                        client_sockets[i] = client_sock;
                        added = true;
                        break;
                    }
                }
            }

            if (added) {
                std::cout << "[Сервер]: Нове підключення!" << std::endl;
                std::cout.flush();
                // Створюємо окремий потік для спілкування з цим конкретним клієнтом
                std::thread(handleClient, client_sock).detach();
            } else {
                // Якщо місць немає — просто виганяємо
                std::cout << "[Сервер]: Відмовлено (макс. кількість клієнтів)" << std::endl;
                close(client_sock);
            }
        }
    }

    close(server_sock);
    return 0;
}

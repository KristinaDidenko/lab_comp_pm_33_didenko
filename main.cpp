#include <iostream>
#include <thread>   // Для створення фонового потоку (прослуховування повідомлень)
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>

#define TCP_PORT 5001
#define UDP_PORT 5005
#define BUFFER_SIZE 1024

// ПОТІК-СЛУХАЧ: працює паралельно з основним вводом
void receiveHandler(int sock) {
    char buffer[BUFFER_SIZE];
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        // Чекаємо на повідомлення від сервера
        ssize_t bytes = recv(sock, buffer, BUFFER_SIZE, 0);
        
        if (bytes > 0) {
            // \r повертає курсор на початок рядка, щоб текст чату перекрив символ "> "
            std::cout << "\r" << std::string(buffer, bytes) << "\n> " << std::flush;
        } else if (bytes == 0) {
            // Якщо recv повернув 0, значить сервер закрив з'єднання
            std::cout << "\n[Клієнт]: З'єднання розірвано сервером." << std::endl;
            exit(0); // Виходимо з програми
        }
    }
}

// ФУНКЦІЯ-РОЗВІДНИК: шукає сервер через UDP Broadcast
std::string findServerIP() {
    // Створюємо UDP сокет (SOCK_DGRAM)
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    int broadcast_en = 1;
    
    // Вмикаємо режим бродкасту (дозволяємо "кричати" на всю мережу)
    setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &broadcast_en, sizeof(broadcast_en));

    sockaddr_in broadcast_addr{};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(UDP_PORT);
    // Спеціальна адреса 255.255.255.255 — повідомлення отримають усі в локальній мережі
    broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;

    // Відправляємо запит-пароль
    std::string request = "WHERE_IS_SERVER?";
    sendto(udp_sock, request.c_str(), request.size(), 0, (sockaddr*)&broadcast_addr, sizeof(broadcast_addr));

    std::cout << "Шукаю сервер через UDP Broadcast..." << std::endl;

    char buffer[BUFFER_SIZE];
    sockaddr_in server_addr{};
    socklen_t addr_len = sizeof(server_addr);
    
    // Чекаємо на відповідь від сервера
    recvfrom(udp_sock, buffer, BUFFER_SIZE, 0, (sockaddr*)&server_addr, &addr_len);
    
    // Перетворюємо отриману IP-адресу з бінарного формату в зрозумілий текст (наприклад, 192.168.0.1)
    std::string ip = inet_ntoa(server_addr.sin_addr);
    close(udp_sock); // Закриваємо тимчасовий UDP сокет
    return ip;
}

int main() {
    // 1.  знаходимо IP сервера
    std::string server_ip = findServerIP();
    std::cout << "Сервер знайдено за адресою: " << server_ip << std::endl;

    // 2. підключаємось по TCP
    int sock = socket(AF_INET, SOCK_STREAM, 0); // SOCK_STREAM = TCP
    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(TCP_PORT);
    // Конвертуємо IP з тексту в бінарний вигляд для структури сокета
    inet_pton(AF_INET, server_ip.c_str(), &serv_addr.sin_addr);

    // Спроба підключитися
    if (connect(sock, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("TCP Connection failed");
        return 1;
    }

    std::cout << "Підключено до форуму! Введіть повідомлення:" << std::endl;
    std::cout.flush();

    // 3. Багатопотоковість: запускаємо receiveHandler окремо, щоб він не заважав нам писати
    std::thread(receiveHandler, sock).detach();

    // 4. Основний цикл: зчитуємо ввід користувача та відправляємо серверу
    std::string input;
    while (true) {
        std::cout << "> ";
        std::cout.flush();
        // Чекаємо, поки користувач введе рядок і натисне Enter
        std::getline(std::cin, input);
        if (!input.empty()) {
            // Відправляємо дані через TCP сокет
            send(sock, input.c_str(), input.size(), 0);
        }
    }

    close(sock);
    return 0;
}

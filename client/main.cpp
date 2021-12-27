#include <iostream>
#include "console_client.cpp"
#include "bot_client.cpp"

int main() {
    int bot = -1;
    std::unique_ptr<BuraBot> bot_instance;
    std::string ip{"cards.igerbit.ru"};

    std::string answer;

    std::cout << "Ip (default: " << ip << "): ";
    std::getline(std::cin, answer);
    if(!answer.empty()) ip = answer;


    while (bot == -1) {
        std::cout << "Enable bot (Y/N | default: Yes): ";
        std::getline(std::cin, answer);
        if(answer == "N") bot = 0;
        if(answer == "Y") bot = 1;
    }

    BuraConsole console(ip, "2021");

    if(bot == 1) {
        bot_instance = std::make_unique<BuraBot>(ip, "2021");
        bot_instance->launch();
    }

    std::string nickname;
    std::wcout << L"Enter your nickname: ";
    std::cin >> nickname;

    console.launch(nickname);
    if(bot_instance) {
        bot_instance->exit();
        bot_instance->join();
    }

    return 0;
}

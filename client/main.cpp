#include <iostream>
#include "console_client.cpp"
#include "bot_client.cpp"



int main(int argc, char* argv[]) {

    int bot = true;
    std::unique_ptr<BuraBot> bot_instance;
    std::string ip{"127.0.0.1"};

    std::string answer;

    std::cout << "Ip (default: 127.0.0.1): ";
    std::getline(std::cin, answer);
    if(!answer.empty()) ip = answer;

    std::cout << "Enable bot (Y/N | default: Yes): ";
    std::getline(std::cin, answer);

    if(answer == "N") bot = false;

    BuraConsole console(ip, "2021");

    if(bot) {
        bot_instance = std::make_unique<BuraBot>(ip, "2021");
        bot_instance->launch();
    }

    std::wstring nickname;
    std::wcout << L"Enter your nickname: ";
    std::wcin >> nickname;



    console.launch(nickname);
    if(bot_instance) {
        bot_instance->exit();
        bot_instance->join();
    }

    return 0;
}

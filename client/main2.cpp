#include <iostream>
#include "bot_client.cpp"



int main(int argc, char* argv[]) {

    bool bot = true;
    std::unique_ptr<BuraBot> bot_instance;
    std::string ip{"127.0.0.1"};

    if(argc > 1) {
        if(std::string(argv[1]) == "host") {
            bot = false;
        }
        else {
            ip = std::string(argv[1]);
            bot = false;
        }
    }

    if(bot) {
        bot_instance = std::make_unique<BuraBot>(ip, "2021");
        bot_instance->launch();
    }

    if(bot_instance) {
        bot_instance->join();
    }

    return 0;
}

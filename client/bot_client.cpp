#include <iostream>
#include <string>
#include <thread>

#include "game.h"

using namespace bura;
using std::wstring;


class BuraBot {
   private:
    std::unique_ptr<std::thread> gameThread;
    bool isExit = false;

    // Game
    std::string ip;
    std::string port;

    BuraClient gameClient;
    GameState gameState;

    void game() {
        gameClient.start(ip);
        gameClient.connect(L"bot");

        while (!isExit) {
            try {
                std::this_thread::sleep_for(std::chrono::milliseconds(5000));
                std::cout << "fetch" << std::endl;
                gameState = gameClient.fetch();

                std::cout << "state " << static_cast<int>(gameState.status) << std::endl;

                auto attackCards = gameState.attack_cards;
                std::vector<Card> attackCardsSort = attackCards;
                auto myCards = gameState.my_cards;
                auto trump = gameState.trump;



                if(!attackCardsSort.empty()) {
                    std::sort(attackCardsSort.begin(), attackCardsSort.end(), [&](Card &a, Card& b) {
                        if(a.suit == trump.suit && b.suit != trump.suit) return true;
                        return a.value < b.value;
                    });
                }

                if(gameState.status == GameStatus::YourMove) {
                    std::cout << "move" << std::endl;
                    std::sort(myCards.begin(), myCards.end(), [&](Card &a, Card& b) {
                        if(a.suit != trump.suit && b.suit == trump.suit) return true;
                        return a.value > b.value;
                    });

                    std::vector<Card> myMove;

                    myMove.emplace_back(myCards.front());

                    std::all_of(myCards.begin() + 1, myCards.end(), [&](Card &item) {
                        if(item.value != myMove.front().value) return false;
                        myMove.emplace_back(item);

                        return true;
                    });

                    std::cout << "fin move" << std::endl;
                    gameClient.finishMove(myMove);

                }
                if(gameState.status == GameStatus::YourDef) {
                    std::cout << "def" << std::endl;
                    if(attackCards.size() > myCards.size()) {
                        gameClient.passDef();
                        return;
                    }

                    std::sort(myCards.begin(), myCards.end(), [&](Card &a, Card& b) {
                        if(a.suit != trump.suit && b.suit == trump.suit) return true;
                        return a.value > b.value;
                    });

                    for (const auto &item : attackCardsSort){

                        auto it = std::find_if(myCards.begin(), myCards.end(), [&](Card &mCard) {
                            return Card::canUseCard(mCard, item, trump.suit);
                        });

                        if(it != myCards.end()) {
                            auto rit = std::find_if(attackCards.begin(), attackCards.end(), [&](Card &card) {
                                return card.suit == item.suit && card.value == item.value;
                            });

                            (*rit) = (*it);
                            myCards.erase(it);
                        }
                        else {
                            gameClient.passDef();
                            return;
                        }

                    }
                    std::cout << "fin def" << std::endl;
                    gameClient.finishDef(attackCards);
                }
                if(gameState.status == GameStatus::Lose || gameState.status == GameStatus::Win) {
                    isExit = true;
                }
            }
            catch (std::exception &err) {
                std::cout << err.what() << std::endl;
            }

        }
    }



   public:
    BuraBot(std::string ip, std::string port) : ip(std::move(ip)), port(std::move(port))  {}


    void launch(){
        gameThread = std::make_unique<std::thread>(&BuraBot::game, this);
    }

    void exit() {
        isExit = true;
    }

    void join() {
        gameThread->join();
    }
};
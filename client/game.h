#ifndef CLIENT_GAME_H
#define CLIENT_GAME_H
#include <iostream>
#include <shared_mutex>
#include <vector>

#include "http.h"

namespace bura {


enum struct CardSuit : int8_t {
    None = -1,
    Hearts = 0,    // Черви
    Diamonds = 1,  // Буби
    Spades = 2,    // Пики
    Clubs = 3      // Крести
};

enum struct CardValue : int8_t {
    None = -1,
    Ace = 0,    // Туз
    King = 1,   // Король
    Queen = 2,  // Дама
    Jack = 3,   // Валет
    Ten = 4,    // 10-ка
    Nine = 5,   // 9-ка
    Eight = 6,  // 8-ка
    Seven = 7,  // 7-ка
    Six = 8     // 6-ка
};

enum struct GameStatus : int8_t {
    None = -1,
    Idle = 0,
    OpponentMove = 1,
    YourMove = 2,
    OpponentDef = 3,
    YourDef = 4,
    Finish = 5,
    Win = 6,
    Lose = 7,
    Connecting = 100,
    WaitUpdate = 101
};

using CardType = uint16_t;

struct Card {
    CardSuit suit;
    CardValue value;
    bool hidden{false};
    bool active{false};

    Card(CardSuit suit, CardValue value, bool hidden, bool active);
    Card(CardSuit suit, CardValue value, bool hidden);
    Card(CardSuit suit, CardValue value);
    explicit Card(CardType type, bool hidden = false);
    Card();

    [[nodiscard]] CardType type() const;

    static bool canUseCard(const Card &a, const Card &b, CardSuit trump) {
        if(a.suit == CardSuit::None || b.suit == CardSuit::None) return false;
        if(a.value == CardValue::None || b.value == CardValue::None) return false;
        if(a.suit == trump && b.suit != trump) return true;
        if(a.suit != b.suit) return false;
        return a.value < b.value;
    }
};

struct GameState {
    std::string id;
    GameStatus status{GameStatus::None};

    std::vector<Card> my_cards;
    std::vector<Card> attack_cards;
    std::vector<Card> defend_cards;
    std::vector<Card> opponent_cards;

    uint8_t inFall{};
    uint8_t inHeap{};

    Card trump{};  // козырь
};

using MoveFunctionPtr = void (*)(std::vector<CardType> &yourMove);
using StartFunctionPtr = void (*)();
using EndFunctionPtr = void (*)();

class BuraClient {
   private:
    GameState state{};
    std::mutex tcpMutex{};
    std::unique_ptr<http::Session> session{};



   public:
    void start(const std::string &host);
    uint16_t connect(const std::wstring &nickname);

    GameState fetch();
    uint16_t finishMove(std::vector<Card> cards);
    uint16_t finishDef(std::vector<Card> cards);
    uint16_t passDef();
};


}  // namespace bura


#endif  // CLIENT_GAME_H

#include "game.h"

#include <sstream>
#include <random>

#include "http.h"
#include "httpreq.cpp"
using namespace bura;

Card::Card(CardSuit suit, CardValue value, bool hidden, bool active) : suit(suit), value(value), hidden(hidden), active(active) {}
Card::Card(CardSuit suit, CardValue type, bool hidden) : suit(suit), value(type), hidden(hidden) {}
Card::Card(CardSuit suit, CardValue value): suit(suit), value(value) {}
Card::Card() : suit(CardSuit::None), value(CardValue::None), hidden(false) {}
Card::Card(bura::CardType type, bool hidden) : hidden(hidden) {
    suit = static_cast<CardSuit>((type >> 8) & 0xFF);
    value = static_cast<CardValue>(type & 0xFF);
}
CardType Card::type() const { return static_cast<uint16_t>(static_cast<int>(suit) << 8 | (static_cast<int>(value))); }

std::string generateRandomString(size_t length)
{
    const char* charmap = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const size_t charmapLength = strlen(charmap);
    auto generator = [&](){

        std::default_random_engine dre(std::random_device{}());
        std::uniform_int_distribution<> dist(0 , charmapLength - 1);

        return charmap[dist(dre)];
    };
    std::string result;
    result.reserve(length);
    generate_n(back_inserter(result), length, generator);
    return result;
}

void BuraClient::start(const std::string &host) {
    std::lock_guard<std::mutex> sLock(tcpMutex);
    state.id = generateRandomString(8);
    session = std::make_unique<http::Session>(host, "2021", std::chrono::seconds(30));
}
uint16_t BuraClient::connect(const std::wstring &nickname) {
    std::lock_guard<std::mutex> sLock(tcpMutex);
    auto result = session->call(2, [&](std::ostringstream &ss) {

        ss << state.id;

        auto wchar = wcsdup(nickname.c_str());
        ss.write(reinterpret_cast<char *>(wchar), sizeof(wchar_t) * (nickname.size() + 1));
        free(wchar);
    });

    return result.error;
}



GameState BuraClient::fetch() {
    std::lock_guard<std::mutex> sLock(tcpMutex);






    auto result = session->call(
        3,
        [&](auto &ss) {
            ss << state.id;
        },
        [&](std::istringstream &r, auto error) {
            if(error != 0) return;

            CardType tmpCardType{};
            uint32_t tmpSize;

            r.read(reinterpret_cast<char *>(&state.status), sizeof(int8_t));

            r.read(reinterpret_cast<char *>(&tmpCardType), sizeof(CardType));
            state.trump = Card(tmpCardType);

            r.read(reinterpret_cast<char *>(&state.inHeap), sizeof(uint8_t));
            r.read(reinterpret_cast<char *>(&state.inFall), sizeof(uint8_t));

            // My Cards
            r.read(reinterpret_cast<char *>(&tmpSize), sizeof(uint32_t));

            state.my_cards.clear();
            state.my_cards.reserve(tmpSize);

            for (uint32_t i = 0; i < tmpSize; ++i) {
                r.read(reinterpret_cast<char *>(&tmpCardType), sizeof(CardType));
                state.my_cards.emplace_back(tmpCardType);
            }

            // Opponent Cards
            r.read(reinterpret_cast<char *>(&tmpSize), sizeof(uint32_t));

            state.opponent_cards.clear();
            state.opponent_cards.reserve(tmpSize);

            for (uint32_t i = 0; i < tmpSize; ++i) {
                r.read(reinterpret_cast<char *>(&tmpCardType), sizeof(CardType));
                state.opponent_cards.emplace_back(Card(tmpCardType, true));
            }

            // Attack Cards
            r.read(reinterpret_cast<char *>(&tmpSize), sizeof(uint32_t));

            state.attack_cards.clear();
            state.attack_cards.reserve(tmpSize);

            for (uint32_t i = 0; i < tmpSize; ++i) {
                r.read(reinterpret_cast<char *>(&tmpCardType), sizeof(CardType));
                state.attack_cards.emplace_back(tmpCardType);
            }

            // Defend Cards
            r.read(reinterpret_cast<char *>(&tmpSize), sizeof(uint32_t));

            state.defend_cards.clear();
            state.defend_cards.reserve(tmpSize);

            for (uint32_t i = 0; i < tmpSize; ++i) {
                r.read(reinterpret_cast<char *>(&tmpCardType), sizeof(CardType));
                state.defend_cards.emplace_back(tmpCardType);
            }
        }
    );

    return state;
}
uint16_t BuraClient::finishMove(std::vector<Card> cards) {
    std::lock_guard<std::mutex> sLock(tcpMutex);
    auto result = session->call(
        4,
        [&](std::ostringstream &ss){
            ss << state.id;
            uint32_t size = static_cast<uint32_t>(cards.size());
            ss.write(reinterpret_cast<char *>(&size), sizeof(uint32_t));
            for (auto &item : cards) {
                auto type = item.type();
                ss.write(reinterpret_cast<char *>(&type), sizeof(CardType));
            }
        }
    );

    return result.error;
}
uint16_t BuraClient::passDef() {
    std::lock_guard<std::mutex> sLock(tcpMutex);
    auto result = session->call(
        5,
        [&](std::ostringstream &ss){
            ss << state.id;
            uint8_t pass = 0;
            ss.write(reinterpret_cast<char *>(&pass), sizeof(uint8_t));
        }
    );

    return result.error;
}
uint16_t BuraClient::finishDef(std::vector<Card> cards) {
    std::lock_guard<std::mutex> sLock(tcpMutex);
    auto result = session->call(
        5,
        [&](std::ostringstream &ss){
            ss << state.id;
            uint8_t pass = 1;
            ss.write(reinterpret_cast<char *>(&pass), sizeof(uint8_t));
            auto size = static_cast<uint32_t>(cards.size());
            ss.write(reinterpret_cast<char *>(&size), sizeof(uint32_t));
            for (auto &item : cards) {
                auto type = item.type();
                ss.write(reinterpret_cast<char *>(&type), sizeof(CardType));
            }
        }
    );

    return result.error;
}

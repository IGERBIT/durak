#include <conio.h>
#include <fcntl.h>
#include <io.h>

#include <iostream>
#include <string>
#include <chrono>
#include <thread>

#include "game.h"
#include "windows.h"

using namespace bura;
using std::wstring;
using std::chrono::duration;
using std::chrono::high_resolution_clock;

constexpr int COLOR_DEFAULT = -1;
constexpr int COLOR_INHERIT = -2;

const int COLOR_DEFAULT_BG = 0xFFFFFF;
const int COLOR_DEFAULT_FG = 0;

struct Pixel {
    explicit Pixel() : symbol(' '), color(COLOR_DEFAULT), bgColor(COLOR_DEFAULT) {}
    explicit Pixel(wchar_t symbol) : symbol(symbol), color(COLOR_DEFAULT), bgColor(COLOR_DEFAULT) {}
    Pixel(wchar_t symbol, int32_t color) : symbol(symbol), color(color), bgColor(COLOR_DEFAULT) {}
    Pixel(wchar_t symbol, int32_t color, int32_t bgColor) : symbol(symbol), color(color), bgColor(bgColor) {}
    wchar_t symbol{' '};
    int32_t color{COLOR_DEFAULT};
    int32_t bgColor{COLOR_DEFAULT};

    bool operator==(const Pixel &rhs) const { return symbol == rhs.symbol && color == rhs.color && bgColor == rhs.bgColor; }
    bool operator!=(const Pixel &rhs) const { return !(rhs == *this); }

    void getFgHexColor(int &r, int &g, int &b) const {
        r = (color >> 16) & 0xFF;
        g = (color >> 8) & 0xFF;
        b = color & 0xFF;
    }

    void getBgHexColor(int &r, int &g, int &b) const {
        r = (bgColor >> 16) & 0xFF;
        g = (bgColor >> 8) & 0xFF;
        b = bgColor & 0xFF;
    }
};

class BuraConsole {
   private:
    // Console
    HANDLE consoleHandle{};
    HWND consoleHwnd{};
    COORD screenSize{};
    uint64_t screenBufferSize{};
    std::unique_ptr<Pixel *> screenBufferA;
    std::unique_ptr<Pixel *> screenBufferB;
    double deltaTime{1};
    std::wstring bgFill{};

    // Game
    std::string ip;
    std::string port;
    std::wstring nickname{};

    BuraClient gameClient;
    GameState gameState;

    std::shared_mutex stateMutex;

    // Local State
    bool isExit{false};
    std::vector<Card> heapCards;
    uint8_t selectedCard{};
    std::vector<Card> selectedCards;
    std::vector<Card> myCards;
    std::wstring errorText;
    std::chrono::time_point<std::chrono::steady_clock> errorTextDuration{};
    int cardCursor{-1};

    void setup() {
        consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
        consoleHwnd = GetConsoleWindow();

        SetConsoleCP(CP_UTF8);
        _setmode(_fileno(stdout), _O_U16TEXT);

        ShowWindow(consoleHwnd, SW_MAXIMIZE);
        auto gwlStyle = GetWindowLong(consoleHwnd, GWL_STYLE);
        gwlStyle &= ~(WS_BORDER | WS_DLGFRAME);
        SetWindowLong(consoleHwnd, GWL_STYLE, gwlStyle);

        DWORD cMode;
        GetConsoleMode(consoleHandle, &cMode);
        cMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(consoleHandle, cMode);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        CONSOLE_SCREEN_BUFFER_INFO bufferInfo;
        GetConsoleScreenBufferInfo(consoleHandle, &bufferInfo);



        screenSize.X = static_cast<short>(bufferInfo.srWindow.Right - bufferInfo.srWindow.Left + 1);
        screenSize.Y = static_cast<short>(bufferInfo.srWindow.Bottom - bufferInfo.srWindow.Top + 1);

        screenBufferSize = screenSize.X * screenSize.Y;

        SetConsoleScreenBufferSize(consoleHandle, screenSize);

        screenBufferA = std::make_unique<Pixel *>(new Pixel[screenBufferSize]);
        screenBufferB = std::make_unique<Pixel *>(new Pixel[screenBufferSize]);

        auto bufferA = *screenBufferA;
        auto bufferB = *screenBufferB;

        for (int i = 0; i < screenBufferSize; ++i) {
            bufferA[i] = Pixel();
            bufferB[i] = Pixel();
        }

        bgFill = std::wstring(screenBufferSize, ' ');
    }

    // Threads

    void render() {
        DWORD written;
        FillConsoleOutputCharacter(consoleHandle, ' ', screenBufferSize, COORD(), &written);

        high_resolution_clock::time_point renderTime, lastRenderTime = high_resolution_clock::now();

        COORD cursorCord{};
        COORD endCord{static_cast<SHORT>(screenSize.X - 1), static_cast<SHORT>(screenSize.Y - 1)};

        int last = 2;

        while (last > 0) {
            renderTime = high_resolution_clock::now();
            deltaTime = (duration<double>{renderTime - lastRenderTime}).count();
            lastRenderTime = renderTime;

            draw();

            auto bufferA = *screenBufferA;
            auto bufferB = *screenBufferB;

            bool isPrevPixelChange = false;
            int prevFgColor = COLOR_DEFAULT;
            int prevBgColor = COLOR_DEFAULT;

            for (int y = 0, px = 0; y < screenSize.Y; ++y) {
                for (int x = 0; x < screenSize.X; (++px, ++x)) {
                    auto &newPixel = bufferA[px], &oldPixel = bufferB[px];

                    if (newPixel != oldPixel) {
                        if (!isPrevPixelChange) {
                            cursorCord.X = static_cast<short>(x);
                            cursorCord.Y = static_cast<short>(y);
                            SetConsoleCursorPosition(consoleHandle, cursorCord);
                        }

                        if (!isPrevPixelChange || prevBgColor != newPixel.bgColor) {
                            if (newPixel.bgColor == COLOR_DEFAULT)
                                std::wcout << L"\x1b[49m";
                            else if (newPixel.bgColor != COLOR_INHERIT) {
                                int r, g, b;
                                newPixel.getBgHexColor(r, g, b);
                                std::wcout << L"\x1b[48;2;" << r << L";" << g << L";" << b << L"m";
                            }
                        }

                        if (!isPrevPixelChange || prevFgColor != newPixel.color) {
                            if (newPixel.color == COLOR_DEFAULT)
                                std::wcout << L"\x1b[39m";
                            else if (newPixel.color != COLOR_INHERIT) {
                                int r, g, b;
                                newPixel.getFgHexColor(r, g, b);
                                std::wcout << L"\x1b[38;2;" << r << L";" << g << L";" << b << L"m";
                            }
                        }


                        std::wcout << newPixel.symbol;
                        isPrevPixelChange = true;
                        prevFgColor = newPixel.color;
                        prevBgColor = newPixel.bgColor;

                        cursorCord.X += 1;

                        if (cursorCord.X >= screenSize.X) {
                            cursorCord.X = 0;
                            cursorCord.Y++;
                        }
                    } else {
                        isPrevPixelChange = false;
                    }

                    oldPixel = {newPixel};
                    newPixel.symbol = ' ';
                    newPixel.color = COLOR_DEFAULT;
                    newPixel.bgColor = COLOR_DEFAULT;
                }
            }

            if (cursorCord.X != endCord.X || cursorCord.Y != endCord.Y) {
                cursorCord.X = endCord.X;
                cursorCord.Y = endCord.Y;
                SetConsoleCursorPosition(consoleHandle, endCord);
            }

            if(isExit) last--;
        }
    }
    void input() {
        while (!isExit) {
            auto character = getch();

            switch (character) {
                case 8: OnPressBackspace(); break;
                case 13: OnPressEnter(); break;
                case 27: OnPressEsc(); break;
                case 32: OnPressSpace(); break;
                case 72: OnPressUp(); break;
                case 75: OnPressLeft(); break;
                case 77: OnPressRight(); break;
                case 80: OnPressDown(); break;
                default:
                    break;
            }
        }
    }
    void update() {
        std::unique_lock<std::shared_mutex> sLock(stateMutex);
        gameState.status = GameStatus::Connecting;
        try {
            gameClient.start(ip);
            gameClient.connect(nickname);
            sLock.unlock();

            int updates = 0;
            while (!isExit) {
                try {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));


                    sLock.lock();
                    gameState = gameClient.fetch();

                    if(gameState.status != GameStatus::YourDef && gameState.status != GameStatus::YourMove) {
                        cardCursor = -1;
                    }
                    else {
                        if(cardCursor == -1) cardCursor = 0;
                    }

                    sLock.unlock();

                } catch (std::exception &e) {
                    errorText = L"Error est";
                    //mbstowcs(&errorText[0], e.what(), strlen(e.what() + 1));
                }
            }

        }
        catch (std::exception &e) {
            mbstowcs(&errorText[0], e.what(), strlen(e.what() + 1));
        }
    }

    // Draw Utils

    [[nodiscard]] int coord2px(int x, int y) const {
        return (screenSize.X * y) + x;
    }

    void px2cord(int px, int &x, int &y) const {
        y = px / screenSize.X;
        x = px % screenSize.X;
    }

    void normalizeCord(int &x, int &y) const {
        y += x / screenSize.X;
        x = x % screenSize.X;
    }

    void setSymbol(int x, int y, wchar_t symbol) {
        auto px = coord2px(x,y);
        if(px < 0 || px >= screenBufferSize) return;
        (*screenBufferA)[px].symbol = symbol;
    }

    void setColor(int x, int y, int color) {
        auto px = coord2px(x,y);
        if(px < 0 || px >= screenBufferSize) return;
        (*screenBufferA)[px].color = color;
    }

    void setBgColor(int x, int y, int color) {
        auto px = coord2px(x,y);
        if(px < 0 || px >= screenBufferSize) return;
        (*screenBufferA)[px].bgColor = color;
    }

    void setPixel(int x, int y, wchar_t symbol, int color, int bgColor = COLOR_DEFAULT) {
        auto px = coord2px(x, y);
        if(px < 0 || px >= screenBufferSize) return;
        auto &pixel = (*screenBufferA)[px];
        pixel.symbol = symbol;
        pixel.color = color;
        pixel.bgColor = bgColor;
    }

    void setPixel(int x, int y, Pixel pxl) {
        auto px = coord2px(x, y);
        if(px < 0 || px >= screenBufferSize) return;
        auto &pixel = (*screenBufferA)[px];
        pixel.symbol = pxl.symbol;
        pixel.color = pxl.color;
        pixel.bgColor = pxl.bgColor;
    }

    void printText(int sx, int sy, const wchar_t *str, int color = COLOR_DEFAULT, int bgColor = COLOR_DEFAULT) {
        auto len = wcslen(str);
        auto px = coord2px(sx,sy);
        int x{},y{};

        for (int i = 0; i < len; ++i) {
            if(str[i] != '\n') {
                px2cord(px, x,y);
                setPixel(x,y, str[i], color, bgColor);
                px++;
            } else {
                x = sx;
                y += 1;
                px = coord2px(x,y);
            }
        }
    }

    void printTextAlignCenter(int sx, int sy, const wchar_t *str, int color = COLOR_DEFAULT, int bgColor = COLOR_DEFAULT) {
        auto wstr = wcsdup(str);
        wchar_t *pch = wcstok(wstr, L"\n");

        int i = 0;
        while (pch != nullptr) {
            auto len = wcslen(pch);
            printText(static_cast<int>(sx - (len / 2)), sy + i, pch, color, bgColor);
            pch = wcstok(nullptr, L"\n");
            ++i;
        }

        free(wstr);
    }

    static std::wstring getCardSuitAndValueStr(Card &card) {
        std::wstring result;
        switch (card.value) {
            case bura::CardValue::Six:
                result += L"6";
                break;
            case bura::CardValue::Seven:
                result += L"7";
                break;
            case bura::CardValue::Eight:
                result += L"8";
                break;
            case bura::CardValue::Nine:
                result += L"9";
                break;
            case bura::CardValue::Ten:
                result += L"10";
                break;
            case bura::CardValue::Jack:
                result += L"J";
                break;
            case bura::CardValue::Queen:
                result += L"Q";
                break;
            case bura::CardValue::King:
                result += L"K";
                break;
            case bura::CardValue::Ace:
                result += L"A";
                break;
            case bura::CardValue::None:
                break;
        }

        switch (card.suit) {
            case bura::CardSuit::Hearts:
                result += L"♥";
                break;
            case bura::CardSuit::Diamonds:
                result += L"♦";
                break;
            case bura::CardSuit::Spades:
                result += L"♠";
                break;
            case bura::CardSuit::Clubs:
                result += L"♣";
                break;
            case bura::CardSuit::None:
                break;
        }

        return result;
    }

    void printCards(int x, int y, std::vector<bura::Card> cards, bool activeMoveDown = false) {
        auto width = 8;
        if (cards.size() > 1) width += static_cast<int>(5 * (cards.size() - 1));

        auto isPrevActive = false;

        if (x == -1) {
            x = (screenSize.X - width) / 2;
        }

        for (int i = 0; i < cards.size(); ++i) {
            auto isActive = cards[i].active;
            int dy = isActive ? (activeMoveDown ? 2 : -2) : 0;

            auto cardValue = getCardSuitAndValueStr(cards[i]);

            if (i == 0) {
                printText(x, y + dy + 0, L"╔══════╗", 0, 0xFFFFFF);
                printText(x, y + dy + 1, L"║      ║", 0, 0xFFFFFF);
                printText(x, y + dy + 2, L"║      ║", 0, 0xFFFFFF);
                printText(x, y + dy + 3, L"║      ║", 0, 0xFFFFFF);
                printText(x, y + dy + 4, L"║      ║", 0, 0xFFFFFF);
                printText(x, y + dy + 5, L"╚══════╝", 0, 0xFFFFFF);
            } else if (isPrevActive == isActive) {
                printText(x, y + dy + 0, L"╦══════╗", 0, 0xFFFFFF);
                printText(x, y + dy + 1, L"║      ║", 0, 0xFFFFFF);
                printText(x, y + dy + 2, L"║      ║", 0, 0xFFFFFF);
                printText(x, y + dy + 3, L"║      ║", 0, 0xFFFFFF);
                printText(x, y + dy + 4, L"║      ║", 0, 0xFFFFFF);
                printText(x, y + dy + 5, L"╩══════╝", 0, 0xFFFFFF);
            } else if (isPrevActive != activeMoveDown) {
                printText(x, y + dy + 0, L"╔═╩════╗", 0, 0xFFFFFF);
                printText(x, y + dy + 1, L"║      ║", 0, 0xFFFFFF);
                printText(x, y + dy + 2, L"║      ║", 0, 0xFFFFFF);
                printText(x, y + dy + 3, L"╣      ║", 0, 0xFFFFFF);
                printText(x, y + dy + 4, L"║      ║", 0, 0xFFFFFF);
                printText(x, y + dy + 5, L"╚══════╝", 0, 0xFFFFFF);
            } else {
                printText(x, y + dy + 0, L"╔══════╗", 0, 0xFFFFFF);
                printText(x, y + dy + 1, L"║      ║", 0, 0xFFFFFF);
                printText(x, y + dy + 2, L"╣      ║", 0, 0xFFFFFF);
                printText(x, y + dy + 3, L"║      ║", 0, 0xFFFFFF);
                printText(x, y + dy + 4, L"║      ║", 0, 0xFFFFFF);
                printText(x, y + dy + 5, L"╚═╦════╝", 0, 0xFFFFFF);
            }

            if (cards[i].hidden) {
                printText(x + 2, y + dy + 2, L"####", 0, 0xFFFFFF);
                printText(x + 2, y + dy + 3, L"####", 0, 0xFFFFFF);
            } else {
                auto color = cards[i].suit == bura::CardSuit::Hearts || cards[i].suit == bura::CardSuit::Diamonds ? 0xFF0000 : 0;
                printText(x + 1, y + dy + 1, cardValue.c_str(), color, 0xFFFFFF);
                printText(static_cast<int>(x + 7 - cardValue.size()), y + dy + 4, cardValue.c_str(), color, 0xFFFFFF);
            }

            x += 5;
            isPrevActive = isActive;
        }
    }

    void printCardsWithSpace(int x, int y, std::vector<bura::Card> cards, int spaceSize, int reserve = -1) {
        auto width = 8;

        if (reserve < 0 || cards.size() > reserve) reserve = static_cast<int>(cards.size());

        if (reserve > 1) width += (spaceSize + 8) * (reserve - 1);

        if (x == -1) {
            x = (screenSize.X - width) / 2;
        }

        for (auto &card : cards) {
            auto cardValue = getCardSuitAndValueStr(card);

            printText(x, y + 0, L"╔══════╗", 0, 0xFFFFFF);
            printText(x, y + 1, L"║      ║", 0, 0xFFFFFF);
            printText(x, y + 2, L"║      ║", 0, 0xFFFFFF);
            printText(x, y + 3, L"║      ║", 0, 0xFFFFFF);
            printText(x, y + 4, L"║      ║", 0, 0xFFFFFF);
            printText(x, y + 5, L"╚══════╝", 0, 0xFFFFFF);

            if (card.hidden) {
                printText(x + 2, y + 2, L"####", 0, 0xFFFFFF);
                printText(x + 2, y + 3, L"####", 0, 0xFFFFFF);
            } else {
                auto color = card.suit == bura::CardSuit::Hearts || card.suit == bura::CardSuit::Diamonds ? 0xFF0000 : 0;
                printText(x + 1, y + 1, cardValue.c_str(), color, 0xFFFFFF);
                printText(static_cast<int>(x + 7 - cardValue.size()) , y + 4, cardValue.c_str(), color, 0xFFFFFF);
            }

            x += 8 + spaceSize;
        }
    }

    void printError(const wchar_t *str, std::chrono::milliseconds timeout) {
        errorText = str;
        errorTextDuration = std::chrono::steady_clock::now() + timeout;
    }

    // Draw method

    void draw() {
        printText(0,0, bgFill.c_str(), COLOR_DEFAULT_FG, COLOR_DEFAULT_BG);
        std::shared_lock<std::shared_mutex> sLock(stateMutex);

        if(isExit) {
            printTextAlignCenter(screenSize.X / 2,screenSize.Y / 2 - 2, L"Exit...", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG);
            return;
        }

        if(gameState.status == GameStatus::None) {
            printTextAlignCenter(screenSize.X / 2,screenSize.Y / 2 - 2, L"Loading...", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG);
            return;
        }

        if(gameState.status == GameStatus::Connecting) {
            printTextAlignCenter(screenSize.X / 2,screenSize.Y / 2, L"Connecting...", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG);
            return;
        }

        if(gameState.status == GameStatus::Idle) {
            printTextAlignCenter(screenSize.X / 2,screenSize.Y / 2, L"Wait opponent...", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG);
            return;
        }

        if(gameState.status == GameStatus::Win) {
            printTextAlignCenter(screenSize.X / 2,screenSize.Y / 2, L"You WIN!!!", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG);
            return;
        }

        if(gameState.status == GameStatus::Lose) {
            printTextAlignCenter(screenSize.X / 2,screenSize.Y / 2, L"You lose :(", 0xFF0000, COLOR_DEFAULT_BG);
            return;
        }

        printText(1,0, L"ESC - Выход\nLeft/Right - Выбор карты\nSpace - Играть карту\nEnter - Завершить ход\nBackspace - Забрать карты", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG);

        std::vector<Card> heap;

        if(gameState.inHeap > 0) {
            heap.emplace_back(gameState.trump);
        }

        if(gameState.inHeap > 1) {
            heap.emplace_back(Card(CardSuit::None, CardValue::None, true));
        }

        // Heap
        printCards(screenSize.X - 14, (screenSize.Y / 2) - 3, heap);

        std::wstring inHeap = L"In Heap: " + std::to_wstring(gameState.inHeap);
        std::wstring inFall = L"In Fall: " + std::to_wstring(gameState.inFall);

        printText(screenSize.X - 14, (screenSize.Y / 2) + 3, inHeap.c_str(), COLOR_DEFAULT_FG, COLOR_DEFAULT_BG);
        printText(screenSize.X - 14, (screenSize.Y / 2) + 4, inFall.c_str(), COLOR_DEFAULT_FG, COLOR_DEFAULT_BG);

        printCards(-1, 0, gameState.opponent_cards);

        myCards = {};
        int i = 0;
        for (const auto &item : gameState.my_cards) {
            Card wrap = Card(item.suit, item.value);
            wrap.active = (i == cardCursor) || std::any_of(selectedCards.begin(), selectedCards.end(), [&](Card &card){
                             return card.suit == item.suit && card.value == item.value;
                          });
            myCards.emplace_back(wrap);
            ++i;
        }

        printCards(-1, screenSize.Y - 6, myCards);

        if(gameState.status == GameStatus::OpponentMove) {
            printTextAlignCenter(screenSize.X / 2,(screenSize.Y / 2) - 2, L"Opponent Move", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG);
        }

        if(gameState.status == GameStatus::YourMove) {
            printTextAlignCenter(screenSize.X / 2,(screenSize.Y / 2) - 2, L"Your Move", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG);
            printCardsWithSpace(-1, (screenSize.Y / 2) + 1, selectedCards, 2);
        }

        if(gameState.status == GameStatus::YourDef) {
            printCardsWithSpace(-1, (screenSize.Y / 2) - 8, gameState.attack_cards, 2);
            printCardsWithSpace(-1, (screenSize.Y / 2) + 1, selectedCards, 2);
        }

        if(gameState.status == GameStatus::OpponentDef) {
            printTextAlignCenter(screenSize.X / 2,(screenSize.Y / 2) - 2, L"Opponent Move", COLOR_DEFAULT_FG, COLOR_DEFAULT_BG);
            printCardsWithSpace(-1, (screenSize.Y / 2) + 1, gameState.attack_cards, 2);
        }

        if(std::chrono::steady_clock::now() < errorTextDuration) {
            printText(2, screenSize.Y - 2, errorText.c_str(), COLOR_DEFAULT_FG, COLOR_DEFAULT_BG);
        }


    }

    // Key Handlers

    void OnPressBackspace() {
        std::unique_lock<std::shared_mutex> sLock(stateMutex);

        if(gameState.status == GameStatus::YourDef) {
            auto result = gameClient.passDef();
            selectedCards = {};

            if(result == 0) {
                gameState.status = GameStatus::WaitUpdate;
            }
        }
    }
    void OnPressEnter() {
        std::unique_lock<std::shared_mutex> sLock(stateMutex);
        if(gameState.status == GameStatus::YourMove) {
            if(selectedCards.empty()) {
                printError(L"You need to choose the cards", std::chrono::seconds(2));
                return;
            }
            auto firstValue = selectedCards.front().value;

            if(std::any_of(selectedCards.begin(), selectedCards.end(),
                            [&](Card &card) {
                                return card.value != firstValue;
                            }
                            )) {
                printError(L"The cards must be of only one suit", std::chrono::seconds(2));
                return;
            }

            auto result = gameClient.finishMove(selectedCards);
            selectedCards = {};

            if(result == 0) {
                gameState.status = GameStatus::WaitUpdate;
            }
        }
        if(gameState.status == GameStatus::YourDef) {
            if(selectedCards.empty()) {
                printError(L"You need to choose the cards", std::chrono::seconds(2));
                return;
            }

            auto size = selectedCards.size();

            if(size != gameState.attack_cards.size()) {
                printError(L"You have to beat off all the opponent's cards", std::chrono::seconds(2));
                return;
            }

            for (int i = 0; i < size; ++i) {
                if(!Card::canUseCard(selectedCards[i], gameState.attack_cards[i], gameState.trump.suit)) {
                    printError(L"You can't make such a move", std::chrono::seconds(2));
                    return;
                }
            }

            auto result = gameClient.finishDef(selectedCards);
            selectedCards = {};

            if(result == 0) {
                gameState.status = GameStatus::WaitUpdate;
            }
        }
    }
    void OnPressEsc() {
        std::unique_lock<std::shared_mutex> sLock(stateMutex);
        isExit = true;
    }
    void OnPressSpace() {
        std::unique_lock<std::shared_mutex> sLock(stateMutex);

        if(gameState.status == GameStatus::YourDef || gameState.status == GameStatus::YourMove) {
            if(cardCursor < 0 || cardCursor >= gameState.my_cards.size()) return;
            auto card = gameState.my_cards.at(cardCursor);

            if(selectedCards.empty()) {
                selectedCards.emplace_back(Card(card.suit, card.value));
                return;
            }

            auto iterator = std::find_if(selectedCards.begin(), selectedCards.end(), [&](Card &el) {
                return el.suit == card.suit && el.value == card.value;
            });

            if(iterator != selectedCards.end()) {
                selectedCards.erase(iterator);
            }
            else {
                selectedCards.emplace_back(Card(card.suit, card.value));
            }
        }
    }
    void OnPressUp() {
        std::unique_lock<std::shared_mutex> sLock(stateMutex);
    }
    void OnPressLeft() {
        std::unique_lock<std::shared_mutex> sLock(stateMutex);

        if(gameState.status == GameStatus::YourDef || gameState.status == GameStatus::YourMove) {
            int cursorNewPos = cardCursor - 1;

            if(cursorNewPos < 0) cursorNewPos = static_cast<int>(gameState.my_cards.size() - 1);

            cardCursor = cursorNewPos;
        }

    }
    void OnPressRight() {
        std::unique_lock<std::shared_mutex> sLock(stateMutex);

        if(gameState.status == GameStatus::YourDef || gameState.status == GameStatus::YourMove) {
            auto cursorNewPos = cardCursor + 1;

            if(cursorNewPos >= gameState.my_cards.size()) cursorNewPos = 0;

            cardCursor = cursorNewPos;
        }
    }
    void OnPressDown() {
        std::unique_lock<std::shared_mutex> sLock(stateMutex);
    }

   public:
    BuraConsole(std::string ip, std::string port) : ip(std::move(ip)), port(std::move(port))  {
        setup();
    }

    void launch(const std::wstring &nick){
        nickname = nick;

        std::thread renderThread(&BuraConsole::render, this);
        std::thread inputThread(&BuraConsole::input, this);
        std::thread updateThread(&BuraConsole::update, this);

        inputThread.join();
        renderThread.join();
        updateThread.join();

        // Clear console

        DWORD written;
        FillConsoleOutputCharacter(consoleHandle, ' ', screenBufferSize, COORD(), &written);
        SetConsoleCursorPosition(consoleHandle, COORD());
        std::wcout << L"\x1b[0m";
    }
};
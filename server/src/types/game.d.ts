import { Socket } from "net";

declare const enum GameStatus {
	None = -1,
	Idle = 0,
	OpponentMove = 1,
	YourMove = 2,
	OpponentDef = 3,
	YourDef = 4,
	Finish = 5,
	Win = 6,
	Lose = 7,
	MoveLog = 8,
	Move = 100,
	Def = 101
}

declare const enum CardSuit {
	None = -1,
	Hearts = 0,    // Черви
	Diamonds = 1,  // Буби
	Spades = 2,    // Пики
	Clubs = 3      // Крести
}

declare const enum CardValue {
	None = -1,
	/** Туз */
	Ace = 0,
	/** Король */
	King = 1,
	/** Дама */
	Queen = 2,
	/** Валет */
	Jack = 3,
	Ten = 4,
	Nine = 5,
	Eight = 6,
	Seven = 7,
	Six = 8
}

declare type CardType = number;

declare interface ICard {
	suit: CardSuit,
	value: CardValue,
}

declare interface IPlayerStore {
	nickname: string
	cards: ICard[],
	lobby: string
}

declare interface IPlayer {
	id: string,
	store: Partial<IPlayerStore>
}

declare interface ILobby {
	id: string,
	cards: ICard[]
	trump: ICard;
	fall: ICard[]
	attack: ICard[]
	playerMove: string,
	winner: string,
	status: GameStatus,
	players: string[],
	attackHist: ICard[]
	defendHist: ICard[]
}
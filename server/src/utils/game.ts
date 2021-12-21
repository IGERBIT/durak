import { CardSuit, CardType, CardValue, ICard } from "types/game";

export function card2cardtype(card: ICard): CardType {
	return (((card?.suit ?? CardSuit.None) & 0xFF) << 8) | ((card?.value ?? CardValue.None) & 0xFF);
}

export function cardtype2card(cardType: CardType): ICard {
	return {
		suit: (cardType >> 8) & 0xFF,
		value: cardType & 0xFF
	}
}

export function generateCards(): ICard[] {
	let cards: ICard[] = []

	for (let suit = 0; suit < 4; suit++) {
		for (let value = 0; value < 9; value++) {
			cards.push({ suit, value });
		}		
	}

	return cards;
}

export function shuffleArray<T>(arr: T[], iter: number = 5): T[] {
	let tmp = arr;
	while (iter > 0) {
		tmp = tmp.sort(() => 0.5 - Math.random())
		iter--;
	}

	return tmp;
}

export function cardEquals(a: ICard, b: ICard): boolean {
	return a.suit == b.suit && a.value == b.value;
}

export function canCardUse(target: ICard, enemy: ICard, trump: CardSuit): boolean {
	if (target.suit == trump && enemy.suit != trump) return true;
	if (target.suit != enemy.suit) return false;
	return target.value < enemy.value;	
}
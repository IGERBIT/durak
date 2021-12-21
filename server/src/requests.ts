import { getPlayer, regHandler } from "./durak";
import { CardSuit, CardValue, GameStatus, ICard, ILobby, IPlayer } from "./types/game";
import { canCardUse, card2cardtype, cardEquals, cardtype2card, generateCards, shuffleArray } from "./utils/game";

const lobbys: Record<string, ILobby> = {};
const MIN_CARD_IN_HANDS = 6;

let lastLobby: ILobby = null;

function createLobby(): ILobby {
	let lobby: ILobby = {
		id: Math.random().toString(),
		status: GameStatus.Idle,
		cards: shuffleArray(generateCards(), 10),
		fall: [],
		winner: null,
		trump: null,
		players: [],
		attack: [],
		playerMove: null
	}

	lobby.trump = lobby.cards[lobby.cards.length - 1];
	return lobby;
}

function getLobby(id: string): ILobby {
	return lobbys[id];
}

function giveCards(id: string) {
	let lobby = getLobby(id);
	if (!lobby) return;
	let stillNeed = true;

	while (stillNeed && lobby.cards.length > 0) {
		stillNeed = false;

		for (const player of lobby.players.map(getPlayer)) {
			if (!player) continue;
			if (lobby.cards.length < 1) {
				stillNeed = false;
				break;
			}
			if (player.store.cards.length < (MIN_CARD_IN_HANDS - 1)) stillNeed = true;

			if (player.store.cards.length < MIN_CARD_IN_HANDS) {
				player.store.cards.push(lobby.cards.shift());
			}
		}
	}
}

function startGame(id: string) {
	let lobby = getLobby(id);
	if (!lobby) return;
	giveCards(lobby.id);
	sortCards(lobby.id);
	lobby.status = GameStatus.Move;
	lobby.playerMove = lobby.players[0];
}


regHandler(2, ({ player, payload }) => {
	let nickname = payload.toString("utf8", 0);
	player.store.nickname = nickname;
	player.store.cards = [];


	if (!lastLobby) {
		lastLobby = createLobby();
		lobbys[lastLobby.id] = lastLobby;
	}

	player.store.lobby = lastLobby.id;
	lastLobby.players.push(player.id);

	if (lastLobby.players.length == 2) {
		startGame(lastLobby.id);
		lastLobby = null;
		
	}

	return 0;
	
})


function getOpponent(player: IPlayer) {
	return getLobby(player?.store?.lobby)?.players?.find(x => x != player.id)
}

regHandler(3, ({ player, payload }) => {
	let lobby = getLobby(player?.store?.lobby);
	
	if (!lobby) return 1;
	let opponent = getPlayer(getOpponent(player));

	let status = lobby.status;

	if (status == GameStatus.Move) status = player.id == lobby.playerMove ? GameStatus.YourMove : GameStatus.OpponentMove;
	if (status == GameStatus.Def) status = player.id == lobby.playerMove ? GameStatus.YourDef : GameStatus.OpponentDef;

	if (status == GameStatus.Finish) status = player.id == lobby.winner ? GameStatus.Win : GameStatus.Lose;

	let offset = 0;

	let size = 1 + 2 + 1 + 1;

	let myCards = player?.store?.cards ?? [];
	let opponentCards = opponent?.store?.cards ?? [];


	size += 4 + (2 * myCards.length);
	size += 4 + (2 * opponentCards.length);
	size += 4 + (2 * lobby.attack.length);
	size += 4;

	let buffer = Buffer.alloc(size);

	offset = buffer.writeInt8(status, offset);
	offset = buffer.writeUInt16LE(card2cardtype(lobby.trump) & 0xFFFF, offset);
	offset = buffer.writeUInt8(lobby.cards.length, offset);
	offset = buffer.writeUInt8(lobby.fall.length, offset);

	offset = buffer.writeUInt32LE(myCards.length, offset);
	for (let i = 0; i < myCards.length; i++) { offset = buffer.writeUInt16LE(card2cardtype(myCards[i]), offset); }

	offset = buffer.writeUInt32LE(opponentCards.length, offset);
	for (let i = 0; i < opponentCards.length; i++) { offset = buffer.writeUInt16LE(card2cardtype({ suit: CardSuit.None, value: CardValue.None }), offset); }

	offset = buffer.writeUInt32LE(lobby.attack.length, offset);
	for (let i = 0; i < lobby.attack.length; i++) { offset = buffer.writeUInt16LE(card2cardtype(lobby.attack[i]), offset); }

	offset = buffer.writeUInt32LE(0, offset);

	return { data: buffer, code: 0 };
})

function sortCards(id: string) {
	let lobby = getLobby(id);
	if (!lobby) return;
	for (const player of lobby.players.map(getPlayer)) {
		if (!player) continue;
		player.store.cards = player.store.cards.sort((a, b) => {
			if (a.suit != lobby.trump.suit && b.suit == lobby.trump.suit) return -1;
			return b.value - a.value;
		})
	}
}

regHandler(4, ({ player, payload }) => {
	let lobby = getLobby(player?.store?.lobby);

	if (!lobby) return 1;
	if (lobby.status != GameStatus.Move) return 2
	if (lobby.playerMove != player.id) return 3
	let opponent = getPlayer(getOpponent(player));
	if (!opponent) return 5;
	let moveCards: ICard[] = []

	let count = payload.readUInt32LE(0);

	for (let i = 0; i < count; i++) {
		moveCards.push(cardtype2card(payload.readUInt16LE(4 + (i * 2))))
	}

	if (moveCards.length < 1) return 4;

	let firstVal = moveCards[0].value;

	if (moveCards.some(card => card.value != firstVal)) return 1;
	if (moveCards.some(card => player.store.cards.some(mcard => cardEquals(mcard, card)) == false)) return 1;

	lobby.attack = moveCards;
	player.store.cards = player.store.cards.filter(x => moveCards.every(c => !cardEquals(c, x)));

	lobby.playerMove = opponent.id;
	lobby.status = GameStatus.Def;

	sortCards(lobby.id);

	return 0;
})

regHandler(5, ({ player, payload }) => {
	let lobby = getLobby(player?.store?.lobby);

	if (!lobby) return 1;
	if (lobby.status != GameStatus.Def) return 2
	if (lobby.playerMove != player.id) return 3
	let opponent = getPlayer(getOpponent(player));
	if (!opponent) return 5;
	let moveCards: ICard[] = []

	console.log(player)
	console.log(lobby)

	let isPass = payload.readUInt8(0) == 0;

	if (isPass) {
		player.store.cards = [...player.store.cards, ...lobby.attack];
		lobby.attack = [];
		lobby.playerMove = opponent.id;
	}
	else {
		let count = payload.readUInt32LE(1);

		for (let i = 0; i < count; i++) {
			moveCards.push(cardtype2card(payload.readInt16LE(5 + (i * 2))))
		}

		console.log(moveCards)

		if (moveCards.length != lobby.attack.length) return 1;

		if (moveCards.some(card => player.store.cards.some(mcard => cardEquals(mcard, card)) == false)) return 6;

		for (let i = 0; i < moveCards.length; i++) {
			const defCard = moveCards[i];
			const atcCard = lobby.attack[i];

			if (!canCardUse(defCard, atcCard, lobby.trump.suit)) return 7;
		}

		player.store.cards = player.store.cards.filter(x => moveCards.every(c => !cardEquals(c, x)));
		lobby.fall.push(...lobby.attack);
		lobby.fall.push(...moveCards);

		lobby.playerMove = player.id;
	}

	lobby.status = GameStatus.Move;
	giveCards(lobby.id);
	sortCards(lobby.id);

	for (const player of lobby.players.map(getPlayer)) {
		if (player?.store?.cards?.length < 1) {
			lobby.winner = player.id;
			lobby.status = GameStatus.Finish;
			break;
		}
	}

	return 0;
})


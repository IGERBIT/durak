import { IPlayer, IPlayerStore } from "./types/game";

type HandlerReturnType = { data?: Buffer, code?: number } | Buffer | number | undefined | void
type CodeHandler = (param: { player: IPlayer, payload: Buffer }) => MaybePromise<HandlerReturnType>; 

const players: Record<string, IPlayer> = {}

const handlers: Record<number, CodeHandler> = {}

export async function RequestHandler(buff: Buffer): Promise<Buffer> {

	//console.log(buff);

	let code = buff.readUInt16LE(0);
	let hash = buff.slice(2, 10).toString();
	let payload = buff.slice(10);

	let retcode = 1;
	let retbuffer = Buffer.alloc(0);

	if (hash) {
		if (!players[hash]) {
			players[hash] = {
				id: hash,
				store: {}
			}
		}

		let player = players[hash];

		let handler = handlers[code];

		let result = await Promise.resolve(handler?.({ player, payload }));

		if (typeof result == "object" && result != null) {
			if (Buffer.isBuffer(result)) retbuffer = result;
			else {
				if (typeof result.code == "number") retcode = result.code;
				if (Buffer.isBuffer(result.data)) retbuffer = result.data;
			}
		}
		else if (typeof result == "number") retcode = result;
	}

	let codeBuff = Buffer.alloc(2);
	codeBuff.writeUInt16LE(retcode);
	let rez = Buffer.concat([codeBuff, retbuffer]);
	//console.log('res', rez );

	return rez;

}

export function getPlayer(id: string) {
	return players[id];
}

export function regHandler(code: number, handler: CodeHandler) {
	handlers[code] = handler;
}
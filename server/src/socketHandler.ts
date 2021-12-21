/// <reference path="./types/common.d.ts" />

import { Socket } from "net"
import { IPlayerStore } from "types/game";

interface SocketInfo {
	socket: Socket,
	metaBuff: Buffer,
	metaSize: number,
	payloadCode: number,
	payloadLength: number,
	payload: Buffer | null,
	store: Record<string,any>
}


const META_SIZE_BYTE = 2 + 4;

type HandlerReturnType = { data?: Buffer, code?: number } | Buffer | number | undefined | void

type CodeHandler = (param: { socket: Socket, store: IPlayerStore, code: number, payload: Buffer }) => MaybePromise<HandlerReturnType>; 

function sendReponse(socket: Socket, payload: Buffer, code = 0) {
	if (!socket.writable) return;
	let l = payload.length;
	let meta = Buffer.alloc(META_SIZE_BYTE);
	meta.writeUInt16LE(code);
	meta.writeUInt32LE(l, 2);

	socket.write(meta);
	socket.write(payload || Buffer.alloc(0));
}

async function parseData(socketInfo: SocketInfo, data: Buffer, methods: Record<number, CodeHandler> ) {
	let cursor = 0;
	const dataLength = data.length;
	//console.log(`recive data: `, data);
	while (cursor < dataLength) {
		if (socketInfo.metaSize < META_SIZE_BYTE) {
			let bytesReaded = data.copy(
				socketInfo.metaBuff,
				socketInfo.metaSize,
				cursor,
				cursor + (META_SIZE_BYTE - socketInfo.metaSize)
			)
			socketInfo.metaSize += bytesReaded;
			cursor += bytesReaded;

			if (socketInfo.metaSize >= META_SIZE_BYTE) {
				socketInfo.payloadCode = socketInfo.metaBuff.readUInt16LE(0);
				let length = socketInfo.metaBuff.readUInt32LE(2);
				socketInfo.payloadLength = 0;
				socketInfo.payload = Buffer.alloc(length);
			}
		}

		if (socketInfo.payload && socketInfo.payload.length > socketInfo.payloadLength) {
			let bytesReaded = data.copy(
				socketInfo.payload,
				socketInfo.payloadLength,
				cursor,
				cursor + (socketInfo.payload.length - socketInfo.payloadLength)
			);
			socketInfo.payloadLength += bytesReaded;
			cursor += bytesReaded;

			
		}

		if (socketInfo.payload) {
			if (socketInfo.payload.length >= socketInfo.payloadLength) {


				let handler = methods[socketInfo.payloadCode];

				//console.log(`run code ${socketInfo.payloadCode}`)

				let result: HandlerReturnType = await Promise.resolve(handler?.({ socket: socketInfo.socket, payload: socketInfo.payload, store: socketInfo.store as IPlayerStore, code: socketInfo.payloadCode }))


				let rbuffer: Buffer;
				let rcode: number = handler ? 0 : 1;

				if (typeof result == "object" && result != null) {
					if (Buffer.isBuffer(result)) rbuffer = result;
					else {
						if (typeof result.code == "number") rcode = result.code;
						if (Buffer.isBuffer(result.data)) rbuffer = result.data;
					}
				}
				else if (typeof result == "number") rcode = result;

				console.log(` result ${socketInfo.payloadCode}: `, rcode);

				sendReponse(socketInfo.socket, rbuffer || Buffer.alloc(0), rcode);

				socketInfo.payload = null;
				socketInfo.payloadCode = -1;
				socketInfo.payloadLength = 0
				socketInfo.metaSize = 0;
			}
		}
	}
}

const net = require("net");

let metaMap = new WeakMap();

const META_SIZE_BYTE = 2 + 8;

const server = net.createServer(function (socket) {
	metaMap.set(socket, {
		metaBuff: Buffer.alloc(META_SIZE_BYTE),
		metaSize: 0,
		dataType: -1,
		dataSize: 0,
		data: null
	});

	socket.on("data", (data) => {
		console.log(`data: `, data);
		let meta = metaMap.get(socket);

		let cursor = 0;
		
		if (meta.metaSize < META_SIZE_BYTE) {
			let copied = data.copy(
				meta.metaBuff,
				meta.metaSize,
				cursor,
				cursor + META_SIZE_BYTE - meta.metaSize
			);
			meta.metaSize += copied;
			cursor += copied;
			if (meta.metaSize >= META_SIZE_BYTE) {
				console.log(`meta loaded`)
				meta.dataType = meta.metaBuff.readUInt16LE(0);
				let length = Number(meta.metaBuff.readBigUInt64LE(2))
				meta.dataSize = 0;
				meta.data = Buffer.alloc(length);
			}
		}
		
		if (meta.data && meta.data.length > meta.dataSize) {
			let copied = data.copy(
				meta.data,
				meta.dataSize,
				cursor,
				cursor + meta.data.length - meta.dataSize
			);
			meta.dataSize += copied;
			cursor += 0;

			if (meta.data.length >= meta.dataSize) {
				console.log(`pl loaded`);
				handler(meta.dataType, meta.data, socket);
				meta.data = null;
				meta.metaSize = 0;

			}
		}
	})

	socket.on('error', err => {
		console.log("sock err");
	})
})

function sendReponse(socket, payload, error = 0) {
	let l = payload.length;
	let meta = Buffer.alloc(META_SIZE_BYTE);
	meta.writeUInt16LE(error);
	meta.writeBigUInt64LE(BigInt(l), 2);

	socket.write(meta);
	console.log("send", meta, payload);
	socket.write(payload);
}

function handler(code, data, socket) {
	if (code == 1) {
		console.log("CODE 1");
		sendReponse(socket, data);
	}

	if (code == 2) {
		console.log("CODE 2");
		sendReponse(
			socket,
			Buffer.from(data.toString().split("").reverse().join(""))
		);
	}

}

server.on("error", () => {
	console.log('err')
})

server.listen(2021);

console.log(`started!`)
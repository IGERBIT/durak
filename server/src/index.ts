import { RequestHandler } from "./durak";
import { createServer } from "http";
import './requests'

const server = createServer(function (req, res) {
	let buffers: Buffer[] = []

	req.on("data", (buf) => {
		buffers.push(buf);
	})

	req.on("end", async () => {
		let full = Buffer.concat(buffers);

		let result = await RequestHandler(full);

		res.writeHead(200).end(result);

	})
})

server.on("error", (err) => {
	console.log('err', err);
})

server.on("clientError", (err) => {
	console.log('cerr', err);
})

server.listen(2021);

console.log(`started!`)
const net = require("net");



const server = net.createServer(function (socket) {
	socket.on("data", (data) => {
		console.log(`recive: `, data);
		socket.write(`ask: ` + data);
	})
})

server.listen(2021);

console.log(`started!`)
const net = require("net");



const server = net.createServer(function (socket) {
	socket.write('Hello')

	socket.on("data", (data) => {
		console.log(`recive: `, data);
		socket.write(data);
	})
})

server.listen(2021);

console.log(`started!`)
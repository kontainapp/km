var port = 8080;
var http = require('http');
var server = http.createServer(function (request, response) {
   console.log('Got a request ', request);
   response.write('Hello');
   response.end();
});
console.log('listening on port ', port)
server.listen(port);

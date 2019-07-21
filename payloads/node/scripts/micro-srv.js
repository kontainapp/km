var port = 8080;
var http = require('http');
var server = http.createServer(function(request, response) {
   if (request.method === "POST") {
      console.log('Got a POST, exiting');
      process.exit(0);
   }
   response.write('Hello from Node.js ')
   response.write(process.version);
   response.write('\n')
   response.end();
});
console.log('listening on port ', port)
server.listen(port);

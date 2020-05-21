const http = require('http');
const server = http.createServer(function (request, response) {
    console.log('Hello world received a request.');

    const target = process.env.TARGET || 'World';
    response.write(`Hello ${target}!`);
    response.write('\n')
    response.end();
});

const port = process.env.PORT || 8080;
console.log('Hello world listening on port', port);
server.listen(port);

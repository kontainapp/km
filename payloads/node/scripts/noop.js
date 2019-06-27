console.log('started');
process.on('exit', function (code) {
   return console.log(`About to exit with code ${code}`);
});
process.exit(22);

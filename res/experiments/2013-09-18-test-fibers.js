var Fiber = require('/home/ben/Code/earthd/node_modules/fibers');

var fn = Fiber(function() {
    console.log('async work here...');
    Fiber.yield();
    console.log('still working...');
    Fiber.yield();
    console.log('just a little bit more...');
    Fiber.yield();
//	try {
    throw new Error('oh crap!');
//	} catch(e) {}
});

//try {
    while (true) {
        fn.run();
    }
//} catch(e) {
//    console.log('safely caught that error!');
//    console.log(e.stack);
//}
console.log('done!');


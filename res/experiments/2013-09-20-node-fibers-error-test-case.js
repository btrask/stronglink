var err = new Error("Something happened");
err.info = "Additional info";
var nonerr = Object.create(err);
console.log(err);
console.log(nonerr);

var util = require("util");
console.log(util.isError(err));
console.log(util.isError(nonerr));


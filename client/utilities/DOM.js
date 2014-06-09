/* Copyright Ben Trask and other contributors. All rights reserved.
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE. */
var DOM = {
    clone: function(id, childByID) {
        var element = document.getElementById(id).cloneNode(true);
        //element.id = "";
        element.removeAttribute("id");
        if (childByID)
            (function findIDsInElement(elem) {
                var children = elem.childNodes, length = children.length, i = 0, dataID;
                if (elem.getAttribute)
                    dataID = elem.getAttribute("data-id");
                if (dataID)
                    childByID[dataID] = elem;
                for (; i < length; ++i)
                    findIDsInElement(children[i]);
            })(element);
        return element;
    },
    classify: function(elem, className, add) {
        if (add || undefined === add) {
            elem.className += " "+className;
            return;
        }
        var classes = (elem.className || "").split(" "), 
        changed = (className || "").split(" "), 
        length = changed.length, i = 0, index;
        for (; i < length; ++i) {
            index = classes.indexOf(changed[i]);
            if (index >= 0)
                classes.splice(index, 1);
        }
        elem.className = classes.join(" ");
    },
    fill: function(elem, child1, child2, etc) {
        var i = 1, type;
        while (elem.hasChildNodes())
            elem.removeChild(elem.firstChild);
        for (; i < arguments.length; ++i)
            if (arguments[i]) {
                type = typeof arguments[i];
                if ("string" === type || "number" === type) {
                    elem.appendChild(document.createTextNode(arguments[i]));
                } else {
                    elem.appendChild(arguments[i]);
                }
            }
    },
    remove: function(elem) {
        if (elem.parentNode)
            elem.parentNode.removeChild(elem);
    },
    addListener: function(elem, name, func) {
        if(elem.addEventListener) elem.addEventListener(name, func);
        else elem.attachEvent("on"+name, func);
    },
    removeListener: function(elem, name, func) {
        if(elem.removeEventListener) elem.removeEventListener(name, func);
        else elem.detachEvent("on"+name, func);
    },
    preventDefault: function(event) {
        if(event.preventDefault) event.preventDefault();
        event.returnValue = false;
    }
};

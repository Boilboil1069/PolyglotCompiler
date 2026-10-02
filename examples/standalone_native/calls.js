/**
 * @param {number} x
 * @returns {number}
 */
function twice(x) {
return x * 2;
}

/**
 * @param {number} x
 * @param {number} y
 * @returns {number}
 */
function combine(x, y) {
return twice(x) + twice(y);
}

/**
 * @returns {boolean}
 */
function main() {
return combine(13, 8) === 42;
}

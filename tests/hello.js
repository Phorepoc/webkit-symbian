// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause

// hello.js -- smoke test for the JSC port (host CLoop build and Phorepoc jsc.exe)
print("hello from jsc");
var s = 0; for (var i = 0; i < 100000; i++) s += i % 7; print("sum", s);
print([3, 1, 2].sort().join(","), JSON.stringify({ a: [1, 2, { b: "x" }] }), (0.1 + 0.2).toFixed(3));
print("héllo".toUpperCase(), /(\d+)-(\d+)/.exec("tel 12-34")[2], new Date(0).toISOString());
print(Math.log2(8), Math.hypot(3, 4), Math.cbrt(27), Math.fround(5.5), 2 ** 0.5);
print(new Intl.NumberFormat("en-US").format(1234567.891), "b".localeCompare("a"), new Intl.DateTimeFormat("en-US", { timeZone: "UTC" }).format(new Date(0)));
function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); } print("fib(20)", fib(20));
class A { #p = 1; get p() { return this.#p; } } print(new A().p, [..."abc"].map(c => c.charCodeAt(0)), 123456789n * 987654321n);
print(JSON.stringify(Object.entries(new Map([[1, 2]]))), typeof WebAssembly, "done");

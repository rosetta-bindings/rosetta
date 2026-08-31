// Copyright (c) fmaerten@gmail.com
// License: MIT

// Exercise the auto-generated bindings produced by auto_emscripten.cpp.
//
// Run after `emcmake cmake -B build && cmake --build build`.

'use strict';

const createModule = require('./reflected_person.js');

createModule().then(m => {
    const p = new m.Person();
    console.log('Person() created');

    console.log('\n-- valid writes --');
    p.name = 'Alice';
    p.age = 42;
    console.log(`  name=${p.name}, age=${p.age}`);

    console.log('\n-- range violation (age=999) --');
    try {
        p.age = 999;
    } catch (e) {
        console.log(`  caught: ${e}`);
    }

    console.log('\n-- readonly violation (id=...) --');
    try {
        p.id = 'p-999';
    } catch (e) {
        console.log(`  caught: ${e}`);
    }

    console.log('\n-- method call --');
    console.log(`  greet('Hello') -> ${p.greet('Hello')}`);

    console.log('\n-- clear() --');
    p.clear();
    console.log(`  after clear: name="${p.name}", age=${p.age}`);

    // embind requires explicit cleanup — JS GC doesn't track WASM heap.
    p.delete();
}).catch(err => {
    console.error('module load failed:', err);
    process.exit(1);
});

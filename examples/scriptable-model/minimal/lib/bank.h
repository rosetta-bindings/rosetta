// Copyright (c) fmaerten@gmail.com
// License: MIT
//
// A library that has never heard of rosetta: no annotations, no rosetta
// include, nothing to opt in. This is the whole of what a project must supply
// to get the reflection API scriptable over its own types.
//
// Stock C++20 on purpose. A header carrying INLINE annotations
// (`[[ = rosetta::doc{...} ]]`) pulls <experimental/meta> into the generated
// auto_dynamic.cpp and would need the C++26 toolchain to build the target —
// see ../../README.md. Put annotations out of line (a .ann.json side-car, as
// examples/dynamic does) if you want them and a stock-compiler build.

#pragma once

#include <string>

namespace bank {

    struct Account {
        std::string owner   = "unnamed";
        double      balance = 0.0;
        bool        frozen  = false;

        void deposit(double amount) {
            if (!frozen) {
                balance += amount;
            }
        }

        bool withdraw(double amount) {
            if (frozen || amount > balance) {
                return false;
            }
            balance -= amount;
            return true;
        }

        std::string describe() const { return owner + ": " + std::to_string(balance); }

        static Account open(const std::string &owner, double initial) {
            Account a;
            a.owner   = owner;
            a.balance = initial;
            return a;
        }
    };

} // namespace bank

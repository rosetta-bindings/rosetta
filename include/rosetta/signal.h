// Copyright (c) fmaerten@gmail.com
// License: MIT

// Signals: the SIGNAL TRAIT.
//
// Event-driven libraries publish their events as public members of a signal
// type — `veyra::Signal<bool> toggled;`, `boost::signals2::signal<void(int)>`,
// sigc++'s `sigc::signal<void(int)>`. Such a member is not a value: it is not
// copyable, and what a script wants from it is not its contents but a way to
// connect a function. Every backend used to skip it like any other
// non-copyable field, so each signal needed a hand-written
// `onToggled(widget, callback)` extension function.
//
// A signal trait says how the library's signal template connects a callable,
// and the backends that support it (wasm, node) bind every signal member as a
// connect METHOD taking a script function:
//
//   button.onClicked(() => console.log('clicked'));          // JS
//
// The manifest declares the template ("signals"), and rosetta_gen turns each
// entry into a partial specialization of the trait below:
//
//   template <typename... A>
//   struct rosetta::signal_traits<veyra::Signal<A...>> {
//       static constexpr bool enabled = true;
//       using function   = std::function<void(A...)>;  // what a script function becomes
//       using connection = veyra::Connection;          // what connecting returns (or void)
//       static constexpr const char *connect = "$signal.connect(&$self, $fn)";
//       static constexpr const char *method  = "on{Field}";
//   };
//
// `connect` is a C++ expression spliced into the emitted code, with
//   $signal — the signal member of the object (`self.clicked`)
//   $self   — the object itself (`self`), for libraries that take a context
//             object, as Qt does and veyra does: the connection is dropped when
//             the object dies, so a script callback never outlives its widget
//   $fn     — the script function, already wrapped into `function`
// `method` names the bound connect method: {Field} is the member name with its
// first letter capitalized, {field} the member name as written.
//
// The walk records each such member as a GenSignal (GenClass::signals), never
// as a field, so a backend that does not opt in keeps ignoring it.
//
// Write the specialization yourself for programmatic use (no manifest).

#pragma once

#include <functional>

namespace rosetta {

    // Primary: not a signal.
    template <class S> struct signal_traits {
        static constexpr bool enabled = false;
    };

} // namespace rosetta

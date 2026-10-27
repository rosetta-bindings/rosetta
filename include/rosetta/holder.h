// Copyright (c) fmaerten@gmail.com
// License: MIT

// Intrusive reference-counted classes: the HOLDER POLICY.
//
// Some libraries never let a caller destroy an object. Every VTK class derives
// from vtkObjectBase, whose destructor is PROTECTED: an object is made by
// `T::New()` (one reference), shared by `Register()`, released by
// `UnRegister()` / `Delete()`, and freed when the count reaches zero. The
// library's own smart pointer — vtkSmartPointer<T> — takes a reference when it
// is constructed from a raw pointer and drops it when destroyed. The pipeline
// API hands out BORROWED raw pointers (`vtkAlgorithm::GetOutput()`) that the
// caller must reference before keeping.
//
// Every runtime backend destroys what it wraps, so such a class used to be
// skipped outright (GenClass::is_destructible). A holder policy says how the
// library manages lifetime instead, and the backends that support it bind the
// class through the library's own holder:
//
//   python (pybind11) — PYBIND11_DECLARE_HOLDER_TYPE(T, Holder<T>, true): an
//                       intrusive holder, so EVERY T* pybind wraps — a factory's
//                       result or a borrowed GetOutput() — takes a reference,
//                       and the Python object's death drops it.
//   wasm (embind)     — smart_ptr_trait<Holder<T>> with INTRUSIVE sharing; a
//                       pointer return is handed out as Holder<T>, so every JS
//                       handle owns one reference and .delete() is safe.
//   julia (jlcxx)     — every pointer crossing into Julia is retained and boxed
//                       as the most-derived bound class, with a finalizer that
//                       releases it: jlcxx's own "allocated" ownership model.
//
// The manifest declares the policy ("holders"), and rosetta_gen turns each
// entry into a specialization of the trait below, selected by derivation from
// the policy's base class:
//
//   template <class T>
//   struct rosetta::holder_policy<T, std::enable_if_t<std::is_base_of_v<vtkObjectBase, T>>> {
//       static constexpr bool        enabled   = true;
//       static constexpr const char *holder    = "vtkSmartPointer";
//       static constexpr const char *header    = "vtkSmartPointer.h";
//       static constexpr const char *get       = "GetPointer";
//       static constexpr const char *retain    = "Register(nullptr)";
//       static constexpr const char *release   = "UnRegister(nullptr)";
//       static constexpr const char *factory   = "New";
//       static constexpr const char *owned     = "New*;NewInstance";
//       static constexpr bool        creatable = ...; // T::New() -> T*, T not abstract
//   };
//
// `creatable` is the part only the compiler can answer: a class that declares
// no New() of its own inherits its base's, which returns the BASE type — so the
// check is that `T::New()` returns exactly `T*`, and that T is not abstract.
//
// Write the specialization yourself for programmatic use (no manifest).

#pragma once

#include <concepts> // std::same_as, in the emitted `creatable` checks
#include <type_traits>

namespace rosetta {

    // Primary: no policy — the class keeps the ordinary value / unique
    // ownership every backend assumes.
    template <class T, class = void> struct holder_policy {
        static constexpr bool enabled = false;
    };

} // namespace rosetta

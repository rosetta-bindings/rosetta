// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for SIGNAL members (rosetta/signal.h).
//
// A public member whose type has rosetta::signal_traits is not a field: the
// walk records it as a GenSignal whose `connect` method takes a script
// function, and the wasm / node / python backends bind it as `on<Name>(fn)` through a
// connect thunk they emit (rosetta_sig::<class>__<member>). The TypeScript
// declarations follow the two runtimes.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

namespace sg {
    // A minimal Qt-like signal: connect(context, fn) returns a Connection.
    struct Connection {
        bool connected() const { return true; }
        void disconnect() {}
    };

    template <typename... A> struct Signal {
        Signal()                          = default;
        Signal(const Signal &)            = delete;
        Signal &operator=(const Signal &) = delete;
        template <typename C, typename F> Connection connect(C *, F &&f) {
            slots.emplace_back(std::forward<F>(f));
            return {};
        }
        void emit(A... a) {
            for (auto &s : slots) {
                s(a...);
            }
        }
        std::vector<std::function<void(A...)>> slots;
    };

    struct Payload {
        Payload(const Payload &) = delete; // not bound, not copyable
    };

    struct Widget {
        double                   value = 0;
        Signal<>                 clicked;
        Signal<bool>             toggled;
        Signal<std::string, int> renamed;
        Signal<Widget *>         picked;  // a pointer to a bound class
        Signal<const Payload &>  carried; // an unbound class: never bindable
        Signal<double>           moved;

        int onMoved() const { return 1; } // takes the name `moved` would bind as
    };
} // namespace sg

template <> struct rosetta::binding_info<sg::Widget> {
    static constexpr const char *header = "sg.h";
};
template <> struct rosetta::binding_info<sg::Connection> {
    static constexpr const char *header = "sg.h";
};

template <typename... A> struct rosetta::signal_traits<sg::Signal<A...>> {
    static constexpr bool enabled = true;
    using function                = std::function<void(A...)>;
    using connection              = sg::Connection;
    static constexpr const char *connect = "$signal.connect(&$self, $fn)";
    static constexpr const char *method  = "on{Field}";
};

namespace {

    rosetta::GenContext ctx() {
        return rosetta::gen_detail::make_context<sg::Widget, sg::Connection>("sgtest");
    }

    const rosetta::GenClass &widget(const rosetta::GenContext &c) {
        for (const auto &k : c.classes) {
            if (k.name == "Widget") {
                return k;
            }
        }
        throw std::runtime_error("Widget not bound");
    }

    const rosetta::GenSignal *signal(const rosetta::GenClass &k, const std::string &field) {
        for (const auto &s : k.signals) {
            if (s.field == field) {
                return &s;
            }
        }
        return nullptr;
    }

    std::string render(const char *lang) {
        return rosetta::backend_registry().at(lang)->render(ctx());
    }

    // The text backends write files rather than rendering one string.
    std::string emitted_dts() {
        auto c    = ctx();
        c.out_dir = std::filesystem::temp_directory_path() / "rosetta_signals_ts";
        rosetta::backend_registry().at("typescript")->emit(c);
        std::ifstream in(c.out_dir / "typescript" / "sgtest.d.ts");
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    bool has(const std::string &s, const std::string &part) {
        return s.find(part) != std::string::npos;
    }

} // namespace

// ---- the walk ---------------------------------------------------------------

TEST(Signals, SignalMembersAreSignalsNotFields) {
    const auto  c = ctx();
    const auto &k = widget(c);
    ASSERT_EQ(k.signals.size(), 6u);
    ASSERT_EQ(k.fields.size(), 1u); // `value` only
    EXPECT_EQ(k.fields.front().name, "value");
}

TEST(Signals, ConnectMethodTakesTheSlotAsACallback) {
    const auto  c = ctx();
    const auto *s = signal(widget(c), "renamed");
    ASSERT_NE(s, nullptr);
    const auto &m = s->connect;
    EXPECT_EQ(m.name, "onRenamed");
    EXPECT_TRUE(m.is_extension);
    EXPECT_EQ(m.ext_qualified, "rosetta_sig::sg_Widget__renamed");
    ASSERT_EQ(m.params.size(), 1u);
    const auto &cb = m.params.front().type;
    ASSERT_TRUE(cb.is_callback);
    ASSERT_EQ(cb.callback_sig.size(), 3u); // void(std::string, int)
    EXPECT_EQ(cb.callback_sig[0].kind, "void");
    EXPECT_EQ(cb.callback_sig[1].kind, "string");
    EXPECT_EQ(cb.callback_sig[2].kind, "number");
    EXPECT_EQ(m.ret.kind, "object");
    EXPECT_EQ(m.ret.object_qualified, "sg::Connection");
    EXPECT_EQ(s->connect_expr, "$signal.connect(&$self, $fn)");
}

TEST(Signals, MethodNamePatternExpandsTheMemberName) {
    EXPECT_EQ(rosetta::gen_detail::signal_method_name("on{Field}", "clicked"), "onClicked");
    EXPECT_EQ(rosetta::gen_detail::signal_method_name("{field}_connect", "clicked"),
              "clicked_connect");
    EXPECT_EQ(rosetta::gen_detail::signal_thunk_name("a::b::W", "x"), "rosetta_sig::a_b_W__x");
}

// ---- wasm -------------------------------------------------------------------

TEST(Signals, WasmEmitsTheThunkWithTheConnectExpressionExpanded) {
    const std::string s = render("wasm");
    EXPECT_TRUE(has(s, "namespace rosetta_sig {"));
    EXPECT_TRUE(has(s, "inline sg::Connection sg_Widget__toggled(sg::Widget &self, "
                       "std::function<void(bool)> fn) {"));
    EXPECT_TRUE(has(s, "return self.toggled.connect(&self, std::move(fn));"));
}

TEST(Signals, WasmBindsEachSignalAsAConnectMethod) {
    const std::string s = render("wasm");
    EXPECT_TRUE(has(s, ".function(\"onClicked\", +[](sg::Widget &self, emscripten::val fn) { "
                       "return rosetta_sig::sg_Widget__clicked(self, "
                       "rosetta_wx::make_fn<std::function<void()>>(fn)); })"));
    EXPECT_TRUE(has(s, ".function(\"onToggled\""));
    EXPECT_TRUE(has(s, ".function(\"onRenamed\""));
}

TEST(Signals, WasmPassesAPointerToABoundClassAsAHandle) {
    const std::string s = render("wasm");
    EXPECT_TRUE(has(s, ".function(\"onPicked\""));
    EXPECT_TRUE(has(s, "std::function<void(sg::Widget *)>"));
    EXPECT_TRUE(has(s, "emscripten::val(x, emscripten::allow_raw_pointers())"));
}

TEST(Signals, WasmSkipsUnconvertibleSlotsAndTakenNames) {
    const std::string s = render("wasm");
    EXPECT_FALSE(has(s, "onCarried")); // unbound class argument
    EXPECT_FALSE(has(s, "sg_Widget__carried"));
    EXPECT_FALSE(has(s, "sg_Widget__moved")); // onMoved() is a real method
    EXPECT_TRUE(has(s, "&sg::Widget::onMoved"));
}

// ---- node -------------------------------------------------------------------

TEST(Signals, NodeBindsThroughExtMethod) {
    const std::string s = render("node");
    EXPECT_TRUE(has(s, "This::InstanceMethod<&This::ext_method<&rosetta_sig::sg_Widget__toggled>>"
                       "(\"onToggled\")"));
    EXPECT_TRUE(has(s, "inline sg::Connection sg_Widget__clicked("));
    // to_napi copies objects and has no pointer conversion: those slots stay out.
    EXPECT_FALSE(has(s, "sg_Widget__picked"));
    EXPECT_FALSE(has(s, "sg_Widget__carried"));
}

// ---- python (pybind11) -----------------------------------------------------

TEST(Signals, PythonBindsTheThunkAsAMethod) {
    const std::string s = render("python");
    EXPECT_TRUE(has(s, "namespace rosetta_sig {"));
    EXPECT_TRUE(has(s, "c.def(\"onToggled\", &rosetta_sig::sg_Widget__toggled"));
    EXPECT_TRUE(has(s, "c.def(\"onClicked\", &rosetta_sig::sg_Widget__clicked"));
    EXPECT_TRUE(has(s, "py::arg(\"fn\")"));
    // A pointer to a bound class reaches Python as a reference to the object.
    EXPECT_TRUE(has(s, "c.def(\"onPicked\", &rosetta_sig::sg_Widget__picked"));
}

TEST(Signals, PythonSkipsUnconvertibleSlotsAndTakenNames) {
    const std::string s = render("python");
    EXPECT_FALSE(has(s, "sg_Widget__carried"));
    EXPECT_FALSE(has(s, "sg_Widget__moved"));
}

// ---- typescript ---------------------------------------------------------------

TEST(Signals, TypeScriptDeclaresTheSlotSignature) {
    const std::string s = emitted_dts();
    EXPECT_TRUE(has(s, "onToggled(fn: (arg0: boolean) => void): Connection;"));
    EXPECT_TRUE(has(s, "onRenamed(fn: (arg0: string, arg1: number) => void): Connection;"));
    EXPECT_TRUE(has(s, "onPicked(fn: (arg0: Widget | null) => void): Connection;"));
    EXPECT_TRUE(has(s, "WebAssembly module only")); // onPicked: node cannot pass a pointer
    EXPECT_FALSE(has(s, "onCarried"));
}

// ---- a connection that is not bound -----------------------------------------

TEST(Signals, AnUnboundConnectionMakesTheMethodReturnNothing) {
    // Without sg::Connection in the module, a script could not hold it.
    const auto c = rosetta::gen_detail::make_context<sg::Widget>("sgtest");
    const std::string s = rosetta::backend_registry().at("wasm")->render(c);
    EXPECT_TRUE(has(s, "inline void sg_Widget__clicked("));
    EXPECT_TRUE(has(s, "{ rosetta_sig::sg_Widget__clicked(self, "));
}

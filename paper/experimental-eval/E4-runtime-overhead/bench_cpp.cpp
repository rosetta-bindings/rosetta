// E4 layer A -- the cost of the meta-object model, with no language boundary.
//
// Three arms, one process, same library:
//
//   direct     an ordinary C++ call
//   dyn        rosetta::dyn by name: registry lookup -> resolve() -> thunk
//   dyn-cached the same, with the MetaMethod/MetaField resolved once and
//              reused. dynamic.h explicitly recommends this ("the returned
//              pointer is stable for the process, so a wrapper should cache it
//              per call site"), so measuring only the uncached path would
//              price a usage the documentation tells you not to write.
//
// Keeping this in C++ is deliberate: it separates the cost of LATE BINDING from
// the cost of CROSSING A LANGUAGE BOUNDARY. Layer B (Python) measures the
// combination; the difference between them is the interpreter's share.

#include <benchlib.h>
#include <auto_dynamic.h>
#include <rosetta/dynamic.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace std::chrono;
namespace dyn = rosetta::dyn;

// Accumulator the optimiser cannot discard: every arm feeds it, and it is
// printed at the end. Without this, `direct` measures an empty loop.
static volatile double g_sink = 0.0;

struct Row {
    std::string op;
    std::string arm;
    double      ns_per_op;
};
static std::vector<Row> g_rows;

template <typename F>
static double time_ns(std::size_t iters, F &&f) {
    // one warm-up pass: first touch of the tables, branch predictor, caches
    for (std::size_t i = 0; i < iters / 10 + 1; ++i) f();
    auto t0 = steady_clock::now();
    for (std::size_t i = 0; i < iters; ++i) f();
    auto t1 = steady_clock::now();
    return duration_cast<nanoseconds>(t1 - t0).count() / double(iters);
}

template <typename F>
static void run(const char *op, const char *arm, std::size_t iters, F &&f) {
    double ns = time_ns(iters, f);
    g_rows.push_back({op, arm, ns});
    std::printf("  %-22s %-12s %10.1f ns\n", op, arm, ns);
}

int main(int argc, char **argv) {
    const std::size_t N   = (argc > 1) ? std::stoul(argv[1]) : 200000;
    const std::size_t VEC = 1000;

    bench::register_all();
    auto &reg = dyn::registry();
    const dyn::MetaClass *k = reg.find_class("bench::Widget");
    if (!k) { std::fprintf(stderr, "bench::Widget not registered\n"); return 1; }

    bench::Widget w;
    dyn::Object   o = dyn::Object::borrow(*k, &w);

    std::vector<double> data(VEC, 1.0);

    // ---- resolved once, for the cached arm ----------------------------
    dyn::ArgList no_args;
    dyn::ArgList three{dyn::Any::real(2.0), dyn::Any::real(3.0), dyn::Any::integer(4)};
    dyn::ArgList two_ints{dyn::Any::integer(2), dyn::Any::integer(3)};

    // Hoisted so every cached arm measures the same thing: the call, not the
    // construction of its arguments. The uncached arms build theirs in the loop
    // because that is what a host-language wrapper actually does per call.
    dyn::ArgList one_real{dyn::Any::real(2.0)};

    std::vector<dyn::Any> boxed;
    boxed.reserve(data.size());
    for (double x : data) boxed.push_back(dyn::Any::real(x));
    dyn::ArgList vec_args;
    vec_args.add(dyn::Any::list(boxed));
    vec_args.add(dyn::Any::real(2.0));

    const dyn::MetaMethod *m_ping    = dyn::resolve(*k, "ping", no_args);
    const dyn::MetaMethod *m_combine = dyn::resolve(*k, "combine", three);
    const dyn::MetaMethod *m_at2     = dyn::resolve(*k, "at", two_ints);
    const dyn::MetaMethod *m_clone   = dyn::resolve(*k, "clone", no_args);
    const dyn::MetaMethod *m_scale   = dyn::resolve(*k, "scale_all", vec_args);
    const dyn::MetaField  *f_value   = nullptr;
    for (std::size_t i = 0; i < k->n_fields; ++i)
        if (std::string(k->fields[i].name) == "value") f_value = &k->fields[i];

    if (!m_ping || !m_combine || !m_at2 || !m_clone || !m_scale || !f_value) {
        std::fprintf(stderr, "resolve failed -- library shape changed?\n");
        return 1;
    }

    std::printf("E4 layer A -- C++, %zu iterations per measurement\n\n", N);

    // ---- 1. trivial call ----------------------------------------------
    run("trivial-call", "direct", N, [&] { g_sink += w.ping(); });
    run("trivial-call", "dyn", N, [&] {
        auto r = o.call("ping");
        g_sink += r.ok() ? r.value.as_int() : 0;
    });
    run("trivial-call", "dyn-cached", N, [&] {
        dyn::Any r = m_ping->invoke(o.ref(), no_args);
        g_sink += r.as_int();
    });

    // ---- 2. field get --------------------------------------------------
    run("field-get", "direct", N, [&] { g_sink += w.value; });
    run("field-get", "dyn", N, [&] {
        auto r = o.get("value");
        g_sink += r.ok() ? r.value.as_number() : 0;
    });
    run("field-get", "dyn-cached", N, [&] {
        dyn::Any r = f_value->get(o.ref(), no_args);
        g_sink += r.as_number();
    });

    // ---- 3. field set --------------------------------------------------
    run("field-set", "direct", N, [&] { w.value = 2.0; g_sink += 1; });
    run("field-set", "dyn", N, [&] {
        auto r = o.set("value", dyn::Any::real(2.0));
        g_sink += r.ok() ? 1 : 0;
    });
    run("field-set", "dyn-cached", N, [&] {
        f_value->set(o.ref(), one_real);
        g_sink += 1;
    });

    // ---- 4. multi-argument call ---------------------------------------
    run("3-arg-call", "direct", N, [&] { g_sink += w.combine(2.0, 3.0, 4); });
    run("3-arg-call", "dyn", N, [&] {
        auto r = o.call("combine", {dyn::Any::real(2.0), dyn::Any::real(3.0),
                                    dyn::Any::integer(4)});
        g_sink += r.ok() ? r.value.as_number() : 0;
    });
    run("3-arg-call", "dyn-cached", N, [&] {
        dyn::Any r = m_combine->invoke(o.ref(), three);
        g_sink += r.as_number();
    });

    // ---- 5. object return ----------------------------------------------
    run("object-return", "direct", N, [&] { g_sink += w.clone().value; });
    run("object-return", "dyn", N, [&] {
        auto r = o.call("clone");
        g_sink += r.ok() ? 1 : 0;
    });
    run("object-return", "dyn-cached", N, [&] {
        dyn::Any r = m_clone->invoke(o.ref(), no_args);
        g_sink += r.empty() ? 0 : 1;
    });

    // ---- 6. vector round-trip (fewer iterations: each is 1000 doubles) --
    const std::size_t NV = N / 100 + 1;
    run("vector-1000", "direct", NV, [&] { g_sink += w.scale_all(data, 2.0)[0]; });
    run("vector-1000", "dyn", NV, [&] {
        dyn::ArgList a;
        std::vector<dyn::Any> boxed;
        boxed.reserve(data.size());
        for (double x : data) boxed.push_back(dyn::Any::real(x));
        a.add(dyn::Any::list(boxed));
        a.add(dyn::Any::real(2.0));
        auto r = o.call("scale_all", a);
        g_sink += r.ok() ? 1 : 0;
    });
    // Cached AND pre-boxed: isolates the call from the marshalling. The gap
    // between this and the row above is the cost of boxing 1000 doubles into
    // Any, which is what a host language pays and caching cannot remove.
    run("vector-1000", "dyn-cached", NV, [&] {
        dyn::Any r = m_scale->invoke(o.ref(), vec_args);
        g_sink += r.empty() ? 0 : 1;
    });

    // ---- 7. overload resolution ----------------------------------------
    run("overload-3way", "direct", N, [&] { g_sink += w.at(2, 3); });
    run("overload-3way", "dyn", N, [&] {
        auto r = o.call("at", {dyn::Any::integer(2), dyn::Any::integer(3)});
        g_sink += r.ok() ? r.value.as_int() : 0;
    });
    run("overload-3way", "dyn-cached", N, [&] {
        dyn::Any r = m_at2->invoke(o.ref(), two_ints);
        g_sink += r.as_int();
    });

    // ---- emit CSV -------------------------------------------------------
    if (argc > 2) {
        std::FILE *f = std::fopen(argv[2], "w");
        if (f) {
            std::fprintf(f, "layer,op,arm,ns_per_op\n");
            for (const auto &r : g_rows)
                std::fprintf(f, "cpp,%s,%s,%.3f\n", r.op.c_str(), r.arm.c_str(),
                             r.ns_per_op);
            std::fclose(f);
            std::printf("\nwrote %s\n", argv[2]);
        }
    }

    std::printf("\n(sink %.3f -- printed so nothing above is optimised away)\n",
                (double)g_sink);
    return 0;
}

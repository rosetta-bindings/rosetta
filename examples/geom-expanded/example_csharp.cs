// csharp target  (target name: csgeom)
// ---------------------------------------------------------------------------
// Idiomatic C# wrappers over a native shared library, reached through P/Invoke.
// Each object is a handle into the native instance store; property and method
// access is a JSON round-trip across a flat C ABI (System.Text.Json does the
// (de)serialisation). The native shim is reflection-free, so it builds with a
// stock C++20 compiler — no clang-p2996.
//
// Build & run — from examples/geom-expanded, one command does everything
// (generate the binding, build the native shim, then dotnet run):
//
//   ./run.sh
//
// It compiles this file together with the generated wrapper via run/run.csproj.
// To do it by hand instead, see README.md step 3f, then:
//
//   # macOS (use LD_LIBRARY_PATH on Linux); -p:DemoTF matches your SDK
//   DYLD_LIBRARY_PATH=bindings/csharp/build \
//       dotnet run --project run -p:DemoTF=net8.0
//
// ---------------------------------------------------------------------------
// Boundary note: the C# binding marshals values as JSON, exactly like the REST
// target — scalars, bool, string, enums and List<…> of those. Methods that pass
// *objects* (Model.getSurfaces(), Surface.getPoints(), the free transform(Point))
// don't cross that boundary, so they are intentionally not exposed here. For the
// full object graph (vector<Surface>, vector<Point>, transform) use the
// node target — see example_node.js.

using csgeom;
using System;
using System.Collections.Generic;

internal static class Example {
    private static void Main() {
        // --- Point: scalar fields, parameterised constructor -----------------
        Console.WriteLine("== Point ==");
        using (var p = new Point(1.0, 2.0, 3.0)) {
            Console.WriteLine($"  constructed ({p.x}, {p.y}, {p.z})");
            p.x = 9.5;                       // property setter -> set_field
            Console.WriteLine($"  after p.x = 9.5 -> ({p.x}, {p.y}, {p.z})");
        }                                    // Dispose() frees the native instance

        // --- Triangle: enum field + out-of-line range validation -------------
        Console.WriteLine("== Triangle ==");
        using (var t = new Triangle(0, 1, 2)) {
            Console.WriteLine($"  a={t.a} b={t.b} c={t.c} kind={t.kind}");
            t.kind = Kind.Volume;            // enum marshalled as its integer value
            Console.WriteLine($"  after t.kind = Kind.Volume -> {t.kind} (int {(int)t.kind})");

            // The range on a/b/c lives in Triangle.ann.json (the header is stock
            // C++); it is enforced natively, so an out-of-range write throws.
            try {
                t.a = -1;
                Console.WriteLine("  ERROR: range was not enforced");
            } catch (RosettaException e) {
                Console.WriteLine($"  range check fired: {e.Message}");
            }
        }

        // --- Surface: std::vector<double> / <int> cross as List<…> -----------
        Console.WriteLine("== Surface ==");
        var positions = new List<double> { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
        var indices   = new List<long>   { 0, 1, 2 };
        using (var s = new Surface(positions, indices)) {
            // setPoints takes a vector<double> — a marshalable argument.
            s.setPoints(new List<double> { 2, 2, 2 });
            Console.WriteLine("  constructed from List<double> + List<long>, setPoints() OK");
        }

        // --- Lifetime: using a disposed handle is an error -------------------
        Console.WriteLine("== lifetime ==");
        var dead = new Point();
        dead.Dispose();
        try {
            _ = dead.x;
            Console.WriteLine("  ERROR: read after Dispose did not throw");
        } catch (RosettaException e) {
            Console.WriteLine($"  read after Dispose threw: {e.Message}");
        }
    }
}

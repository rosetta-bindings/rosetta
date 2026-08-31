// Copyright (c) fmaerten@gmail.com
// License: MIT

// mini_moc example: a Thermostat model wired to two independent receivers
// (a Display and a Heater controller) via reflection-driven signals/slots.
//
// What this shows:
//   - [[= property{"handle", "notifySignal"}]] data members with set<>/get<>
//   - Multiple subscribers on the same signal (multicast)
//   - Compile-time-checked connect<"sig","slot">() — typos won't compile
//   - Equality-gated setters: redundant writes don't fire NOTIFY
//   - A property with no NOTIFY still works as data
//
// Build flags: -freflection -freflection-latest -fannotation-attributes
// See CMakeLists.txt in this folder.
//
//   ./build/thermostat

#include <print>
#include <rosetta/mini_moc.h>
#include <string>

// -----------------------------------------------------------------------------
// Model
// -----------------------------------------------------------------------------
class Thermostat {
public:
    // A Signal<...> data member is a signal by type — no annotation needed.
    rosetta::moc::Signal<double>              temperatureChanged;
    rosetta::moc::Signal<double>              targetChanged;
    rosetta::moc::Signal<std::string const &> modeChanged;

    [[= rosetta::moc::property{"temperature", "temperatureChanged"}]]
    double m_temperature = 20.0;

    [[= rosetta::moc::property{"target", "targetChanged"}]]
    double m_target = 21.0;

    [[= rosetta::moc::property{"mode", "modeChanged"}]]
    std::string m_mode = "off";

    // No NOTIFY — get/set still work, just won't multicast.
    [[= rosetta::moc::property{"serial"}]]
    std::string m_serial = "TH-0001";
};

// -----------------------------------------------------------------------------
// Receivers
// -----------------------------------------------------------------------------
struct Display {
    [[= rosetta::moc::slot]]
    void showTemperature(double t) {
        std::println("  [display] room is now {:.1f}°C", t);
    }

    [[= rosetta::moc::slot]]
    void showTarget(double t) {
        std::println("  [display] target set to {:.1f}°C", t);
    }

    [[= rosetta::moc::slot]]
    void showMode(std::string const &m) {
        std::println("  [display] mode = {}", m);
    }
};

struct Heater {
    bool on  = false;
    int  cycles = 0;

    // Receives temperature updates; toggles itself to chase the (cached) target.
    double target_cache = 21.0;

    [[= rosetta::moc::slot]]
    void onTemperature(double t) {
        bool want_on = t < target_cache - 0.2;
        if (want_on != on) {
            on = want_on;
            ++cycles;
            std::println("  [heater] {} (cycle #{})", on ? "ON" : "OFF", cycles);
        }
    }

    [[= rosetta::moc::slot]]
    void onTargetChanged(double t) {
        target_cache = t;
        std::println("  [heater] new target {:.1f}°C cached", t);
    }
};

// -----------------------------------------------------------------------------
// Demo
// -----------------------------------------------------------------------------
int main() {
    Thermostat th;
    Display    d;
    Heater     h;

    // Compile-time-checked. Try typoing a name and watch the static_assert
    // fire — no .moc file, no codegen step.
    rosetta::moc::connect<"temperatureChanged", "showTemperature">(th, d);
    rosetta::moc::connect<"targetChanged",      "showTarget"     >(th, d);
    rosetta::moc::connect<"modeChanged",        "showMode"       >(th, d);

    rosetta::moc::connect<"temperatureChanged", "onTemperature"  >(th, h);
    rosetta::moc::connect<"targetChanged",      "onTargetChanged">(th, h);

    std::println("initial state: room={:.1f}°C target={:.1f}°C mode={} serial={}",
                 rosetta::moc::get<"temperature">(th),
                 rosetta::moc::get<"target">(th),
                 rosetta::moc::get<"mode">(th),
                 rosetta::moc::get<"serial">(th));

    std::println("\n-- switch on --");
    rosetta::moc::set<"mode">(th, std::string{"heat"});

    std::println("\n-- nudge target up to 22.5 --");
    rosetta::moc::set<"target">(th, 22.5);

    std::println("\n-- sensor reads 19.8 (heater should kick on) --");
    rosetta::moc::set<"temperature">(th, 19.8);

    std::println("\n-- sensor reads 19.8 again (equality-gated, silent) --");
    rosetta::moc::set<"temperature">(th, 19.8);

    std::println("\n-- sensor reads 22.6 (heater off) --");
    rosetta::moc::set<"temperature">(th, 22.6);

    std::println("\n-- update serial (no NOTIFY declared, no signals fire) --");
    rosetta::moc::set<"serial">(th, std::string{"TH-0042"});

    std::println("\nfinal: heater on={} cycles={} serial={}",
                 h.on, h.cycles, rosetta::moc::get<"serial">(th));

    // Compile-error demos — uncomment one at a time to see them fail:
    //
    // rosetta::moc::connect<"temperatreChanged", "showTemperature">(th, d);
    //   -> static_assert: no Signal<...> member with that name on sender
    //
    // rosetta::moc::connect<"modeChanged", "onTemperature">(th, h);
    //   -> template error: slot takes double, signal carries std::string const&
    //
    // rosetta::moc::set<"temperatre">(th, 21.0);
    //   -> static_assert: no [[=property]] with that handle on T

    return 0;
}

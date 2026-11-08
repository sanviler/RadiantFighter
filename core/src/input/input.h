#pragma once
// The two Amiga control ports and the devices plugged into them. Each port can be given: nothing, the
// keyboard (numeric keypad 8/4/6/5 or 2 = directions, 7/9/1/3 = diagonals, Right Ctrl = fire, keypad 0 =
// second button), the mouse, or any joystick or game controller found by the system. Joysticks are kept by
// model (SDL GUID) and position among the joysticks of the same model, so a choice survives a different USB
// order. The choice is saved in lightmere.ini as port1 / port2.
#include "amiga/machine.h"
#include <SDL.h>
#include <string>
#include <vector>

namespace lm {

enum class DeviceKind { None, Keyboard, Mouse, Joystick };

struct PortChoice {
    DeviceKind kind = DeviceKind::None;
    std::string guid;                               // joystick model
    int ordinal = 0;                                // n-th joystick of that model
};

// what the menu shows for a port
struct PortLabel {
    DeviceKind kind = DeviceKind::None;
    int number = 0;                                 // joystick number (1, 2, ...), 0 if not connected
    std::string name;                               // joystick name from the system
};

class InputPorts {
public:
    InputPorts();
    ~InputPorts();

    // settings: "none", "keyboard", "mouse", "joystick:<guid>:<n>"
    void load(const std::string& port1, const std::string& port2);
    std::string save(int port) const;
    // first run: port 1 mouse, port 2 the first joystick if there is one, otherwise the keyboard
    void setDefaults();

    // SDL events: joysticks added or removed, mouse motion and buttons
    void handleEvent(const SDL_Event& ev);
    bool usesKeyboard() const { return choice_[0].kind == DeviceKind::Keyboard || choice_[1].kind == DeviceKind::Keyboard; }
    // keys taken by the keyboard joystick (not sent to the Amiga keyboard)
    bool isJoystickKey(SDL_Scancode sc) const;

    // next / previous device for a port (menu)
    void cycle(int port, int dir);
    PortLabel label(int port) const;

    // writes the state of both ports into the machine; extra = joystick bits from tests (port 2:
    // 1 up, 2 down, 4 left, 8 right, 16 fire); paused: nothing pressed
    void apply(amiga::Machine& m, int extra, bool paused);

    // game controller buttons for the menus (any controller)
    static bool isMenuButton(const SDL_Event& ev, int button) {
        return ev.type == SDL_CONTROLLERBUTTONDOWN && ev.cbutton.button == button;
    }

private:
    struct Pad {
        SDL_JoystickID id = -1;
        SDL_GameController* gc = nullptr;
        SDL_Joystick* js = nullptr;
        std::string guid, name;
    };
    void openAll();
    void open(int deviceIndex);
    const Pad* find(const PortChoice& c, int* number = nullptr) const;
    std::vector<PortChoice> choices() const;        // None, Keyboard, Mouse, every connected joystick

    std::vector<Pad> pads_;
    PortChoice choice_[2];
    int mouseDx_ = 0, mouseDy_ = 0;
    float mouseFx_ = 0, mouseFy_ = 0;
    bool mouseL_ = false, mouseR_ = false;
};

} // namespace lm

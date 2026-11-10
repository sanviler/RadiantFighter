#include "input/input.h"
#include <algorithm>
#include <cstdlib>

namespace lm {

namespace {
std::string guidOf(SDL_Joystick* js) {
    char buf[64] = {};
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(js), buf, sizeof buf);
    return buf;
}

PortChoice parse(const std::string& s) {
    PortChoice c;
    if (s == "keyboard") c.kind = DeviceKind::Keyboard;
    else if (s == "mouse") c.kind = DeviceKind::Mouse;
    else if (s.rfind("joystick:", 0) == 0) {
        c.kind = DeviceKind::Joystick;
        const std::string rest = s.substr(9);
        const auto colon = rest.find(':');
        c.guid = rest.substr(0, colon);
        c.ordinal = colon == std::string::npos ? 0 : std::atoi(rest.c_str() + colon + 1);
    }
    return c;
}

bool same(const PortChoice& a, const PortChoice& b) {
    return a.kind == b.kind && (a.kind != DeviceKind::Joystick || (a.guid == b.guid && a.ordinal == b.ordinal));
}
} // namespace

InputPorts::InputPorts() {
    choice_[0].kind = DeviceKind::Mouse;
    choice_[1].kind = DeviceKind::Keyboard;
    openAll();
}

InputPorts::~InputPorts() {
    for (Pad& p : pads_) {
        if (p.gc) SDL_GameControllerClose(p.gc);
        else if (p.js) SDL_JoystickClose(p.js);
    }
}

void InputPorts::openAll() {
    for (int i = 0; i < SDL_NumJoysticks(); ++i) open(i);
}

void InputPorts::open(int deviceIndex) {
    const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(deviceIndex);
    for (const Pad& p : pads_) if (p.id == id) return;
    Pad p;
    if (SDL_IsGameController(deviceIndex)) {
        p.gc = SDL_GameControllerOpen(deviceIndex);
        p.js = p.gc ? SDL_GameControllerGetJoystick(p.gc) : nullptr;
    }
    if (!p.js) p.js = SDL_JoystickOpen(deviceIndex);
    if (!p.js) return;
    p.id = SDL_JoystickInstanceID(p.js);
    p.guid = guidOf(p.js);
    const char* n = p.gc ? SDL_GameControllerName(p.gc) : SDL_JoystickName(p.js);
    p.name = n ? n : "Joystick";
    pads_.push_back(p);
}

void InputPorts::load(const std::string& port1, const std::string& port2) {
    if (!port1.empty()) choice_[0] = parse(port1);
    if (!port2.empty()) choice_[1] = parse(port2);
}

std::string InputPorts::save(int port) const {
    const PortChoice& c = choice_[port & 1];
    switch (c.kind) {
        case DeviceKind::Keyboard: return "keyboard";
        case DeviceKind::Mouse: return "mouse";
        case DeviceKind::Joystick: return "joystick:" + c.guid + ":" + std::to_string(c.ordinal);
        default: return "none";
    }
}

void InputPorts::setDefaults() {
    choice_[0] = PortChoice{DeviceKind::Mouse, {}, 0};
    const std::vector<PortChoice> all = choices();
    const auto joy = std::find_if(all.begin(), all.end(), [](const PortChoice& c) { return c.kind == DeviceKind::Joystick; });
    choice_[1] = joy != all.end() ? *joy : PortChoice{DeviceKind::Keyboard, {}, 0};
}

void InputPorts::setDefaultsTwoPlayers(int player1Port) {
    const int p1 = player1Port & 1, p2 = p1 ^ 1;
    std::vector<PortChoice> joys;
    for (const PortChoice& c : choices()) if (c.kind == DeviceKind::Joystick) joys.push_back(c);
    choice_[p1] = !joys.empty() ? joys[0] : PortChoice{DeviceKind::Keyboard, {}, 0};
    choice_[p2] = joys.size() > 1 ? joys[1] : !joys.empty() ? PortChoice{DeviceKind::Keyboard, {}, 0} : PortChoice{DeviceKind::None, {}, 0};
}

std::vector<PortChoice> InputPorts::choices() const {
    std::vector<PortChoice> v = {{DeviceKind::None, {}, 0}, {DeviceKind::Keyboard, {}, 0}, {DeviceKind::Mouse, {}, 0}};
    for (size_t i = 0; i < pads_.size(); ++i) {
        int ord = 0;
        for (size_t j = 0; j < i; ++j) if (pads_[j].guid == pads_[i].guid) ++ord;
        v.push_back({DeviceKind::Joystick, pads_[i].guid, ord});
    }
    return v;
}

const InputPorts::Pad* InputPorts::find(const PortChoice& c, int* number) const {
    if (c.kind != DeviceKind::Joystick) return nullptr;
    int ord = 0;
    for (size_t i = 0; i < pads_.size(); ++i) {
        if (pads_[i].guid != c.guid) continue;
        if (ord++ == c.ordinal) { if (number) *number = int(i) + 1; return &pads_[i]; }
    }
    return nullptr;
}

void InputPorts::cycle(int port, int dir) {
    const std::vector<PortChoice> all = choices();
    PortChoice& c = choice_[port & 1];
    int at = -1;
    for (size_t i = 0; i < all.size(); ++i) if (same(all[i], c)) at = int(i);
    const int n = int(all.size());
    at = at < 0 ? 0 : ((at + (dir < 0 ? -1 : 1)) % n + n) % n;
    c = all[size_t(at)];
}

PortLabel InputPorts::label(int port) const {
    const PortChoice& c = choice_[port & 1];
    PortLabel l;
    l.kind = c.kind;
    if (const Pad* p = find(c, &l.number)) l.name = p->name;
    return l;
}

bool InputPorts::isJoystickKey(SDL_Scancode sc) const {
    if (!usesKeyboard()) return false;
    switch (sc) {
        case SDL_SCANCODE_KP_1: case SDL_SCANCODE_KP_2: case SDL_SCANCODE_KP_3: case SDL_SCANCODE_KP_4:
        case SDL_SCANCODE_KP_5: case SDL_SCANCODE_KP_6: case SDL_SCANCODE_KP_7: case SDL_SCANCODE_KP_8:
        case SDL_SCANCODE_KP_9: case SDL_SCANCODE_KP_0: case SDL_SCANCODE_RCTRL: return true;
        default: return false;
    }
}

void InputPorts::handleEvent(const SDL_Event& ev) {
    switch (ev.type) {
        case SDL_JOYDEVICEADDED: open(ev.jdevice.which); break;
        case SDL_JOYDEVICEREMOVED:
            for (size_t i = 0; i < pads_.size(); ++i)
                if (pads_[i].id == ev.jdevice.which) {
                    if (pads_[i].gc) SDL_GameControllerClose(pads_[i].gc);
                    else if (pads_[i].js) SDL_JoystickClose(pads_[i].js);
                    pads_.erase(pads_.begin() + long(i));
                    break;
                }
            break;
        case SDL_MOUSEMOTION: {
            // the Amiga mouse counts about one step per lores pixel; window pixels are several times smaller
            mouseFx_ += float(ev.motion.xrel) * 0.5f;
            mouseFy_ += float(ev.motion.yrel) * 0.5f;
            const int dx = int(mouseFx_), dy = int(mouseFy_);
            mouseDx_ += dx; mouseDy_ += dy;
            mouseFx_ -= float(dx); mouseFy_ -= float(dy);
            break;
        }
        case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: {
            const bool dn = ev.type == SDL_MOUSEBUTTONDOWN;
            if (ev.button.button == SDL_BUTTON_LEFT) mouseL_ = dn;
            if (ev.button.button == SDL_BUTTON_RIGHT) mouseR_ = dn;
            break;
        }
        default: break;
    }
}

void InputPorts::apply(amiga::Machine& m, int extra, bool paused) {
    const Uint8* kb = SDL_GetKeyboardState(nullptr);
    for (int port = 0; port < 2; ++port) {
        const PortChoice& c = choice_[port];
        m.setPortDevice(port, c.kind == DeviceKind::Mouse ? amiga::Machine::PortDevice::Mouse : amiga::Machine::PortDevice::Joystick);
        if (c.kind == DeviceKind::Mouse) {
            m.setPortMouse(port, paused ? 0 : mouseDx_, paused ? 0 : mouseDy_, !paused && mouseL_, !paused && mouseR_);
            continue;
        }
        bool up = false, down = false, left = false, right = false, fire = false, fire2 = false;
        if (!paused && c.kind == DeviceKind::Keyboard) {
            up = kb[SDL_SCANCODE_KP_8] || kb[SDL_SCANCODE_KP_7] || kb[SDL_SCANCODE_KP_9];
            down = kb[SDL_SCANCODE_KP_5] || kb[SDL_SCANCODE_KP_2] || kb[SDL_SCANCODE_KP_1] || kb[SDL_SCANCODE_KP_3];
            left = kb[SDL_SCANCODE_KP_4] || kb[SDL_SCANCODE_KP_7] || kb[SDL_SCANCODE_KP_1];
            right = kb[SDL_SCANCODE_KP_6] || kb[SDL_SCANCODE_KP_9] || kb[SDL_SCANCODE_KP_3];
            fire = kb[SDL_SCANCODE_RCTRL];
            fire2 = kb[SDL_SCANCODE_KP_0];
        } else if (!paused && c.kind == DeviceKind::Joystick) {
            if (const Pad* p = find(c)) {
                if (p->gc) {
                    const int ax = SDL_GameControllerGetAxis(p->gc, SDL_CONTROLLER_AXIS_LEFTX);
                    const int ay = SDL_GameControllerGetAxis(p->gc, SDL_CONTROLLER_AXIS_LEFTY);
                    left = SDL_GameControllerGetButton(p->gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || ax < -16000;
                    right = SDL_GameControllerGetButton(p->gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || ax > 16000;
                    up = SDL_GameControllerGetButton(p->gc, SDL_CONTROLLER_BUTTON_DPAD_UP) || ay < -16000;
                    down = SDL_GameControllerGetButton(p->gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ay > 16000;
                    fire = SDL_GameControllerGetButton(p->gc, SDL_CONTROLLER_BUTTON_A);
                    fire2 = SDL_GameControllerGetButton(p->gc, SDL_CONTROLLER_BUTTON_B);
                } else {
                    // a plain joystick: first hat or first two axes, buttons 0 and 1
                    const Uint8 hat = SDL_JoystickNumHats(p->js) > 0 ? SDL_JoystickGetHat(p->js, 0) : 0;
                    const int ax = SDL_JoystickNumAxes(p->js) > 0 ? SDL_JoystickGetAxis(p->js, 0) : 0;
                    const int ay = SDL_JoystickNumAxes(p->js) > 1 ? SDL_JoystickGetAxis(p->js, 1) : 0;
                    left = (hat & SDL_HAT_LEFT) || ax < -16000;
                    right = (hat & SDL_HAT_RIGHT) || ax > 16000;
                    up = (hat & SDL_HAT_UP) || ay < -16000;
                    down = (hat & SDL_HAT_DOWN) || ay > 16000;
                    fire = SDL_JoystickNumButtons(p->js) > 0 && SDL_JoystickGetButton(p->js, 0);
                    fire2 = SDL_JoystickNumButtons(p->js) > 1 && SDL_JoystickGetButton(p->js, 1);
                }
            }
        }
        if (port == 1) {                            // automated tests drive port 2
            up |= (extra & 1) != 0; down |= (extra & 2) != 0; left |= (extra & 4) != 0;
            right |= (extra & 8) != 0; fire |= (extra & 16) != 0;
        }
        m.setPortJoystick(port, up, down, left, right, fire, fire2);
    }
    mouseDx_ = mouseDy_ = 0;
}

} // namespace lm

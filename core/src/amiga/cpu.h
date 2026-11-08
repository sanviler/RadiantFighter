#pragma once
// Adapter between the Moira 68000 core and the machine.
#include "Moira.h"

namespace amiga {

class Machine;

class Cpu : public moira::Moira {
public:
    explicit Cpu(Machine& m);
    moira::u8 read8(moira::u32 a) const override;
    moira::u16 read16(moira::u32 a) const override;
    moira::u32 read32(moira::u32 a) const override;
    void write8(moira::u32 a, moira::u8 v) const override;
    void write16(moira::u32 a, moira::u16 v) const override;
    void write32(moira::u32 a, moira::u32 v) const override;
    void cpuDidHalt() override;
    void willExecute(moira::M68kException exc, moira::u16 vector) override;
private:
    Machine& m_;
};

} // namespace amiga

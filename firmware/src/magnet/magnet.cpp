/*
 * Copyright (C) 2015 Pavel Kirienko <pavel.kirienko@gmail.com>
 */

/*
 * TODO
 * VDD CAN is Vin, generate critical error when Vin >6.5V, abs max 7V, CAN transiver limit
 * BOR detect
 */

#include "magnet.hpp"
#include "charger.hpp"
#include <sys/board.hpp>
#include <uavcan/util/lazy_constructor.hpp>

namespace magnet
{
namespace
{

static constexpr std::uint16_t TurnOffCycleArray[][2] =
{
    { 475, 0 },
    { 450, 0 },
    { 300, 1 },
    { 290, 0 },
    { 280, 1 },
    { 270, 0 },
    { 260, 1 },
    { 250, 0 },
    { 240, 1 },
    { 230, 0 },
    { 220, 1 },
    { 210, 0 },
    { 200, 1 },
    { 190, 0 },
    { 180, 1 },
    { 170, 0 },
    { 160, 1 },
    { 150, 0 },
    { 140, 1 },
    { 130, 0 },
    { 120, 1 },
    { 110, 0 },
    { 100, 1 },
    { 90,  0 },
    { 80,  1 },
    { 70,  0 },
    { 60,  1 },
    { 50,  0 },
    { 40,  1 },
    { 30,  0 },
    { 20,  1 },
    { 20,  0 },
    { 20,  1 },
    { 20,  0 },
    { 20,  1 },
    { 20,  0 },
    { 20,  1 },
    { 20,  0 },
    { 20,  1 },
    { 20,  0 },
    { 20,  1 },
    { 20,  0 },
    { 20,  1 }
};

static constexpr unsigned TurnOffCycleArraySize = sizeof(TurnOffCycleArray) / sizeof(TurnOffCycleArray[0]);

static board::MonotonicDuration MinCommandInterval = board::MonotonicDuration::fromMSec(2500);

static uavcan::LazyConstructor<charger::Charger> chrg;

/**
 * Positive when turning on
 * Negative when turning off
 * Zero when idle
 */
static int remaining_cycles = 0;

static Health health = Health::Ok;

static std::uint8_t charger_status_flags = 0;

static bool magnet_is_on = false;               ///< This is default

static board::MonotonicTime last_command_ts;


void updateChargerStatusFlags(std::uint8_t x)
{
    charger_status_flags = x;
}

void pollOn()
{
    if (!chrg.isConstructed())
    {
        board::syslog("Mag ON chrg started\r\n");
        chrg.construct<unsigned>(475);
    }

    const auto status = chrg->runAndGetStatus();
    updateChargerStatusFlags(chrg->getErrorFlags());

    if (status == charger::Charger::Status::InProgress)
    {
        ; // Nothing to do
    }
    else if (status == charger::Charger::Status::Done)
    {
        board::setMagnetPos();          // The cap is charged, switching the magnet
        magnet_is_on = true;

        chrg.destroy();                 // Then updating the state
        remaining_cycles--;
        health = Health::Ok;
    }
    else
    {
        chrg.destroy();
        remaining_cycles = 0;
        health = Health::Error;
    }
}

void pollOff()
{
    const unsigned cycle_index = TurnOffCycleArraySize - unsigned(-remaining_cycles);

    const auto cycle_array_item = TurnOffCycleArray[cycle_index];

    if (!chrg.isConstructed())
    {
        board::syslog("Mag OFF chrg started cyc ", cycle_index, "\r\n");
        chrg.construct<unsigned>(cycle_array_item[0]);
    }

    const auto status = chrg->runAndGetStatus();
    updateChargerStatusFlags(chrg->getErrorFlags());

    if (status == charger::Charger::Status::InProgress)
    {
        ; // Nothing to do
    }
    else if (status == charger::Charger::Status::Done)
    {
        if (cycle_array_item[1])        // The cap is charged, switching the magnet
        {
            board::setMagnetPos();
        }
        else
        {
            board::setMagnetNeg();
            magnet_is_on = false;
        }

        chrg.destroy();
        remaining_cycles++;
        health = Health::Ok;
    }
    else
    {
        chrg.destroy();
        remaining_cycles = 0;
        health = Health::Error;
    }
}

} // namespace

void turnOn(unsigned num_cycles)
{
    if (remaining_cycles == 0)          // Ignore the command if switching is already in progress
    {
        const auto ts = board::clock::getMonotonic();
        if (magnet_is_on && (ts - last_command_ts < MinCommandInterval))
        {
            return;         // Rate limiting
        }
        last_command_ts = ts;

        num_cycles = std::max<unsigned>(MinTurnOnCycles, num_cycles);
        num_cycles = std::min<unsigned>(MaxCycles, num_cycles);
        remaining_cycles = int(num_cycles);

        board::syslog("Mag on ", remaining_cycles, "\r\n");
    }
}

void turnOff()
{
    if (remaining_cycles == 0)          // Ignore the command if switching is already in progress
    {
        const auto ts = board::clock::getMonotonic();
        if (!magnet_is_on && (ts - last_command_ts < MinCommandInterval))
        {
            return;         // Rate limiting
        }
        last_command_ts = ts;

        board::syslog("Mag off\r\n");
        remaining_cycles = -int(TurnOffCycleArraySize);
        if (!magnet_is_on)
        {
            remaining_cycles += 3;
        }
    }
}

bool isTurnedOn()
{
    return magnet_is_on;
}

void poll()
{
    if (remaining_cycles > 0)
    {
        pollOn();
    }
    else if (remaining_cycles < 0)
    {
        pollOff();
    }
    else
    {
        ;
    }
}

Health getHealth()
{
    return health;
}

std::uint8_t getStatusFlags()
{
    static constexpr std::uint8_t StatusFlagSwitchingOn  = 1 << (charger::Charger::ErrorFlagsBitLength + 0);
    static constexpr std::uint8_t StatusFlagSwitchingOff = 1 << (charger::Charger::ErrorFlagsBitLength + 1);

    std::uint8_t x = charger_status_flags;

    if (remaining_cycles > 0)
    {
        x |= StatusFlagSwitchingOn;
    }
    if (remaining_cycles < 0)
    {
        x |= StatusFlagSwitchingOff;
    }

    return x;
}

}



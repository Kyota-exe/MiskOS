#include "Scheduler.h"
#include "Serial.h"
#include "LAPIC.h"
#include "PIC.h"
#include "SystemCall.h"
#include "CPU.h"
#include "Keyboard.h"
#include "IO.h"

void PageFaultHandler()
{
    Serial::Print("Page fault occurred.");

    uint64_t cr2;
    asm volatile("mov %%cr2, %0" : "=r"(cr2));
    Serial::Printf("CR2: %x", cr2);
}

[[noreturn]] void ExceptionHandler(InterruptFrame* interruptFrame)
{
    Serial::Printf("Exception: %x", interruptFrame->interruptNumber);
    Serial::Printf("Error code: %x", interruptFrame->errorCode);
    Serial::Printf("RIP: %x", interruptFrame->rip);
    Serial::Printf("RSP: %x", interruptFrame->rsp);
    Serial::Printf("Core: %d", CPU::GetCoreID());

    if (interruptFrame->interruptNumber == 0xe) PageFaultHandler();

    Panic();
}

void KeyboardInterruptHandler()
{
    Keyboard::SendKeyToTerminal(inb(0x60));
    PICSendEIO(1);
}

void LAPICTimerInterrupt(InterruptFrame* interruptFrame)
{
    outb(0xe9, '0' + CPU::GetCoreID());
    Scheduler* scheduler = Scheduler::GetScheduler();
    scheduler->SwitchToNextTask(interruptFrame);
    scheduler->lapic->SendEOI();
}

void SystemCallHandler(InterruptFrame* interruptFrame)
{
    Error error = Error::None;

    interruptFrame->rax = SystemCall((SystemCallType)interruptFrame->rax, interruptFrame->rdi,
                                     interruptFrame->rsi, interruptFrame->rdx,interruptFrame->rcx,
                                     interruptFrame->r8,interruptFrame->r9, interruptFrame, error);

    if (error != Error::None)
    {
        interruptFrame->rax = -static_cast<int64_t>(error);
    }
}

extern "C" void ISRHandler(InterruptFrame* interruptFrame)
{
    switch (interruptFrame->interruptNumber)
    {
        case 48:
            LAPICTimerInterrupt(interruptFrame);
            break;
        case 32 + 1:
            KeyboardInterruptHandler();
            break;
        case 0x80:
            SystemCallHandler(interruptFrame);
            break;
        case 0x81:
            Scheduler::GetScheduler()->SwitchToNextTask(interruptFrame);
            break;
        case 0 ... 31:
            ExceptionHandler(interruptFrame);
        default:
            Serial::Printf("Could not find ISR for interrupt %x.", interruptFrame->interruptNumber);
            Panic();
    }
}
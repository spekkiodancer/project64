#include <Project64-rsp-core/RSPDebugger.h>
#include <Project64-rsp-core/RSPInfo.h>
#include <Project64-rsp-core/Recompiler/RspRecompilerCPU.h>
#include <Project64-rsp-core/Settings/RspSettings.h>
#include <Project64-rsp-core/cpu/RSPCpu.h>
#include <Project64-rsp-core/cpu/RSPRegisters.h>
#include <Project64-rsp-core/cpu/RspSystem.h>
#include <Settings/Settings.h>

CRSPSystem RSPSystem;

CRSPSystem::CRSPSystem() :
    CHleTask(*this),
    m_Recompiler(*this),
    m_RSPRegisterHandler(nullptr),
    m_Op(*this),
    m_NextInstruction(RSPPIPELINE_NORMAL),
    m_JumpTo(0),
    m_HEADER(nullptr),
    m_RDRAM(nullptr),
    m_DMEM(nullptr),
    m_IMEM(nullptr),
    m_MI_INTR_REG(nullptr),
    m_SP_MEM_ADDR_REG(nullptr),
    m_SP_DRAM_ADDR_REG(nullptr),
    m_SP_RD_LEN_REG(nullptr),
    m_SP_WR_LEN_REG(nullptr),
    m_SP_STATUS_REG(nullptr),
    m_SP_DMA_FULL_REG(nullptr),
    m_SP_DMA_BUSY_REG(nullptr),
    m_SP_PC_REG(nullptr),
    m_SP_SEMAPHORE_REG(nullptr),
    m_DPC_START_REG(nullptr),
    m_DPC_END_REG(nullptr),
    m_DPC_CURRENT_REG(nullptr),
    m_DPC_STATUS_REG(nullptr),
    m_DPC_CLOCK_REG(nullptr),
    m_DPC_BUFBUSY_REG(nullptr),
    m_DPC_PIPEBUSY_REG(nullptr),
    m_DPC_TMEM_REG(nullptr),
    CheckInterrupts(nullptr),
    ProcessDList(nullptr),
    ProcessRdpList(nullptr),
    m_RdramSize(0)
{
    m_OpCode.Value = 0;
}

CRSPSystem::~CRSPSystem()
{
    if (m_RSPRegisterHandler != nullptr)
    {
        delete m_RSPRegisterHandler;
        m_RSPRegisterHandler = nullptr;
    }
}

void CRSPSystem::Reset(RSP_INFO & Info)
{
    m_Reg.Reset();

    m_HEADER = Info.HEADER;
    m_RDRAM = Info.RDRAM;
    m_DMEM = Info.DMEM;
    m_IMEM = Info.IMEM;
    m_MI_INTR_REG = Info.MI_INTR_REG;
    m_SP_MEM_ADDR_REG = Info.SP_MEM_ADDR_REG;
    m_SP_DRAM_ADDR_REG = Info.SP_DRAM_ADDR_REG;
    m_SP_RD_LEN_REG = Info.SP_RD_LEN_REG;
    m_SP_WR_LEN_REG = Info.SP_WR_LEN_REG;
    m_SP_STATUS_REG = Info.SP_STATUS_REG;
    m_SP_DMA_FULL_REG = Info.SP_DMA_FULL_REG;
    m_SP_DMA_BUSY_REG = Info.SP_DMA_BUSY_REG;
    m_SP_PC_REG = Info.SP_PC_REG;
    m_SP_SEMAPHORE_REG = Info.SP_SEMAPHORE_REG;
    m_DPC_START_REG = Info.DPC_START_REG;
    m_DPC_END_REG = Info.DPC_END_REG;
    m_DPC_CURRENT_REG = Info.DPC_CURRENT_REG;
    m_DPC_STATUS_REG = Info.DPC_STATUS_REG;
    m_DPC_CLOCK_REG = Info.DPC_CLOCK_REG;
    m_DPC_BUFBUSY_REG = Info.DPC_BUFBUSY_REG;
    m_DPC_PIPEBUSY_REG = Info.DPC_PIPEBUSY_REG;
    m_DPC_TMEM_REG = Info.DPC_TMEM_REG;
    CheckInterrupts = Info.CheckInterrupts;
    ProcessDList = Info.ProcessDList;
    ProcessRdpList = Info.ProcessRdpList;

    m_RdramSize = Set_AllocatedRdramSize != 0 ? GetSystemSetting(Set_AllocatedRdramSize) : 0;
    if (m_RdramSize == 0)
    {
        m_RdramSize = 0x00400000;
    }
    m_RSPRegisterHandler = new RSPRegisterHandlerPlugin(*this);
}

void CRSPSystem::RomClosed(void)
{
    if (m_RSPRegisterHandler != nullptr)
    {
        delete m_RSPRegisterHandler;
        m_RSPRegisterHandler = nullptr;
    }
}

void CRSPSystem::RunRecompiler(void)
{
    m_Recompiler.RunCPU();
}

uint32_t CRSPSystem::RunInterpreterCPU(uint32_t Cycles)
{
    uint32_t CycleCount;
    RSP_Running = true;
    if (g_RSPDebugger != nullptr)
    {
        g_RSPDebugger->StartingCPU();
    }
    CycleCount = 0;
    uint32_t & GprR0 = m_Reg.m_GPR[0].UW;
    uint32_t & ProgramCounter = *m_SP_PC_REG;
    while (RSP_Running)
    {
        if (g_RSPDebugger != nullptr)
        {
            g_RSPDebugger->BeforeExecuteOp();
        }
        m_OpCode.Value = *(uint32_t *)(m_IMEM + (ProgramCounter & 0xFFC));
        (m_Op.*(m_Op.Jump_Opcode[m_OpCode.op]))();
        GprR0 = 0x00000000; // MIPS $zero hard-wired to 0

        switch (m_NextInstruction)
        {
        case RSPPIPELINE_NORMAL:
            ProgramCounter = (ProgramCounter + 4) & 0xFFC;
            break;
        case RSPPIPELINE_DELAY_SLOT:
            m_NextInstruction = RSPPIPELINE_JUMP;
            ProgramCounter = (ProgramCounter + 4) & 0xFFC;
            break;
        case RSPPIPELINE_JUMP:
            m_NextInstruction = RSPPIPELINE_NORMAL;
            ProgramCounter = m_JumpTo;
            break;
        case RSPPIPELINE_SINGLE_STEP:
            ProgramCounter = (ProgramCounter + 4) & 0xFFC;
            m_NextInstruction = RSPPIPELINE_SINGLE_STEP_DONE;
            break;
        case RSPPIPELINE_SINGLE_STEP_DONE:
            ProgramCounter = (ProgramCounter + 4) & 0xFFC;
            *m_SP_STATUS_REG |= SP_STATUS_HALT;
            RSP_Running = false;
            break;
        }
    }
    return Cycles;
}

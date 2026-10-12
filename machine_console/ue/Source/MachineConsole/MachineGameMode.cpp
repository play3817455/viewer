#include "MachineGameMode.h"
#include "MachinePawn.h"

AMachineGameMode::AMachineGameMode()
{
	DefaultPawnClass = AMachinePawn::StaticClass();
}

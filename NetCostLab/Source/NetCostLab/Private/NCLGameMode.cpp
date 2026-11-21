// P05 NetCostLab - ANCLGameMode.

#include "NCLGameMode.h"

#include "Engine/World.h"
#include "NCLGameState.h"
#include "NCLPlayerAvatar.h"
#include "NCLPlayerController.h"
#include "NCLWorkloadSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NCLGameMode)

ANCLGameMode::ANCLGameMode()
{
	PlayerControllerClass = ANCLPlayerController::StaticClass();
	GameStateClass = ANCLGameState::StaticClass();      // carries the benchmark phase to every client
	DefaultPawnClass = nullptr;
}

void ANCLGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	if (UNCLWorkloadSubsystem* Workload = GetWorld()->GetSubsystem<UNCLWorkloadSubsystem>())
	{
		if (ANCLPlayerAvatar* Avatar = Workload->AddPlayer(NewPlayer))
		{
			NewPlayer->SetViewTarget(Avatar);
		}
	}
}

void ANCLGameMode::Logout(AController* Exiting)
{
	if (UNCLWorkloadSubsystem* Workload = GetWorld()->GetSubsystem<UNCLWorkloadSubsystem>())
	{
		Workload->RemovePlayer(Exiting);
	}
	Super::Logout(Exiting);
}

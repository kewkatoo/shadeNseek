#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShadeTypes.h"
#include "ShadeAutoplayComponent.generated.h"

class AShadeCharacter;
class AShadeInteractable;
class AShadePlayerController;

UENUM()
enum class EShadeAutoStep : uint8
{
	StartGame,    // wait on the title, then begin
	WaitPhase,    // until the game mode reaches Phase
	Wait,         // fixed seconds
	MoveTo,       // walk to the TargetPoint tagged Tag
	Face,         // turn the camera + body toward actor tagged Tag
	Interact,     // press E (on actor tagged Tag)
	Hold,         // hold E on actor tagged Tag until it is solved (or Time)
	PushCart,     // push the grabbed cart forward until solved (or Time)
	WaitEvent,    // until game-mode event Tag (or Time)
	Jump,         // Space
	Finish
};

USTRUCT()
struct FShadeAutoStep
{
	GENERATED_BODY()

	EShadeAutoStep Type = EShadeAutoStep::Wait;
	FName Tag;
	float Time = 0.f;
	EShadePhase Phase = EShadePhase::Playing;
	float Accept = 40.f;
	bool bSprint = false;
};

/**
 * -autoplay: plays the authored route using exactly the Shade API that player input calls
 * (MoveInput / LookInput / JumpInput / InteractPressed / InteractReleased). The level places TargetPoints tagged AP_*
 * for the route; puzzle actors are tagged Dev_*.
 */
UCLASS(ClassGroup = (Shade))
class SHADENSEEK_API UShadeAutoplayComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShadeAutoplayComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void BuildScript();
	void NextStep();
	AActor* FindTagged(FName Tag) const;
	AShadeCharacter* GetShade() const;
	AShadePlayerController* GetPC() const;
	class AShadeGameMode* GetGM() const;
	void SteerCamera(const FVector& LookAt, float Dt, float Speed, float Pitch);
	void DriveToward(const FVector& Target, float Dt, bool bSprint);

	TArray<FShadeAutoStep> Script;
	int32 StepIndex = -1;
	float StepTime = 0.f;
	bool bStepStarted = false;
	FVector LastProgressPos = FVector::ZeroVector;
	float StuckTimer = 0.f;
	int32 StuckJumps = 0;
	int32 StepEvaporations = 0;
};

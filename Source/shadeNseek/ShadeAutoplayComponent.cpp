#include "ShadeAutoplayComponent.h"
#include "shadeNseek.h"
#include "ShadeCharacter.h"
#include "ShadeGameMode.h"
#include "ShadeInteractables.h"
#include "ShadePlayerController.h"
#include "ShadeSunSubsystem.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/InputSettings.h"

UShadeAutoplayComponent::UShadeAutoplayComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UShadeAutoplayComponent::BeginPlay()
{
	Super::BeginPlay();
	BuildScript();
	FParse::Value(FCommandLine::Get(), TEXT("apfrom="), ApFrom);
	NextStep();
}

void UShadeAutoplayComponent::BuildScript()
{
	auto Step = [this](EShadeAutoStep Type, const TCHAR* Tag = nullptr, float Time = 0.f, float Accept = 40.f, bool bSprint = false)
	{
		FShadeAutoStep S;
		S.Type = Type;
		S.Tag = Tag ? FName(Tag) : NAME_None;
		S.Time = Time;
		S.Accept = Accept;
		S.bSprint = bSprint;
		Script.Add(S);
	};
	using E = EShadeAutoStep;
	Step(E::StartGame, nullptr, 5.f);
	{
		FShadeAutoStep S;
		S.Type = E::WaitPhase;
		S.Phase = EShadePhase::Playing;
		Script.Add(S);
	}
	Step(E::Wait, nullptr, 1.0f);
	// Lane: a deliberate toe into the sun to show the burn, then back to the cool side.
	Step(E::MoveTo, TEXT("AP_Lane1"));
	Step(E::MoveTo, TEXT("AP_SunTouch"), 0.f, 22.f);
	Step(E::Wait, nullptr, 0.18f);
	Step(E::MoveTo, TEXT("AP_SunBack"), 0.f, 30.f);
	Step(E::Wait, nullptr, 0.6f);
	// Puzzle 1: lemon cart into the alley sunbeam.
	Step(E::MoveTo, TEXT("AP_CartGrab"), 0.f, 30.f);
	Step(E::Face, TEXT("Dev_Cart"), 0.5f);
	Step(E::Interact, TEXT("Dev_Cart"));
	Step(E::PushCart, TEXT("Dev_Cart"), 14.f);
	Step(E::Interact, TEXT("Dev_Cart"));
	Step(E::MoveTo, TEXT("AP_Alley1"), 0.f, 30.f);
	Step(E::MoveTo, TEXT("AP_Alley2"), 0.f, 35.f);
	Step(E::MoveTo, TEXT("AP_Alley3"));
	// Puzzle 2: tug the awning cord.
	Step(E::MoveTo, TEXT("AP_Cord"), 0.f, 30.f);
	Step(E::Face, TEXT("Dev_Awning"), 0.5f);
	Step(E::Hold, TEXT("Dev_Awning"), 6.f);
	// Let the canvas finish rolling out: crossing while its shadow is still sweeping in sometimes clipped the sun.
	Step(E::Wait, nullptr, 1.5f);
	// Keep to the house's shade before stepping under the canvas: the diagonal from the cord crossed ~1 m of sun.
	Step(E::MoveTo, TEXT("AP_Piazza0"), 0.f, 30.f);
	Step(E::MoveTo, TEXT("AP_Piazza1"), 0.f, 35.f);
	Step(E::MoveTo, TEXT("AP_Piazza2"), 0.f, 35.f);
	Step(E::MoveTo, TEXT("AP_Steps1"), 0.f, 35.f);
	// Puzzle 3: run the washing out, then cross the gap in a lull between the sea gusts.
	Step(E::MoveTo, TEXT("AP_Pulley"), 0.f, 30.f);
	Step(E::Face, TEXT("Dev_Wash"), 0.35f);
	Step(E::Hold, TEXT("Dev_Wash"), 5.f);
	Step(E::MoveTo, TEXT("AP_WashStart"), 0.f, 30.f);
	Step(E::WaitShade, TEXT("AP_WashMid"), 9.f);
	Step(E::MoveTo, TEXT("AP_WashEnd"), 0.f, 35.f, true);
	// Puzzle 4: tug the shutter cord and trot through its shade before it swings shut.
	Step(E::MoveTo, TEXT("AP_ShutterCord"), 0.f, 30.f);
	Step(E::Face, TEXT("Dev_Shutter"), 0.35f);
	Step(E::Interact, TEXT("Dev_Shutter"));
	Step(E::WaitShade, TEXT("AP_ShutterMid"), 2.5f);
	Step(E::MoveTo, TEXT("AP_ShutterEnd"), 0.f, 35.f, true);
	// Puzzle 5: Casa Azzurra's awning shades the wall, then the pot bridges the last gap.
	Step(E::MoveTo, TEXT("AP_AzzurraCord"), 0.f, 30.f);
	Step(E::Face, TEXT("Dev_Azzurra"), 0.35f);
	Step(E::Hold, TEXT("Dev_Azzurra"), 5.f);
	Step(E::Wait, nullptr, 0.4f);
	Step(E::MoveTo, TEXT("AP_WallJump"), 0.f, 30.f);
	Step(E::Jump);
	Step(E::MoveTo, TEXT("AP_WallTop"), 0.f, 30.f);
	Step(E::MoveTo, TEXT("AP_PotNudge"), 0.f, 25.f);
	Step(E::Face, TEXT("Dev_Pot"), 0.4f);
	Step(E::Interact, TEXT("Dev_Pot"));
	Step(E::Wait, nullptr, 1.6f);
	Step(E::MoveTo, TEXT("AP_WallDown"), 0.f, 35.f);
	Step(E::MoveTo, TEXT("AP_Terrace"), 0.f, 35.f);
	// Finale: wait in the porch while the bell-tower shadow sweeps over Biscotto.
	Step(E::MoveTo, TEXT("AP_Porch"), 0.f, 30.f);
	Step(E::Face, TEXT("Dev_Biscotto"), 0.8f);
	Step(E::WaitEvent, TEXT("TowerShadeArrived"), 22.f);
	Step(E::Wait, nullptr, 0.3f);
	Step(E::MoveTo, TEXT("AP_Biscotto"), 0.f, 30.f);
	Step(E::Face, TEXT("Dev_Biscotto"), 0.4f);
	Step(E::Interact, TEXT("Dev_Biscotto"));
	{
		FShadeAutoStep S;
		S.Type = E::WaitPhase;
		S.Phase = EShadePhase::Ended;
		Script.Add(S);
	}
	Step(E::Wait, nullptr, 5.f);
	Step(E::Finish);
}

AShadeCharacter* UShadeAutoplayComponent::GetShade() const
{
	const AShadePlayerController* PC = GetPC();
	return PC ? Cast<AShadeCharacter>(PC->GetPawn()) : nullptr;
}

AShadePlayerController* UShadeAutoplayComponent::GetPC() const
{
	return Cast<AShadePlayerController>(GetOwner());
}

AShadeGameMode* UShadeAutoplayComponent::GetGM() const
{
	return GetWorld() ? GetWorld()->GetAuthGameMode<AShadeGameMode>() : nullptr;
}

AActor* UShadeAutoplayComponent::FindTagged(FName Tag) const
{
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(Tag))
		{
			return *It;
		}
	}
	return nullptr;
}

void UShadeAutoplayComponent::NextStep()
{
	++StepIndex;
	StepTime = 0.f;
	bStepStarted = false;
	StuckTimer = 0.f;
	StuckJumps = 0;
	bSeenLit = false;
	ShadedTime = 0.f;
	if (AShadeCharacter* S = GetShade())
	{
		LastProgressPos = S->GetActorLocation();
		S->SetSprint(false);
		S->SetForcedFocus(nullptr);
	}
	if (Script.IsValidIndex(StepIndex))
	{
		UE_LOG(LogShade, Log, TEXT("SHADE_AP step %d type %d tag %s"), StepIndex, static_cast<int32>(Script[StepIndex].Type), *Script[StepIndex].Tag.ToString());
	}
}

void UShadeAutoplayComponent::SteerCamera(const FVector& LookAt, float Dt, float Speed, float Pitch)
{
	AShadePlayerController* PC = GetPC();
	AShadeCharacter* S = GetShade();
	if (!PC || !S)
	{
		return;
	}
	const FVector To = LookAt - S->GetActorLocation();
	if (To.SizeSquared2D() < 1.f)
	{
		return;
	}
	const FRotator Cur = PC->GetControlRotation();
	const float WantYaw = To.Rotation().Yaw;
	const float DYaw = FMath::FindDeltaAngleDegrees(Cur.Yaw, WantYaw);
	const float NewYaw = Cur.Yaw + DYaw * FMath::Min(1.f, Dt * Speed);
	const float CurPitch = FRotator::NormalizeAxis(Cur.Pitch);
	const float NewPitch = FMath::Lerp(CurPitch, Pitch, FMath::Min(1.f, Dt * Speed * 0.6f));
	// Same path as mouse look: feed the delta through LookInput.
	const float YawStep = FMath::FindDeltaAngleDegrees(Cur.Yaw, NewYaw);
	const float PitchStep = NewPitch - CurPitch;
	const bool bLegacy = GetDefault<UInputSettings>()->bEnableLegacyInputScales;
	const float YawScale = (bLegacy && PC->InputYawScale_DEPRECATED != 0.f) ? PC->InputYawScale_DEPRECATED : 1.f;
	const float PitchScale = (bLegacy && PC->InputPitchScale_DEPRECATED != 0.f) ? PC->InputPitchScale_DEPRECATED : 1.f;
	S->LookInput(FVector2D(YawStep / YawScale, PitchStep / PitchScale));
}

void UShadeAutoplayComponent::DriveToward(const FVector& Target, float Dt, bool bSprint)
{
	AShadeCharacter* S = GetShade();
	AShadePlayerController* PC = GetPC();
	if (!S || !PC)
	{
		return;
	}
	const FVector To = (Target - S->GetActorLocation());
	const FVector Dir = FVector(To.X, To.Y, 0.f).GetSafeNormal();
	const FRotator Yaw(0.f, PC->GetControlRotation().Yaw, 0.f);
	const FVector Fwd = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
	// Ease off near the target so we stop where we mean to.
	const float Mag = FMath::Clamp(To.Size2D() / 80.f, 0.35f, 1.f);
	S->SetSprint(bSprint);
	S->MoveInput(FVector2D(FVector::DotProduct(Dir, Right), FVector::DotProduct(Dir, Fwd)) * Mag);
	// Look where we're going, a touch above the horizon so the sunlit upper facades stay in frame.
	SteerCamera(Target + Dir * 400.f + FVector(0.f, 0.f, 60.f), Dt, 2.0f, 1.f);
}

void UShadeAutoplayComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Script.IsValidIndex(StepIndex))
	{
		return;
	}
	const float Dt = FMath::Min(DeltaTime, 0.05f);
	StepTime += Dt;
	const FShadeAutoStep& St = Script[StepIndex];
	AShadeGameMode* G = GetGM();
	AShadeCharacter* S = GetShade();
	if (!G || !S)
	{
		return;
	}

	switch (St.Type)
	{
	case EShadeAutoStep::StartGame:
		if (StepTime > St.Time)
		{
			G->StartGame();
			NextStep();
		}
		break;
	case EShadeAutoStep::WaitPhase:
		if (G->GetPhase() == St.Phase)
		{
			NextStep();
		}
		break;
	case EShadeAutoStep::Wait:
		if (!ApFrom.IsEmpty())
		{
			// QA shortcut: jump to the requested waypoint and carry on from there.
			const FName From(*ApFrom);
			ApFrom.Empty();
			for (int32 i = StepIndex + 1; i < Script.Num(); ++i)
			{
				if (Script[i].Type == EShadeAutoStep::MoveTo && Script[i].Tag == From)
				{
					if (AActor* T = FindTagged(From))
					{
						S->SetActorLocation(T->GetActorLocation() + FVector(0.f, 0.f, 30.f), false, nullptr, ETeleportType::TeleportPhysics);
					}
					UE_LOG(LogShade, Log, TEXT("SHADE_AP jumping to step %d (%s)"), i, *From.ToString());
					StepIndex = i - 1;
					NextStep();
					return;
				}
			}
		}
		if (StepTime > St.Time)
		{
			NextStep();
		}
		break;
	case EShadeAutoStep::MoveTo:
	{
		AActor* T = FindTagged(St.Tag);
		if (!T)
		{
			UE_LOG(LogShade, Warning, TEXT("SHADE_AP missing waypoint %s"), *St.Tag.ToString());
			NextStep();
			break;
		}
		if (S->IsEvaporating())
		{
			break;
		}
		if (!bStepStarted)
		{
			bStepStarted = true;
			StepEvaporations = S->GetEvaporations();
		}
		if (S->GetEvaporations() - StepEvaporations >= 2)
		{
			UE_LOG(LogShade, Warning, TEXT("SHADE_AP burnt twice on the way to %s, skipping"), *St.Tag.ToString());
			NextStep();
			break;
		}
		const FVector Target = T->GetActorLocation();
		const float D = FVector::Dist2D(S->GetActorLocation(), Target);
		const float DZ = FMath::Abs(S->GetActorLocation().Z - Target.Z);
		if (D <= St.Accept && DZ < 120.f)
		{
			NextStep();
			break;
		}
		DriveToward(Target, Dt, St.bSprint);
		// Stuck: hop, then give up on the waypoint.
		if (FVector::Dist2D(S->GetActorLocation(), LastProgressPos) > 25.f)
		{
			LastProgressPos = S->GetActorLocation();
			StuckTimer = 0.f;
		}
		else
		{
			StuckTimer += Dt;
			if (StuckTimer > 1.2f && StuckJumps < 3)
			{
				S->JumpInput();
				++StuckJumps;
				StuckTimer = 0.4f;
			}
			if (StuckTimer > 1.0f && StuckJumps >= 3)
			{
				UE_LOG(LogShade, Warning, TEXT("SHADE_AP stuck at %s, skipping %s"), *S->GetActorLocation().ToCompactString(), *St.Tag.ToString());
				NextStep();
			}
		}
		break;
	}
	case EShadeAutoStep::Face:
	{
		if (AActor* T = FindTagged(St.Tag))
		{
			AShadeInteractable* I = Cast<AShadeInteractable>(T);
			const FVector At = I ? I->GetFocusLocation() : T->GetActorLocation();
			SteerCamera(At, Dt, 5.f, -18.f);
			S->SetForcedFocus(I);
		}
		if (StepTime > St.Time)
		{
			NextStep();
		}
		break;
	}
	case EShadeAutoStep::Interact:
	{
		AShadeInteractable* I = Cast<AShadeInteractable>(FindTagged(St.Tag));
		S->InteractPressed(I);
		S->InteractReleased();
		NextStep();
		break;
	}
	case EShadeAutoStep::Hold:
	{
		AShadeInteractable* I = Cast<AShadeInteractable>(FindTagged(St.Tag));
		if (!bStepStarted)
		{
			bStepStarted = true;
			S->SetForcedFocus(I);
			S->InteractPressed(I);
		}
		if (I)
		{
			SteerCamera(I->GetFocusLocation() + FVector(0, 0, 160.f), Dt, 1.5f, -6.f);
		}
		if ((I && I->IsSolved()) || StepTime > St.Time)
		{
			S->InteractReleased();
			NextStep();
		}
		break;
	}
	case EShadeAutoStep::PushCart:
	{
		AShadeLemonCart* Cart = Cast<AShadeLemonCart>(FindTagged(St.Tag));
		if (!Cart || S->GetGrabbed() != Cart)
		{
			UE_LOG(LogShade, Warning, TEXT("SHADE_AP not holding the cart"));
			NextStep();
			break;
		}
		AShadePlayerController* PC = GetPC();
		const FRotator Yaw(0.f, PC->GetControlRotation().Yaw, 0.f);
		const FVector Axis = Cart->GetPushDirection();
		S->MoveInput(FVector2D(FVector::DotProduct(Axis, FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y)), FVector::DotProduct(Axis, FRotationMatrix(Yaw).GetUnitAxis(EAxis::X))));
		// Look past the cart, a little to the side, so the shadow pouring over the lane is in frame.
		SteerCamera(Cart->GetActorLocation() + Axis * 250.f + FVector(0, 0, -60.f), Dt, 1.6f, -20.f);
		if (Cart->GetOffset() >= Cart->SolvedOffset + 5.f || StepTime > St.Time)
		{
			NextStep();
		}
		break;
	}
	case EShadeAutoStep::WaitEvent:
		// While waiting, look down the terrace at Biscotto so the sweeping shadow is in frame.
		if (AActor* Cat = FindTagged(TEXT("Dev_Biscotto")))
		{
			SteerCamera(Cat->GetActorLocation(), Dt, 1.2f, -24.f);
		}
		if (G->HasEvent(St.Tag) || StepTime > St.Time)
		{
			NextStep();
		}
		break;
	case EShadeAutoStep::Jump:
		S->JumpInput();
		NextStep();
		break;
	case EShadeAutoStep::WaitShade:
	{
		AActor* T = FindTagged(St.Tag);
		const UShadeSunSubsystem* Sun = UShadeSunSubsystem::Get(this);
		if (!T || !Sun)
		{
			UE_LOG(LogShade, Warning, TEXT("SHADE_AP missing shade point %s"), *St.Tag.ToString());
			NextStep();
			break;
		}
		// Watch the gap (waypoints sit 40 cm above the ground; test just above the ground like Shade's paws).
		SteerCamera(T->GetActorLocation() + FVector(0.f, 0.f, 90.f), Dt, 2.f, -8.f);
		if (Sun->IsSunlit(T->GetActorLocation() - FVector(0.f, 0.f, 30.f), S))
		{
			bSeenLit = true;
			ShadedTime = 0.f;
		}
		else if (bSeenLit)
		{
			ShadedTime += Dt;
		}
		if ((bSeenLit && ShadedTime > 0.12f) || StepTime > St.Time)
		{
			UE_LOG(LogShade, Log, TEXT("SHADE_AP shade at %s after %.2fs (seen lit %d)"), *St.Tag.ToString(), StepTime, bSeenLit ? 1 : 0);
			NextStep();
		}
		break;
	}
	case EShadeAutoStep::Finish:
		UE_LOG(LogShade, Log, TEXT("SHADE_AP finished t=%.1f"), G->GetRunTime());
		StepIndex = Script.Num();
		break;
	}
}
